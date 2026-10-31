@echo off
rem Builds and runs the replication core tests (no game needed). Prints ALL OK when everything passes.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio Build Tools not found. Run scripts\setup.ps1 first.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /EHsc /std:c++17 /W4 /Fo:out\ /Fe:out\core_tests.exe core_tests.cpp || exit /b 1
out\core_tests.exe || exit /b 1
rem the co-op launch folder logic (core\coop_launch.h) on temporary folders
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /Fo:out\ /Fe:out\launch_tests.exe launch_tests.cpp || exit /b 1
out\launch_tests.exe || exit /b 1
rem the world-transfer manifest (core\coop_manifest.h): diff, build, verify on temporary folders
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /Fo:out\ /Fe:out\manifest_tests.exe manifest_tests.cpp || exit /b 1
out\manifest_tests.exe
exit /b %errorlevel%
