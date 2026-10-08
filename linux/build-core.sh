#!/usr/bin/env bash
# Builds fastsm_core as a static lib on Linux with g++ — the Linux mirror of
# macos/build-core.sh. Compiles the same portable source list plus the C-ABI
# factory and the libcurl transport, minus the Windows-only winhttp_client and
# the Darwin NSURLSession transport.
#
# Unlike the macOS script this one is incremental (mtime-based) and parallel,
# because building through WSL's /mnt/c filesystem is slow.
#
# Usage:  linux/build-core.sh [clean] [smoke] [test]
set -euo pipefail
cd "$(dirname "$0")/.."   # repo root

./linux/fetch-deps.sh

OUT=build/linux
SAN=()
if [[ " $* " == *" asan "* ]]; then
    # AddressSanitizer build in its own tree (never mix sanitized objects in).
    OUT=build/linux-asan
    SAN=(-fsanitize=address -fno-omit-frame-pointer)
elif [[ " $* " == *" tsan "* ]]; then
    OUT=build/linux-tsan
    SAN=(-fsanitize=thread -fno-omit-frame-pointer)
fi
OBJ="$OUT/obj"
if [[ " $* " == *" clean "* ]]; then
    rm -rf "$OUT"
fi
mkdir -p "$OBJ"

# The per-file mtime check below only compares each .cpp against its own .o, so
# a changed header would leave stale objects with a different class layout —
# which links "fine" and corrupts memory at runtime. Use the archive as a
# stamp: any newer header invalidates every object.
if [ -f "$OUT/fastsm_core.a" ] &&
    [ -n "$(find core/include core/src deps -type f \( -name '*.h' -o -name '*.hpp' \) \
        -newer "$OUT/fastsm_core.a" -print -quit 2>/dev/null)" ]; then
    echo "(header changed: full core rebuild)"
    rm -f "$OBJ"/*.o
fi

CORE=core/src
CXXFLAGS=(-std=c++20 -fexceptions -frtti -O2 -g "${SAN[@]}"
    -I core/include -I deps -I deps/stb_vorbis
    -Wall -Wno-deprecated-declarations)

# Portable core sources (mirrors macos/build-core.sh), plus the C-ABI factory
# and the libcurl transport.
CORE_SRC=(
    version.cpp
    net/http_client.cpp net/sse_parser.cpp net/curl_http_client.cpp
    models/serialization.cpp models/user_cache.cpp
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
    presentation/reply_helper.cpp presentation/alias_store.cpp
    sound/sound_manager.cpp
    media/media_player.cpp
    input/keymap.cpp
    update/update_checker.cpp
    session/core_session.cpp
    capi/fastsm_core.cpp
)

CORE_LIBS=(-lcurl -lpthread -ldl -lm)

JOBS=$(nproc)
PIDS=()
OBJS=()
throttle() {
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        wait -n
    done
}

echo "=== Compiling core ($((${#CORE_SRC[@]})) C++ sources, up to $JOBS jobs) ==="
for src in "${CORE_SRC[@]}"; do
    obj="$OBJ/$(echo "$src" | tr '/' '_').o"
    OBJS+=("$obj")
    if [ -f "$obj" ] && [ ! "$CORE/$src" -nt "$obj" ]; then
        continue
    fi
    echo "  CXX $src"
    throttle
    g++ "${CXXFLAGS[@]}" -c "$CORE/$src" -o "$obj" &
    PIDS+=($!)
done

STB_OBJ="$OBJ/stb_vorbis.o"
if [ ! -f "$STB_OBJ" ] || [ deps/stb_vorbis/stb_vorbis.c -nt "$STB_OBJ" ]; then
    echo "  CC  stb_vorbis.c"
    throttle
    gcc -std=c11 -w -O2 -I deps/stb_vorbis -c deps/stb_vorbis/stb_vorbis.c -o "$STB_OBJ" &
    PIDS+=($!)
fi
OBJS+=("$STB_OBJ")

for pid in "${PIDS[@]:-}"; do
    [ -n "$pid" ] && wait "$pid"
done

echo "=== Archiving fastsm_core.a ==="
ar rcs "$OUT/fastsm_core.a" "${OBJS[@]}"

if [[ " $* " == *" smoke "* ]]; then
    echo "=== Building + running the C-ABI smoke test ==="
    g++ "${CXXFLAGS[@]}" macos/smoke_core.cpp "$OUT/fastsm_core.a" \
        "${CORE_LIBS[@]}" -o "$OUT/smoke_core"
    "$OUT/smoke_core"
fi

if [[ " $* " == *" test "* ]]; then
    echo "=== Building + running the unit tests ==="
    g++ "${CXXFLAGS[@]}" -I tests tests/*.cpp "$OUT/fastsm_core.a" \
        "${CORE_LIBS[@]}" -o "$OUT/fastsm_tests"
    "$OUT/fastsm_tests"
fi

if [[ " $* " == *" asan "* ]]; then
    echo "(ASan tree: $OUT)"
fi

echo "Done: $OUT/fastsm_core.a"
