@echo off
rem Starts Spore with SporeModLoader and the mods from SporeModLoader\ModLibs (test copy SPORE_A).
rem A freshly built CoopMod.dll waits in mod\CoopMod\staging while the game is running; copy it in first.
if exist "%~dp0mod\CoopMod\staging\CoopMod.dll" copy /y "%~dp0mod\CoopMod\staging\CoopMod.dll" "%~dp0test\SPORE_A\SporeModLoader\ModLibs\CoopMod.dll" >nul
cd /d "%~dp0test\SPORE_A\SporebinEP1"
start "" "SporeApp.exe"
