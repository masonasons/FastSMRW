#!/usr/bin/env bash
# Builds fastsm_core as a static lib on macOS with clang (no Xcode needed), and
# optionally the C-ABI smoke test. This is the command-line mirror of the
# fastsm_core target in FastSMRW.xcodeproj — it compiles the same portable source
# list the Android CMakeLists uses, plus the C-ABI factory and the Darwin
# NSURLSession transport, minus the Windows-only winhttp_client.
#
# Usage:  macos/build-core.sh [smoke]
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

./macos/fetch-deps.sh

OUT=build/macos
OBJ="$OUT/obj"
mkdir -p "$OBJ"

CORE=core/src
CXXFLAGS=(-std=c++20 -fexceptions -frtti -O2 -g
    -I core/include -I deps -I deps/stb_vorbis
    -Wall -Wno-deprecated-declarations)

# Portable core sources (mirrors android/app/src/main/cpp/CMakeLists.txt), plus
# the C-ABI factory. Windows-only winhttp_client is excluded; the Darwin
# transport (.mm) is compiled separately with ARC.
CORE_SRC=(
    version.cpp
    net/http_client.cpp net/sse_parser.cpp
    models/serialization.cpp
    util/html_stripper.cpp util/quote_text.cpp util/date_parsing.cpp
    util/relative_date.cpp util/url.cpp util/log.cpp util/languages.cpp
    util/demojify.cpp util/base64.cpp
    platform/mastodon/mastodon_map.cpp platform/mastodon/mastodon_account.cpp
    platform/bluesky/bluesky_map.cpp platform/bluesky/bluesky_account.cpp
    platform/bluesky/bluesky_richtext.cpp
    auth/mastodon_auth.cpp auth/bluesky_auth.cpp
    store/paths.cpp store/dpapi.cpp store/timeline_cache.cpp store/timeline_codec.cpp
    store/app_config.cpp store/app_settings.cpp store/account_store.cpp
    runtime/worker_queue.cpp
    timeline/timeline_controller.cpp timeline/streaming_client.cpp
    timeline/movement.cpp timeline/client_filter.cpp
    presentation/status_presenter.cpp presentation/speech_settings.cpp
    presentation/reply_helper.cpp
    sound/sound_manager.cpp
    input/keymap.cpp
    update/update_checker.cpp
    session/core_session.cpp
    capi/fastsm_core.cpp
)

echo "=== Compiling core ($((${#CORE_SRC[@]})) C++ sources) ==="
OBJS=()
for src in "${CORE_SRC[@]}"; do
    obj="$OBJ/$(echo "$src" | tr '/' '_').o"
    clang++ "${CXXFLAGS[@]}" -c "$CORE/$src" -o "$obj"
    OBJS+=("$obj")
done

echo "=== Compiling Darwin transport (Objective-C++, ARC) ==="
clang++ "${CXXFLAGS[@]}" -fobjc-arc -c "$CORE/net/darwin_http_client.mm" \
    -o "$OBJ/darwin_http_client.o"
OBJS+=("$OBJ/darwin_http_client.o")

echo "=== Compiling stb_vorbis (C, warnings off) ==="
clang -std=c11 -w -O2 -I deps/stb_vorbis -c deps/stb_vorbis/stb_vorbis.c \
    -o "$OBJ/stb_vorbis.o"
OBJS+=("$OBJ/stb_vorbis.o")

echo "=== Archiving fastsm_core.a ==="
libtool -static -o "$OUT/fastsm_core.a" "${OBJS[@]}" 2>/dev/null

FRAMEWORKS=(-framework Foundation -framework CoreFoundation
    -framework CoreAudio -framework AudioToolbox -framework AudioUnit)

if [ "${1:-}" = "smoke" ]; then
    echo "=== Building + running the C-ABI smoke test ==="
    clang++ "${CXXFLAGS[@]}" macos/smoke_core.cpp "$OUT/fastsm_core.a" \
        "${FRAMEWORKS[@]}" -o "$OUT/smoke_core"
    "$OUT/smoke_core"
fi

echo "Done: $OUT/fastsm_core.a"
