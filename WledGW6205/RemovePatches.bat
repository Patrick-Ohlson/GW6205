@echo off
REM ============================================================================
REM  WledGW6205 - Revert GW6205 patches from the upstream WLED tree
REM
REM  Restores three files to their pristine git state:
REM    ..\wled\wled00\const.h
REM    ..\wled\wled00\bus_wrapper.h
REM    ..\wled\wled00\bus_manager.cpp
REM
REM  Use this before pulling a new upstream WLED version, or to verify the
REM  patches re-apply cleanly. Other files in wled\ are not touched.
REM
REM  Run from the WledGW6205\ directory (or double-click in Explorer).
REM ============================================================================

setlocal enableextensions
cd /d "%~dp0"

set WLED_DIR=..\wled

echo.
echo === WledGW6205: Revert WLED patches ===
echo.

REM --- Sanity checks ---------------------------------------------------------

if not exist "%WLED_DIR%\wled00\const.h" (
    echo [ERROR] Cannot find %WLED_DIR%\wled00\const.h
    goto :error
)

if not exist "%WLED_DIR%\.git" (
    echo [ERROR] %WLED_DIR% is not a git repository.
    echo         Without git there is no clean baseline to restore from.
    goto :error
)

where git >nul 2>nul
if errorlevel 1 (
    echo [ERROR] git.exe not found in PATH.
    goto :error
)

REM --- Show current state ----------------------------------------------------

pushd "%WLED_DIR%"

git diff --quiet wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
if errorlevel 1 (
    echo Current local changes in the target files:
    git diff --stat wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
    echo.
) else (
    echo No local changes detected in the target files. Nothing to revert.
    popd
    endlocal
    exit /b 0
)

REM --- Confirm ---------------------------------------------------------------

echo This will discard ALL local changes to:
echo   wled00\const.h
echo   wled00\bus_wrapper.h
echo   wled00\bus_manager.cpp
echo.
echo Other files in the wled\ tree are untouched. The WledGW6205\ project
echo (this folder) is also untouched.
echo.
choice /c YN /n /m "Proceed? [Y/N] "
if errorlevel 2 goto :abort_pop

REM --- Revert ----------------------------------------------------------------

echo.
echo Reverting ...
git checkout -- wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
if errorlevel 1 (
    echo [ERROR] git checkout failed.
    goto :error_pop
)

echo.
echo Verifying clean state:
git diff --stat wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
git status --short wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp

popd

echo.
echo === Patches reverted successfully ===
echo.
echo You can now safely:
echo   - git pull in ..\wled to get a new upstream version
echo   - run InstallPatches.bat to re-apply our patches
echo.

endlocal
exit /b 0

:error_pop
popd
:error
echo.
echo === Patch removal FAILED ===
endlocal
exit /b 1

:abort_pop
popd
echo Aborted by user.
endlocal
exit /b 2
