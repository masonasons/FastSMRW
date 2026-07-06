#!/usr/bin/env bash
# Fetches FastSMRW's single-header dependencies into the gitignored deps/ so the
# macOS build (and the C-ABI smoke test) can compile. Mirrors the pinned
# versions in download-deps.bat. UniversalSpeech is Windows-only and skipped —
# on macOS the core plays earcons via miniaudio and speech is handled by the app
# (VoiceOver), so no extra speech dependency is needed.
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

echo "=== FastSMRW fetch-deps (macOS) ==="
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

echo "Dependencies ready in deps/."
