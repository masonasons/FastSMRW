#!/usr/bin/env bash
# Fetches FastSMRW's single-header dependencies into the gitignored deps/ so the
# Linux build can compile. Mirrors the pinned versions in download-deps.bat.
# UniversalSpeech is Windows-only and skipped — on Linux the core plays earcons
# via miniaudio and the app speaks through Speech Dispatcher (libspeechd).
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

echo "=== FastSMRW fetch-deps (Linux) ==="
mkdir -p deps/nlohmann deps/miniaudio deps/stb_vorbis

fetch() { # url dest
    if [ ! -f "$2" ]; then
        echo "Fetching $(basename "$2")..."
        curl -fsSL -o "$2" "$1"
    fi
}

fetch https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp deps/nlohmann/json.hpp
fetch https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h deps/miniaudio/miniaudio.h
fetch https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c deps/stb_vorbis/stb_vorbis.c

# FastPlay's engine (media playback), from FastPlay's CI: the header and
# libfastplay_engine.so. FASTPLAY_ENGINE_REFRESH=1 fetches the newest again.
# FASTPLAY_ENGINE_DIR=<dir> takes it from a local build instead: a folder with
# include/fastplay_engine/fastplay_engine.h and lib/libfastplay_engine.so (or a
# FastPlay checkout's build-engine*/out for the library).
ENGINE_DIR=deps/fastplay_engine
if [ -n "${FASTPLAY_ENGINE_REFRESH:-}" ] || [ -n "${FASTPLAY_ENGINE_DIR:-}" ]; then
    rm -rf "$ENGINE_DIR"
fi
if [ ! -f "$ENGINE_DIR/lib/libfastplay_engine.so" ]; then
    mkdir -p "$ENGINE_DIR/include/fastplay_engine" "$ENGINE_DIR/lib"
    if [ -n "${FASTPLAY_ENGINE_DIR:-}" ]; then
        echo "Using FastPlay Engine from $FASTPLAY_ENGINE_DIR..."
        cp -p "$FASTPLAY_ENGINE_DIR/include/fastplay_engine/fastplay_engine.h" \
            "$ENGINE_DIR/include/fastplay_engine/"
        lib=$(find "$FASTPLAY_ENGINE_DIR" -name libfastplay_engine.so -print -quit)
        [ -n "$lib" ] || { echo "No libfastplay_engine.so under $FASTPLAY_ENGINE_DIR" >&2; exit 1; }
        cp -p "$lib" "$ENGINE_DIR/lib/"
    else
        echo "Fetching FastPlay Engine..."
        curl -fsSL -o deps/fastplay-engine.zip \
            https://github.com/masonasons/FastPlay/releases/download/engine-latest/fastplay-engine-linux-x64.zip
        unzip -qo deps/fastplay-engine.zip -d "$ENGINE_DIR"
        rm deps/fastplay-engine.zip
    fi
fi

echo "Dependencies ready in deps/."
