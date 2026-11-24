#!/usr/bin/env bash
# Builds the Linux GTK app: fastsm_core via linux/build-core.sh, then the GTK3
# front end, then assembles the dist/linux run folder (binary + bundled default
# soundpacks). Requires: g++, pkg-config, libgtk-3-dev, libcurl4-openssl-dev,
# libspeechd-dev.
#
# Usage:  linux/build.sh [clean] [test] [smoke]
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

./linux/build-core.sh "$@"

OUT=build/linux
APP_OBJ="$OUT/app-obj"
mkdir -p "$APP_OBJ"

GTK_CFLAGS=$(pkg-config --cflags gtk+-3.0 gstreamer-1.0 x11)
GTK_LIBS=$(pkg-config --libs gtk+-3.0 gstreamer-1.0 x11)
SPD_CFLAGS=$(pkg-config --cflags speech-dispatcher 2>/dev/null || echo "")
SPD_LIBS=$(pkg-config --libs speech-dispatcher 2>/dev/null || echo "-lspeechd")

CXXFLAGS=(-std=c++20 -O2 -g -Wall -I core/include -I deps)

APP_SRC=(main.cpp main_window.cpp compose_dialog.cpp post_info_dialog.cpp settings_dialog.cpp
    speech_detail_dialog.cpp user_profile_dialog.cpp invisible_hotkeys.cpp report_dialog.cpp
    keymap_manager_dialog.cpp media_player.cpp speech.cpp)

# Any changed header invalidates all app objects (see build-core.sh).
if [ -f "$OUT/FastSMRW" ] &&
    [ -n "$(find linux/src core/include -type f \( -name '*.h' -o -name '*.hpp' \) \
        -newer "$OUT/FastSMRW" -print -quit 2>/dev/null)" ]; then
    echo "(header changed: full app rebuild)"
    rm -f "$APP_OBJ"/*.o
fi

echo "=== Compiling app (${#APP_SRC[@]} sources) ==="
OBJS=()
for src in "${APP_SRC[@]}"; do
    obj="$APP_OBJ/${src%.cpp}.o"
    OBJS+=("$obj")
    if [ -f "$obj" ] && [ ! "linux/src/$src" -nt "$obj" ]; then
        continue
    fi
    echo "  CXX $src"
    # shellcheck disable=SC2086  # pkg-config output must word-split
    g++ "${CXXFLAGS[@]}" $GTK_CFLAGS $SPD_CFLAGS -c "linux/src/$src" -o "$obj"
done

echo "=== Linking FastSMRW ==="
# shellcheck disable=SC2086
g++ "${OBJS[@]}" "$OUT/fastsm_core.a" $GTK_LIBS $SPD_LIBS -lcurl -lpthread -ldl -lm \
    -o "$OUT/FastSMRW"

echo "=== Assembling dist/linux ==="
mkdir -p dist/linux
# The build/ copy keeps its debug info (for gdb); the shipped binary is
# stripped — the -g DWARF for the whole inlined core is tens of MB.
strip -o dist/linux/FastSMRW "$OUT/FastSMRW"
rm -rf dist/linux/soundpacks
cp -r assets/soundpacks dist/linux/soundpacks

echo "Done: dist/linux/FastSMRW"
