@echo off
rem ===========================================================================
rem  Kite - build + collect the files that go into a game folder
rem ---------------------------------------------------------------------------
rem  Double-click this file, or run it from a shell:
rem      package.bat                 build + dist\kite-<version>\
rem      package.bat -Zip            ... and a zip next to it
rem      package.bat -NoBuild        reuse the existing Release\Kite.dll
rem
rem  The real work is in tools\package.ps1; this file only forwards arguments
rem  and keeps the window open when it was double-clicked.
rem ===========================================================================
setlocal

set "PAUSE_AT_END="
echo %cmdcmdline% | find /i "%~nx0" >nul && set "PAUSE_AT_END=1"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\package.ps1" %*
set "EXITCODE=%ERRORLEVEL%"

if not "%EXITCODE%"=="0" (
    echo.
    echo Packaging FAILED - exit code %EXITCODE%.
)

if defined PAUSE_AT_END pause

exit /b %EXITCODE%
