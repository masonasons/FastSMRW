@echo off
REM Build script for FastSMRW (FastPlay-style direct cl/lib build, no CMake).
REM
REM Usage:
REM   build.bat              Release build (fastsm_core.lib + FastSMRW.exe)
REM   build.bat debug        Debug build (/MTd, /Zi, symbols)
REM   build.bat test         Build + run the unit tests
REM   build.bat clean        Remove the build\ tree
REM   (args combine, e.g. "build.bat debug test")
REM
REM Convention: keep source file basenames unique within a target (objs are
REM flattened into one dir per target).

setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ---- locate Visual Studio and set up the x64 toolchain ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Error: vswhere.exe not found - Visual Studio 2017+ with C++ tools required
    exit /b 1
)
set "VSINSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo Error: Cannot find Visual Studio with the C++ toolset
    exit /b 1
)
if not exist "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" (
    echo Error: Cannot find vcvars64.bat under "%VSINSTALL%"
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

REM ---- ensure dependencies are present (not committed; fetched into deps\) ----
if not exist deps\nlohmann\json.hpp (
    echo Dependencies missing - running download-deps.bat...
    call download-deps.bat
    if errorlevel 1 exit /b 1
)

REM ---- parse arguments ----
set "CONFIG=release"
set "RUN_TESTS=0"
set "DO_CLEAN=0"
set "BUILD_DLL=0"
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="debug"   set "CONFIG=debug"
if /i "%~1"=="release" set "CONFIG=release"
if /i "%~1"=="test"    set "RUN_TESTS=1"
if /i "%~1"=="clean"   set "DO_CLEAN=1"
if /i "%~1"=="dll"     set "BUILD_DLL=1"
shift
goto parse_args
:args_done

if "%DO_CLEAN%"=="1" (
    if exist build rmdir /s /q build
    echo Cleaned build\ tree.
    if "%RUN_TESTS%"=="0" goto end
)

set "BUILD=build\%CONFIG%"
set "OBJ=%BUILD%\obj"
if not exist "%OBJ%\core" mkdir "%OBJ%\core"
if not exist "%OBJ%\app"  mkdir "%OBJ%\app"
if not exist "%OBJ%\test" mkdir "%OBJ%\test"

REM ---- compiler flags ----
REM /external:W0 silences warnings from fetched single-header libs while we keep
REM /W4 on our own code; deps\ is also the search root for those includes.
set "COMMON=/nologo /std:c++20 /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /external:W0 /external:I deps"
if /i "%CONFIG%"=="debug" (
    set "CFLAGS=%COMMON% /Od /Zi /MTd /D_DEBUG"
    set "RUNTIME=/MTd"
    set "LINKFLAGS=/DEBUG"
) else (
    set "CFLAGS=%COMMON% /O2 /MT /DNDEBUG"
    set "RUNTIME=/MT"
    set "LINKFLAGS="
)
set "COREINC=/I core\include"

REM ---- embed the short git commit so the "latest" update branch can tell builds
REM apart (empty for a build outside a git checkout) ----
set "BUILD_COMMIT="
for /f "tokens=*" %%i in ('git rev-parse --short HEAD 2^>nul') do set "BUILD_COMMIT=%%i"
if defined BUILD_COMMIT set "CFLAGS=%CFLAGS% /DFASTSM_BUILD_COMMIT=\"%BUILD_COMMIT%\""

echo.
echo === Building FastSMRW [%CONFIG%] ===

REM ---- 1) core -> fastsm_core.lib ----
set "CORE_SRC=core\src\version.cpp core\src\net\http_client.cpp core\src\net\winhttp_client.cpp core\src\net\sse_parser.cpp core\src\models\serialization.cpp"
set "CORE_SRC=%CORE_SRC% core\src\util\html_stripper.cpp core\src\util\date_parsing.cpp core\src\util\relative_date.cpp core\src\util\url.cpp"
set "CORE_SRC=%CORE_SRC% core\src\platform\mastodon\mastodon_map.cpp core\src\platform\mastodon\mastodon_account.cpp"
set "CORE_SRC=%CORE_SRC% core\src\platform\bluesky\bluesky_map.cpp core\src\platform\bluesky\bluesky_account.cpp"
set "CORE_SRC=%CORE_SRC% core\src\auth\mastodon_auth.cpp core\src\auth\bluesky_auth.cpp"
set "CORE_SRC=%CORE_SRC% core\src\util\base64.cpp core\src\store\paths.cpp core\src\store\dpapi.cpp core\src\store\timeline_cache.cpp core\src\store\timeline_codec.cpp core\src\store\app_config.cpp core\src\store\account_store.cpp"
set "CORE_SRC=%CORE_SRC% core\src\runtime\worker_queue.cpp core\src\timeline\timeline_controller.cpp core\src\timeline\streaming_client.cpp core\src\timeline\movement.cpp"
set "CORE_SRC=%CORE_SRC% core\src\presentation\status_presenter.cpp core\src\presentation\speech_settings.cpp core\src\presentation\reply_helper.cpp core\src\sound\sound_manager.cpp"
set "CORE_SRC=%CORE_SRC% core\src\util\languages.cpp core\src\util\demojify.cpp"
set "CORE_SRC=%CORE_SRC% core\src\store\app_settings.cpp"
set "CORE_SRC=%CORE_SRC% core\src\input\keymap.cpp"
set "CORE_SRC=%CORE_SRC% core\src\update\update_checker.cpp"
set "CORE_SRC=%CORE_SRC% core\src\session\core_session.cpp core\src\capi\fastsm_core.cpp"
echo Compiling core...
cl %CFLAGS% %COREINC% /c %CORE_SRC% /Fo"%OBJ%\core\\"
if errorlevel 1 goto error
REM stb_vorbis is C and warns heavily; compile it separately, warnings off.
cl /nologo /c /w /O2 %RUNTIME% /D_CRT_SECURE_NO_WARNINGS deps\stb_vorbis\stb_vorbis.c /Fo"%OBJ%\core\stb_vorbis.obj"
if errorlevel 1 goto error
lib /nologo /OUT:"%BUILD%\fastsm_core.lib" "%OBJ%\core\*.obj"
if errorlevel 1 goto error

REM ---- optional: fastsm_core.dll (exported C ABI, for non-C++ language bindings) ----
if "%BUILD_DLL%"=="1" (
    echo Building fastsm_core.dll ...
    cl %CFLAGS% /DFASTSM_CORE_DLL /DFASTSM_CORE_BUILD %COREINC% /c core\src\capi\fastsm_core.cpp /Fo"%OBJ%\dll_capi.obj"
    if errorlevel 1 goto error
    REM dll_capi.obj defines the exports; the rest is pulled from the static lib.
    link /nologo /DLL "%OBJ%\dll_capi.obj" "%BUILD%\fastsm_core.lib" winhttp.lib crypt32.lib ole32.lib winmm.lib /OUT:"%BUILD%\fastsm_core.dll" /IMPLIB:"%BUILD%\fastsm_core_dll.lib"
    if errorlevel 1 goto error
)

REM ---- optional: UniversalSpeech static speech (built by download-deps.bat) ----
set "USPEECH_INC="
set "USPEECH_DEF="
set "USPEECH_LIB="
if exist "deps\UniversalSpeech\UniversalSpeechStatic.lib" (
    echo UniversalSpeech static lib found - enabling speech output.
    set "USPEECH_INC=/I deps\UniversalSpeech\include"
    set "USPEECH_DEF=/DHAVE_UNIVERSALSPEECH /DUSE_UNIVERSAL_SPEECH /DUNIVERSAL_SPEECH_STATIC"
    set "USPEECH_LIB=deps\UniversalSpeech\UniversalSpeechStatic.lib oleaut32.lib version.lib psapi.lib"
)

REM ---- 2) Win32 app -> FastSMRW.exe ----
echo Compiling resources...
rc /nologo /I windows\resources /fo "%BUILD%\app.res" windows\resources\app.rc
if errorlevel 1 goto error
echo Compiling and linking FastSMRW.exe...
set "APP_SRC=windows\src\main.cpp windows\src\main_window.cpp windows\src\compose_dialog.cpp windows\src\add_account_dialog.cpp windows\src\new_timeline_dialog.cpp windows\src\settings_dialog.cpp windows\src\post_info_dialog.cpp windows\src\user_profile_dialog.cpp windows\src\invisible_hotkeys.cpp windows\src\invisible_keyhook.cpp windows\src\keymap_manager_dialog.cpp windows\src\win_speech.cpp"
cl %CFLAGS% %USPEECH_DEF% %COREINC% /I windows\src %USPEECH_INC% %APP_SRC% "%BUILD%\fastsm_core.lib" "%BUILD%\app.res" /Fo"%OBJ%\app\\" /Fe"%BUILD%\FastSMRW.exe" /link %LINKFLAGS% user32.lib gdi32.lib comctl32.lib shell32.lib winhttp.lib crypt32.lib ole32.lib winmm.lib %USPEECH_LIB%
if errorlevel 1 goto error

REM ---- 2b) assemble the dist run folder (exe + bundled assets) ----
echo Assembling dist...
if not exist dist mkdir dist
xcopy /e /i /y assets\* dist\ >nul
copy /y "%BUILD%\FastSMRW.exe" dist\ >nul
if exist docs xcopy /e /i /y docs\* dist\docs\ >nul
REM UniversalSpeech runtime bridge DLLs (NVDA/SAPI/ZDSR), if present.
if exist "deps\UniversalSpeech\bin-x64\*.dll" copy /y deps\UniversalSpeech\bin-x64\*.dll dist\ >nul

REM ---- 3) optional: tests ----
if "%RUN_TESTS%"=="1" (
    echo Compiling tests...
    cl %CFLAGS% %COREINC% /I tests tests\main.cpp tests\test_models.cpp tests\test_util.cpp tests\test_mastodon_map.cpp tests\test_bluesky_map.cpp tests\test_auth.cpp tests\test_store.cpp tests\test_presentation.cpp tests\test_speech.cpp tests\test_sse.cpp tests\test_capi.cpp tests\test_thread.cpp tests\test_keymap.cpp tests\test_update.cpp "%BUILD%\fastsm_core.lib" /Fo"%OBJ%\test\\" /Fe"%BUILD%\fastsm_tests.exe" /link %LINKFLAGS% crypt32.lib
    if errorlevel 1 goto error
    echo Running tests...
    "%BUILD%\fastsm_tests.exe"
    if errorlevel 1 (
        echo.
        echo Tests FAILED.
        exit /b 1
    )
)

echo.
echo Build successful.  Output in %BUILD%\
goto end

:error
echo.
echo Build FAILED.
exit /b 1

:end
endlocal
