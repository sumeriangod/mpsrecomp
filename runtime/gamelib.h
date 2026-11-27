// The recompiled game code: a library built from the player's own game files (`make gen` + `make` in a source
// checkout, the start screen's setup otherwise; runtime/gamebuild.c) and loaded at startup (runtime/gamelib.c).
// Nothing from the game ships with the app.
#pragma once
#include "game.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu.h"

#if defined(_WIN32)
#define GAMELIB_NAME GAME_ID "game.dll"
#define GAMELIB_EXPORT __declspec(dllexport)
#elif defined(__APPLE__)
#define GAMELIB_NAME GAME_ID "game.dylib"
#define GAMELIB_EXPORT __attribute__((visibility("default")))
#elif defined(__ANDROID__)
#define GAMELIB_NAME "lib" GAME_ID "game.so"  // (an APK's native libraries are lib*.so; loaded by this name)
#define GAMELIB_EXPORT __attribute__((visibility("default")))
#else
#define GAMELIB_NAME GAME_ID "game.so"
#define GAMELIB_EXPORT __attribute__((visibility("default")))
#endif

// GAMELIB_ID (-D, from the Makefile): a hash of the generator and the headers the game code is compiled against. A
// library built with another one is rebuilt, not loaded.
#ifndef GAMELIB_ID
#define GAMELIB_ID "dev"
#endif
#define GAMELIB_ABI 3

// The game's NSO modules in the console's load order (main, the subsdks, sdk), X(name) each: game.mk GAME_MODULES.
// Module i is loaded and recompiled at MODULE_BASE(i) (runtime/rt.h).
#ifndef GAME_MODULES_X
#define GAME_MODULES_X X(main) X(sdk)
#endif

typedef struct {
    const char *name;
    const uint64_t *addrs;  // sorted guest addresses
    const GuestFn *fns;     // the functions at them
    const uint64_t *count;
} GameLibModule;

// An NRO the game loads at run time (nn::ro, runtime/hle/ro.c), recompiled for a fixed base (tools/gen_nros.py).
typedef struct {
    GameLibModule fns;       // its file name without .nro, and its functions
    uint64_t base;           // NRO_BASE + slot * NRO_SLOT (runtime/rt.h): where nn::ro::LoadModule places it
    uint8_t build_id[0x20];  // the NRO header's: how LoadModule recognizes the image the game hands it
} GameLibNro;

typedef struct {
    uint32_t abi;    // GAMELIB_ABI
    const char *id;  // GAMELIB_ID
    uint32_t nmods;  // GAME_MODULES_X, in that order
    const GameLibModule *mods;
    const GameLibNro *nros;  // up to an entry with base 0; NULL: a game without NROs
} GameLibTables;

// The library's one exported function (tools/gamelib_entry.c).
typedef const GameLibTables *(*GameLibEntry)(void);
#define GAMELIB_ENTRY "recomp_game_tables"

#ifdef __cplusplus
extern "C" {
#endif

// Loads the game code for the game files in game_dir (once; later calls return the first result). Looks at
// LARECOMP_GAME_LIB, next to the executable (a source checkout's build/), then game_dir. false: why says what's wrong.
bool gamelib_load(const char *game_dir, char *why, size_t why_size);
// The tables of the loaded library (NULL before gamelib_load succeeds).
const GameLibTables *gamelib_tables(void);
// True when a library usable with this build is in one of those places (without loading it).
bool gamelib_present(const char *game_dir);

#ifdef __cplusplus
}
#endif
