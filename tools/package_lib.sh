# Sourced by tools/package.sh (Windows, Linux) and tools/bundle_macos.sh (the .app): what every player package holds
# besides the runtime itself, in <dest> (the package folder, or the .app's Contents/Resources). Nothing from the game:
# the setup screen (runtime/ui/launcher.mm) makes everything game-specific on the player's machine from their own dump.
#   python/        a Python 3.12 of its own, with capstone, unicorn and lz4 (tools/fetch_python.sh)
#   tools/         the recompiler (gen.py, gen_nros.py and their modules) and the game's setup script (setup.py,
#                  versions.py: the known v1.0.0 build ids and hashes)
#   gamelib-src/   the headers the game code is compiled against, its entry file, the HLE list
#   zig/           the compiler the game code is built with (tools/fetch_zig.sh)
#   licenses/      third-party licenses; THIRD-PARTY.txt says which is which
# usage: stage_common <dest> <target> <zig target>   (target: windows-x86_64 | linux-x86_64 | linux-aarch64 | macos-aarch64 | macos-x86_64)
stage_common() {
    local dest=$1 target=$2 zt=$3
    mkdir -p "$dest/tools/games/mps" "$dest/gamelib-src" "$dest/licenses"
    # Python
    [ -d "build/python-$target" ] || tools/fetch_python.sh "$target"
    cp -R "build/python-$target" "$dest/python"
    find "$dest/python" -name '__pycache__' -type d -prune -exec rm -rf {} +
    # the recompiler and the setup scripts
    cp tools/gen.py tools/lifter.py tools/analyze.py tools/nso.py tools/passes.py tools/patches.py tools/simd_c.py tools/gen_nros.py "$dest/tools/"
    cp games/mps/setup.py games/mps/versions.py "$dest/tools/games/mps/"  # (folders only: no keys, no hactool)
    # what the game code is compiled against (the same files GAMELIB_ID hashes)
    cp runtime/cpu.h runtime/simd.h runtime/simd_fp.h runtime/gamelib.h runtime/game.h tools/gamelib_entry.c "$dest/gamelib-src/"
    cp games/mps/hle_symbols.txt "$dest/gamelib-src/hle_symbols.txt"
    if [ -f games/mps/patches.py ]; then cp games/mps/patches.py "$dest/gamelib-src/patches.py"; fi
    # the compiler
    [ -d "build/zig-$zt" ] || ZIG_TARGET=$zt ZIG_OUT="build/zig-$zt" tools/fetch_zig.sh
    cp -R "build/zig-$zt" "$dest/zig"
    # licenses
    cp LICENSE "$dest/licenses/LICENSE-GPL-3.0.txt"
    cp LICENSE-LASRecomp "$dest/licenses/LICENSE-LASRecomp.txt"
    cp third_party/imgui/LICENSE.txt "$dest/licenses/LICENSE-imgui.txt"
    cp third_party/smaa/LICENSE.txt "$dest/licenses/LICENSE-smaa.txt" 2>/dev/null || true
    cp third_party/zstd/LICENSE "$dest/licenses/LICENSE-zstd.txt"
    cp third_party/bcdec/LICENSE "$dest/licenses/LICENSE-bcdec.txt"
    cp third_party/mbedtls/LICENSE "$dest/licenses/Apache-2.0.txt"
    cp LICENSE-Ryujinx "$dest/licenses/LICENSE-Ryujinx.txt"
    cp third_party/fonts/PressStart2P/OFL.txt "$dest/licenses/OFL-PressStart2P.txt"
    {
        echo "This program: GPL-3.0-or-later, licenses/LICENSE-GPL-3.0.txt (source: its repository). Third-party components and their licenses:"
        echo
        echo "- LAS Recomp (the recompiler and runtime SMM2 Recomp, and so this, is forked from): MIT, licenses/LICENSE-LASRecomp.txt"
        echo "- Dear ImGui (the setup screen and the overlay menu): MIT, licenses/LICENSE-imgui.txt"
        echo "- SMAA (built in): MIT-style, licenses/LICENSE-smaa.txt"
        echo "- Mbed TLS 3.6 (built in: the game's TLS client): Apache-2.0, licenses/Apache-2.0.txt"
        echo "- Zstandard (built in: the shader cache): BSD-3-Clause, licenses/LICENSE-zstd.txt"
        echo "- bcdec (built in: BC texture decoding where the GPU lacks it): MIT / Unlicense, licenses/LICENSE-bcdec.txt"
        echo "- Ryujinx (parts of the runtime follow or port it: audio, shader decoding, some system services): MIT, licenses/LICENSE-Ryujinx.txt"
        echo "- Press Start 2P (built in: the overlay menu's NES theme font): SIL Open Font License 1.1, licenses/OFL-PressStart2P.txt"
        echo "- zig (zig/, the compiler the game code is built with): MIT, zig/LICENSE; its clang / lld: Apache-2.0 with"
        echo "  LLVM exception (licenses/Apache-2.0.txt)"
        echo "- Python 3.12 (python/, python-build-standalone): PSF License and its components' own: python/README-LICENSES.txt"
        echo "- capstone (BSD-3-Clause), unicorn (GPL-2.0; source: https://github.com/unicorn-engine/unicorn) and lz4"
        echo "  (BSD-3-Clause), the game code generator's Python packages: python/.../site-packages/*.dist-info"
        case "$target" in
        windows-*)
            echo "- SDL3 (SDL3.dll): zlib license, https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt"
            echo "- shaderc, glslang, SPIRV-Tools (libshaderc_shared.dll): Apache-2.0 / BSD-3-Clause, licenses/Apache-2.0.txt"
            echo "- libiconv (libiconv-2.dll, used by SDL3): LGPL-2.1, an unmodified MSYS2 build (source: https://www.gnu.org/software/libiconv/)" ;;
        linux-*)
            echo "- shaderc, glslang, SPIRV-Tools (lib/libshaderc.so.1, Debian's build): Apache-2.0 / BSD-3-Clause, licenses/Apache-2.0.txt"
            echo "- SDL3 and the Vulkan loader come from your distribution (not included)" ;;
        macos-*)
            echo "- SDL3 (Contents/Frameworks/libSDL3.0.dylib): zlib license, licenses/LICENSE-SDL3.txt" ;;
        esac
    } > "$dest/THIRD-PARTY.txt"
}

# The player's README.txt. usage: write_readme <file> <target> <version>
write_readme() {
    local f=$1 target=$2 ver=$3 run data
    case "$target" in
        windows-*) run='mpsrecomp.exe' data='this folder (keep it somewhere you can write to, e.g. Documents - not Program Files)' ;;
        linux-*)   run='./mpsrecomp' data='~/.local/share/mpsrecomp' ;;
        macos-*)   run='Mario Party Superstars.app' data='~/Library/Application Support/org.local.mpsrecomp' ;;
    esac
    cat > "$f" <<EOF
Mario Party Superstars (recompiled) $ver ($target)

Mario Party Superstars v1.0.0, recompiled for your computer from YOUR OWN game. Nothing of the game is in this
download: you bring your extracted game files, and the setup screen prepares everything from them, on your machine.

1. Start $run.
2. The setup screen asks for a folder with your game's extracted ExeFS and RomFS (version 1.0.0, without updates).
   NSP / XCI files aren't read: no keys or firmware are needed. Your computer's fonts stand in for the console's.
3. Press "Set up and play". The first time it checks your files against the known 1.0.0 version, lays them out and
   recompiles the game code (a few minutes to half an hour), then the game starts. Later starts go straight to the
   game. The overlay menu (F1 / Esc) > Game files brings the setup back.

Your saves, settings, the set-up game files and the logs are in $data.

Keys: WASD stick, arrows d-pad, K/J/L/I = A/B/X/Y, Q/E = L/R, 1/3 = ZL/ZR, Enter = +, Backspace = -, F1 menu.
Controllers work through SDL.
EOF
    case "$target" in
        linux-*) cat >> "$f" <<EOF

Linux needs SDL3 3.2 or newer and a Vulkan 1.3 driver from your distribution (Debian 13, Ubuntu 25.04, Fedora 41,
Arch or newer: e.g. sudo apt install libsdl3-0 mesa-vulkan-drivers). x86-64 needs a CPU with x86-64-v2 (AVX2 used when available).
EOF
        ;;
        windows-*) cat >> "$f" <<EOF

Windows 10 / 11, 64-bit, a CPU with x86-64-v2 (AVX2 used when available) and a graphics driver with Vulkan 1.3.
EOF
        ;;
        macos-*) cat >> "$f" <<EOF

macOS 15 or newer, on Apple silicon or Intel (x86-64). The app isn't notarized: the first time, right-click it and choose Open (macOS
then checks the whole app once, which can take a few minutes). If
macOS still refuses it or the recompiler ("zig", "python3") is blocked, run once in Terminal:
    xattr -dr com.apple.quarantine "/path/to/Mario Party Superstars.app"
EOF
        ;;
    esac
    cat >> "$f" <<EOF

Help and the source: this project's repository (README.md). Online play and local wireless aren't supported.
EOF
    case "$target" in windows-*) sed -i.bak 's/$/\r/' "$f" && rm -f "$f.bak" ;; esac
}
