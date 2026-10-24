@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
cd /d "C:\games\Spore_wtf_mode\mod\CoopMod"
msbuild CoopMod.vcxproj -p:Configuration=Release -p:Platform=Win32 -v:m -nologo
