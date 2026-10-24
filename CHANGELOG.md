# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format: one bullet per change: what, why, and anything the other developer must know (packet/protocol changes, new commands).

## Unreleased

- **Creature models travel with the session (S8, gate G2) and a dump to find out why animals looked wrong.** The cause of
  "every animal has our species' model" is that a save holds only creature keys; the bodies (`bem` records) live in the host's
  `EditorSaves.package` / `Pollination.package`. New `Models.cpp/.h`: the guest asks the host (`kMsgModelReq`) for every species
  `GetSpeciesProfile` cannot build, the host reads the records from the resource manager and sends them (reliable, 40 KB chunks),
  the guest writes `<game>\SporebinEP1\CoopModels\m_<group>_<instance>.package` (uncompressed DBPF) and registers it with
  `ResourceManager.RegisterDatabase` (cached packages are registered again at the next start). Mirrors now **wait up to 20 s
  for the model** instead of silently using our own species; after that they use the stand-in and it is counted (`coop status`:
  "stand-in models", log `world:`). Idea and ids from WoodyWizard/SporeCoop (MIT), see `docs/SYNC_REDESIGN.md` 6b.
  `coop dump` now also lists every creature species with `buildable=0/1` and the package that holds its record (spike S13);
  `coop modeltest` checks read + package + register for the avatar's species on one PC; `scripts\worlddiff.ps1 host\CoopDump.txt
  guest\CoopDump.txt` compares two dumps (gate G0). `docs/SYNC_REDESIGN.md`: the redesign plan (events, manifests, order of gates).
  **`kProtocolVersion` is 5.** Not tested with two PCs yet; `coop modeltest` and `coop dump` need the creature stage.

- **The remote player's puppet walks instead of drifting** (Track B, S3): instead of `Teleport` every frame, the game's
  own locomotion walks it to the received position (`MoveTo`, new `mod/CoopMod/game/PuppetMotion.cpp`), so its legs move
  while it moves and it stands still when the other player stands; it only teleports when more than 6 m behind.
  **No protocol change** (the speed is derived from the received positions; `StatePacket` is unchanged). Tested on one PC
  with the new `coop echo` (a puppet that repeats you 0.4 s later, 4 m to the side): it walks at up to 4.0 m/s and follows
  2-4 m behind. New console commands: `coop move` (default) / `coop walk` (WalkTo) / `coop teleport` (old way),
  `coop motionlog` (`motion:` lines in `CoopMod.log`), `coop echo`. Known: at a full sprint the puppet lags a bit
  (walk speed cap); next: run animation via `SetDesiredSpeed`.

- **Restart helper finds the console script by itself.** After the automatic restart the console window did not come back
  when `COOP_CONSOLE_PS1` was missing or pointed to a file that had moved (the repo tidy-up moved `console.ps1` to
  `scripts\`). `LaunchRestartHelper` now looks for `scripts\console.ps1` / `console.ps1` up to 5 folders above the game
  folder when the variable does not work. Checked: game started without the variable, `coop restart`, new game process and
  a new console window.

- **Shared world, first version (M2, Track A): the host's animals are replicated to the guest.** New `World.cpp/.h`
  (the game adapter of `core/coop_core.h`). Host: every 100 ms the `cCreatureAnimal`s (not the avatar, not the player puppet)
  go into `HostWorld`; deltas (cut into messages of 20 entities, `HostWorld::MakeDeltas`) on Steam channel 2 (unreliable),
  Spawn/Destroy events, a full snapshot every 10 s and the species definitions on channel 3 (reliable, ordered, queued when
  Steam's buffer is full). Guest: `GuestWorld` rebuilds the table, each entity gets a mirror (a one-animal herd made with
  `CreateHerd`, moved by `Teleport`, smoothed); the `NPCTickAI` detour skips every animal but the guest's own avatar
  (`World::ShouldSkipAI`), and the guest's own animals are removed every 0.5 s once the first snapshot arrived (strays).
  The guest reports table/mirror/stray counts to the host every 2 s: `coop status` and `CoopMod.log` (`world:` lines)
  show host animals vs guest table/mirrors/strays. Only animals so far (nests, eggs, plants are next). Not tested in the
  game yet. **`kProtocolVersion` is 4**: both players must update. Core: `MakeDeltas` + a test (`tests\run_core_tests.bat`).

- **`docs/HANDOFF_B.md`: work split after the first two-PC test** (docs only). The test showed two separate worlds and a
  drifting puppet. Track A (ArKoWol): shared world, entity table, mirrors, guest AI/spawn neutered (S1, S2, M2).
  Track B (exxecute): puppet walking animation first, then the other player's own creature model, start flow, sidecar.

- **`update.bat` in the repo root** (calls `package\coop_update\update.bat`), next to `launch_spore_mod.bat` and
  `console.bat`. The updater no longer asks for the Spore folder when it can find it: besides `-GameDir`, the saved config,
  the folder above it and `test\SPORE_A`/`SPORE`, it now reads the game's registry entry (`installloc`). Players benefit
  too (a fresh `coop_update` copy finds a GOG install by itself).
  After the move to `package/coop_update` it also takes the saved settings from the old `update/coop_update.json`.

- **Repository tidied up** (no code change): PowerShell tools moved to `scripts/` (`setup`, `launch`, `console`,
  `make_package`, `publish`), player files to `package/` in the zip's layout (`README.txt` was `coop_package_README.txt`,
  `fix_spore_registry.ps1` was in `local/`, `coop_update/` was `update/`), `DESIGN.md` to `docs/`. The double-click entry
  points stay in the root with the same names: `launch_spore_mod.bat`, `console.bat`; the updater from the repo is now
  `package\coop_update\update.bat` (it finds the repo by `.git` above itself). The release zip has exactly the same files.
  `local/` is now fully ignored. **Pull before you edit any of the moved files** (git follows the renames).

## build-20261024-1210

- **Automatic restart and auto-join after a received world**: when the client has received the host's world the mod restarts the game by itself 3 s later (a hidden PowerShell helper waits for the game to end and starts `SporeApp.exe -w` again; the game is closed with `WM_CLOSE`, so it saves), the world is installed at that start as before, and the restarted game **joins the same host again by itself** (the host id waits in `Games\CoopAutoJoin.txt`, read once). The player no longer closes the game or types `coopjoin` a second time, only presses Continue. `launch.ps1` sets `COOP_CONSOLE_PS1` so the console window reopens after the restart. `COOP_NO_RESTART` keeps the old manual flow; `coop restart` restarts the game without a world (also a dev tool). A separate save slot cannot replace the restart: Spore only knows `Game0`. Tested here: restart (new process, new console window), install at start and auto-join from a simulated received world; not yet over a real transfer between two PCs. No protocol change.

- **Splash picture is in**: `assets/splash.png` is now exxecute's art (both developers, "The best mod ever") instead of the
  red placeholder. To change it, replace that file (PNG 1920x1080) and build.

- **Intro splash**: our own picture for 2.5 s after the EA logo, then the intro continues (Maxis, Spore logo). The intro is
  one movie, so the mod pauses it after the EA part and shows a window on top. The picture is `assets/splash.png`
  (1920x1080, 16:9; keep the important part in the middle ~1440x1080); today a **red placeholder** until exxecute's art
  is in. New `mod\CoopMod\make_assets.ps1` (run by `build.bat`) packs it into `CoopMod.package`, which the launcher,
  `make_package.ps1` (`Data\` in the zip) and `update.ps1` install into `<game>\Data\`. No protocol change.

- **The updater checks and asks** (`update\update.ps1`, `update.bat`): from a repo checkout it looks at both news sources, new commits of the repo on GitHub (`git fetch`, the new commit subjects are listed) and a newer GitHub release than the installed one, and asks `P` = git pull, `R` = install the release into the game, `B` = both, `S` = skip (default). Without a repo (a player's `<game>\coop_update`) it still installs the latest release without asking. New switches: `-Install` (install the release, no questions: used by `launch.ps1 -Mode release`), `-Check` and `-Only repo|release|both`. `launch.ps1` in `dev` mode runs `update.ps1 -Check -Only repo` before building: it is silent when nothing is new and asks whether to pull when there are new commits (`-NoUpdateCheck` skips it). A failing `git pull` (local commits, uncommitted changes in the way) prints what to do instead of crashing. No protocol change.

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

















