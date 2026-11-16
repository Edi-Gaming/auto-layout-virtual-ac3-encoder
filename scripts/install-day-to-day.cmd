@echo off
setlocal
title OHL Virtual AC3 Encoder - Day-to-Day Installer
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-day-to-day.ps1"
echo.
if errorlevel 1 (
  echo Install/update failed. See the error above.
) else (
  echo Install/update complete. The OHL tray icon should now be running.
)
pause
