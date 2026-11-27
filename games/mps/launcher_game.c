// Mario Party Superstars on the setup screen (runtime/ui/launcher_game.h; its files: games/mps/setup.py, versions.py)
#include "ui/launcher_game.h"

const LauncherGame *launcher_game(void) {
    static const LauncherGame g = {
        .version = "1.0.0",
        .main_build_id = "0413a570bf2b3aa7741c20559ff7b8fa314adbf9",
        .about = "Mario Party Superstars v1.0.0, recompiled on your computer from your own game. Nothing of the game "
                 "comes with this program: point it at your dump, and it does the rest.",
        .game_card = "Mario Party Superstars 1.0.0, the game itself (no update): a folder with its extracted ExeFS and "
                     "RomFS. "
                     "NSP / XCI files aren't read, so no keys or firmware are needed. The version is checked against "
                     "the known 1.0.0 files.",
        .extracted_only = true,
        // (no releases of its own to check for: the setup screen and the menu don't offer the update check)
    };
    return &g;
}
