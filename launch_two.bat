@echo off
rem One-PC co-op test: two games side by side (A hosts, B joins over 127.0.0.1). See docs\TESTING.md.
rem   launch_two.bat                build, install, start both
rem   launch_two.bat -Connect       the consoles host and join by themselves
rem   launch_two.bat -Mode play     start what is installed
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\launch_two.ps1" %*
