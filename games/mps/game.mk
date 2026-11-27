# Mario Party Superstars (title 01006FE013472000), version 1.0.0: what the shared Makefile needs to know.
GAME_ID        := mps
GAME_TITLE     := Mario Party Superstars
GAME_MOBILE_TITLE := MP Superstars
GAME_BUNDLE_ID := org.local.mpsrecomp
GAME_TITLE_ID  := 0x01006FE013472000
GAME_DISPLAY_VERSION := 1.0.0
# No movie playback: the game ships no videos (no romfs movie/ folder, none in its .bea archives; the opening is
# real-time 3D), though subsdk1 (the SDK's multimedia player) and main's BEXMovieMgr are linked in
GAME_MOVIES    := 0
# No frame pacing through the GPU counters (SMM2's 30 fps switch): MPS reads them for its dynamic resolution, and the
# "slow" frames it reported kept the 3D at the lowest one (800x450 of 1600x900)
GAME_PACE      := 0
# Its GPU counters one value per presented frame: real timestamps also count the GPU waiting for our command translation
# between submissions (~15 of a 16 ms frame at the title, more on the boards), which its dynamic resolution read as a
# saturated GPU. shortcut: the counters then show no GPU time at all; measure busy time only if a game needs real values
GAME_COUNTER   := frozen
# The console's load order: main, the subsdks, sdk (module i at MODULE_BASE(i), runtime/gamelib.h)
GAME_MODULES   := main subsdk0 subsdk1 sdk
# The boards' and minigames' code, loaded with nn::ro (runtime/hle/ro.c): 126 NROs
GAME_NRO_DIR   := romfs/nro/NX_Release

GAME_SRC       := $(wildcard games/mps/*.c)
# Guest symbols replaced by host code before recompiling (one mangled name per line; empty: none)
GAME_HLE       := games/mps/hle_symbols.txt
GAME_PATCHES   := games/mps/patches.py
GAME_PATCH_DEPS :=

# Your own game, extracted (a folder with exefs/ and romfs/ of v1.0.0): `make mps DUMP=<that folder>` (or `make setup`)
# checks it and lays it out in $(GAMEFILES); `make gen` reads $(GAMEFILES) (DATA=<folder with extracted/> to use another)
DUMP           ?= $(HOME)/mps-extracted
SETUP_ARGS     ?= --source "$(DUMP)"
