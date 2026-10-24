@echo off
rem One-step test launcher: pull+build (dev, default), install the release (release) or just start (play),
rem start the game windowed and open a PowerShell console connected to it (it closes with the game).
rem   launch_spore_mod.bat                 dev
rem   launch_spore_mod.bat -Mode release   latest GitHub release
rem   launch_spore_mod.bat -Mode play      as installed
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch.ps1" %*
