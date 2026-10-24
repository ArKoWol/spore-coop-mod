# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format: one bullet per change: what, why, and anything the other developer must know (packet/protocol changes, new commands).

## Unreleased

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



