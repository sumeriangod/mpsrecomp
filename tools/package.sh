#!/bin/bash
# usage: tools/package.sh <target> <build dir> <game id> <deps dir>   (make package TARGET=... does this)
# A player package for windows-x86_64 (a .zip), linux-x86_64 or linux-aarch64 (a .tar.gz): the runtime with its setup
# screen, what recompiling the game on the player's machine needs, and nothing from the game. On first launch the
# setup screen (runtime/ui/launcher.mm) takes the player's own extracted game files, checks and lays them out, recompiles the game
# code (runtime/gamebuild.c: the bundled Python runs the generator, the bundled zig compiles it) and runs the game.
#   <game id>recomp[.exe]   the runtime
#   <game id>recomp.dll.a   (Windows) its import library: the game code DLL links against the exe
#   *.dll                   (Windows) SDL3 + libiconv, shaderc (Vulkan itself comes with the graphics driver)
#   lib/libshaderc.so.1     (Linux) shaderc; SDL3 and the Vulkan loader come from the distribution
#   python/ tools/ gamelib-src/ zig/ licenses/ THIRD-PARTY.txt (tools/package_lib.sh)
#   README.txt              what to do; the full text is the repository's README.md
# The package is checked for game content (scripts/check_package.py) before it is archived.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/package_lib.sh
TARGET=$1 B=$2 ID=$3 DEPS=${4:-}
VERSION=${RECOMP_VERSION:-$(git describe --tags --match 'v[0-9]*' 2>/dev/null || echo 0.1.0)}
OS=${TARGET%%-*}
EXE=$B/${ID}recomp$([ "$OS" = windows ] && echo .exe || true)
[ -f "$EXE" ] || { echo "no $EXE: make TARGET=$TARGET all first"; exit 1; }
NAME="MPSRecomp-$TARGET"
OUT=dist/$NAME
rm -rf "$OUT" "dist/$NAME.zip" "dist/$NAME.tar.gz"
mkdir -p "$OUT"
cp "$EXE" "$OUT/"
if [ "$OS" = windows ]; then
    cp "$B/${ID}recomp.dll.a" "$OUT/"
    for d in SDL3.dll libiconv-2.dll libshaderc_shared.dll; do cp "$DEPS/bin/$d" "$OUT/"; done
    cp docs/branding/app-icon.ico "$OUT/MPSRecomp.ico"  # (also in the exe: runtime/platform/windows.rc)
else
    mkdir -p "$OUT/lib"
    cp -L "$DEPS/lib/libshaderc.so" "$OUT/lib/libshaderc.so.1"
    # the app icon and a menu entry: install-desktop-entry.sh writes it to ~/.local/share/applications with this
    # folder's absolute paths (a .desktop file can't name files relative to itself)
    cp docs/branding/app-icon-256.png "$OUT/mpsrecomp.png"
    printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Mario Party Superstars' \
        'Comment=Mario Party Superstars, recompiled from your own dump' 'Exec=@DIR@/mpsrecomp' 'Path=@DIR@' \
        'Icon=@DIR@/mpsrecomp.png' 'Terminal=false' 'Categories=Game;' > "$OUT/mpsrecomp.desktop"
    printf '%s\n' '#!/bin/sh' '# Adds Mario Party Superstars to the desktop menu (with its icon), pointing at this folder' 'set -e' \
        'dir=$(cd "$(dirname "$0")" && pwd)' 'mkdir -p "$HOME/.local/share/applications"' \
        'sed "s|@DIR@|$dir|g" "$dir/mpsrecomp.desktop" > "$HOME/.local/share/applications/mpsrecomp.desktop"' \
        'echo "Added: $HOME/.local/share/applications/mpsrecomp.desktop"' > "$OUT/install-desktop-entry.sh"
    chmod +x "$OUT/install-desktop-entry.sh"
fi
ZT=$([ "$TARGET" = windows-x86_64 ] && echo x86_64-windows || echo "${TARGET#*-}-linux")
stage_common "$OUT" "$TARGET" "$ZT"
write_readme "$OUT/README.txt" "$TARGET" "$VERSION"
python3 scripts/check_package.py "$OUT"
if [ "$OS" = windows ]; then
    # (zip, or Python's zipfile where zip isn't installed)
    (cd dist && if command -v zip >/dev/null; then zip -qr -9 "$NAME.zip" "$NAME"; else python3 -c '
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1] + ".zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(sys.argv[1]):
        dirs.sort()
        for f in sorted(files): z.write(os.path.join(root, f))
' "$NAME"; fi)
    ARCHIVE=dist/$NAME.zip
else
    (cd dist && COPYFILE_DISABLE=1 tar -czf "$NAME.tar.gz" "$NAME")
    ARCHIVE=dist/$NAME.tar.gz
fi
echo "$ARCHIVE: $(du -h "$ARCHIVE" | cut -f1) ($(du -sh "$OUT" | cut -f1) unpacked), $VERSION"
