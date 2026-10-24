# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format: one bullet per change: what, why, and anything the other developer must know (packet/protocol changes, new commands).

## Unreleased

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





