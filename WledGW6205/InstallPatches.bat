@echo off
REM ============================================================================
REM  WledGW6205 - Apply GW6205 patches to the upstream WLED tree
REM
REM  Patches three files:
REM    ..\wled\wled00\const.h
REM    ..\wled\wled00\bus_wrapper.h
REM    ..\wled\wled00\bus_manager.cpp
REM
REM  Run from the WledGW6205\ directory (or double-click in Explorer).
REM ============================================================================

setlocal enableextensions
cd /d "%~dp0"

set WLED_DIR=..\wled
set PATCH_FILE=%~dp0patches\wled-gw6205.patch

echo.
echo === WledGW6205: Apply WLED patches ===
echo.

REM --- Sanity checks ---------------------------------------------------------

if not exist "%WLED_DIR%\wled00\const.h" (
    echo [ERROR] Cannot find %WLED_DIR%\wled00\const.h
    echo         Make sure the wled\ folder is a sibling of WledGW6205\
    echo         and contains a checkout of the WLED source tree.
    goto :error
)

if not exist "%WLED_DIR%\.git" (
    echo [ERROR] %WLED_DIR% is not a git repository.
    echo         The patch flow requires git to track changes and roll them
    echo         back. Initialize the wled\ folder as a git repo first, or
    echo         clone WLED from GitHub:
    echo             git clone https://github.com/wled/WLED.git wled
    goto :error
)

if not exist "%PATCH_FILE%" (
    echo [ERROR] Patch file not found: %PATCH_FILE%
    goto :error
)

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] git.exe not found in PATH.
    echo         Install Git for Windows: https://git-scm.com/download/win
    goto :error
)

REM --- Check current state ---------------------------------------------------

pushd "%WLED_DIR%"

echo Checking current state of wled\wled00\ ...
git diff --quiet wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
if errorlevel 1 (
    echo.
    echo [WARN] The target files already have local modifications:
    git diff --stat wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
    echo.
    echo If these are already our patches, you do not need to run this script.
    echo If they are unrelated edits, commit or stash them first, then re-run.
    echo.
    choice /c YN /n /m "Proceed anyway and try to apply on top? [Y/N] "
    if errorlevel 2 goto :abort
)

REM --- Dry run ---------------------------------------------------------------

echo.
echo Dry-run: checking if patch applies cleanly ...
git apply --check "%PATCH_FILE%"
if errorlevel 1 (
    echo.
    echo [ERROR] Patch does not apply cleanly. The upstream WLED files have
    echo         changed in ways our patch can not reconcile automatically.
    echo.
    echo         Manual rebase steps:
    echo           1. cd ..\wled
    echo           2. Edit wled00\const.h, bus_wrapper.h, bus_manager.cpp by hand
    echo              (refer to ..\WledGW6205\patches_wled.md for the change list)
    echo           3. Regenerate the patch:
    echo                git diff wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp ^> ..\WledGW6205\patches\wled-gw6205.patch
    goto :error_pop
)

REM --- Apply -----------------------------------------------------------------

echo Applying patch ...
git apply "%PATCH_FILE%"
if errorlevel 1 (
    echo [ERROR] git apply failed unexpectedly after passing --check.
    goto :error_pop
)

echo.
echo Modified files:
git diff --stat wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp

popd

REM --- Refresh local NeoESP32RmtHI copy --------------------------------------
REM
REM We keep a local copy of WLED's vendored NeoESP32RmtHI library at
REM lib\NeoESP32RmtHI\ instead of pointing PlatformIO across drives. After
REM an upstream sync the local copy may be stale, so refresh it here.

set NPB_SRC=%WLED_DIR%\lib\NeoESP32RmtHI
set NPB_DST=%~dp0lib\NeoESP32RmtHI

if exist "%NPB_SRC%\library.json" (
    echo.
    echo Refreshing local NeoESP32RmtHI copy from %NPB_SRC% ...
    if not exist "%NPB_DST%\include" mkdir "%NPB_DST%\include"
    if not exist "%NPB_DST%\src"     mkdir "%NPB_DST%\src"
    copy /y "%NPB_SRC%\library.json"                 "%NPB_DST%\library.json"                 >nul
    copy /y "%NPB_SRC%\include\NeoEsp32RmtHIMethod.h" "%NPB_DST%\include\NeoEsp32RmtHIMethod.h" >nul
    copy /y "%NPB_SRC%\src\NeoEsp32RmtHIMethod.cpp"   "%NPB_DST%\src\NeoEsp32RmtHIMethod.cpp"   >nul
    copy /y "%NPB_SRC%\src\NeoEsp32RmtHI.S"           "%NPB_DST%\src\NeoEsp32RmtHI.S"           >nul
    if errorlevel 1 (
        echo [WARN] Could not refresh NeoESP32RmtHI files.
    ) else (
        echo NeoESP32RmtHI refreshed.
    )
) else (
    echo.
    echo [WARN] %NPB_SRC%\library.json not found.
    echo        Skipping NeoESP32RmtHI refresh - lib\NeoESP32RmtHI\ may be stale.
)

echo.
echo === Patches applied successfully ===
echo.
echo Next steps:
echo   1. (one-time per upstream sync) Build the WLED web UI:
echo        cd ..\wled ^&^& npm ci ^&^& npm run build
echo   2. Build the firmware:
echo        pio run -e wled_gw6205_esp32
echo      ...or use the VSCode build task.
echo.

endlocal
exit /b 0

:error_pop
popd
:error
echo.
echo === Patch installation FAILED ===
endlocal
exit /b 1

:abort
popd
echo Aborted by user.
endlocal
exit /b 2
