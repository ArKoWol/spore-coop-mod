@echo off
rem Installs or updates the Spore co-op mod from the latest GitHub release.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update.ps1" %*
echo.
pause
