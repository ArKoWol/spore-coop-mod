@echo off
rem Starts the game with the mod and a connected console and joins a host: join.bat 7656...  (no id: the last one, or it asks).
set "ID=%~1"
if "%ID%"=="" set "ID=last"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch.ps1" -JoinHost %ID%
