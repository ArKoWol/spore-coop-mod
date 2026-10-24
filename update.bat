@echo off
rem Updater for the repo checkout: checks new commits on GitHub and the latest release, asks what to do
rem (git pull / install the release into test\SPORE_A). The updater itself lives in package\coop_update,
rem where players have it too.
call "%~dp0package\coop_update\update.bat" %*
