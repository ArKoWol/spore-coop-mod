# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format: one bullet per change: what, why, and anything the other developer must know (packet/protocol changes, new commands).

## Unreleased

- **Intro splash**: our own picture for 2.5 s after the EA logo, then the intro continues (Maxis, Spore logo). The intro is
  one movie, so the mod pauses it after the EA part and shows a window on top. The picture is `assets/splash.png`
  (1920x1080, 16:9; keep the important part in the middle ~1440x1080); today a **red placeholder** until exxecute's art
  is in. New `mod\CoopMod\make_assets.ps1` (run by `build.bat`) packs it into `CoopMod.package`, which the launcher,
  `make_package.ps1` (`Data\` in the zip) and `update.ps1` install into `<game>\Data\`. No protocol change.
- **The launcher no longer runs `git pull`** (`launch.ps1`, so `launch_spore_mod.bat` too): in `dev` mode it only builds and installs. Pull yourself when you want the other developer's changes. The `-NoPull` switch is gone. No protocol change.

- **M0 replication core** (`core/coop_core.h`, plain C++ without Spore or Windows headers) and its tests (`tests/core_tests.cpp`, run `tests\run_core_tests.bat`, prints `ALL OK`): entity table with netIds, wire format, host side (`Spawn/Update/Destroy`, deltas, full snapshots, reliable events), guest side with stale-state rejection, tombstones and reconcile-on-full. The tests run a fake world through a network with loss, duplicates and reordering (and a 100-tick total blackout) with oracles for unknown ids, resurrected ids, going back in time and final equality, plus mutation twins (each protection switched off once must make a test fail). The test rig found a real bug on the first run (a late duplicate delta recreated a destroyed entity after a full snapshot had cleared its tombstone) and it is fixed; 500 seeds pass. Not used by the mod yet (the game adapter comes next).
- **S1 `coop dump`**: lists every entity (the game's nouns, `GameNounManager.mNouns`) with position, species and health to the console, `CoopMod.log` and `CoopDump.txt`. New files `Entities.cpp/h`; `Logf` moved to `Log.cpp/h`. Compiles; not yet run in a creature stage.
- **`coop fps`** toggles logging of frames per second and whether the game window has the focus (`fps:` lines in `CoopMod.log`). `tests/focus_test.ps1` uses it plus two dumps to check whether the world keeps simulating while another window has the focus (run it with the game in the creature stage). Measured so far: windowed Spore runs about 50 fps in the main menu with or without the focus.
- **The game is always started windowed**: the mod hands the game a command line with ` -w` appended (hooks on `GetCommandLineA/W`), so a plain shortcut behaves like the launcher. A fullscreen Spore is minimized when it loses the focus (Alt+Tab) and runs no frames, which freezes the shared world for the other player. Set the environment variable `COOP_FULLSCREEN` to keep the game's own mode.

- **Design v2.1** (`DESIGN.md`): why the current "everyone in their own world, puppets see each other" build is a dead end, and the agreed plan with the owner's decisions: host-authoritative replicated world, each player has their own creature and progress (all in the host's save through a sidecar), new co-op game that skips the cell stage, stage change only when both reached the goal and both finished the editor, PvP with respawn at the own nest, guest spawns next to the host with his own nest, no pack in v1, guest creature disappears on disconnect, tests on two PCs with in-band telemetry. Spikes S1-S11, milestones M0-M7, risks, work split between the two developers. Nothing implemented yet except the next item.
- **Test aid `COOP_MULTI=1`**: with this environment variable a second game copy gets a private mutex and gets past Spore's "other instance of the game is already running" check (logged as `multi:`). Combine with a separate `APPDATA` to keep saves apart. First finding: the second copy then crashes at `SporeApp.exe+0x29de23` (null read) shortly after start; cause not found yet (spike S6). No effect without the variable.

- **Exit crash: solved / was a misreading.** The Event 1000 at offset `0xFFFFFFFF` we both took for an exit crash comes
  from `SteamAPI_Init` (Steam's `CSERHelper.dll` provokes and handles an access violation on purpose): it is logged
  about a second after `coop host` / `coopjoin` while the game keeps running. Tested with and without Steam on top of
  ArKoWol's cleanup: exit code 0, "Shutting Down", no event after the close. Details in CLAUDE.md "Hard-won facts".
- **Crash log in `CoopMod.log`**: access violations are logged as `crash:` lines (address, thread, modules on the
  stack). Found with it: ModAPI routes our `new`/`delete` to the game's allocator, so static containers must not be
  destroyed after the game's heap is gone; `Dispose` now frees them, and the DLL must not be pinned in memory.

- **One-step test launcher**: `launch.ps1` (wrapped by `launch_spore_mod.bat`) closes a running game the normal way, does `git pull` + build + install (`-Mode dev`, default; `release` installs the latest release, `play` starts as is), starts the game windowed, waits for the mod and opens a PowerShell console window connected to the game (`console.ps1`, with an interactive `spore` prompt). **The console window closes by itself when the game exits.** Host and join stay manual commands in that console (`coop host`, `coopjoin <id>`). Replaces the old batch-only `launch_spore_mod.bat` logic (modes, log separator and `-w` are kept). No protocol change.

- **Cleanup on exit** (partial fix for crashes/frozen processes when the game is closed): `Dispose()` now removes everything that points into the mod DLL before the loader unloads it: `SteamAPI_Shutdown` (after unregistering the session-request callback), the console thread is joined, both update listeners (`App::RemoveUpdateFunction`), all our cheats (`CheatManager.RemoveCheat`), and the `NPCTickAI` and `ArgScript::PrintF` detours are detached. **The exit crash is not gone yet**: with Steam initialized the game still ends with Windows Event 1000 (`0xC0000005`, module unknown, offset `0xFFFFFFFF`) right after our DLL is unloaded; a session that never touched Steam exits cleanly. No protocol change.

- **Fixed: the puppet only turned on the spot and never moved** (on host and client). `SetPosition` is undone by the
  creature's physics; the puppet is now moved with `Teleport(position, orientation)`. Both players need this build.
- **Co-op indicator**: the game window title shows the co-op state, and entering the creature stage shows it as an
  event log message (left side of the screen). For the client it says whether the loaded save is the host's world
  ("CO-OP: you are in the HOST's world") or the player's own ("...this save is YOUR OWN world, not the host's").
  A received world now carries `coop_host.txt` (the host's Steam ID) to tell them apart. The event log item is borrowed
  from the space popups (`GenericMissionProgress`); whether it shows in the creature stage is not verified yet.

- **Fixed: the host sent the world only once per hosting session**, so a client that restarted (to install the world)
  or joined again got nothing and saw no message. The host now treats a client silent for 10 s, or a different Steam ID,
  as a new connection ("client reconnected") and sends the world again; the client ignores a world arriving during a
  game (and after installing one at start). The client now prints "coop: connected to the host." (plus "Choose
  Continue" after an install) when the host answers. Host-side fix: the **host** needs this build. No protocol change.
- Remote console: a new `console.ps1` connection replaces a forgotten one instead of waiting behind it.

- Added `console.bat`, a wrapper for `console.ps1` that bypasses the PowerShell execution policy for that one run (scripts are disabled by default on many PCs: "execution of scripts is disabled on this system").

- **Fixed: a second world install deleted the player's own save backup.** `InstallPendingWorld()` used to wipe `Game0.coopbackup` before every install, so after two joins the first backup (the player's own save) was gone. It now keeps every backup and uses the first free name (`Game0.coopbackup`, `coopbackup2`, `coopbackup3`, ...). No protocol change.

- **Fixed: the client could not install the received world** ("could not install the received world", log
  `install 0 (error 183)`). The running game keeps three files in `Game0` locked, so the world is now stored as
  `Game0.coopready` and installed when the game starts next (before it opens the save). New client flow: `coopjoin`,
  "world received", **restart the game**, `coopjoin` again, Continue. The copy the host sends on the second join is
  ignored. Player README updated. No protocol change (host and client of the old build still talk).
- **Remote console**: the mod listens on `127.0.0.1:27780`; `console.ps1` in the repo runs console commands from
  PowerShell and shows the console output (`.\console.ps1` interactive, `.\console.ps1 "coop status"` one-shot).
  Only works while the game window is not minimized, so `launch_spore_mod.bat` now starts the game windowed (`-w`).
- `launch_spore_mod.bat` has modes: `dev` (default) now **builds** the mod before installing it, `release` installs the
  latest GitHub release into the test copy, `play` starts the game unchanged. It offers to close a running game and
  writes a `===== <date> launch (<mode>) =====` line into `CoopMod.log` so each session is easy to find.
- `update\update.bat` run straight from the repo no longer asks for the game folder: it finds `test\SPORE_A`, then `SPORE`
  (junctions work). The files it writes next to itself (`coop_update.json`, `fix_spore_registry.ps1`, `docs\`) are now in
  `.gitignore`. Players' installs (`<game>\coop_update`) behave as before.

## build-20261024-0925

- Fixed joining: `coop join <id>` was rejected by the game ("Expecting at most 1 arguments"). Joining is now the one-argument commands `coopjoin <steamid>` and `coopudpjoin <ip>`; `coop host/udphost/status/abs/anim/noworld/sendworld/off` are unchanged. No protocol change.

- Fixed publish.ps1 moving the Unreleased heading in the wrong place (it hit the intro text).

## build-20261024-0906

- Added `CLAUDE.md` (project guide for developers and their AI assistants) and this changelog; both ship in the package under `docs\`.
- Added `setup.ps1`: one-time developer setup; build paths in `CoopMod.vcxproj`/`build.bat` are now relative to the repo.

## build-20261024-0851 (first release)

- Steam (Spacewar, AppID 480) transport through the flat C API with a UDP fallback (`coop udphost` / `coop udpjoin`).
- Host sends its save game (`Games\Game0`) to a client that joins from the main menu; client keeps a backup.
- Remote player puppet: one-animal herd, AI skipped via `NPCTickAI` detour, smoothed position/orientation, absolute positions by default.
- Protocol version 3: players with different mod versions refuse each other and see a message.
- Commands: `coop host|join|udphost|udpjoin|abs|anim|noworld|sendworld|status|off`, `stage <name>`, `coophello`.
- `update.bat` installs/updates from the latest GitHub release; `publish.ps1` publishes one.

## Before the first release (history)

- Found that the GOG build works with ModAPI (the Steam build of Oct 2024 does not); fixed the incomplete GOG registry entries.
- Built the toolchain (VS 2022 Build Tools, Spore ModAPI SDK, SporeModLoader) and a test mod.
- Puppet spawning: `cCreatureAnimal::Create` without a herd crashes; `CreateHerd` works.














