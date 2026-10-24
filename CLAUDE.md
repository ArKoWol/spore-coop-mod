# Spore co-op mod: project guide (read this first)

This file is for people and AI assistants (Claude Code) working on the project. Two developers, each with their own
Claude, share this repo. Keep this file and `CHANGELOG.md` accurate: they are how we understand each other.

## Rules for every change
1. **Pull before you start** (`git pull`), commit small, push often.
2. **Record every change in `CHANGELOG.md`** under `## Unreleased` (what changed, why, and who/what to be careful of).
   `publish.ps1` turns the Unreleased section into the release notes and moves it under the new build tag.
3. If the packet layout, save transfer or anything both players must agree on changes, **bump `kProtocolVersion`** in
   `mod/CoopMod/dllmain.cpp` and say so in the changelog. Players with different versions refuse each other on purpose.
4. New knowledge about the game or ModAPI (a crash cause, an address, an API that works or does not) goes into the
   "Hard-won facts" section below, not only into the changelog.
5. Never commit game files, saves, `steam_api.dll` (Valve's file), logs, or anything in `local/` (it is ignored).
   Commit author email must be the GitHub noreply address, not a personal one.
6. Only one person publishes a release at a time (`latest` is simply the newest release). Tell the other before you do.
7. Do not run destructive or outward-facing actions (publishing, deleting, force-push) without the owner's OK.

## What this is
An experimental co-op mod for Spore: Galactic Adventures, GOG build (exe `SporebinEP1\SporeApp.exe`, 3.1.0.29, unencrypted).
Two players see each other as creatures in the creature stage and play on the same planet.
Status: early test. Creature stage only; the remote creature is a puppet with the local creature's look and no walking animation.

## How it works
```
SporeApp.exe
  loads dinput8.dll (SporeModLoader)            -> injects DLLs before the game starts
    SporeModLoader\CoreLibs\SporeModAPI*.dll    -> Spore ModAPI core: knows game function addresses
    SporeModLoader\ModLibs\CoopMod.dll          -> OUR MOD (source: mod\CoopMod\dllmain.cpp)
      steam_api_coop.dll (loaded by hand)       -> Steam networking, AppID 480 (Spacewar)
```
- **Console commands** (Ctrl+Shift+C in game): `coop host`, `coopjoin <steamid>`, `coop udphost`, `coopudpjoin <ip>`,
  `coop status`, `coop abs`, `coop anim`, `coop noworld`, `coop sendworld`, `coop off`, plus `stage <name>` and `coophello`.
  The game rejects a command without arguments, so every command has at least one word.
- **Each frame** (`App::AddUpdateFunction(NetUpdate)`): pump Steam callbacks, receive packets, send our avatar's
  position + orientation (30 Hz, `StatePacket`), drive the puppet from the newest received state.
- **Transport**: Steam `ISteamNetworkingMessages` through the flat C exports of a modern 32-bit `steam_api.dll`
  (kept as `steam_api_coop.dll` so it does not clash with the GOG build's old stub `steam_api.dll`). Peers are found by
  Steam ID; Steam relays the traffic. Channel 0 = state (unreliable), channel 1 = save-game files (reliable). UDP on port
  27777 is the fallback (no world transfer).
- **World transfer**: when a client first contacts the host, the host sends the files of `%APPDATA%\Spore\Games\Game0`
  (about 3 MB, 60 KB chunks, a few per frame because Steam's send buffer is small). The client stores them as
  `Game0.coopready`; the game must be **restarted**, and `InstallPendingWorld()` (called from `DllMain`, before the game
  opens its save) swaps it in as `Game0` (the old one is kept as `Game0.coopbackup`, never deleted; later installs use `coopbackup2`, `coopbackup3`, ...). The player joins again (the world the host
  sends again is ignored in that run) and presses Continue. Both sides then share the planet, so absolute positions
  (`sAbsolute`, default) line up. The host treats a client that was silent for 10 s (`kReconnectMs`) or a different
  Steam ID as a new connection and sends the world again; the client ignores a world that arrives during a game, and
  prints "connected to the host" when the host's first packet arrives.
- **Remote console**: the mod listens on `127.0.0.1:27780` (local only); `console.ps1` sends lines that run as console
  commands and gets the console output back (detour on the game's `ArgScript::PrintF`). The socket has its own thread;
  commands are queued and run on the main thread in `ConsoleUpdate`. One client at a time; a new one replaces the old.
- **Puppet**: a one-animal herd made with `GameNounManager.CreateHerd`; the animal shows up in `herd->mHerd` a few frames
  later; its AI is skipped by a detour on `cCreatureAnimal::NPCTickAI`; its transform is set with `SetPosition` /
  `SetOrientation` (smoothed). It is removed with `GameNounManager.DestroyInstance(herd)`.

## Repository layout
- `mod/CoopMod` - the mod (C++, VS 2022 Build Tools, x86). `build.bat` builds to `mod\CoopMod\staging\CoopMod.dll`.
- `setup.ps1` - one-time developer setup (Build Tools, ModAPI SDK, SporeModLoader, first build, optional test copy of the game).
- `update/` - `update.bat` / `update.ps1`: players run it to install or update from the latest GitHub release.
- `make_package.ps1`, `publish.ps1` - build `coop_package.zip`, publish a release (needs `gh auth login`).
- `launch_spore_mod.bat [dev|release|play]` - starts the test copy: `dev` (default) builds and installs `staging\CoopMod.dll`,
  `release` installs the latest GitHub release, `play` starts as is. Closes a running game first (asks). Starts windowed (`-w`).
- `console.bat` / `console.ps1` - remote game console (use the .bat where script execution is disabled): `.\console.ps1` (interactive) or `.\console.ps1 "coop status" "coop host"` (one-shot).
- `coop_package_README.txt` - player instructions shipped in the package. `local/` - ignored personal notes.
- Not in git: `SPORE/` (game), `test/SPORE_A` (test copy), `downloads/` (SDK, loader), `coop_package*`.

## Workflow
1. `.\setup.ps1 -GameDir "<your Spore folder>"` once.
2. Edit `mod/CoopMod/dllmain.cpp`.
3. Run `launch_spore_mod.bat`: it builds, closes the game if needed (the DLL is locked while it runs, hence `staging`),
   installs the build and starts the game. Each launch writes a `===== ... launch (mode) =====` line into `CoopMod.log`.
4. Check `test\SPORE_A\SporebinEP1\CoopMod.log` (our log) and `spore_log.txt` (ModAPI log).
5. Update `CHANGELOG.md`, commit, push. To ship: build, then `.\publish.ps1`. Players run `update.bat`.

Two-PC test: host loads a game into the creature stage and types `coop host`, sends the printed Steam ID; the client stays
in the main menu and types `coopjoin <id>`; after "world received" it restarts the game, types `coopjoin <id>` again and
presses Continue.

## Hard-won facts (do not rediscover these)
- **Use the GOG build.** The Steam build updated in Oct 2024 has an encrypted exe that breaks ModAPI (workaround: downgrade
  the exe to the March 2017 depot). ModAPI reports the GOG exe as platform `March2017`.
- **GOG install can leave the registry incomplete** (`HKLM\SOFTWARE\WOW6432Node\Electronic Arts\spore*`): symptoms are
  alerts "Configuration Script Failed [2000]" and "data directory is missing or corrupt [1004]", even without mods.
  Fix: `local/fix_spore_registry.ps1 -GameDir <folder>` as administrator (shipped in the package).
- **ArgScript**: `line.GetArguments(n)` accepts exactly `n` arguments: fewer gives "Expecting at least n arguments", more gives "Expecting at most n arguments" (the game prints it, our code is not reached). So every command has a fixed argument count: `coop <word>` (1 arg), `coopjoin <steamid>` (1 arg), `coopudpjoin <ip>` (1 arg). `GetArgumentsRange` was suspected of crashing the game and is avoided.
  Calling a cheat with zero arguments is rejected by the game itself ("Expecting at least 1 arguments").
- **Do not call `cCreatureAnimal::Create` without a herd**: it crashes right after spawning. Use `CreateHerd`.
- **`cCreatureBase::WalkTo`** crashed in an early test (no herd then); it is behind the experimental `coop anim` switch.
- Creature stage is on a **sphere**: positions and orientations are full 3D values; copy them as they are, never assume a
  flat plane or `+Y` forward.
- A `member_detour` needs `GetAddress(Class, Method)`; ours is `NPCTickAI_detour` in `AttachDetours()`.
- **Steam API**: set env `SteamAppId`/`SteamGameId` = 480 before `SteamAPI_InitFlat`. `SteamNetworkingMessage_t` starts with
  `void* m_pData; int m_cbSize;` (only these are read). Unknown senders must be accepted through
  `SetGlobalCallback_MessagesSessionRequest` (callback receives the identity as its first field). Reliable sends return
  `1` (`k_EResultOK`) or fail when the send buffer is full: retry next frame.
- A crash in `SporeModAPI.dll` / `SporeApp.exe` leaves Windows Event 1000 in the Application log with the fault offset;
  a clean exit writes "Shutting Down" in `spore_log.txt`.
- `Simulator::IsCreatureGame()` is false in the main menu, which is where the client sits before the world arrives.
- **The running game keeps `planetRecords.pkp`, `planetRecords.pkt` and `PlanetScripts.pld` in `Game0` open** (read share
  only, `StarManager` databases) from startup, even in the main menu: `Game0` cannot be renamed or overwritten while the
  game runs (MoveFile fails, then error 183). The galaxy is also already in memory. Hence the install-on-restart above.
- **A minimized Spore runs no frames**: no update functions, no co-op networking, no remote console. Fullscreen Spore
  minimizes as soon as it loses focus; `SporeApp.exe -w` runs windowed and keeps running (throttled) without focus.
- The game prints its own console errors ("Expecting at least 1 arguments") outside `ArgScript::PrintF`;
  `CheatManager.ProcessLine` returns false for them.
- Game saves live in `%APPDATA%\Spore\Games\Game0` (`.spo`, `stars.db`, `PlanetScripts.pld`, ...). Console: Ctrl+Shift+C.

## Open work / ideas
- Restart the game automatically after the world arrives (and rejoin), instead of asking the player to.
- Load the received world automatically (`cGamePersistenceManager::LoadGame`, undocumented) instead of pressing Continue.
- Puppet with the remote player's own creature (send the species key / model), walking animation, combat/damage sync.
- Shared NPCs and nests (host authority), stage transitions with a "ready" barrier, late join, disconnect handling.
- Other stages (tribe, civ, space). Save model: host save is canonical, the guest keeps a separate save.




