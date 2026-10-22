@echo off
rem ---------------------------------------------------------------------------
rem Generate the week-number all-day calendar (res\weeks.ics).
rem Double-click to run; all user-facing text lives in weeks.ps1.
rem ---------------------------------------------------------------------------

setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0weeks.ps1" %*
set "RC=%ERRORLEVEL%"

if not "%RC%"=="0" (
    echo.
    echo [weeks] finished with exit code %RC%.
)

rem Keep the window open when started by double-click (cmd /c ...).
echo "%CMDCMDLINE%" | find /i "/c" >nul
if not errorlevel 1 pause

exit /b %RC%
