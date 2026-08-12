@echo off
setlocal EnableDelayedExpansion
rem Configure + build the `msvc` preset.
rem
rem MSVC needs its environment (cl.exe, the Windows SDK, ml64 for the AngelScript trampoline)
rem on PATH, and a CMake preset cannot establish that - hence this wrapper. Run it from a plain
rem shell; it finds Visual Studio itself.
rem
rem   build-msvc.cmd                       configure + build everything
rem   build-msvc.cmd --target UI           build just that target
rem   build-msvc.cmd -- -k 0               keep going after failures (args after -- go to ninja)
rem
rem Set BUILD_VS_PATH to an installation root to override the search entirely, e.g.
rem   set "BUILD_VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community"
rem
rem Any arguments are forwarded to the build step; configure always runs (a no-op when nothing
rem changed).

set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"

set "BEST_PATH="
set "BEST_VER="

rem --- Candidate installs ---------------------------------------------------------------
rem Two sources, because neither is sufficient alone: vswhere is the supported API but has been
rem observed here NOT to report a side-by-side VS 2026 (18.x) install that is present and
rem working, while a bare directory scan cannot tell which of several installs is newest.
rem Collect from both, then pick by MSVC TOOLSET version (14.51 > 14.44) rather than by
rem edition/year - the directory names use different schemes ("18" vs "2022") and do not sort.
if defined BUILD_VS_PATH (
    call :consider "%BUILD_VS_PATH%"
) else (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -prerelease -products * ^
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
                -property installationPath 2^>nul`) do call :consider "%%i"
    )
    for %%R in ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") do (
        if exist "%%~R" for /d %%Y in ("%%~R\*") do (
            for /d %%E in ("%%~Y\*") do call :consider "%%~E"
        )
    )
)

if not defined BEST_PATH (
    echo [build-msvc] No Visual Studio install with the C++ x64 toolset was found.
    echo [build-msvc] Install the "Desktop development with C++" workload, or set
    echo [build-msvc]   BUILD_VS_PATH=^<installation root^>
    exit /b 1
)

rem --- Enter the MSVC environment -------------------------------------------------------
echo [build-msvc] Visual Studio: %BEST_PATH%
echo [build-msvc] MSVC toolset : %BEST_VER%
call "%BEST_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [build-msvc] vcvars64.bat failed.
    exit /b 1
)

rem --- Configure ------------------------------------------------------------------------
rem The preset asks for "cl", which CMake resolves from PATH and then PINS in the cache. If the
rem selected toolchain differs from the cached one the build tree must be regenerated, or the
rem old compiler keeps being used - so detect that and wipe the cache.
rem Compare with FORWARD slashes: CMake stores CMAKE_CXX_COMPILER with '/', while BEST_PATH came
rem from the filesystem with '\'. Matching the raw path never hits, which silently wiped the cache
rem and forced a full rebuild on every single run.
set "BEST_FWD=%BEST_PATH:\=/%"
set "CACHE=%REPO%\build\msvc\CMakeCache.txt"
if exist "%CACHE%" (
    findstr /c:"CMAKE_CXX_COMPILER:STRING=" "%CACHE%" | findstr /i /c:"%BEST_FWD%" >nul
    if errorlevel 1 (
        echo [build-msvc] Cached compiler differs from the selected toolchain - reconfiguring.
        del /q "%CACHE%"
    )
)

echo [build-msvc] Configuring...
cmake --preset msvc -S "%REPO%"
if errorlevel 1 (
    echo [build-msvc] Configure FAILED.
    exit /b 1
)

rem --- Build ----------------------------------------------------------------------------
echo [build-msvc] Building...
cmake --build --preset msvc %*
if errorlevel 1 (
    echo [build-msvc] Build FAILED.
    exit /b 1
)

echo [build-msvc] OK - binaries in Bin\Debug\Win64-MSVC
endlocal
exit /b 0

rem --- :consider <installation root> -----------------------------------------------------
rem Keeps the candidate with the highest MSVC toolset version. Toolset strings are 14.NN.BBBBB
rem with fixed-width components, so a plain string comparison orders them correctly.
:consider
set "CAND=%~1"
if not exist "%CAND%\VC\Auxiliary\Build\vcvars64.bat" goto :eof
set "VERFILE=%CAND%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
if not exist "%VERFILE%" goto :eof
set "CANDVER="
for /f "usebackq tokens=* delims= " %%v in ("%VERFILE%") do if not defined CANDVER set "CANDVER=%%v"
if not defined CANDVER goto :eof
if not defined BEST_VER (
    set "BEST_VER=%CANDVER%"
    set "BEST_PATH=%CAND%"
    goto :eof
)
if "%CANDVER%" GTR "%BEST_VER%" (
    set "BEST_VER=%CANDVER%"
    set "BEST_PATH=%CAND%"
)
goto :eof
