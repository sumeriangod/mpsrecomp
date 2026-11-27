// nn::pl shared system fonts. The Switch's system fonts are not part of the game; in order of preference:
//  1. LARECOMP_FONT=<file> (.ttf / .otf / .ttc face 0 / .bfttf)
//  2. a dump from the user's own console in fonts/: nintendo_udsg-r_std_003.bfttf (Standard, Morisawa UD Shin Go)
//     and nintendo_ext_003.bfttf (NintendoExt button glyphs); plain .ttf versions of those names also work.
//     The Chinese and Korean shared fonts (the game falls back to them for characters the Standard font lacks, e.g.
//     simplified Chinese course names) are loaded the first time the game asks for them, from the same folder
//  3. that language's system font (as LAS Recomp): Microsoft YaHei / JhengHei, Malgun Gothic on Windows, for the
//     Chinese and Korean types, and for Standard too when the game plays in that language (it draws some text with
//     Standard, and a Japanese font lacks hangul and some simplified Chinese)
//  4. a Japanese gothic like UD Shin Go (much closer to the game's look than Arial): Hiragino Sans W4 on macOS,
//     Yu Gothic Regular / Medium on Windows. Not in Russian: Japanese fonts draw Cyrillic full-width (LAS issue #32)
//  5. Arial
// A substitute without a space or "□" (U+25A1, the game's missing-character box, looked up without a check: LAS
// issue #45, a crash) is passed over for the next one. A Chinese / Korean type with no font serves Standard's.
// .bfttf is the console's obfuscated TTF: 8-byte header (magic, size) then every u32 XORed with a fixed key.
// .ttc collections get face 0 copied out as a standalone font (the game's font engine wants one sfnt).
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../hle.h"
#include "../settings.h"

typedef struct { uint64_t addr, size; } Font;
// Indexed by SharedFontType: 0 Standard, 1 ChineseSimple, 2 ChineseSimpleExt, 3 ChineseTraditional, 4 Korean,
// 5 NintendoExt. A slot without a font (no dump, or the file is missing) answers with the Standard one.
#define NFONTS 6
static Font fonts[NFONTS];
static uint8_t font_tried[NFONTS];
static const char *const dump_name[NFONTS] = {
    "nintendo_udsg-r_std_003",          "nintendo_udsg-r_org_zh-cn_003", "nintendo_udsg-r_ext_zh-cn_003",
    "nintendo_udjxh-db_zh-tw_003",      "nintendo_udsg-r_ko_003",        "nintendo_ext_003"};

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(*len + 4);
    if (fread(p, 1, *len, f) != *len) { free(p); p = NULL; }
    fclose(f);
    return p;
}

// .bfttf -> TTF in place (returns the new length, 0 if it isn't one)
static size_t bfttf_decode(uint8_t *d, size_t len) {
    static const uint32_t KEY = 0x49621806;
    if (len < 12) return 0;
    for (int swap = 0; swap < 2; swap++) {  // the key's byte order differs between descriptions; accept both
        uint32_t k = swap ? __builtin_bswap32(KEY) : KEY;
        uint32_t w0, w1, w2;
        memcpy(&w0, d, 4); memcpy(&w1, d + 4, 4); memcpy(&w2, d + 8, 4);
        uint32_t size = w1 ^ k, ver = w2 ^ k;
        if (swap) size = __builtin_bswap32(size);  // (the header words are big-endian: the console's own files)
        if (size == 0 || size > len - 8 || (ver != 0x00000100u && ver != 0x4F54544Fu)) continue;  // sfnt 1.0 / OTTO
        (void)w0;
        for (size_t i = 0; i + 4 <= size; i += 4) {
            uint32_t w;
            memcpy(&w, d + 8 + i, 4);
            w ^= k;
            memcpy(d + i, &w, 4);
        }
        return size;
    }
    return 0;
}

// .ttc -> face 0 as its own sfnt (table data copied, offsets rewritten)
static uint8_t *ttc_face0(const uint8_t *f, size_t len, size_t *out_len) {
    if (len < 16 || memcmp(f, "ttcf", 4)) return NULL;
    uint32_t o = be32(f + 12);
    if (o + 12 > len) return NULL;
    uint32_t nt = (uint32_t)f[o + 4] << 8 | f[o + 5];
    size_t total = 12 + 16 * nt;
    for (uint32_t i = 0; i < nt; i++) total += (be32(f + o + 12 + 16 * i + 12) + 3) & ~3u;
    uint8_t *out = calloc(1, total);
    memcpy(out, f + o, 12);
    size_t pos = 12 + 16 * nt;
    for (uint32_t i = 0; i < nt; i++) {
        const uint8_t *e = f + o + 12 + 16 * i;
        uint32_t off = be32(e + 8), n = be32(e + 12);
        if ((size_t)off + n > len) { free(out); return NULL; }
        memcpy(out + 12 + 16 * i, e, 8);  // tag, checksum
        put32(out + 12 + 16 * i + 8, (uint32_t)pos);
        put32(out + 12 + 16 * i + 12, n);
        memcpy(out + pos, f + off, n);
        pos += (n + 3) & ~3u;
    }
    *out_len = total;
    return out;
}

// Does the font map this character to a glyph? (its cmap: format 4 or 12 subtables, Unicode encodings; LAS Recomp's)
static bool font_has(const uint8_t *f, size_t len, uint32_t cp) {
    if (len < 12) return false;
    uint32_t nt = (uint32_t)f[4] << 8 | f[5], cmap = 0, cmap_len = 0;
    for (uint32_t i = 0; i < nt && 12 + 16 * (i + 1) <= len; i++)
        if (!memcmp(f + 12 + 16 * i, "cmap", 4)) { cmap = be32(f + 12 + 16 * i + 8); cmap_len = be32(f + 12 + 16 * i + 12); }
    if (!cmap || (size_t)cmap + cmap_len > len || cmap_len < 4) return false;
    const uint8_t *c = f + cmap, *end = f + cmap + cmap_len;
    uint32_t ns = (uint32_t)c[2] << 8 | c[3];
    for (uint32_t i = 0; i < ns && c + 4 + 8 * (i + 1) <= end; i++) {
        uint32_t plat = (uint32_t)c[4 + 8 * i] << 8 | c[5 + 8 * i], enc = (uint32_t)c[6 + 8 * i] << 8 | c[7 + 8 * i];
        if (!(plat == 0 || (plat == 3 && (enc == 1 || enc == 10)))) continue;  // (Unicode subtables only)
        const uint8_t *t = c + be32(c + 8 + 8 * i);
        if (t + 4 > end) continue;
        uint32_t fmt = (uint32_t)t[0] << 8 | t[1];
        if (fmt == 4 && cp <= 0xFFFF && t + 14 <= end) {
            uint32_t segs = ((uint32_t)t[6] << 8 | t[7]) / 2;
            const uint8_t *ends = t + 14, *starts = ends + 2 * segs + 2, *deltas = starts + 2 * segs, *ranges = deltas + 2 * segs;
            if (ranges + 2 * segs > end) continue;
            for (uint32_t k = 0; k < segs; k++) {
                uint32_t e = (uint32_t)ends[2 * k] << 8 | ends[2 * k + 1], st = (uint32_t)starts[2 * k] << 8 | starts[2 * k + 1];
                if (cp > e) continue;
                if (cp < st) break;
                uint32_t ro = (uint32_t)ranges[2 * k] << 8 | ranges[2 * k + 1], d = (uint32_t)deltas[2 * k] << 8 | deltas[2 * k + 1];
                if (!ro) return ((cp + d) & 0xFFFF) != 0;
                const uint8_t *g = ranges + 2 * k + ro + 2 * (cp - st);
                return g + 2 <= end && ((((uint32_t)g[0] << 8 | g[1]) + (g[0] | g[1] ? d : 0)) & 0xFFFF) != 0;
            }
        } else if (fmt == 12 && t + 16 <= end) {
            uint32_t ng = be32(t + 12);
            for (uint32_t k = 0; k < ng && t + 16 + 12 * (k + 1) <= end; k++) {
                const uint8_t *gr = t + 16 + 12 * k;
                if (cp >= be32(gr) && cp <= be32(gr + 4)) return be32(gr + 8) + (cp - be32(gr)) != 0;
            }
        }
    }
    return false;
}

// check: a substitute, used only with a space and the game's "□" box (U+25A1)
static int load_one(const char *path, Font *ft, bool check) {
    size_t len;
    uint8_t *d = read_file(path, &len);
    if (!d) return 0;
    size_t n = strlen(path);
    if (n > 6 && !strcasecmp(path + n - 6, ".bfttf")) {
        len = bfttf_decode(d, len);
        if (!len) { LOG("[pl] %s: not a valid .bfttf\n", path); free(d); return 0; }
    } else if (len >= 4 && !memcmp(d, "ttcf", 4)) {
        size_t l2;
        uint8_t *f0 = ttc_face0(d, len, &l2);
        free(d);
        if (!f0) { LOG("[pl] %s: bad font collection\n", path); return 0; }
        d = f0;
        len = l2;
    }
    if (check && !(font_has(d, len, 0x20) && font_has(d, len, 0x25A1))) {
        LOG("[pl] %s: no glyph for a space or U+25A1 - skipped\n", path);
        free(d);
        return 0;
    }
    void *p = hle_alloc(len + 16);
    memcpy(p, d, len);
    free(d);
    ft->addr = (uint64_t)p;
    ft->size = len;
    LOG("[pl] system font %s (%zu bytes)\n", path, len);
    return 1;
}

static int g_fonts_loaded;
RT_ON_RESET(pl_reset) {  // (they were in the HLE heap)
    g_fonts_loaded = 0;
    memset(fonts, 0, sizeof fonts);
    memset(font_tried, 0, sizeof font_tried);
}

// That language's system fonts (Windows, macOS, Linux names), for its type and for Standard in a game in that language
static const char *const subst_zh_cn[] = {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf",
                                          "/System/Library/Fonts/Hiragino Sans GB.ttc",
                                          "/System/Library/Fonts/Supplemental/Hiragino Sans GB.ttc",
                                          "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
                                          "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", NULL};
static const char *const subst_zh_tw[] = {"C:/Windows/Fonts/msjh.ttc", "/usr/share/fonts/truetype/arphic/uming.ttc",
                                          NULL};  // (macOS: Hiragino, through Standard, has the traditional forms)
static const char *const subst_ko[] = {"C:/Windows/Fonts/malgun.ttf", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
                                       "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
                                       "/usr/share/fonts/nanum/NanumGothic.ttf", NULL};
static const char *const *const subst[NFONTS] = {NULL, subst_zh_cn, NULL, subst_zh_tw, subst_ko, NULL};

static void load_subst(unsigned type, Font *ft) {
    for (size_t i = 0; subst[type] && subst[type][i] && !ft->addr; i++) load_one(subst[type][i], ft, true);
}

// The console's font of this type from the dump (fonts/<name>.bfttf, or .ttf), else that language's system font;
// loaded once
static void load_dump_font(unsigned type) {
    if (font_tried[type]) return;
    font_tried[type] = 1;
    char path[96];
    snprintf(path, sizeof path, "fonts/%s.bfttf", dump_name[type]);
    if (!load_one(path, &fonts[type], false)) {
        snprintf(path, sizeof path, "fonts/%s.ttf", dump_name[type]);
        if (!load_one(path, &fonts[type], false)) load_subst(type, &fonts[type]);
    }
}

// The same file as a TTF in a malloc'd buffer of the caller's (NULL without one): the overlay's name entry draws
// Chinese, Japanese and Korean text with the game's own fonts (runtime/ui/menu.mm, #99; not the copy above, which is
// in the HLE heap and goes with a reset)
uint8_t *pl_dump_font_ttf(unsigned type, size_t *len) {
    char path[96];
    snprintf(path, sizeof path, "fonts/%s.bfttf", dump_name[type]);
    uint8_t *d = read_file(path, len);
    if (d && (*len = bfttf_decode(d, *len))) return d;
    free(d);
    snprintf(path, sizeof path, "fonts/%s.ttf", dump_name[type]);
    return read_file(path, len);
}

static void load_fonts(void) {
    if (g_fonts_loaded) return;
    g_fonts_loaded = 1;
    const char *env = getenv("LARECOMP_FONT");
    const char *own[] = {env, "fonts/nintendo_udsg-r_std_003.bfttf", "fonts/nintendo_udsg-r_std_003.ttf",
                         "fonts/nintendo_udsgr_std_003.ttf", "fonts/nintendo_udsgr_std_003.bfttf"};
    for (size_t i = 0; i < sizeof own / sizeof *own && !fonts[0].addr; i++)
        if (own[i]) load_one(own[i], &fonts[0], false);
    const char *lang = settings_language();
    if (!fonts[0].addr)
        load_subst(!strcmp(lang, "zh-CN") ? 1 : !strcmp(lang, "zh-TW") ? 3 : !strcmp(lang, "ko") ? 4 : 0, &fonts[0]);
    bool cyrillic = !strcmp(lang, "ru");
    const char *std[] = {"/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc", "C:/Windows/Fonts/YuGothR.ttc",
                         "C:/Windows/Fonts/YuGothM.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf",
                         "C:/Windows/Fonts/arial.ttf",
                         // Linux: DejaVu Sans (Debian / Ubuntu, Arch, Fedora). Not Noto Sans CJK: CFF outlines,
                         // which the game's font engine crashes on (it wants TrueType)
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                         "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"};
    for (size_t i = 0; i < sizeof std / sizeof *std && !fonts[0].addr; i++)
        if (!(cyrillic && i < 3)) load_one(std[i], &fonts[0], true);  // (0-2: the Japanese gothics)
    if (!fonts[0].addr) FATAL("no usable system font substitute\n");
    font_tried[0] = 1;
    load_dump_font(5);
}

static const Font *font_for(uint64_t type) {
    load_fonts();
    if (type >= 1 && type < NFONTS) {
        load_dump_font((unsigned)type);
        if (fonts[type].addr) return &fonts[type];
    }
    return &fonts[0];
}

HLE_FUNC("_ZN2nn2pl21RequestSharedFontLoadENS0_14SharedFontTypeE", pl_request_load) { (void)c; }  // (always loaded)
HLE_FUNC("_ZN2nn2pl22GetSharedFontLoadStateENS0_14SharedFontTypeE", pl_get_load_state) { c->x[0] = 1; }
HLE_FUNC("_ZN2nn2pl20GetSharedFontAddressENS0_14SharedFontTypeE", pl_get_address) { c->x[0] = font_for(c->x[0])->addr; }
HLE_FUNC("_ZN2nn2pl17GetSharedFontSizeENS0_14SharedFontTypeE", pl_get_size) { c->x[0] = font_for(c->x[0])->size; }
