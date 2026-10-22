@echo off
rem ---------------------------------------------------------------------------
rem thu-courses-ics - double-click launcher.
rem
rem Builds on the first run, then generates res\coursesAuto.ics.
rem All user-facing text lives in run.ps1 (UTF-8 with BOM); this file is kept
rem ASCII-only on purpose, because cmd.exe re-reads batch files using the
rem active code page and the program switches the console to UTF-8.
rem ---------------------------------------------------------------------------

setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo [thu-courses-ics] finished with exit code %RC%.
)

rem Keep the console window open when started by double-click (cmd /c ...).
rem In an existing terminal the window is left as-is.
echo "%CMDCMDLINE%" | find /i "/c" >nul
if not errorlevel 1 pause

exit /b %RC%
