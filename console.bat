@echo off
rem Remote game console. Same as console.ps1, but works where PowerShell script execution is disabled.
rem   console.bat                 interactive
rem   console.bat "coop status"   run commands and print the game's answer
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0console.ps1" %*
