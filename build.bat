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

REM ---- parse arguments ----
set "CONFIG=release"
set "RUN_TESTS=0"
set "DO_CLEAN=0"
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="debug"   set "CONFIG=debug"
if /i "%~1"=="release" set "CONFIG=release"
if /i "%~1"=="test"    set "RUN_TESTS=1"
if /i "%~1"=="clean"   set "DO_CLEAN=1"
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
REM /external:W0 silences warnings from vendored single-header libs while we keep
REM /W4 on our own code; third_party is also the search root for those includes.
set "COMMON=/nologo /std:c++20 /W4 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /external:W0 /external:I third_party"
if /i "%CONFIG%"=="debug" (
    set "CFLAGS=%COMMON% /Od /Zi /MTd /D_DEBUG"
    set "LINKFLAGS=/DEBUG"
) else (
    set "CFLAGS=%COMMON% /O2 /MT /DNDEBUG"
    set "LINKFLAGS="
)
set "COREINC=/I core\include"

echo.
echo === Building FastSMRW [%CONFIG%] ===

REM ---- 1) core -> fastsm_core.lib ----
set "CORE_SRC=core\src\version.cpp core\src\net\http_client.cpp"
echo Compiling core...
cl %CFLAGS% %COREINC% /c %CORE_SRC% /Fo"%OBJ%\core\\"
if errorlevel 1 goto error
lib /nologo /OUT:"%BUILD%\fastsm_core.lib" "%OBJ%\core\*.obj"
if errorlevel 1 goto error

REM ---- 2) Win32 app -> FastSMRW.exe ----
echo Compiling resources...
rc /nologo /I windows\resources /fo "%BUILD%\app.res" windows\resources\app.rc
if errorlevel 1 goto error
echo Compiling and linking FastSMRW.exe...
cl %CFLAGS% %COREINC% /I windows\src windows\src\main.cpp "%BUILD%\fastsm_core.lib" "%BUILD%\app.res" /Fo"%OBJ%\app\\" /Fe"%BUILD%\FastSMRW.exe" /link %LINKFLAGS% user32.lib gdi32.lib comctl32.lib shell32.lib winhttp.lib crypt32.lib
if errorlevel 1 goto error

REM ---- 3) optional: tests ----
if "%RUN_TESTS%"=="1" (
    echo Compiling tests...
    cl %CFLAGS% %COREINC% /I tests tests\main.cpp "%BUILD%\fastsm_core.lib" /Fo"%OBJ%\test\\" /Fe"%BUILD%\fastsm_tests.exe" /link %LINKFLAGS%
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
