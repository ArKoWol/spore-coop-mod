@echo off
rem Starts the game with the mod and a connected console, runs "coop host" and copies your Steam ID to the clipboard.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch.ps1" -AsHost %*
