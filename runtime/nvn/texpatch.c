// Texture edits on upload (texpatch.h). LAS Recomp recognised Link's Awakening's UI textures here (button prompts,
// the Hero Mode icon, tunic and heart colours); that knowledge is the game's, so the shared runtime knows no
// textures: nothing is identified and nothing changes. A game brings its own set through the same interface: these
// are weak, and games/<game>/ may define any of them (games/mps/texpatch.c: MPS's button icons).
#include "texpatch.h"

__attribute__((weak)) int texpatch_identify(const NvnTexture *t) { (void)t; return -1; }
__attribute__((weak)) int texpatch_bottle_kind(int id) { (void)id; return 0; }
__attribute__((weak)) bool texpatch_is_item_cursor(int id) { (void)id; return false; }
__attribute__((weak)) uint32_t texpatch_generation(void) { return 0; }
__attribute__((weak)) void texpatch_apply(int id, uint32_t lv, uint8_t *blocks, uint32_t w, uint32_t h, uint32_t row) {
    (void)id, (void)lv, (void)blocks, (void)w, (void)h, (void)row;
}
__attribute__((weak)) int texpatch_heart_kind(int id) { (void)id; return 0; }
__attribute__((weak)) void texpatch_heart_tint(int kind, float tint[8]) { (void)kind, (void)tint; }
