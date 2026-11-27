// The setup screen's words for the game it sets up (runtime/ui/launcher.mm). The weak default there is Super Mario
// Maker 2's; another game defines launcher_game() in games/<game>/ (its setup script: games/<game>/setup.py).
#pragma once
#include <stdbool.h>

typedef struct {
    const char *version;        // the version the recomp is of ("3.0.3"), as games/<game>/versions.py says
    const char *main_build_id;  // that version's main (hex build id, a prefix): the extracted files are this game
    const char *base_build_id;  // (or NULL) the base game's main when the version is an update: "add the update"
    const char *about;          // under the title: what this is
    const char *game_card;      // card 1, "Your game": what to choose
    bool smm1_card;             // card 3: Super Mario Maker (Wii U) files, for SMM2's SMM1 mods
    const char *releases_api, *releases_page;  // the update check: GitHub's latest-release URL and page (NULL: none)
    // Only folders of the extracted ExeFS / RomFS, never NSP / XCI: no keys, firmware or hactool. The computer's fonts
    // stand in for the console's (hle/pl.c) and an empty NgWord2 for its word filter (services/fs.c)
    bool extracted_only;
} LauncherGame;

#ifdef __cplusplus
extern "C"
#endif
const LauncherGame *launcher_game(void);
