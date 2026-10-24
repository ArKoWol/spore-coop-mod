@echo off
rem Builds CoopMod.dll and CoopMod.package (from assets\) into mod\CoopMod\staging (needs the VS Build Tools and the SDK libs, see scripts\setup.ps1).
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio Build Tools not found. Run scripts\setup.ps1 first.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
msbuild CoopMod.vcxproj -p:Configuration=Release -p:Platform=Win32 -v:m -nologo || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_assets.ps1" || exit /b 1
