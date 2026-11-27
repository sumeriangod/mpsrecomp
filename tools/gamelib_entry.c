// Entry of the game code library (runtime/gamelib.h), compiled into it with the generated code: the function
// tables, found by name when the app loads the library. Everything else in it stays hidden.
#include "gamelib.h"

#define X(n) extern const uint64_t n##_fn_addrs[], n##_fn_count; extern const GuestFn n##_fn_ptrs[];
GAME_MODULES_X
#undef X
#ifdef GAME_HAS_NROS
extern const GameLibNro game_nros[];  // (generated/<game>/nro/index.c, tools/gen_nros.py)
#define NROS game_nros
#else
#define NROS NULL
#endif

GAMELIB_EXPORT const GameLibTables *recomp_game_tables(void) {
#define X(n) {#n, n##_fn_addrs, n##_fn_ptrs, &n##_fn_count},
    static const GameLibModule mods[] = {GAME_MODULES_X};
#undef X
    static const GameLibTables t = {GAMELIB_ABI, GAMELIB_ID, sizeof mods / sizeof *mods, mods, NROS};
    return &t;
}
