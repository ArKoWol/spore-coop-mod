# Spore co-op mod: project guide (read this first)

This file is for people and AI assistants (Claude Code) working on the project. Two developers, each with their own
Claude, share this repo. Keep this file and `CHANGELOG.md` accurate: they are how we understand each other.

## Rules for every change
1. **Pull before you start** (`git pull`), commit small, push often.
2. **Record every change in `CHANGELOG.md`** under `## Unreleased` (what changed, why, and who/what to be careful of).
   `scripts\publish.ps1` turns the Unreleased section into the release notes and moves it under the new build tag.
3. If the packet layout, save transfer or anything both players must agree on changes, **bump `kProtocolVersion`** in
   `mod/CoopMod/dllmain.cpp` and say so in the changelog. Players with different versions refuse each other on purpose.
4. New knowledge about the game or ModAPI (a crash cause, an address, an API that works or does not) goes into the
   "Hard-won facts" section below, not only into the changelog.
5. Never commit game files, saves, `steam_api.dll` (Valve's file), logs, or anything in `local/` (it is ignored).
   Commit author email must be the GitHub noreply address, not a personal one.
6. Only one person publishes a release at a time (`latest` is simply the newest release). Tell the other before you do.
7. Do not run destructive or outward-facing actions (publishing, deleting, force-push) without the owner's OK.

> **Direction:** the current "everyone plays their own world" build is a demo. The real plan is in `docs/DESIGN.md` (host-authoritative replicated world); read it before starting work on gameplay sync.

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
- **Console commands** (Ctrl+Shift+C in game): `coop host`, `coopjoin <steamid | last | number | name>`, `coop friends`,
  `coop udphost`, `coopudpjoin <ip>`,
  `coop status`, `coop abs`, `coop move` / `coop walk` / `coop teleport` (how the puppet follows, `anim` = `walk`),
  `coop motionlog`, `coop echo` (one-PC puppet test: it repeats you 0.4 s later, 4 m to the side), `coop noworld`, `coop sendworld`, `coop off`, plus `stage <name>` and `coophello`.
  Main-menu spike (`game/MenuWorlds.cpp`): `coop slots`, `coop addworld`, `coop addcenter`, `coop delworld`, `coop menureload`.
  The game rejects a command without arguments, so every command has at least one word.
- **Each frame** (`App::AddUpdateFunction(NetUpdate)`): pump Steam callbacks, receive packets, send our avatar's
  position + orientation (30 Hz, `StatePacket`), drive the puppet from the newest received state.
- **Transport**: Steam `ISteamNetworkingMessages` through the flat C exports of a modern 32-bit `steam_api.dll`
  (kept as `steam_api_coop.dll` so it does not clash with the GOG build's old stub `steam_api.dll`). Peers are found by
  Steam ID; Steam relays the traffic. Channel 0 = state (unreliable), channel 1 = save-game files (reliable). UDP on port
  27777 is the fallback (no world transfer).
- **World transfer**: when a client first contacts the host, the host sends the files of `%APPDATA%\Spore\Games\Game0`
  (about 3 MB, 60 KB chunks, a few per frame because Steam's send buffer is small). The client stores them as
  `Game0.coopready`; the game is then **restarted by the mod itself** (3 s later: a hidden PowerShell `%TEMP%\coop_restart.ps1` waits for the process to end and starts `SporeApp.exe -w` again; the new game opens its own console window (`OpenConsoleWindow`: `scripts\console.ps1`, `COOP_NO_CONSOLE` turns it off); the game is closed with `WM_CLOSE`, so it saves; `COOP_NO_RESTART` turns this off; `coop restart` tests it without a world), and `InstallPendingWorld()` (called from `DllMain`, before the game
  opens its save) swaps it in as `Game0` (the old one is kept as `Game0.coopbackup`, never deleted; later installs use `coopbackup2`, `coopbackup3`, ...). The host id is left in `Games\CoopAutoJoin.txt` (read once and deleted), so the restarted game **joins the host again by itself** (`Initialize`); the world the host
  sends again is ignored in that run; the player only presses Continue. Both sides then share the planet, so absolute positions
  (`sAbsolute`, default) line up. The host treats a client that was silent for 10 s (`kReconnectMs`) or a different
  Steam ID as a new connection and sends the world again; the client ignores a world that arrives during a game, and
  prints "connected to the host" when the host's first packet arrives.
- **Remote console**: the mod listens on `127.0.0.1:27780` (local only); `scripts/console.ps1` sends lines that run as console
  commands and gets the console output back (detour on the game's `ArgScript::PrintF`). The socket has its own thread;
  commands are queued and run on the main thread in `ConsoleUpdate`. One client at a time; a new one replaces the old.
- **Puppet**: a one-animal herd made with `GameNounManager.CreateHerd`; the animal shows up in `herd->mHerd` a few frames
  later; its AI is skipped by a detour on `cCreatureAnimal::NPCTickAI`; the received position is smoothed and handed to
  `game/PuppetMotion.cpp`, which lets the game's locomotion walk it there (`MoveTo` to a point a little ahead,
  `StopMovement` + `SetOrientation` when the remote player stands, `Teleport` only when more than 6 m behind), so the game
  plays the walk/idle animations itself. The remote speed is derived from the received positions (no packet change). It is removed with `GameNounManager.DestroyInstance(herd)`.

## Repository layout
```
launch_spore_mod.bat    double-click: build, install, start the game, open the console (scripts\launch.ps1)
console.bat             remote game console (scripts\console.ps1)
update.bat              check GitHub for new commits / a new release and ask (package\coop_update\update.bat)
CLAUDE.md CHANGELOG.md README.md
docs/                   DESIGN.md (the co-op plan)
mod/CoopMod/            the mod (C++), build.bat, make_assets.ps1
core/  tests/           replication core (plain C++) and its tests
assets/                 files packed into Data\CoopMod.package (splash.png)
scripts/                developer tools (PowerShell)
package/                what players get, laid out as in coop_package.zip
```
- `core/coop_core.h` - the replication core (plain C++, no game headers); `tests/` - its tests (`tests\run_core_tests.bat`) and `focus_test.ps1`.
- `mod/CoopMod` - the mod (C++, VS 2022 Build Tools, x86). `build.bat` builds `mod\CoopMod\staging\CoopMod.dll` and,
  through `make_assets.ps1`, `staging\CoopMod.package` from `assets/` (today `splash.png`, the picture after the EA logo).
  The launcher (dev), `scripts\make_package.ps1` and `update.ps1` install the package as `<game>\Data\CoopMod.package`.
- `scripts/setup.ps1` - one-time developer setup (Build Tools, ModAPI SDK, SporeModLoader, first build, optional test copy of the game).
- `scripts/launch.ps1` (wrapped by `launch_spore_mod.bat`) - the one-step test launcher: closes a running game, builds + installs (`-Mode dev`, default; before building it asks whether to pull when GitHub has new commits, `-NoUpdateCheck` skips that), `release` (install the latest release) or `play`; starts the game windowed (`-w`), waits for the mod and opens a PowerShell console connected to it; the console window closes when the game exits. Option: `-NoConsole`.
- `scripts/console.ps1` (wrapped by `console.bat`, which also works where script execution is disabled) - remote game console: `console.bat` (interactive) or `console.bat "coop status" "coop host"` (one-shot); several commands in one line are separated by `;`. The interactive
  prompt is a small line editor: Tab completes commands (twice: lists them), Up/Down is the history. Add new console commands
  to its `$commands` list too.
- `scripts/make_package.ps1`, `scripts/publish.ps1` - build `coop_package.zip`, publish a release (needs `gh auth login`).
- `package/` - the player files, in the same layout as inside `coop_package.zip`: `README.txt` (player instructions),
  `fix_spore_registry.ps1`, and `coop_update/` (`update.bat` / `update.ps1` / `repo.txt`): players run it to install or
  update from the latest GitHub release; run from the repo (`package\coop_update\update.bat`) it checks new commits and
  releases and asks whether to pull, install the release, both or skip (`update.bat` in the root calls it). It finds the
  repo by looking for `.git` above itself, and the game by: `-GameDir`, the saved `coop_update.json`, the folder above it
  (player install), `test\SPORE_A` / `SPORE` in the repo, then the game's own registry entry (`installloc`); it asks for
  the path only if all of these fail.
- `local/` - ignored personal notes.
- Not in git: `SPORE/` (game), `test/SPORE_A` (test copy), `downloads/` (SDK, loader), `coop_package*`.

## Workflow
1. `.\scripts\setup.ps1 -GameDir "<your Spore folder>"` once.
2. Edit `mod/CoopMod/dllmain.cpp`.
3. Run `launch_spore_mod.bat` (after your own `git pull`): it builds, closes a running game (the DLL is locked while it runs, hence `staging`), installs the build, starts the game and opens the connected console window (type `coop host` / `coopjoin <id>` there).
4. Check `test\SPORE_A\SporebinEP1\CoopMod.log` (our log) and `spore_log.txt` (ModAPI log).
5. Update `CHANGELOG.md`, commit, push. To ship: build, then `.\scripts\publish.ps1`. Players run `update.bat`.

Two-PC test: host loads a game into the creature stage and types `coop host`, sends the printed Steam ID; the client stays
in the main menu and types `coopjoin <id>`; after "world received" the game restarts itself, joins again by itself and the
player presses Continue.

## Hard-won facts (do not rediscover these)
- **Use the GOG build.** The Steam build updated in Oct 2024 has an encrypted exe that breaks ModAPI (workaround: downgrade
  the exe to the March 2017 depot). ModAPI reports the GOG exe as platform `March2017`.
- **GOG install can leave the registry incomplete** (`HKLM\SOFTWARE\WOW6432Node\Electronic Arts\spore*`): symptoms are
  alerts "Configuration Script Failed [2000]" and "data directory is missing or corrupt [1004]", even without mods.
  Fix: `package/fix_spore_registry.ps1 -GameDir <folder>` as administrator (shipped in the package).
- **ArgScript**: `line.GetArguments(n)` accepts exactly `n` arguments: fewer gives "Expecting at least n arguments", more gives "Expecting at most n arguments" (the game prints it, our code is not reached). So every command has a fixed argument count: `coop <word>` (1 arg), `coopjoin <steamid>` (1 arg), `coopudpjoin <ip>` (1 arg). `GetArgumentsRange` was suspected of crashing the game and is avoided.
  Calling a cheat with zero arguments is rejected by the game itself ("Expecting at least 1 arguments").
- **Move creatures with `Teleport(pos, rot)`, not `SetPosition`**: the creature's physics puts it back after
  `SetPosition` (the SDK says so on `cSpatialObject::Teleport`); `SetOrientation` alone does stick.
- **Do not call `cCreatureAnimal::Create` without a herd**: it crashes right after spawning. Use `CreateHerd`.
- **A puppet walks with the game's own locomotion even with its AI skipped**: `cLocomotiveObject::MoveTo(goal, 0.3, 0.6)`
  every 100 ms on the herd animal (whose `NPCTickAI` we skip) makes it walk there with leg animation; `StopMovement`
  stops it. Verified with `coop echo` (2026-10-24): puppet speed up to **4.0 m/s** (seems to be the creature's walk
  speed; a sprinting player at ~4.8 m/s gets 2-4 m ahead), no teleports while following. `SetDesiredSpeed` is the knob to try
  for running. `Teleport` every frame (the old way) gives no leg animation.
- **`cCreatureBase::WalkTo`** crashed in an early test (no herd then); it is `coop walk` now (untested with a herd).
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
- **The "exit crash with Steam" was not an exit crash.** `SteamAPI_Init` itself makes Windows log Event 1000
  (`0xC0000005`, offset `0xFFFFFFFF`, module unknown): Steam's `CSERHelper.dll` deliberately writes to `0xFFFFFFFF`,
  handles the exception and reports it; the event appears about a second after `coop host` / `coopjoin` while the game
  keeps running. Ignore that one. Verified 2026-10-24: with and without Steam the game exits with code 0, "Shutting
  Down" in `spore_log.txt` and no event after the close. Check a real exit crash by its time (after the close) and by
  the `crash:` lines in `CoopMod.log`.
- **The loader unloads the mod with `FreeLibrary` right after `Dispose`** ("CoopMod detached (FreeLibrary)" in the
  log), while the game still runs: everything pointing into the DLL must be removed in `Dispose` (done: Steam,
  console thread, update listeners, cheats, detours). Do **not** pin the DLL: static destructors then run at process
  exit, and since **ModAPI routes our `new`/`delete` to the game's allocator**, a static `std::string`/`std::vector`
  holding memory crashes in the game's free (`SporeApp.exe+0x527270`) because the game's heap is already gone.
  `Dispose` also frees our static containers as a safety net.
- **Crash log**: a vectored exception handler (`CrashHandler`) writes access violations to `CoopMod.log` as
  `crash:` lines with the thread, the address and the modules on the stack (first 3 only; the first one is usually
  the harmless Steam one above). To map `CoopMod.dll+0x...` to a function, relink with `set LINK=/MAP:<file>`.
- **A crashed or killed Spore can leave an unkillable process** after the AMD driver resets (`LiveKernelEvent 117`);
  a new game then hangs at startup or says "Another instance of the game is already running". Only a reboot clears it.
- **Remembered hosts**: `RecentHosts.cpp` keeps the hosts that answered in `%APPDATA%\Spore\Games\CoopHosts.txt`
  (`steamid<TAB>unixtime<TAB>name`, newest first, 10 at most). The name is `ISteamFriends::GetFriendPersonaName`; it can be
  empty/"[unknown]" right after joining, so the mod calls `RequestUserInformation` and stores it again after 3 s and 10 s.
- Game saves live in `%APPDATA%\Spore\Games\Game0` (`.spo`, `stars.db`, `PlanetScripts.pld`, ...). Console: Ctrl+Shift+C.
- **Intro splash**: the whole intro (EA logo, Maxis, Spore logo) is **one movie**, `0x7af7df4f` (type `0x376840d7`, EA VP6:
  `MVhd` + `MV0K`/`MV0F` chunks, video only, 1280x720 @ 29.97, in `Spore_EP1_Data.package`), started through
  `MovieSystem::PlayMovie` about 5.7 s after launch; its first 4 s (the EA logo) cannot be skipped. Our splash pauses it at
  4 s (`PauseMovie`), shows a UTFWin window with `coopmod!splash.png` (a UTFWin window **is drawn over a paused movie**) and
  continues after 2.5 s. **Detour movie methods on `Movie::IMovieSystem`, not `MovieSystem`**: `MovieSystem` derives from
  `ILayer` first, the game passes `this` as the `IMovieSystem` part, and virtual calls on a `MovieSystem*` crashed.
- **Own resources**: a DBPF package with uncompressed entries works when dropped into `Data\` (`make_assets.ps1` writes
  one: header version 3, index flags 4, entries `type, group, instance, offset, size|0x80000000, size, 0, 1`). Names are
  `id("...")` = FNV-1 of the lower-case name. A PNG of any size works for UTFWin images.

- **Creature species of the creature stage** (measured with `coop dump`): the NPC species of a planet are stock data
  (`Spore_Content.package`), not in the save. A save holds creature keys only; the avatar's own creature lives in
  `EditorSaves.package`. `CoopModels\m_*.package` (see `Models.cpp`) is the safety net for species a guest cannot build.
  A planet has about 100 animals, 100 herds and 100 nests. Plan and gates: `docs/SYNC_REDESIGN.md`.
  **`cCreatureBase::mSpeciesKey` is zero for most NPC animals**; the species is in `mpSpeciesProfile` (+0xB20, or the herd's
  `mpSpeciesProfile`); its key is the one it has in `SpeciesManager.mSpeciesProfiles`. Dynamic objects (animals, nests, eggs,
  obstacles, ornaments) differ between two machines that loaded the same save; fruit, plants, rocks and markers are identical.
- **Never iterate `SpeciesManager.mSpeciesProfiles`** (ModAPI's hash map layout does not match the game: crash "reading 3ea99f65").
  The key of a `cSpeciesProfile` is `profile->mCreatureKey`. To find a crash in our DLL: build with `set LINK=/MAP:file`, the
  `CoopMod.dll+0x...` of the log plus the preferred base 0x10000000 gives the function in the map.
- **Mirror animals**: make an empty herd (`CreateHerd(..., 0, ...)`, `mTargetHerdSize = 0`, `SetEnabled(false)`) and put the
  animal in with `cCreatureAnimal::Create(pos, profile, 1, herd)`; a herd of size 1 fills itself only when the game wants.

- **The main menu (GGE mode `0x2CCD1D2`, the galaxy) lists saved games live from `StarManager.mSavedGameStarRecords`**
  (stars with `kStarFlagSaveGame`; the slot's `.spo` is named after the star's played planet). A star pushed into that list while
  the menu is open appears at once with its card (name, stage, date, creature): no restart, no UI rebuild (verified
  2026-10-25, `coop addcenter`). The galaxy view is large: a star far from the centre is off screen. **Do not call
  `GlobalGGEUI::InitializeUI` / `Initialize` a second time**: it returns, changes nothing visible, and the game crashes at exit
  (eip 0 / 0x28 / 0x2a0 after `CoopMod disposed`). Closing the game from the menu does not save `stars.db`.
- **Main menu internals** (2026-10-25, one PC): the **selected star** is the StarID at `[GlobalGGEUI::Get() + 0xFC] + 0x7C`
  (it keeps the last selection when you click empty space; `+0x138` is something else). **Play** calls
  `cGameModeManager::SetActiveModeByName("LoadGame")` (then mode `0x1654C08`, then `Game_Creature`) and sends
  `kMessageLoadGame`; it never calls `cGamePersistenceManager::LoadGame`. Modes change through `SetActiveModeAt` /
  `SetActiveModeByName`, not `SetActiveMode`. A save star whose `.spo` was copied from another save loads normally by Play.

## Open work / ideas
- Load the received world automatically (`cGamePersistenceManager::LoadGame`, undocumented) instead of pressing Continue.
- Puppet with the remote player's own creature (send the species key / model), walking animation, combat/damage sync.
- Shared NPCs and nests (host authority), stage transitions with a "ready" barrier, late join, disconnect handling.
- Other stages (tribe, civ, space). Save model: host save is canonical, the guest keeps a separate save.












