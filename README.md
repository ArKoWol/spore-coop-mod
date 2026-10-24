# Spore co-op mod

Experimental co-op for Spore: Galactic Adventures (GOG build, exe 3.1.0.29). A DLL mod for
[SporeModLoader](https://github.com/Rosalie241/SporeModLoader) built on the
[Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI). The second player is shown as a puppet creature;
traffic goes through Steam networking (Spacewar, AppID 480) with a plain UDP fallback, and the host sends its save game
to the client so both play in the same world.

Status: early test. Creature stage only, no walking animation on the puppet, the puppet uses the local creature's look.

## Layout
- `launch_spore_mod.bat` - one-step test launcher: builds, starts the game and opens a connected console that closes with the game.
- `console.bat` - remote game console (type console commands from PowerShell).
- `update.bat` - checks GitHub for new commits and a new release and asks what to do (pull / install).
- `mod/CoopMod` - the mod (C++, Visual Studio 2022 Build Tools, x86). Build with `build.bat`.
- `core/`, `tests/` - the replication core and its tests. `assets/` - the splash picture.
- `scripts/` - developer tools: `setup.ps1`, `launch.ps1`, `console.ps1`, `make_package.ps1`, `publish.ps1`.
- `package/` - what players get: `README.txt` (instructions), `fix_spore_registry.ps1`, `coop_update/` (the updater).
- `docs/DESIGN.md` - the co-op plan. `CLAUDE.md` - project guide and hard-won facts. `CHANGELOG.md` - every change.

Game files, Valve's `steam_api.dll` and save games are not part of this repository.

## Playing
Download `coop_package.zip` from the [latest release](../../releases/latest), extract it, run `coop_update\update.bat`.
Full instructions are in `package/README.txt` (`README.txt` in the zip).

## Development
```
git clone https://github.com/ArKoWol/spore-coop-mod
cd spore-coop-mod
.\scripts\setup.ps1 -GameDir "D:\Games\SPORE"   # installs Build Tools, the SDK, SporeModLoader; builds the mod; makes test\SPORE_A
```
Edit `mod\CoopMod\dllmain.cpp`, build with `mod\CoopMod\build.bat` (output: `mod\CoopMod\staging\CoopMod.dll`),
test with `launch_spore_mod.bat` (builds, installs and starts the game), release with `.\scripts\publish.ps1` (needs `gh auth login`).
Bump `kProtocolVersion` in `dllmain.cpp` whenever the packet layout changes.




