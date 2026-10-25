# Spore Co-op

An experimental two-player co-op mod for **Spore: Galactic Adventures**. Two friends play the **creature stage on the same
planet** and see each other's creature walking around.

> **Status: early test build.** It is made by two friends for themselves, changes several times a day and can crash the
> game. Back up your saves before you try it. Creature stage only.

## What works and what does not (yet)

| Works | Not yet |
|---|---|
| Connecting over Steam by Steam ID, no port forwarding | The other player has **your** creature's look, not their own |
| The host's world is sent to the guest automatically; the guest's game restarts and rejoins by itself | Eating, fighting, singing, health: nothing of this is synced |
| The other player's creature walks, stops and turns (game's own walk animation) | Plants, fruit, eggs and nests are not shared (eaten fruit stays on the other side) |
| First version of shared animals: the host's animals are copied to the guest (not tested on two PCs yet) | Running: the other player's creature walks at most at walking speed and falls behind a sprint |
| A remote console to type game commands from PowerShell | Tribe, civilization, space stages |

The plan and the order in which the rest gets built are in [`docs/SYNC_REDESIGN.md`](docs/SYNC_REDESIGN.md) and
[`docs/DESIGN.md`](docs/DESIGN.md). Every change is listed in [`CHANGELOG.md`](CHANGELOG.md).

## What you need

- **Spore + Galactic Adventures, GOG "SPORE Collection"** (`SporebinEP1\SporeApp.exe`, version 3.1.0.29).
  The current **Steam build does not work** with mods (its exe is encrypted since October 2024).
- **Steam** installed, running and logged in. The connection goes through Steam's test app "Spacewar" (AppID 480);
  you do not need to own anything. The mod takes the Steam API file from one of your installed Steam games.
- The game must start normally **before** you install the mod.
- Windows 10/11.

## Install and play

1. Download `coop_package.zip` from the [latest release](../../releases/latest) and extract it anywhere.
2. Close the game, run **`coop_update\update.bat`**. It finds your Spore folder, installs the newest build and the Steam
   API file. Run it again later to update. **Both players need the same version**, otherwise they refuse each other.
3. Start the game (`SporebinEP1\SporeApp.exe` or the GOG shortcut). The mod starts it **windowed**: a minimized or
   full-screen game in the background stops running, and so does the co-op.
4. **Host**: load a game, go into the creature stage, open the console (**Ctrl+Shift+C**) and type `coop host`.
   Send the printed Steam ID to your friend.
5. **Guest**: stay in the main menu, open the console and type `coopjoin <host's Steam ID>`. The host's world arrives,
   your game restarts and reconnects by itself (your own save is kept as `Game0.coopbackup`). Press **Continue**.

Full player instructions: [`package/README.txt`](package/README.txt) (shipped as `README.txt` in the zip).

### Console commands

| Command | What it does |
|---|---|
| `coop host` | host a session over Steam, prints your Steam ID |
| `coopjoin <steamid>` | join a host (from the main menu) |
| `coopjoin last` | join the host you joined last time; also `coopjoin 2` (number from the list) or `coopjoin <part of the name>` |
| `coop friends` | the hosts you joined before, with their Steam names |
| `coop status` | connection state, received packets, puppet, shared-world counts |
| `coop off` | leave the session |
| `coop sendworld` / `coop noworld` | host: send the world again / stop sending it automatically |
| `coop udphost` / `coopudpjoin <ip>` | fallback over plain UDP, port 27777 (no world transfer, needs VPN or port forwarding) |
| `coop move` / `coop walk` / `coop teleport` | how the other player's creature follows (`move` is the default) |
| `coop echo` | test on one PC: a copy of your creature repeats you 0.4 s later, 4 m to the side |
| `coop dump`, `coop fps`, `coop motionlog`, `coop restart`, `coop modeltest` | developer diagnostics (see `CLAUDE.md`) |

The game accepts exactly one word after `coop`, so joining is the separate command `coopjoin`. In the remote console
(`console.bat`) several commands go in one line separated by `;`: `coop dump; coop status`.

### When something goes wrong

| Problem | What to do |
|---|---|
| "Configuration Script Failed [2000]" or "data directory is missing or corrupt [1004]" | the GOG install left the registry incomplete: run `fix_spore_registry.ps1 -GameDir "<game folder>"` in PowerShell as administrator (it is next to `update.bat`) |
| "different mod version" in the console | both players run `update.bat` |
| The friend never connects | Steam must be running and logged in on both PCs; the game window must not be minimized |
| "Another instance of the game is already running" right after a crash | an old Spore process is stuck (it can happen after a display driver reset); restart Windows |
| Anything else | send `SporebinEP1\CoopMod.log` from the game folder |

## For developers

The mod is a C++ DLL loaded by [SporeModLoader](https://github.com/Rosalie241/SporeModLoader) and built on the
[Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI). Read [`CLAUDE.md`](CLAUDE.md) first: the rules for working
in this repo (two developers, each with their own AI assistant) and the **hard-won facts** about the game, so nobody
rediscovers them.

```
git clone https://github.com/ArKoWol/spore-coop-mod
cd spore-coop-mod
.\scripts\setup.ps1 -GameDir "D:\Games\SPORE"
```
`setup.ps1` installs the Visual Studio Build Tools (C++ x86) if needed, downloads and builds the ModAPI SDK, downloads
SporeModLoader, builds the mod and makes a modded test copy of the game in `test\SPORE_A`.

Daily work, from the repo root:

| | |
|---|---|
| `git pull` | first, always |
| `launch_spore_mod.bat` | check GitHub, build, install into the test copy, start the game windowed, open a console connected to it (each step shows a spinner) |
| `console.bat "coop status"` | type game commands from PowerShell (or `console.bat` for an interactive prompt) |
| `update.bat` | check GitHub for new commits and a new release |
| `tests\run_core_tests.bat` | tests of the replication core, no game needed |
| `.\scripts\publish.ps1` | build the package and publish a GitHub release (tell the other developer first) |

Logs: `test\SPORE_A\SporebinEP1\CoopMod.log` (the mod) and `spore_log.txt` (ModAPI). Every change goes into
`CHANGELOG.md`; a change of the packet layout bumps `kProtocolVersion` in `mod/CoopMod/dllmain.cpp`.

### How it works

```
SporeApp.exe
 └─ dinput8.dll (SporeModLoader)            loads the mods before the game starts
     ├─ SporeModAPI.dll                     knows the game's functions
     └─ CoopMod.dll                         this mod
         └─ steam_api_coop.dll              Steam networking (Spacewar), loaded by hand
```
- Each player's creature: position and orientation 30 times a second; the other side's copy walks there with the game's
  own locomotion.
- World: the host sends its save (about 3 MB) when the guest joins; the guest installs it on a restart, because the
  running game keeps its save files locked.
- Shared animals (first version): the host streams its animals, the guest shows copies and switches its own off.

### Repository layout

| Path | What |
|---|---|
| `launch_spore_mod.bat`, `console.bat`, `update.bat` | the three double-click tools |
| `mod/CoopMod/` | the mod (C++), `build.bat` |
| `core/`, `tests/` | replication core (plain C++, no game headers) and its tests |
| `assets/` | files packed into the game's data (the splash picture after the EA logo) |
| `scripts/` | developer tools (PowerShell) |
| `package/` | what players get, in the zip's layout: instructions, registry fix, updater |
| `docs/` | `DESIGN.md` (product decisions), `SYNC_REDESIGN.md` (how the world is kept equal), `HANDOFF_B.md` (work split) |

Game files, saves and Valve's `steam_api.dll` are never part of this repository.

## Credits

- [Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI) and [SporeModLoader](https://github.com/Rosalie241/SporeModLoader),
  without which none of this would be possible.
- The creature model transfer follows an idea of [WoodyWizard/SporeCoop](https://github.com/WoodyWizard/SporeCoop) (MIT).
- Spore is a trademark of Electronic Arts. This is an unofficial fan project, not affiliated with EA or Maxis.
