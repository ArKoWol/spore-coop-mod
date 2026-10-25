# How the mod works now

> Status: reference · Owner: both · Updated: 2026-10-25
> Read when: before you change the mod's code, the protocol or the tools. It describes the code as it is, not the plan
> (the plan is `PLAN.md`). Update it in the same change that makes it wrong.

## 1. Loading
```
SporeApp.exe
  loads dinput8.dll (SporeModLoader)            -> injects DLLs before the game starts
    SporeModLoader\CoreLibs\SporeModAPI*.dll    -> Spore ModAPI core: knows game function addresses
    SporeModLoader\ModLibs\CoopMod.dll          -> OUR MOD (source: mod\CoopMod)
      steam_api_coop.dll (loaded by hand)       -> Steam networking, AppID 480 (Spacewar)
```
`InstallPendingWorld()` runs from `DllMain`, before the game opens its save (section 5). The mod hooks `GetCommandLineA/W`
to append ` -w`, so the game always starts windowed (`COOP_FULLSCREEN` keeps the game's own mode). The game window title
shows the co-op state. `Initialize` attaches the detours,
the console commands and the update functions; `Dispose` removes all of them (see `FACTS.md` section 3 for why).

## 2. Source files

| File | What it does |
|---|---|
| `mod/CoopMod/dllmain.cpp` | start-up and shutdown, console commands, Steam/UDP transport, `StatePacket`, world transfer, restart, remote console, puppet, detours (`NPCTickAI`, `TakeDamage`, `PlayAnimation`, `ExecuteAction`, movie), crash handler, `kProtocolVersion` |
| `mod/CoopMod/World.cpp/.h` | the shared world: the host's animals streamed to the guest as mirrors, species and model requests, avatar health, damage requests, nests, animation events |
| `mod/CoopMod/Objects.cpp/.h` | fruit, fruit groups, plants, eggs: scan and removal events (broadcast and manifest apply switched off, they only log: `eco:` lines) |
| `mod/CoopMod/Entities.cpp/.h` | `coop dump`: what exists in the world, per noun type |
| `mod/CoopMod/Models.cpp/.h` | creature models (`bem` records) sent to the guest and registered as packages |
| `mod/CoopMod/RecentHosts.cpp/.h` | remembered hosts in `%APPDATA%\Spore\Games\CoopHosts.txt` (`steamid<TAB>unixtime<TAB>name`, newest first, 10 at most) |
| `mod/CoopMod/Log.cpp/.h` | `CoopMod.log` |
| `mod/CoopMod/game/PuppetMotion.cpp/.h` | how a puppet or mirror follows received positions with the game's locomotion |
| `mod/CoopMod/game/MenuWorlds.cpp/.h` | save stars in the open galaxy (`AddWorld`, `RemoveWorld`, `SelectedStar`; dev commands `coop slots/addworld/addcenter/delworld/menureload/ggefind`); detours on `cGameModeManager::SetActiveMode/At/ByName` (logging; `ByName("LoadGame")` asks `CoopMenu::HoldPlay`) and the main menu mode's `OnEnter` |
| `mod/CoopMod/game/CoopMenu.cpp/.h` | the CO-OP star and window in the main menu: GDI+ pictures shown by a UTFWin window (look: `features/UI_STYLE.md`), friends list, Join through `CoopNet` (connect, download, restart, then the star is a copy of the host's newest creature save), hides the card's Play until a world is ready (`coop menu/carddump`) |
| `mod/CoopMod/game/SaveIds.cpp/.h` | research: RefPack and DBPF read/write; moves a save or a planet record to another star (used only with `COOP_MOVE_SAVE=1`) |
| `mod/CoopMod/game/LoadTrace.cpp/.h` | research: with `COOP_LOAD_TRACE=1` logs how a save loads (`load:` lines: planet database records, planet resources, record lookups, `cPlanet::Read`, planet views) |
| `mod/CoopMod/game/CoopNet.h` | what the windows need from the session code (implemented in `dllmain.cpp`): `Join`, `Host`, `Cancel`, `State` (answer, version, download share, staged, installed at start), `Hosting` (hosting, guest here, own Steam ID), `Name` |
| `mod/CoopMod/game/UiDraw.cpp/.h` | the shared look and plumbing of the in-game windows: GDI+ drawing (`Plate`, `Button`, `Text`), PNG, `PictureWindow` (a picture in a UTFWin window, clicks to a handler) |
| `mod/CoopMod/game/NamePlates.cpp/.h` | Steam names above the players' creatures (creature stage, during a session); screen position from `cViewer::GetCameraToPoint` |
| `mod/CoopMod/game/HostPanel.cpp/.h` | the "Host co-op" plate in the creature stage (top right): host, Steam ID, copy, stop, who is here |
| `core/coop_core.h` | the replication core (plain C++, no game headers): entity table, snapshots and deltas; tests in `tests/` |

## 3. Each frame
`App::AddUpdateFunction(NetUpdate)`: pump Steam callbacks, receive packets, send our avatar's position and orientation (30 Hz,
`StatePacket`), drive the puppet from the newest received state, run `World` and `Objects` updates, run queued console lines
(`ConsoleUpdate`).

## 4. Transport, channels and messages
- **Steam** `ISteamNetworkingMessages` through the flat C exports of a modern 32-bit `steam_api.dll`, kept as
  `steam_api_coop.dll` so it does not clash with the GOG build's old stub `steam_api.dll`. Peers are found by Steam ID; Steam
  relays the traffic. **UDP** on port 27777 is the fallback (`coop udphost` / `coopudpjoin`, no world transfer).
- `kProtocolVersion` (`dllmain.cpp`) is **12**. Players with different versions refuse each other on purpose.

| Channel | Content | Reliability |
|---|---|---|
| 0 | avatar state (`StatePacket`) | unreliable |
| 1 | save-game files (world transfer) | reliable |
| 2 | world deltas (`World.h` `kChannelDelta`) | unreliable |
| 3 | world events (`World.h` `kChannelReliable`) | reliable, ordered |

| Message (first byte) | Direction | Meaning |
|---|---|---|
| 100 `kMsgSpecies` | host -> guest | which species a hash stands for |
| 101 `kMsgTelemetry` | guest -> host | table, mirror and stray counts |
| 102 / 103 / 104 `kMsgModelReq` / `kMsgModel` / `kMsgModelNone` | both | a species the guest cannot build, its model records, "none here either" |
| 106 `kMsgAvatar` | both, unreliable | health, max health, scale of the sender's avatar |
| 107 `kMsgDamageReq` | guest -> host | report of the guest's own hit on a mirror (netId, damage, type); netId 0 = the host's avatar |
| 108 `kMsgDamage` | host -> guest | apply this damage to your own avatar |
| 109 / 110 `kMsgManifest` / `kMsgRemove` | both | object manifest and removals (`Objects.h`; applying them is switched off) |
| 112 `kMsgAnim` | both | creature (0 = host avatar, 0xFFFFFFFF = guest avatar, else netId) started animation id |
| 113 `kMsgNests` | host -> guest | the host's nests (position, species), every 20 s and on connect |
| 114 `kMsgEaten` | guest -> host | the guest ate this animal's corpse (netId); the host removes the real one |
| 115 `kMsgAvatarSpecies` | both | species key of the sender's avatar; the receiver builds the puppet from it (model requests go both ways) |

## 5. World transfer (joining)
When a client first contacts the host, the host sends the files of `%APPDATA%\Spore\Games\Game0` (about 3 MB, 60 KB chunks, a
few per frame because Steam's send buffer is small). The client stores them as `Game0.coopready`; the game is then
**restarted by the mod itself** 3 s later: a hidden PowerShell `%TEMP%\coop_restart.ps1` waits for the process to end and
starts `SporeApp.exe -w` again; the game is closed with `WM_CLOSE`, so it saves (`COOP_NO_RESTART` turns this off; `coop
restart` tests it without a world). The new game opens its own console window (`OpenConsoleWindow`: `scripts\console.ps1` from `COOP_CONSOLE_PS1`, else searched
up to 5 folders above the game folder; `COOP_NO_CONSOLE` turns it off). A received world carries `coop_host.txt` (the host's
Steam ID), so the mod can tell the host's world from the player's own. `InstallPendingWorld()` swaps it in as `Game0` (the old one is kept as `Game0.coopbackup`,
never deleted; later installs use `coopbackup2`, `coopbackup3`, ...). The host id is left in `Games\CoopAutoJoin.txt` (read
once and deleted), so the restarted game **joins the host again by itself**; the world the host sends again is ignored in
that run; the player presses Continue. Both sides then share the planet, so absolute positions (`sAbsolute`, default) line up.
The host treats a client that was silent for 10 s (`kReconnectMs`) or a different Steam ID as a new connection and sends the
world again; the client ignores a world that arrives during a game and prints "connected to the host" when the host's first
packet arrives. The planned replacement is `features/JOIN_FLOW.md`.

## 6. Remote console
The mod listens on `127.0.0.1:27780` (local only); `scripts/console.ps1` sends lines that run as console commands and gets
the console output back (detour on the game's `ArgScript::PrintF`). The socket has its own thread; commands are queued and run
on the main thread in `ConsoleUpdate`. One client at a time; a new one replaces the old.

Console commands (Ctrl+Shift+C in the game, or `console.bat`): `coop host`, `coopjoin <steamid | last | number | name>`,
`coop friends`, `coop udphost`, `coopudpjoin <ip>`, `coop status`, `coop abs`, `coop move` / `coop walk` / `coop teleport`
(how the puppet follows, `anim` = `walk`), `coop motionlog`, `coop echo` (one-PC puppet test: it repeats you 0.4 s later, 4 m
to the side), `coop noworld`, `coop sendworld`, `coop off`, `coop dump`, `coop fps`, `coop restart`, `coop modeltest`,
`coop hit` / `coop kill` (one-PC check: 1 damage / the death path on the nearest animal),
`coop slots`, `coop addworld`, `coop addcenter`, `coop delworld`, `coop menureload`, `coop holdplay`, `coop ggefind`, plus
`stage <name>` and `coophello`.
Every command has at least one word (`FACTS.md` section 3). A new command is also added to the `$commands` list of
`scripts/console.ps1` (Tab completion).

## 7. Puppet and mirrors
- **Puppet** (the other player): a one-animal herd made with `GameNounManager.CreateHerd`; the animal shows up in
  `herd->mHerd` a few frames later; its AI is skipped by the `cCreatureAnimal::NPCTickAI` detour; the received position is
  smoothed and handed to `game/PuppetMotion.cpp`, which lets the game's locomotion walk it there (`MoveTo` to a point a little
  ahead, `StopMovement` + `SetOrientation` when the remote player stands, `Teleport` only when more than 6 m behind). The
  remote speed comes from the received positions. Removed with `GameNounManager.DestroyInstance(herd)`. Today it is built
  from the **local** avatar's species.
- **Host side of the world**: every 100 ms the `cCreatureAnimal`s (not the avatar, not the puppet) go into `HostWorld`
  (`core/coop_core.h`); deltas of 20 entities on channel 2, spawn/destroy events, a full snapshot every 10 s and all species
  definitions with every full snapshot on channel 3. An animal with an unknown species is held back up to 3 s. Every entity
  carries position, orientation, scale, age, health and the flags dead / alpha.
- **Mirrors** (the host's animals on the guest): each in its own empty, disabled herd of one, no nest (`World.cpp`
  `MakeMirror`), built with the host's age (rebuilt when age or species change), with scale, health and alpha applied on
  every update; AI skipped (`World::ShouldSkipAI`); within 150 m walked by `PuppetMotion::Follower`, farther ones and
  corpses teleported. A dead one is killed locally once (`KillLocally`) and pinned to the corpse position. The guest's own
  animals are removed as strays every 0.5 s. A species the guest cannot build waits up to 20 s for its model, then becomes a
  counted stand-in. The guest reports its counts every 2 s (`kMsgTelemetry`).
- **Nests**: the guest removes its own nests once (except its avatar's) and makes the host's ones as quiet herds with a nest
  (`CreateHerd(..., createNest = true)`), a few per half second, trusting only its own record of what it made.
- **Animations**: the same animation of the same creature is sent at most every 400 ms; a standing creature gets no stop/turn
  orders for 2.5 s after a remote animation was played on it (`PuppetMotion::Hold`).
- **Avatars**: both sides send their own avatar's health, max health and size 5 times a second (`kMsgAvatar`) and show them
  on their puppet of the other player.
- **Damage** (PLAN rule 3, the actor decides): the guest's own bite on a mirror is applied by the game on his machine at once
  (health, death, DNA are native) and reported as `kMsgDamageReq`; the host applies the same hit to the real animal with the
  guest's puppet as attacker (no reward). The mirror takes the host's health only when it changed (`lastHostHealth`), so the guest's
  hit is not undone before the echo arrives. Hits from anybody else on a mirror are swallowed. A host-side hit on the guest's
  puppet is sent to the guest as `kMsgDamage`.
- **Eating**: a mirror that the game marks eaten or deleted on the guest (`mbHasBeenEaten`, `mbMarkedForDeletion`) is reported as
  `kMsgEaten`; the host destroys the real animal and the guest makes no new mirror for that netId.

## 8. Rules the sync code keeps (do not break them without a plan change)
1. **One authority per kind of thing**: world objects and animals = host; each avatar's movement = its owner; damage and
   eating outcomes = host (`PLAN.md` moves the actor's own action results to the actor).
2. **State for what is continuous, events for what happens**: transforms as state (channels 0 and 2), create / destroy /
   damage / animation / death as reliable events (channel 3) with exact ids.
3. **Capture at the engine primitive, not by polling** (`CreateInstance`, `DestroyInstance`, `CreateHerd`, `CreateNest`,
   `TakeDamage`, `PlayAnimation`, `ExecuteAction`), and apply on the other side through the same primitive with an **echo
   guard**, so the receiver neither sends it back nor runs the game's follow-up logic twice.
4. **Never fall back silently**: a missing species is a counted stand-in with a log line, not a quiet substitute.
5. **Objects that exist on both sides from the save** are matched by a stable key (noun, species/model, rounded position), not
   streamed; everything made later gets a host netId through an event.
6. Packets carry netIds, never pointers.

## 9. Tools
| Tool | What |
|---|---|
| `launch_spore_mod.bat` (`scripts/launch.ps1`) | closes a running game, asks whether to pull when GitHub has new commits (`-NoUpdateCheck` skips), builds + installs (`-Mode dev`, default), or `release` / `play`; starts the game windowed (`-w`), waits for the mod and opens a console connected to it, which closes with the game. `-NoConsole` |
| `console.bat` (`scripts/console.ps1`) | remote console: interactive, or `console.bat "coop status" "coop host"`; several commands in one line with `;`; Tab completion, Up/Down history |
| `update.bat` (`package/coop_update/update.bat`) | from the repo: checks new commits and releases, asks to pull / install / both / skip. For players: installs the latest release. Finds the game by `-GameDir`, `coop_update.json`, the folder above, `test\SPORE_A` / `SPORE`, then the registry (`installloc`) |
| `scripts/setup.ps1` | one-time developer setup: Build Tools, ModAPI SDK, SporeModLoader, first build, optional test copy of the game |
| `mod/CoopMod/build.bat` | builds (output in `%TEMP%\coopmod_build.log`, shown by the launcher when the build fails) `staging\CoopMod.dll` and, through `make_assets.ps1`, `staging\CoopMod.package` from `assets/` (installed as `<game>\Data\CoopMod.package`) |
| `scripts/make_package.ps1`, `scripts/publish.ps1` | build `coop_package.zip`; publish a release (needs `gh auth login`) |
| `scripts/worlddiff.ps1` | compares two `CoopDump.txt` files from `coop dump` on two machines |
| `tests/run_core_tests.bat`, `tests/focus_test.ps1` | core tests (no game), focus test |
