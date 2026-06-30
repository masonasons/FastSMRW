@echo off
REM Fetches FastSMRW's external dependencies so the project builds reproducibly
REM (CI and end users). The vendored single-headers are committed, so this is
REM only required to enable UniversalSpeech speech output, but it will also
REM (re)fetch the headers if they are missing.

setlocal
cd /d "%~dp0"
echo === FastSMRW download-deps ===

if not exist deps mkdir deps

REM --- single-header libs ---
if not exist deps\nlohmann\json.hpp (
    echo Fetching nlohmann/json...
    if not exist deps\nlohmann mkdir deps\nlohmann
    curl -fsSL -o deps\nlohmann\json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp || goto err
)
if not exist deps\miniaudio\miniaudio.h (
    echo Fetching miniaudio...
    if not exist deps\miniaudio mkdir deps\miniaudio
    curl -fsSL -o deps\miniaudio\miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h || goto err
)
if not exist deps\stb_vorbis\stb_vorbis.c (
    echo Fetching stb_vorbis...
    if not exist deps\stb_vorbis mkdir deps\stb_vorbis
    curl -fsSL -o deps\stb_vorbis\stb_vorbis.c https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c || goto err
)

REM --- UniversalSpeech (samtupy/UniversalSpeechMSVCStatic): build static lib + bridge DLLs ---
REM Built from source with SCons (needs Python + Visual C++ build tools), like
REM FastPlay. If the build can't run, speech is simply disabled.
if not exist deps\UniversalSpeech (
    echo Cloning UniversalSpeech...
    git clone --depth 1 https://github.com/samtupy/UniversalSpeechMSVCStatic.git deps\UniversalSpeech || goto err
)
if not exist deps\UniversalSpeech\UniversalSpeechStatic.lib (
    echo Building UniversalSpeech with SCons...
    pushd deps\UniversalSpeech
    where scons >nul 2>&1
    if errorlevel 1 (
        echo SCons not found - installing via pip...
        pip install scons
    )
    call scons
    popd
    if exist deps\UniversalSpeech\UniversalSpeechStatic.lib (
        echo UniversalSpeech built.
    ) else (
        echo WARNING: UniversalSpeech build failed ^(need Python + scons + VC build tools^). Speech disabled.
    )
)

echo.
echo Dependencies ready. Run build.bat.
exit /b 0

:err
echo.
echo download-deps FAILED.
exit /b 1
