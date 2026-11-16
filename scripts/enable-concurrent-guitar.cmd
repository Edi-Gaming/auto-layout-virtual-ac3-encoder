@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0configure-concurrent-guitar.ps1"
echo.
pause
exit /b %errorlevel%
