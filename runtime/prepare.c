// <exe> --prepare <game folder>: the setup screen's work without a window, for a launcher that runs a setup step before
// Play (Recomp Launcher's "prepare": games/<game>/recomplauncher.json). It lays out the player's files in the data
// folder (games/<game>/setup.py with --no-symlinks: the RomFS is hard-linked or copied, so Play works without the chosen
// folder), then builds the game code there. Progress goes to stdout as "progress <0..1> <text>"; on failure the reason
// is printed last and the exit code is 1. A game code library that is already current: "progress 1 Already built".
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "game.h"
#include "gamebuild.h"
#include "gamelib.h"
#include "paths.h"
#include "platform.h"
#include "rt.h"

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

static void say(double f, const char *text) {
    printf("progress %.3f %s\n", f, text);
    fflush(stdout);
}

static int fail(const char *what, const char *hint) {
    LOG("[prepare] failed: %s\n", what);
    printf("%s\n", what);
    if (hint && *hint) printf("%s\n", hint);
    fflush(stdout);
    return 1;
}

// setup.py, its "@" lines turned into progress lines (0..0.1); the error it gives, if any, in err ("what\thint")
static int run_setup(const char *python, const char *script, const char *game, const char *log, char *err, size_t n) {
    const char *argv[] = {python, script, "--source", game, "--out", "extracted", "--no-symlinks", NULL};
    PlatProc p = plat_spawn(argv, log, false);
    if (!p) return -1;
    char step[256] = "Checking your game files", line[1024];
    double at = 0;  // (a new step keeps the progress so far)
    long seen = 0;
    int rc;
    do {
        rc = plat_proc_wait(p, false);
        FILE *f = fopen(log, "rb");
        if (f && fseek(f, seen, SEEK_SET) == 0) {
            while (fgets(line, sizeof line, f) && strchr(line, '\n')) {  // (whole lines only: the rest comes next time)
                seen = ftell(f);
                line[strcspn(line, "\r\n")] = 0;
                if (!strncmp(line, "@step ", 6)) snprintf(step, sizeof step, "%s", line + 6), say(at, step);
                else if (!strncmp(line, "@progress ", 10)) at = 0.1 * atof(line + 10), say(at, step);
                else if (!strncmp(line, "@error ", 7)) snprintf(err, n, "%s", line + 7);
            }
        }
        if (f) fclose(f);
        if (rc < 0) plat_sleep_until_ns(plat_now_ns() + 100000000ull);
    } while (rc < 0);
    return rc;
}

static void build_progress(double f, const char *step) {
    static int last = -1;
    static const char *last_step;
    int permille = (int)(f * 1000);
    if (step == last_step && permille < last + 5) return;  // (every half percent, or a new step)
    last = permille, last_step = step;
    say(0.1 + 0.9 * f, step);
}

int prepare_cli(const char *game) {
    char python[1100], script[1100], log[1200], err[1024] = "";
    if (!gamebuild_python(python, sizeof python) ||
        !rt_resource("tools" SEP "games" SEP GAME_ID SEP "setup.py", "games/" GAME_ID "/setup.py", script, sizeof script))
        return fail("The setup tools aren't there.", "Download " GAME_TITLE " again and unpack all of it.");
    mkdir("logs", 0755);  // (the data folder is the working directory: rt_paths_init)
    snprintf(log, sizeof log, "logs" SEP "setup.log");
    LOG("[prepare] setting up the game files from %s\n", game);
    say(0, "Checking your game files");
    int rc = run_setup(python, script, game, log, err, sizeof err);
    if (rc != 0 || err[0]) {
        char *tab = strchr(err, '\t'), *hint = tab ? strchr(tab + 1, '\t') : NULL;  // "<code>\t<what>\t<what to do>"
        if (hint) *hint++ = 0;
        return fail(tab ? tab + 1 : rc < 0 ? "The setup tool didn't start." : "Setting up the game files failed.",
                    hint ? hint : "The log (logs" SEP "setup.log in the data folder) has the details.");
    }
    if (gamelib_present("extracted")) {
        say(1, "Already built");
        return 0;
    }
    if (!gamebuild_available())
        return fail("The recompiler isn't there.", "Download " GAME_TITLE " again and unpack all of it (zig, tools, python).");
    // (as the setup screen: a compiler takes up to ~1 GB, the generators meanwhile ~4 GB together)
    int cores = SDL_GetNumLogicalCPUCores(), ram_gb = SDL_GetSystemRAM() / 1024, jobs = cores < ram_gb - 6 ? cores : ram_gb - 6;
    GameBuildOpts o = {build_progress, jobs > 1 ? jobs : 1};
    char why[512] = "";
    LOG("[prepare] recompiling the game code (%d jobs)\n", o.jobs);
    if (!gamebuild_run("extracted", &o, why, sizeof why)) {
        char what[600];
        snprintf(what, sizeof what, "The game code didn't build: %s", why);
        return fail(what, "Make sure there are about 4 GB of disk space and 4 GB of memory free, then try again.");
    }
    say(1, "Done");
    return 0;
}
