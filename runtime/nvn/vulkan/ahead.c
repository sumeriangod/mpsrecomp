// Shader compile-ahead for the Vulkan backend (shader_ahead.h; the Metal backend's is in renderer.m, the design and
// measurements in recomp/docs/smm2-bringup.md "Shader compile-ahead"). Same order and sources as Metal's: the programs
// the game sets (fresh ones first, the boot backlog oldest first), the game's shader files in the RomFS by rank, a
// file the game reads or a game's hint first. Each is translated to GLSL as the first use will translate it and
// compiled to SPIR-V into cache/spirv/ (shaders.c), where the first use then finds it: unlike Metal's shader cache, that
// one is ours and stays. Left for the first use: the pipelines (the driver's compile; VkPipelineCache), programs with
// another key. The scan reads and queues on one low-priority thread; two low-priority compilers (MPS's .bea archives
// hold some 130,000 shaders) translate and compile, each waiting while the game is building (build_idle_ms()).
// LARECOMP_AHEAD=0: off; =force: ignore the stamp (cache/ahead_vk.done). LARECOMP_AHEAD_QUIET_MS (1500).
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../buildsync.h"
#include "../shader_ahead.h"
#include "shaders.h"
#include "vk.h"

static struct {
    atomic_int started;
    volatile int stop;
    ShaderKey vkey, fkey;
    atomic_int items, set_progs, compiled, cached, failed, untranslatable;
    double t0, last_log;
} vah;

// ---- the scan's shaders, queued for the compilers (copies: the scan's buffers and names are gone after its callback)
typedef struct Job { struct Job *next; AheadShader sh; char name[160]; uint8_t bytes[]; } Job;
#define Q_MAX 256
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_more = PTHREAD_COND_INITIALIZER, q_room = PTHREAD_COND_INITIALIZER;
static Job *q_head, *q_tail;
static int q_len, q_busy;
static volatile int q_scanning;

static void job_add(const AheadShader *s) {
    size_t total = 0;
    for (int k = 0; k < 3; k++) if (s->code[k]) total += s->cmax[k];
    Job *j = malloc(sizeof *j + total);
    if (!j) return;
    j->next = NULL, j->sh = *s;
    snprintf(j->name, sizeof j->name, "%s", s->archive ? s->archive : "");
    j->sh.archive = j->name;
    uint8_t *b = j->bytes;
    for (int k = 0; k < 3; k++)
        if (s->code[k]) memcpy(b, s->code[k], s->cmax[k]), j->sh.code[k] = b, b += s->cmax[k];
    pthread_mutex_lock(&q_lock);
    while (q_len >= Q_MAX && !vah.stop) pthread_cond_wait(&q_room, &q_lock);
    if (q_tail) q_tail->next = j; else q_head = j;
    q_tail = j, q_len++;
    pthread_cond_signal(&q_more);
    pthread_mutex_unlock(&q_lock);
}
static Job *job_take(int wait_ms) {
    pthread_mutex_lock(&q_lock);
    if (!q_head && wait_ms > 0 && !vah.stop) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)wait_ms * 1000000L;
        while (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
        pthread_cond_timedwait(&q_more, &q_lock, &ts);
    }
    Job *j = q_head;
    if (j) {
        q_head = j->next;
        if (!q_head) q_tail = NULL;
        q_len--, q_busy++;
        pthread_cond_signal(&q_room);
    }
    pthread_mutex_unlock(&q_lock);
    return j;
}
static void job_done(Job *j) {
    free(j);
    pthread_mutex_lock(&q_lock);
    q_busy--;
    pthread_mutex_unlock(&q_lock);
}
static int jobs_left(void) {
    pthread_mutex_lock(&q_lock);
    int n = q_len + q_busy;
    pthread_mutex_unlock(&q_lock);
    return n;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void compile_glsl(const char *src, int stage) {
    if (!src || vah.stop) return;
    if (vk_spirv_cached(src, stage)) { vah.cached++; return; }
    static double quiet = -1;  // (the game's own builds first: they come in bursts, a quiet moment must last a while)
    if (quiet < 0) quiet = getenv("LARECOMP_AHEAD_QUIET_MS") ? atof(getenv("LARECOMP_AHEAD_QUIET_MS"))
                           : shader_ahead_fast() ? 0 : 1500;  // (Video > Shaders, full speed: no waiting)
    while (build_idle_ms() < quiet && !vah.stop) SDL_Delay(20);
    if (vah.stop) return;
    size_t n = 0;
    uint32_t *spv = vk_glsl_to_spirv(src, stage, "ahead", &n);  // (stored in cache/spirv)
    if (spv) vah.compiled++; else vah.failed++;
    free(spv);
}

static void item(const AheadShader *s) {
    ShaderKey vk = vah.vkey, fk = vah.fkey, gk;
    ShaderInfo *vi = NULL, *fi = NULL, *gi = NULL;
    bool gs = s->stage == 2 && s->code[2] && g_vk.geometry;
    if (s->stage != 1) {
        if (gs) vk.vs_fn = 1;  // (the geometry stage applies the clip-space epilogue)
        vi = maxwell_translate(s->code[0], s->cmax[0], STAGE_VERTEX, &vk);
    }
    uint32_t to_fs = 0xFFFFFFFFu;  // (a fragment shader alone: as if its vertex shader wrote all it reads)
    if (vi && vi->msl) to_fs = vi->out_mask;
    if (gs && vi && vi->msl) {
        gk = vk;
        gk.vs_fn = 0;
        gk.vs_out_mask = vi->out_mask;
        gi = maxwell_translate(s->code[2], s->cmax[2], STAGE_GEOMETRY, &gk);
        to_fs = gi->out_mask;
    }
    if (s->stage == 1 || (s->stage == 2 && vi && vi->msl && (!gi || gi->msl))) {
        fk.vs_out_mask = to_fs;
        fi = maxwell_translate(s->code[1], s->cmax[1], STAGE_FRAGMENT, &fk);
    }
    if ((vi && !vi->msl) || (gi && !gi->msl) || (fi && !fi->msl) || (!vi && !fi)) vah.untranslatable++;
    if (vi && vi->msl) compile_glsl(vi->msl, STAGE_VERTEX);
    if (gi && gi->msl) compile_glsl(gi->msl, STAGE_GEOMETRY);
    if (fi && fi->msl) compile_glsl(fi->msl, STAGE_FRAGMENT);
    if (vi) maxwell_free(vi);
    if (gi) maxwell_free(gi);
    if (fi) maxwell_free(fi);
}

static void take_set(int wait_ms, double max_age_ms, int most) {
    for (const AheadShader *g; most-- > 0 && !vah.stop && (g = shader_ahead_set_next(wait_ms, max_age_ms)); wait_ms = 0) {
        item(g);
        shader_ahead_set_done(g);
        vah.set_progs++;
    }
}

// A compiler: the programs the game has just set first (what it draws soonest), then the scan's queue with the boot
// backlog one at a time between (the order: renderer.m's ahead_scanned); once the scan is done, the programs the game
// sets, for as long as it runs
static void *compiler(void *arg) {
    (void)arg;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    maxwell_gmem_base = HEAP_BASE;  // (as the game's translations: the bounds appear in the sources)
    maxwell_gmem_size = HEAP_MAX;
    while (!vah.stop) {
        take_set(0, 10000, 1 << 30);
        Job *j = job_take(q_scanning ? 100 : 0);
        if (j) {
            item(&j->sh);
            job_done(j);
            take_set(0, -1, 1);
            continue;
        }
        take_set(q_scanning ? 0 : 1000, -1, 1 << 30);
    }
    return NULL;
}

static bool scanned(const AheadShader *s, void *ud) {
    (void)ud;
    if (vah.stop) return false;
    vah.items++;
    shader_ahead_status_set(1, vah.items, vah.compiled);  // (the menu's progress: Video > Shaders)
    job_add(s);
    if (now_ms() - vah.last_log > 30000) {
        vah.last_log = now_ms();
        LOG("[shader] compile-ahead: %d shaders read, %d programs the game set, %d compiled to SPIR-V, %.0f s in; now %s\n",
            (int)vah.items, (int)vah.set_progs, (int)vah.compiled, (vah.last_log - vah.t0) / 1000, s->archive);
    }
    return !vah.stop;
}

static void stamp(char *out, size_t cap) {
    char exe[1024] = "";
    struct stat sb = {0};
    if (plat_exe_path(exe, sizeof exe)) stat(exe, &sb);
    snprintf(out, cap, "ahead-vk 1 exe %lld-%lld key %d%d gmem %d%d\n", (long long)sb.st_size, (long long)sb.st_mtime, vah.vkey.flip_y,
             vah.vkey.depth_minus_one, maxwell_glsl_bda, maxwell_glsl_gmap_gfx);  // (the GLSL differs with the gmem mode)
}

static void stop_ahead(void) {
    vah.stop = 1;
    pthread_mutex_lock(&q_lock);
    pthread_cond_broadcast(&q_room);
    pthread_cond_broadcast(&q_more);
    pthread_mutex_unlock(&q_lock);
}

static void *worker(void *arg) {  // (the scan)
    (void)arg;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    vah.t0 = vah.last_log = now_ms();
    shader_ahead_status_set(1, 0, 0);
    {
        AheadScanStats st;
        bool full = shader_ahead_scan(g_romfs_dir, scanned, NULL, &vah.stop, &st);
        while (!vah.stop && jobs_left()) SDL_Delay(100);  // (what it queued compiled before it counts as done)
        q_scanning = 0;
        LOG("[shader] compile-ahead %s: %d shaders + %d programs from %d files (%d read early for the game), %d programs the "
            "game set; %d compiled to SPIR-V, %d cached already, %d failed, %d not translatable; %.1f s\n",
            full ? "finished" : "stopped", st.shaders, st.programs, st.files, st.preferred, (int)vah.set_progs,
            (int)vah.compiled, (int)vah.cached, (int)vah.failed, (int)vah.untranslatable, (now_ms() - vah.t0) / 1000);
        if (full && !vah.stop) {
            char s[256];
            stamp(s, sizeof s);
            mkdir("cache", 0755);
            FILE *f = fopen("cache/ahead_vk.done", "w");
            if (f) { fputs(s, f); fclose(f); }
        }
        shader_ahead_status_set(full ? 2 : 0, st.shaders + st.programs, vah.compiled);
    }
    return NULL;
}

// (exec.c, the game thread: its first program with the plain key)
void vk_ahead_start(const ShaderKey *vkey, const ShaderKey *fkey) {
    const char *mode = getenv("LARECOMP_AHEAD");
    if (atomic_exchange(&vah.started, 1) || (mode && !strcmp(mode, "0")) || !g_romfs_dir) return;
    ShaderKey v = {0}, f = {0};
    v.glsl = f.glsl = 1;
    v.flip_y = vkey->flip_y, v.depth_minus_one = vkey->depth_minus_one;
    f.flip_y = fkey->flip_y, f.depth_minus_one = fkey->depth_minus_one;
    vah.vkey = v, vah.fkey = f;
    char now[256], old[256] = "";
    stamp(now, sizeof now);
    FILE *fp = fopen("cache/ahead_vk.done", "r");
    if (fp) { size_t n = fread(old, 1, sizeof old - 1, fp); old[n] = 0; fclose(fp); }
    bool scan = strcmp(now, old) != 0 || (mode && !strcmp(mode, "force"));
    if (!scan) LOG("[shader] compile-ahead: the game's files were done before (cache/ahead_vk.done); programs the game sets still are\n");
    if (!scan) shader_ahead_status_set(2, 0, 0);
    atexit(stop_ahead);
    pthread_t t;
    q_scanning = scan;
    // (two: eight took the board's frame rate from 50 to 46 fps and doubled its stutter on a 13700K, the first session
    // worse for content not yet seen. LARECOMP_AHEAD_THREADS=<n> for tests)
    const char *te = getenv("LARECOMP_AHEAD_THREADS");
    int n = te && atoi(te) > 0 ? atoi(te) : 2;
    n = n > 16 ? 16 : n;
    for (int i = 0; i < n; i++)
        if (pthread_create(&t, NULL, compiler, NULL) == 0) pthread_detach(t);
    if (scan && pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
}
