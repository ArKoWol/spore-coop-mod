@echo off
rem Starts Spore (test copy test\SPORE_A) with SporeModLoader and the co-op mod.
rem   launch_spore_mod.bat           dev:     build mod\CoopMod, install the fresh CoopMod.dll, start the game
rem   launch_spore_mod.bat release   release: install the latest GitHub release (what players get), start the game
rem   launch_spore_mod.bat play      just start the game with whatever is installed
rem The DLL is locked while the game runs, so a running game is closed first (after asking).
setlocal
set "GAME=%~dp0test\SPORE_A"
set "MODE=%~1"
if "%MODE%"=="" set "MODE=dev"

tasklist /fi "imagename eq SporeApp.exe" | find /i "SporeApp.exe" >nul
if not errorlevel 1 (
  choice /m "Spore is running. Close it"
  if errorlevel 2 exit /b 1
  taskkill /im SporeApp.exe /f >nul
  timeout /t 2 /nobreak >nul
)

if /i "%MODE%"=="dev" (
  echo Building CoopMod...
  call "%~dp0mod\CoopMod\build.bat" || goto :failed
  copy /y "%~dp0mod\CoopMod\staging\CoopMod.dll" "%GAME%\SporeModLoader\ModLibs\CoopMod.dll" >nul || goto :failed
  echo Installed the local build.
) else if /i "%MODE%"=="release" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update\update.ps1" -GameDir "%GAME%" || goto :failed
) else if /i not "%MODE%"=="play" (
  echo Unknown mode "%MODE%". Use: dev, release or play.
  goto :failed
)

rem separator so this session's lines are easy to find in the appended log
>>"%GAME%\SporebinEP1\CoopMod.log" echo ===== %DATE% %TIME% launch (%MODE%) =====
cd /d "%GAME%\SporebinEP1"
start "" "SporeApp.exe"
exit /b 0

:failed
echo.
echo Failed, the game was not started.
pause
exit /b 1
