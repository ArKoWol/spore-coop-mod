# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format: one bullet per change: what, why, and anything the other developer must know (packet/protocol changes, new commands).

## Unreleased

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







