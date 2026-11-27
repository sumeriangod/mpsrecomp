// MPS's button icons in the Xbox / PlayStation look (runtime/nvn/texpatch.h; the menu's "Button icons", Auto: the
// controller SDL reports). The game draws them as glyphs of its three "extension" fonts (Parts.lyt hsfont_large /
// middle / small_extension.bffnt): one BC7 sheet each, found by the hash of its first 64 KiB as uploaded (v1.0.0).
// On upload the glyphs of A B X Y, L R ZL ZR, + and - are redrawn for the host button each one is bound to
// (bind_prompt_host: remaps follow): Xbox's face buttons are a dark disc with the game's own letter in the button's
// colour (green A, red B, blue X, yellow Y); the rest is drawn here (white shapes with the symbol cut out, as the
// game's). The Nintendo look changes nothing.
// LARECOMP_TEXPATCH_DUMP=1 writes each redrawn sheet to build/ (level 0's BC7 rows, stored upside down).
// shortcut: Nintendo look with remapped buttons keeps the game's icons; hosts without an icon (sticks, d-pad, paddles)
// keep the Switch glyph.
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "input_bind.h"
#include "nvn/texpatch.h"
#include "settings.h"

void bcdec_bc7(const void *compressedBlock, void *decompressedBlock, int destinationPitch);  // (runtime/nvn/bcsoft.c)
int input_pad_family(void);                                                                // (runtime/hle/input.c)

typedef struct { int x, y, w, h; } Rect;
enum { S_A, S_B, S_X, S_Y, S_L, S_R, S_ZL, S_ZR, S_PLUS, S_MINUS, NSLOT };
static const int bind_of[NSLOT] = {BIND_A, BIND_B, BIND_X, BIND_Y, BIND_L, BIND_R, BIND_ZL, BIND_ZR, BIND_PLUS, BIND_MINUS};
// The glyphs' boxes in each sheet as stored (upside down), and the square the - icon gets (the glyph is a thin bar)
static const struct { uint64_t hash; uint32_t w, h; Rect glyph[NSLOT], minus_box; } sheets[] = {
    {0xfd4dd29090d70aa6ull, 1024, 1024,  // large
     {{1, 743, 105, 105}, {176, 743, 105, 105}, {350, 743, 105, 105}, {524, 743, 105, 105}, {697, 748, 107, 92},
      {1, 597, 107, 92}, {175, 591, 105, 106}, {349, 591, 105, 106}, {1, 444, 96, 96}, {176, 479, 95, 25}},
     {176, 444, 96, 96}},
    {0x5cd35d5889e6fd9aull, 512, 1024,  // middle
     {{353, 955, 53, 53}, {1, 873, 53, 53}, {89, 873, 53, 53}, {177, 873, 53, 53}, {265, 875, 54, 47},
      {353, 875, 54, 47}, {1, 791, 53, 53}, {89, 791, 53, 53}, {353, 793, 48, 48}, {1, 728, 48, 13}},
     {1, 710, 48, 48}},
    {0xc2591171838d6439ull, 512, 1024,  // small
     {{346, 964, 49, 49}, {415, 964, 49, 49}, {1, 894, 49, 49}, {70, 894, 49, 49}, {139, 897, 49, 43},
      {208, 897, 50, 43}, {278, 896, 48, 49}, {347, 896, 48, 49}, {70, 825, 44, 45}, {139, 842, 44, 11}},
     {139, 825, 44, 45}},
};
#define NSHEETS (int)(sizeof sheets / sizeof *sheets)

static uint64_t fnv64(const uint8_t *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

int texpatch_identify(const NvnTexture *t) {
    if (t->format != 0x4d || t->levels != 1) return -1;  // (BC7)
    for (int i = 0; i < NSHEETS; i++) {
        if (t->width != sheets[i].w || t->height != sheets[i].h) continue;
        if (fnv64((const uint8_t *)(uintptr_t)t->addr, 64 << 10) != sheets[i].hash) continue;
        LOG("[texpatch] button icon sheet %d (%ux%u)\n", i, t->width, t->height);
        return i;
    }
    return -1;
}

static int style(void) {  // 0 the game's own, 1 Xbox, 2 PlayStation
    switch (g_settings.prompts) {
    case PROMPTS_NINTENDO: return 0;
    case PROMPTS_XBOX: return 1;
    case PROMPTS_PLAYSTATION: return 2;
    default: return input_pad_family();
    }
}

uint32_t texpatch_generation(void) {
    uint32_t g = (uint32_t)style() + 1;
    for (int s = 0; s < NSLOT; s++) g = g * 31 + (uint32_t)(bind_prompt_host(bind_of[s]) + 1);
    return g;
}

// ---- drawing: signed distances in pixels, upright (y down), the symbol cut out of the base shape
typedef struct { float x, y; } V;
static float len(float x, float y) { return sqrtf(x * x + y * y); }
static float sd_segment(V p, V a, V b, float r) {
    float bx = b.x - a.x, by = b.y - a.y, px = p.x - a.x, py = p.y - a.y;
    float h = (px * bx + py * by) / (bx * bx + by * by + 1e-9f);
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    return len(px - bx * h, py - by * h) - r;
}
static float sd_rbox(V p, V c, float hx, float hy, float r) {
    float qx = fabsf(p.x - c.x) - hx + r, qy = fabsf(p.y - c.y) - hy + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return len(ox, oy) + fminf(fmaxf(qx, qy), 0) - r;
}

// The few letters the shoulder icons need, as strokes in a 4 x 6 cell (y down); -1,-1 ends a stroke
static const float *glyph_strokes(char ch) {
    static const float L[] = {0, 0, 0, 6, 3.6f, 6, -1, -1, -9};
    static const float R[] = {0, 6, 0, 0, 2.5f, 0, 3.4f, .5f, 3.7f, 1.5f, 3.4f, 2.5f, 2.5f, 3, 0, 3, -1, -1,
                              2, 3, 3.8f, 6, -1, -1, -9};
    static const float B[] = {0, 0, 0, 6, 2.7f, 6, 3.6f, 5.4f, 3.8f, 4.5f, 3.5f, 3.6f, 2.6f, 3, 0, 3, -1, -1,
                              0, 0, 2.4f, 0, 3.3f, .45f, 3.5f, 1.4f, 3.2f, 2.5f, 2.6f, 3, -1, -1, -9};
    static const float T[] = {0, 0, 4, 0, -1, -1, 2, 0, 2, 6, -1, -1, -9};
    static const float ONE[] = {.9f, 1.3f, 2.2f, 0, 2.2f, 6, -1, -1, -9};
    static const float TWO[] = {.2f, 1.2f, .9f, .25f, 2, 0, 3, .25f, 3.7f, 1.1f, 3.6f, 2.2f, 0, 6, 3.9f, 6, -1, -1, -9};
    switch (ch) {
    case 'L': return L; case 'R': return R; case 'B': return B; case 'T': return T; case '1': return ONE; case '2': return TWO;
    }
    return NULL;
}
static float sd_text(V p, const char *s, V c, float hgt) {
    int n = (int)strlen(s);
    float k = hgt / 6, total = (n * 4 + (n - 1) * 1.6f) * k, d = 1e9f;
    for (int i = 0; i < n; i++) {
        const float *g = glyph_strokes(s[i]);
        float ox = c.x - total / 2 + i * 5.6f * k, oy = c.y - hgt / 2;
        for (int j = 0; g && g[j] != -9; j += 2) {
            if (g[j] < 0 || g[j + 2] < 0) continue;
            V a = {ox + g[j] * k, oy + g[j + 1] * k}, b = {ox + g[j + 2] * k, oy + g[j + 3] * k};
            d = fminf(d, sd_segment(p, a, b, .55f * k));
        }
    }
    return d;
}

enum { I_SOUTH, I_EAST, I_WEST, I_NORTH, I_LB, I_RB, I_LT, I_RT, I_START, I_BACK, NICON };
static int icon_of(int host) {
    switch (host) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return I_SOUTH;
    case SDL_GAMEPAD_BUTTON_EAST: return I_EAST;
    case SDL_GAMEPAD_BUTTON_WEST: return I_WEST;
    case SDL_GAMEPAD_BUTTON_NORTH: return I_NORTH;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return I_LB;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return I_RB;
    case PADB_LTRIGGER: return I_LT;
    case PADB_RTRIGGER: return I_RT;
    case SDL_GAMEPAD_BUTTON_START: return I_START;
    case SDL_GAMEPAD_BUTTON_BACK: return I_BACK;
    }
    return -1;
}

// Icon `ic` of look `st` at pixel p of a w x h box (upright)
static float sd_icon(int st, int ic, V p, float w, float h) {
    V c = {w / 2, h / 2};
    float m = fminf(w, h), t = .055f * m, base, cut = 1e9f;
    static const char *const xb_text[] = {[I_LB] = "LB", [I_RB] = "RB", [I_LT] = "LT", [I_RT] = "RT"};
    static const char *const ps_text[] = {[I_LB] = "L1", [I_RB] = "R1", [I_LT] = "L2", [I_RT] = "R2"};
    if (ic <= I_NORTH || (st == 1 && (ic == I_START || ic == I_BACK))) {  // a disc
        base = len(p.x - c.x, p.y - c.y) - m * .48f;
    } else if (ic == I_START || ic == I_BACK) {                              // PlayStation's small pill buttons
        base = sd_rbox(p, c, w * .46f, h * .3f, h * .3f);
    } else {                                                                 // shoulder (squarer) / trigger (rounder)
        base = sd_rbox(p, c, w / 2 - .5f, h / 2 - .5f, (ic >= I_LT ? .32f : .2f) * m);
    }
    switch (ic) {
    case I_SOUTH: case I_EAST: case I_WEST: case I_NORTH:  // (PlayStation: Xbox's are the game's letters)
        if (ic == I_SOUTH) {  // cross
            float r = .2f * m;
            cut = fminf(sd_segment(p, (V){c.x - r, c.y - r}, (V){c.x + r, c.y + r}, t),
                        sd_segment(p, (V){c.x - r, c.y + r}, (V){c.x + r, c.y - r}, t));
        } else if (ic == I_EAST) {  // circle
            cut = fabsf(len(p.x - c.x, p.y - c.y) - .22f * m) - t;
        } else if (ic == I_WEST) {  // square
            cut = fabsf(sd_rbox(p, c, .19f * m, .19f * m, .02f * m)) - t;
        } else {  // triangle, apex up
            float r = .25f * m, cy = c.y + .04f * m;
            V a = {c.x, cy - r}, b = {c.x + r * .866f, cy + r * .5f}, d = {c.x - r * .866f, cy + r * .5f};
            cut = fminf(sd_segment(p, a, b, t), fminf(sd_segment(p, b, d, t), sd_segment(p, d, a, t)));
        }
        break;
    case I_LB: case I_RB: case I_LT: case I_RT:
        cut = sd_text(p, st == 1 ? xb_text[ic] : ps_text[ic], c, .5f * h);
        break;
    case I_START:  // three lines (Xbox: Menu; PlayStation: Options)
        for (int i = -1; i <= 1; i++) {
            float y = c.y + i * (st == 1 ? .15f * m : .12f * h), x = st == 1 ? .2f * m : .16f * w;
            cut = fminf(cut, sd_segment(p, (V){c.x - x, y}, (V){c.x + x, y}, .045f * m));
        }
        break;
    case I_BACK:
        if (st == 1) {  // View: two overlapping squares
            float o = .07f * m, hs = .13f * m;
            cut = fminf(fabsf(sd_rbox(p, (V){c.x - o, c.y - o}, hs, hs, .02f * m)) - .04f * m,
                        fabsf(sd_rbox(p, (V){c.x + o, c.y + o}, hs, hs, .02f * m)) - .04f * m);
        } else {        // Create: three rays
            V o = {c.x, c.y + .12f * h};
            for (int i = -1; i <= 1; i++)
                cut = fminf(cut, sd_segment(p, o, (V){c.x + i * .14f * m, c.y - .12f * h}, .045f * m));
        }
        break;
    }
    return fmaxf(base, -cut);
}

// ---- BC7 mode 6: one RGBA line per 4x4 block (RGBA texels), its ends the two texels farthest apart, 4-bit weights.
// shortcut: a block with three colours off one line (a letter touching the rim) gets the nearest of the two ends; none
// in these icons, whose letters sit well inside the disc.
static void bc7_mode6(const uint8_t px[64], uint8_t out[16]) {
    static const int wt[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    int end[2] = {0, 0}, far = -1;
    for (int i = 0; i < 16; i++)
        for (int j = i + 1; j < 16; j++) {
            int d = 0;
            for (int c = 0; c < 4; c++) d += (px[i * 4 + c] - px[j * 4 + c]) * (px[i * 4 + c] - px[j * 4 + c]);
            if (d > far) far = d, end[0] = i, end[1] = j;
        }
    int q[2][4], p[2], e[2][4];  // the ends: 7 bits a channel, a p-bit each, the 8-bit values they decode to
    for (int k = 0; k < 2; k++) {
        const uint8_t *v = px + end[k] * 4;
        int best = 1 << 30;
        for (int pb = 0; pb < 2; pb++) {
            int err = 0, qq[4];
            for (int c = 0; c < 4; c++) {
                qq[c] = (v[c] - pb + 1) >> 1;
                qq[c] = qq[c] < 0 ? 0 : qq[c] > 127 ? 127 : qq[c];
                int dl = ((qq[c] << 1) | pb) - v[c];
                err += dl * dl * (c == 3 ? 4 : 1);  // (alpha first: a transparent end stays 0, not 1)
            }
            if (err < best) best = err, p[k] = pb, memcpy(q[k], qq, sizeof qq);
        }
        for (int c = 0; c < 4; c++) e[k][c] = (q[k][c] << 1) | p[k];
    }
    int idx[16];
    for (int i = 0; i < 16; i++) {
        int bd = 1 << 30;
        for (int w = 0; w < 16; w++) {
            int d = 0;
            for (int c = 0; c < 4; c++) {
                int dl = ((e[0][c] * (64 - wt[w]) + e[1][c] * wt[w] + 32) >> 6) - px[i * 4 + c];
                d += dl * dl;
            }
            if (d < bd) bd = d, idx[i] = w;
        }
    }
    int a = 0, b = 1;
    if (idx[0] > 7) {  // (the first index has no top bit: swap the ends)
        a = 1, b = 0;
        for (int i = 0; i < 16; i++) idx[i] = 15 - idx[i];
    }
    memset(out, 0, 16);
    int n = 0;
#define PUT(v, k) do { for (int b_ = 0; b_ < (k); b_++, n++) if (((v) >> b_) & 1) out[n >> 3] |= (uint8_t)(1 << (n & 7)); } while (0)
    PUT(1 << 6, 7);
    for (int c = 0; c < 4; c++) { PUT(q[a][c], 7); PUT(q[b][c], 7); }
    PUT(p[a], 1);
    PUT(p[b], 1);
    PUT(idx[0], 3);
    for (int i = 1; i < 16; i++) PUT(idx[i], 4);
#undef PUT
}

static float lin(uint8_t v) {  // sRGB -> linear
    float c = v / 255.f;
    return c <= .04045f ? c / 12.92f : powf((c + .055f) / 1.055f, 2.4f);
}

// Alpha of a w x h rect of the sheet (as stored)
static uint8_t *read_alpha(const uint8_t *blocks, uint32_t row, Rect r) {
    uint8_t *out = calloc((size_t)r.w * r.h, 1), px[64];
    for (int by = r.y / 4; by <= (r.y + r.h - 1) / 4; by++)
        for (int bx = r.x / 4; bx <= (r.x + r.w - 1) / 4; bx++) {
            bcdec_bc7(blocks + (size_t)by * row + (size_t)bx * 16, px, 16);
            for (int j = 0; j < 16; j++) {
                int x = bx * 4 + (j & 3) - r.x, y = by * 4 + (j >> 2) - r.y;
                if (x >= 0 && y >= 0 && x < r.w && y < r.h) out[y * r.w + x] = px[j * 4 + 3];
            }
        }
    return out;
}

void texpatch_apply(int id, uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row) {
    int st = style();
    if (id < 0 || id >= NSHEETS || lv || !st || w != sheets[id].w || h != sheets[id].h) return;
    const Rect *g = sheets[id].glyph;
    uint8_t *letter[4] = {0};  // Xbox: the game's A B X Y, read before any is drawn over
    if (st == 1)
        for (int i = 0; i < 4; i++) letter[i] = read_alpha(blocks, row, g[S_A + i]);
    for (int s = 0; s < NSLOT; s++) {
        int ic = icon_of(bind_prompt_host(bind_of[s]));
        if (ic < 0) continue;
        Rect box = s == S_MINUS ? sheets[id].minus_box : g[s];
        int x1 = (g[s].x < box.x ? g[s].x : box.x) & ~3, y1 = (g[s].y < box.y ? g[s].y : box.y) & ~3;
        int x2 = ((g[s].x + g[s].w > box.x + box.w ? g[s].x + g[s].w : box.x + box.w) + 3) & ~3;
        int y2 = ((g[s].y + g[s].h > box.y + box.h ? g[s].y + g[s].h : box.y + box.h) + 3) & ~3;
        int rw = x2 - x1, rh = y2 - y1;
        uint8_t *a = malloc((size_t)rw * rh * 4);  // RGBA: white and transparent but what is drawn
        for (size_t i = 0; i < (size_t)rw * rh; i++) memcpy(a + i * 4, "\xff\xff\xff\0", 4);
        if (st == 1 && ic <= I_NORTH) {  // a dark disc, the game's letter (its cut-out) in that button's Xbox colour
            // (as shown on screen: the game shows these texels sRGB-encoded, measured 0x2a -> 0x72, so they're stored linear)
            static const uint8_t col[4][3] = {{0x3d, 0xbe, 0x29}, {0xe5, 0x31, 0x2f}, {0x2b, 0x7d, 0xe9}, {0xf5, 0xb8, 0x00}};
            static const uint8_t dark[3] = {0x2a, 0x2a, 0x2e};
            const Rect *src = &g[S_A + ic];
            float m = (float)(box.w < box.h ? box.w : box.h);
            for (int py = box.y; py < box.y + box.h; py++)
                for (int px = box.x; px < box.x + box.w; px++) {
                    int sx = px - box.x - (box.w - src->w) / 2, sy = py - box.y - (box.h - src->h) / 2;
                    float ga = sx >= 0 && sy >= 0 && sx < src->w && sy < src->h ? letter[ic][sy * src->w + sx] / 255.f : 0;
                    float r = len(px - box.x + .5f - box.w / 2.f, py - box.y + .5f - box.h / 2.f);
                    float in = .5f - (r - .4f * m), l = (1 - ga) * (in < 0 ? 0 : in > 1 ? 1 : in), al = ga + l;
                    uint8_t *o = a + ((size_t)(py - y1) * rw + (px - x1)) * 4;
                    for (int c = 0; c < 3; c++)
                        o[c] = (uint8_t)(255 * (lin(dark[c]) + (lin(col[ic][c]) - lin(dark[c])) * (al > 0 ? l / al : 0)) + .5f);
                    o[3] = (uint8_t)(255 * al + .5f);
                }
        } else {
            for (int py = box.y; py < box.y + box.h; py++)
                for (int px = box.x; px < box.x + box.w; px++) {
                    V p = {px - box.x + .5f, box.y + box.h - py - .5f};  // (stored upside down)
                    float v = .5f - sd_icon(st, ic, p, (float)box.w, (float)box.h);
                    a[((py - y1) * rw + (px - x1)) * 4 + 3] = (uint8_t)(255 * (v < 0 ? 0 : v > 1 ? 1 : v) + .5f);
                }
        }
        for (int by = 0; by < rh / 4; by++)
            for (int bx = 0; bx < rw / 4; bx++) {
                uint8_t q[64];
                for (int j = 0; j < 16; j++) memcpy(q + j * 4, a + ((by * 4 + (j >> 2)) * rw + bx * 4 + (j & 3)) * 4, 4);
                bc7_mode6(q, blocks + (size_t)(y1 / 4 + by) * row + (size_t)(x1 / 4 + bx) * 16);
            }
        free(a);
    }
    for (int i = 0; i < 4; i++) free(letter[i]);
    if (getenv("LARECOMP_TEXPATCH_DUMP")) {  // (debug: the sheet as redrawn, level 0's BC7 rows)
        char path[96];
        snprintf(path, sizeof path, "build/texpatch_sheet%d_look%d.bin", id, st);
        FILE *f = fopen(path, "wb");
        if (f) fwrite(blocks, 1, (size_t)row * (h / 4), f), fclose(f);
    }
}
