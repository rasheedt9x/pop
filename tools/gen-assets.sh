#!/data/data/com.termux/files/usr/bin/bash
# Regenerates assets/ and icon.png from tools/gen-assets.c (Termux's desktop raylib, no window needed)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build assets/images assets/sounds
clang -O2 -o build/gen-assets tools/gen-assets.c -lraylib -lm
build/gen-assets
