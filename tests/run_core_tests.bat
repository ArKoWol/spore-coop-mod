@echo off
rem Builds and runs the replication core tests (no game needed). Prints ALL OK when everything passes.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo Visual Studio Build Tools not found. Run setup.ps1 first.
  exit /b 1
)
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /EHsc /std:c++17 /W4 /Fo:out\ /Fe:out\core_tests.exe core_tests.cpp || exit /b 1
out\core_tests.exe
exit /b %errorlevel%
