# Spore co-op mod

Experimental co-op for Spore: Galactic Adventures (GOG build, exe 3.1.0.29). A DLL mod for
[SporeModLoader](https://github.com/Rosalie241/SporeModLoader) built on the
[Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI). The second player is shown as a puppet creature;
traffic goes through Steam networking (Spacewar, AppID 480) with a plain UDP fallback, and the host sends its save game
to the client so both play in the same world.

Status: early test. Creature stage only, no walking animation on the puppet, the puppet uses the local creature's look.

## Layout
- `mod/CoopMod` - the mod (C++, Visual Studio 2022 Build Tools, x86). Build with `build.bat`.
- `update/` - `update.bat`/`update.ps1`, the updater players run to get the latest release.
- `make_package.ps1`, `publish.ps1` - build `coop_package.zip` and publish it as a GitHub release.
- `coop_package_README.txt` - player instructions (shipped inside the package).
- `launch_spore_mod.bat` - local test launcher.

Game files, Valve's `steam_api.dll` and save games are not part of this repository.
