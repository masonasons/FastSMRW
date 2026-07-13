cmake_minimum_required(VERSION 3.22.1)
project(fastsm LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Build the shared C++ core (core/) plus the JNI bridge into libfastsm.so.
# The Windows-only sources (winhttp_client, the C-ABI factory) are excluded;
# paths.cpp and dpapi.cpp compile portably (POSIX / passthrough branches). This
# mirrors the CORE_SRC list in build.bat minus the Win32 pieces.
# ---------------------------------------------------------------------------

# cpp/ -> main -> src -> app -> android -> repo root
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(CORE "${REPO_ROOT}/core/src")
set(DEPS "${REPO_ROOT}/deps")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SRC
    ${CORE}/version.cpp
    ${CORE}/net/http_client.cpp
    ${CORE}/net/sse_parser.cpp
    ${CORE}/models/serialization.cpp
    ${CORE}/util/html_stripper.cpp
    ${CORE}/util/quote_text.cpp
    ${CORE}/util/date_parsing.cpp
    ${CORE}/util/relative_date.cpp
    ${CORE}/util/url.cpp
    ${CORE}/util/log.cpp
    ${CORE}/util/languages.cpp
    ${CORE}/util/demojify.cpp
    ${CORE}/util/base64.cpp
    ${CORE}/platform/mastodon/mastodon_map.cpp
    ${CORE}/platform/mastodon/mastodon_account.cpp
    ${CORE}/platform/bluesky/bluesky_map.cpp
    ${CORE}/platform/bluesky/bluesky_account.cpp
    ${CORE}/auth/mastodon_auth.cpp
    ${CORE}/auth/bluesky_auth.cpp
    ${CORE}/store/paths.cpp
    ${CORE}/store/dpapi.cpp
    ${CORE}/store/timeline_cache.cpp
    ${CORE}/store/timeline_codec.cpp
    ${CORE}/store/app_config.cpp
    ${CORE}/store/app_settings.cpp
    ${CORE}/store/account_store.cpp
    ${CORE}/runtime/worker_queue.cpp
    ${CORE}/timeline/timeline_controller.cpp
    ${CORE}/timeline/streaming_client.cpp
    ${CORE}/timeline/movement.cpp
    ${CORE}/timeline/client_filter.cpp
    ${CORE}/presentation/status_presenter.cpp
    ${CORE}/presentation/speech_settings.cpp
    ${CORE}/presentation/alias_store.cpp
    ${CORE}/presentation/reply_helper.cpp
    ${CORE}/sound/sound_manager.cpp
    ${CORE}/input/keymap.cpp
    ${CORE}/update/update_checker.cpp
    ${CORE}/session/core_session.cpp
)

# stb_vorbis is C and warns heavily; compile it quietly.
set_source_files_properties(${DEPS}/stb_vorbis/stb_vorbis.c PROPERTIES COMPILE_FLAGS "-w")

add_library(fastsm SHARED
    ${CORE_SRC}
    ${DEPS}/stb_vorbis/stb_vorbis.c
    fastsm_jni.cpp
)

target_include_directories(fastsm PRIVATE
    ${REPO_ROOT}/core/include
    ${DEPS}
    ${DEPS}/stb_vorbis
)

# nlohmann/json and miniaudio need exceptions + RTTI (MSVC builds with /EHsc).
target_compile_options(fastsm PRIVATE -fexceptions -frtti)

# miniaudio (compiled inside sound_manager.cpp) dynamically loads AAudio/OpenSL,
# so no direct audio link is needed; log/android are handy for diagnostics.
find_library(log-lib log)
target_link_libraries(fastsm ${log-lib} android)
