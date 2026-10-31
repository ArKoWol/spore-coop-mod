# How the mod works now

> Status: reference · Owner: both · Updated: 2026-10-31
> Read when: before you change the mod's code, the protocol or the tools. It describes the code as it is, not the plan
> (the plan is `PLAN.md`). Update it in the same change that makes it wrong.

## 1. Loading
```
SporeApp.exe
  loads dinput8.dll (SporeModLoader)            -> injects DLLs before the game starts
    SporeModLoader\CoreLibs\SporeModAPI*.dll    -> Spore ModAPI core: knows game function addresses
    SporeModLoader\ModLibs\CoopMod.dll          -> OUR MOD (source: mod\CoopMod)
      steam_api_coop.dll (loaded by hand)       -> Steam networking, AppID 480 (Spacewar); started when the main menu first opens
```
`CoopLaunch::Prepare()` runs from `DllMain`, before the game opens its save (section 5). The mod hooks `GetCommandLineA/W`
to append ` -w`, so the game always starts windowed (`COOP_FULLSCREEN` keeps the game's own mode). The game window title
shows the co-op state. `Initialize` attaches the detours,
the console commands and the update functions; `Dispose` removes all of them (see `FACTS.md` section 3 for why).

## 2. Source files

| File | What it does |
|---|---|
| `mod/CoopMod/dllmain.cpp` | start-up and shutdown, console commands, Steam/UDP transport, `StatePacket`, glue of the world transfer, restart, remote console, puppet, detours (`NPCTickAI`, `TakeDamage`, `PlayAnimation`, `ExecuteAction`, movie), crash handler, `kProtocolVersion` |
| `mod/CoopMod/World.cpp/.h` | the shared world: the host's animals streamed to the guest as mirrors, species and model requests, avatar health, damage requests, nests, animation events |
| `mod/CoopMod/Objects.cpp/.h` | fruit, fruit groups, plants, eggs: scan and removal events (broadcast and manifest apply switched off, they only log: `eco:` lines) |
| `mod/CoopMod/Entities.cpp/.h` | `coop dump`: what exists in the world, per noun type |
| `mod/CoopMod/Models.cpp/.h` | creature models (`bem` records) sent to the guest and registered as packages |
| `mod/CoopMod/RecentHosts.cpp/.h` | remembered hosts in `%APPDATA%\Spore\Games\CoopHosts.txt` (`steamid<TAB>unixtime<TAB>name`, newest first, 10 at most) |
| `mod/CoopMod/Log.cpp/.h` | `CoopMod.log` |
| `mod/CoopMod/game/PuppetMotion.cpp/.h` | how a puppet or mirror follows received positions with the game's locomotion |
| `mod/CoopMod/game/MenuWorlds.cpp/.h` | the main menu's galaxy, read-only: `SelectedStar` and `SelectStar` (spike J9: writes the StarID at `[GlobalGGEUI + 0xFC] + 0x7C`), `HostPlanet` (the star and planet of the newest creature-stage save of `Game0`), the `select:` log; detours on `cGameModeManager::SetActiveMode/At/ByName` (logging, `ByName("LoadGame")` starts `LoadTrace`). The galaxy is never changed (no star, no save is added) |
| `mod/CoopMod/game/CoopMenu.cpp/.h` | the CO-OP button plate (bottom left of the main menu, `COOP_BUTTON_CORNER=tl\|tr\|bl\|br`) and the CO-OP window anchored to it: GDI+ pictures shown by UTFWin windows (look: `features/UI_STYLE.md`), the list (invitations, friends hosting, friends running the mod, recent sessions; rules in `core/coop_presence.h`, data from `game/Presence.cpp`), Join through `CoopNet` (connect, download, restart), `JoinHost` for the invitation toast. In a co-op launch the window opens by itself (`<host>'s world: press Play on <planet>`, a Leave button) and the host's star is selected once (J9) (`coop menu`) |
| `mod/CoopMod/game/LoadTrace.cpp/.h` | research: with `COOP_LOAD_TRACE=1` logs how a save loads (`load:` lines: planet database records, planet resources, record lookups, `cPlanet::Read`, planet views) |
| `mod/CoopMod/game/CoopNet.h` | what the windows need from the session code (implemented in `dllmain.cpp`): `Join`, `Host` (refused in a co-op launch), `Cancel`, `Leave`, `RestartGame`, `State` (answer, version, download share, staged, `upToDate`, `error`, co-op launch = `installedAtStart`), `Hosting` (hosting, guest here, own Steam ID), `Name`; for M3 `StartSteam`, `FriendsApi`, `SetRichPresence`, the friends list calls and `SendTo` (one reliable message to any Steam user) |
| `mod/CoopMod/game/Presence.cpp/.h` | M3: starts Steam when the main menu first opens, publishes the Rich Presence key `sporecoop` (section 4), reads the key of the Steam friends every 5 s while a window watches (`presence:` lines), keeps the received invitations, sends and parses `kMsgInvite` (`invite:` lines) |
| `mod/CoopMod/game/InvitePanel.cpp/.h` | M3: the host's invite list under the host plate (friends in their main menu, each with Invite) and the toast of an invitation in the main menu (`Join`, `Not now`) |
| `mod/CoopMod/game/UiDraw.cpp/.h` | the shared look and plumbing of the in-game windows: GDI+ drawing (`Plate`, `Button`, `Text`), PNG, `PictureWindow` (a picture in a UTFWin window, clicks to a handler) |
| `mod/CoopMod/game/Actions.cpp/.h` | abilities, creature effects and voices: captured at the game's functions, replayed on the other side's copy (`kMsgAction`) |
| `mod/CoopMod/game/SelfTest.cpp/.h` | `coop selftest`: plays abilities, an animation and an effect on our avatar, checks them on the echo puppet (PASS / FAIL), presses keys 1-9 |
| `mod/CoopMod/game/OwnCreature.cpp/.h` | the player's own creature: watches the editor (mode, new creature records in EditorSaves), the avatar's profile in memory, reads our body from every database (`ReadFresh`), writes `Games\CoopOwnCreature.txt` and switches the guest's avatar to it in a co-op launch (`own:` lines) |
| `mod/CoopMod/game/FollowFriend.cpp/.h` | a click on the friend's creature (`GetHoveredObject`) makes our avatar follow it with `MoveTo`; keys or another click end it |
| `mod/CoopMod/game/NamePlates.cpp/.h` | Steam names above the players' creatures (creature stage, during a session); screen position from `cViewer::GetCameraToPoint` |
| `mod/CoopMod/game/HostPanel.cpp/.h` | the "Host co-op" plate in the creature stage (top right): host, Steam ID, copy, stop, who is here, Invite (opens the list of `InvitePanel`); for a guest in a co-op launch the plate `In <host>'s world` with Leave |
| `mod/CoopMod/game/CoopLaunch.cpp/.h` | the co-op launch in the game: `Prepare()` from `DllMain` (calls `core/coop_launch.h`, logs `launch:` lines), `Active/Host/Notice`, `HostName`; in the main menu a plate with the notice of a failed folder move and the one-time restore question of an old install |
| `mod/CoopMod/game/WorldTransfer.cpp/.h` | the world transfer on channel 1: the host's snapshot, manifest and chunk pump; the guest's state (download, retry, staged, error, abort); calls `core/coop_manifest.h` |
| `core/coop_manifest.h` | the manifest logic (plain C++, Win32 file calls only): CRC32, wire formats, snapshot of a folder, diff against the cache, the `Download` (chunks in, build, verify), `Commit`, `RemoveIncoming`; tests in `tests/manifest_tests.cpp` |
| `core/coop_core.h` | the replication core (plain C++, no game headers): entity table, snapshots and deltas; tests in `tests/` |
| `core/coop_launch.h` | the folder logic of the co-op launch (Win32 file calls only, no game headers): restore, launch, migration; tests in `tests/launch_tests.cpp` |
| `core/coop_presence.h` | the Rich Presence string (format, parse), who may be joined or invited and why not, the text of a row, the order of the friends (plain C++); tests in `tests/presence_tests.cpp` |

## 3. Each frame
`App::AddUpdateFunction(NetUpdate)`: `Presence::Update` and `InvitePanel::Update` (also with no session; without a Steam session
`SteamIdlePump` runs the callbacks and reads the invitations), pump Steam callbacks, receive packets, send our avatar's position and orientation (30 Hz,
`StatePacket`), drive the puppet from the newest received state, run `World` and `Objects` updates, run queued console lines
(`ConsoleUpdate`).

## 4. Transport, channels and messages
- **Steam** `ISteamNetworkingMessages` through the flat C exports of a modern 32-bit `steam_api.dll`, kept as
  `steam_api_coop.dll` so it does not clash with the GOG build's old stub `steam_api.dll`. Peers are found by Steam ID; Steam
  relays the traffic. **UDP** on port 27777 is the fallback (`coop udphost` / `coopudpjoin`, no world transfer).
- `kProtocolVersion` (`dllmain.cpp`) is **16** (14: manifest world transfer and, in parallel, abilities/effects/voices; 15: both merged; 16: `kMsgInvite` and the presence key). Players with different versions refuse each other on purpose.

| Channel | Content | Reliability |
|---|---|---|
| 0 | avatar state (`StatePacket`) | unreliable |
| 1 | world transfer: manifest, wanted files, file chunks (table below) | reliable |
| 2 | world deltas (`World.h` `kChannelDelta`) | unreliable |
| 3 | world events (`World.h` `kChannelReliable`) | reliable, ordered |

| Channel 1 message (first u32) | Direction | Meaning |
|---|---|---|
| `'SPMF'` | host -> guest | manifest: count, then per file name, size, CRC32 (`core/coop_manifest.h`) |
| `'SPND'` | guest -> host | names of the files the guest needs; not sent when nothing is needed |
| `'SPFL'` | host -> guest | chunk of a needed file: name[64], offset, total size, data length, up to 60 KB data |
| `'SPRQ'` | guest -> host | "send a fresh manifest" (the one retry after a download that did not check out) |

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
| 113 `kMsgNests` | host -> guest | the host's nests (position, species, orientation, model key, scale), every 20 s and on connect |
| 114 `kMsgEaten` | guest -> host | the guest ate this animal's corpse (netId); the host removes the real one |
| 115 `kMsgAvatarSpecies` | both | species key of the sender's avatar and a hash of its body; the receiver builds the puppet from it (model requests go both ways) |
| 116 `kMsgCorpse` | host -> guest | once per dead animal: netId, food value, flags (the guest writes the food value into the corpse) |
| 117 `kMsgAction` | both | creature (as in 112) played an ability (type), started or stopped a creature effect (effect id, pool id), or a voice (name) |
| 118 `Presence::kMsgInvite` | host -> any Steam friend | invitation: protocol u32, host Steam ID u64, planet name (length u8 + UTF-8). Channel 3 like the world events, but the pump in `dllmain.cpp` gives it to `Presence::OnMessage` first, also when no session runs (`SteamIdlePump`) |

**Presence and invitations (M3, `game/Presence.cpp`).** Steam is started once when the main menu first opens (`Presence::Update`;
the init writes the harmless CSERHelper line, `FACTS.md` section 4) and stays up; host and join reuse it. If it fails (Steam not running)
the window says so and Refresh tries again. Every frame the mod builds the key `sporecoop` = `<protocol>|<state>|<planet>|<players>/<slots>`
(`core/coop_presence.h`) and writes it with `SetRichPresence` when it changed, at most every 2 s (`presence: set` line). State from the
game mode: main menu `menu`, creature stage `hosting` / `guest` / `playing` (no session), editors `editor`, anything else `busy`; a guest
in the menu is `guest`. A running host counts `1/2`, `2/2` with a guest, in any state (a host in the editor is a grey Join with the reason); others publish `0/0`. While the CO-OP window or the invite list is open,
every 5 s: immediate friends, only those playing AppID 480, `RequestFriendRichPresence` and `GetFriendRichPresence`; a friend without the
key is plain Spacewar and not listed (`presence: N friends, M on AppID 480, K run the mod` when the result changed). Dispose clears the key.
An invitation is accepted only from a Steam friend, with our protocol, while we are in the main menu with no join, no co-op launch and no
hosting; otherwise it is logged and ignored. The newest one wins (one per host, 4 at most, they fade after 10 minutes). Missing friends
exports turn M3 off with one log line (`steam: missing export ...`); joining by Steam ID still works.

## 5. World transfer (joining)
When a client first contacts the host, the host takes a snapshot of the files of `%APPDATA%\Spore\Games\Game0` (about 3 MB, held in
memory so manifest and bytes agree) and sends the manifest (`'SPMF'`: name, size, CRC32 per file). The client compares it with its
cache `Games\Coop\<host id>` (`MakePlan`: a file is kept when size and CRC32 match) and answers with the names it needs (`'SPND'`).
The host sends only those files (`'SPFL'`, 60 KB chunks, a few per frame because Steam's send buffer is small). The client
builds `Games\Coop\<host id>.incoming\` from the unchanged cache files plus the received ones plus `coop_host.txt` (the host's
Steam ID), checks every file against the manifest (size + CRC32, no other file), and only then replaces the cache
(`Commit`: old cache aside, new folder in, old cache deleted). Flows of the guest (`game/WorldTransfer.cpp`):
- **Up to date** (every file matches, no extra file, `coop_host.txt` right): nothing is sent or built; straight to `CoopLaunch.txt`
  (`JoinState.upToDate`).
- **Partial / full**: as above. Files the manifest no longer lists are not carried over (a cache with extra files is rebuilt even when
  nothing is needed).
- **Check fails** (wrong CRC, bad chunk): `.incoming` is deleted, the guest sends `'SPRQ'` and downloads everything once more from
  a fresh manifest; the second failure ends the join with `JoinState.error` (and a console line). The cache is not touched.
- **Cancel / host gone** (no packet for `kReconnectMs`, or nothing on the file channel for 30 s): `NetStop`/`GuestUpdate` delete
  `Coop\<id>.incoming` (the only folder `RemoveIncoming` deletes), `JoinState.error` says why. A killed game leaves its
  `.incoming`; the next download clears it.

After the world is complete the mod writes `Games\CoopLaunch.txt`
(the host id) and the game is **restarted by the mod itself** 3 s later: a hidden PowerShell `%TEMP%\coop_restart.ps1` waits
for the process to end and starts `SporeApp.exe -w` again; the game is closed with `WM_CLOSE` (`COOP_NO_RESTART` turns the
restart off; `coop restart` tests it without a world). The new game opens its own console window (`OpenConsoleWindow`:
`scripts\console.ps1` from `COOP_CONSOLE_PS1`, else searched up to 5 folders above the game folder; `COOP_NO_CONSOLE` turns it off).

**Start-up folders** (`CoopLaunch::Prepare()` in `DllMain`, before the game opens `Game0`; logic in `core/coop_launch.h`, log lines
`launch:`). The player's own `Game0` never goes away for good and nothing is ever deleted:
1. *Migration*, only when `Games\CoopMigrate.txt` exists (the answer Restore of an old install's question, deleted when read):
   `Game0` (a host's world) goes to `Coop\<id>` and the oldest `Game0.coopbackup*` without `coop_host.txt` becomes `Game0`; the
   other backups stay (named in the log).
2. *Restore*, when `Game0.own` exists (the last run was a co-op launch, also after a crash or kill): `Game0` goes to
   `Coop\<id>` (id from its `coop_host.txt`; unreadable: `Coop\unknown-<time>`; a taken name gets `.old2`, `.old3`, ...) and
   `Game0.own` becomes `Game0`. A co-op launch of a player without a `Game0` leaves `Games\CoopNoOwn.txt` instead of `Game0.own`:
   restore then only moves the host's world away and invents no `Game0`.
3. *Co-op launch*, when `CoopLaunch.txt` names a host whose `Coop\<id>` exists (the file is deleted when read): `Game0` goes to
   `Game0.own`, `Coop\<id>` becomes `Game0`. This run is a co-op launch (`CoopLaunch::Active()`, `CoopNet::State().installedAtStart`):
   `Initialize` joins the host by itself, a world sent again is ignored, `coop host` is refused. In the main menu the CO-OP window
   opens by itself (`CoopMenu`), finds the star of the newest creature-stage save of `Game0` (the host's played planet, `MenuWorlds::
   HostPlanet`) and writes its id into the menu's selection once (J9; `menu: J9` lines); the player presses Play on the card.
A failed move (a process holds the folder) is retried twice, then undone as far as it went, logged and told to the player: in the
console at start and in a plate in the main menu; the co-op launch does not start (`CoopLaunch.txt` is still consumed) and a failed
restore keeps `Game0.own` for the next start.

**Leave** (`CoopNet::Leave()`, from the Leave plate of `HostPanel`, the Leave button of the CO-OP window or `coop leave`) plans the
same restart without `CoopLaunch.txt`; the new start restores (step 2). Closing the game or killing it has the same result at the
next start. **Restore question** (`CoopLaunch::Update`, first time the main menu opens, only when `Game0` has `coop_host.txt`, no
`Game0.own` exists, an own backup is left and `Games\CoopMigrateAsked.txt` is missing): Restore writes `CoopMigrate.txt`, marks the
question as asked and restarts; Not now only marks it.

Both sides then share the planet, so absolute positions (`sAbsolute`, default) line up. The host treats a client that was silent
for 10 s (`kReconnectMs`) or a different Steam ID as a new connection and sends the world again; the client ignores a world that
arrives during a game or a co-op launch and prints "connected to the host" when the host's first packet arrives. The rest of the
plan is `features/JOIN_FLOW.md`.

## 6. Remote console
The mod listens on `127.0.0.1:27780` (local only); `scripts/console.ps1` sends lines that run as console commands and gets
the console output back (detour on the game's `ArgScript::PrintF`). The socket has its own thread; commands are queued and run
on the main thread in `ConsoleUpdate`. One client at a time; a new one replaces the old.

Console commands (Ctrl+Shift+C in the game, or `console.bat`): `coop host`, `coop leave` (in a co-op launch: restart in your own
world), `coopjoin <steamid | last | number | name>`,
`coop friends`, `coop udphost`, `coopudpjoin <ip>`, `coop status`, `coop abs`, `coop move` / `coop walk` / `coop teleport`
(how the puppet follows, `anim` = `walk`), `coop motionlog`, `coop echo` (one-PC puppet test: it repeats you 0.4 s later, 4 m
to the side), `coop noworld`, `coop sendworld`, `coop off`, `coop dump`, `coop fps`, `coop restart`, `coop modeltest`,
`coop hit` / `coop kill` (one-PC check: 1 damage / the death path on the nearest animal),
`coop menu` (opens the CO-OP window), plus `stage <name>` and `coophello`.
Every command has at least one word (`FACTS.md` section 3). A new command is also added to the `$commands` list of
`scripts/console.ps1` (Tab completion).

## 7. Puppet and mirrors
- **Puppet** (the other player): a one-animal herd made with `GameNounManager.CreateHerd`; the animal shows up in
  `herd->mHerd` a few frames later; its AI is skipped by the `cCreatureAnimal::NPCTickAI` detour; the received position is
  smoothed and handed to `game/PuppetMotion.cpp`, which lets the game's locomotion walk it there (`MoveTo` to a point a little
  ahead, `StopMovement` + `SetOrientation` when the remote player stands, `Teleport` only when more than 6 m behind). The
  remote speed comes from the received positions. While the remote player walks, the puppet keeps his facing and gets its
  velocity set directly instead of `MoveTo` (steps back and sideways; `coop facing` limits it to steps >60 degrees off the
  facing or switches it off); `MoveTo` walks a creature up to a standing target (stops within 1 m, starts again beyond 1.5 m).
  After every `MoveTo` its desired speed is set to the remote speed when that is above its standard speed (up to x2). A
  puppet that should walk but stands for 1.5 s while more than 1.5 m behind is teleported. A creature that stands more than
  1.5 m off a standing target for 600 ms is teleported there ("settle snap": blocked by a body or a hit reaction). Removed with `GameNounManager.DestroyInstance(herd)`.
- **The other player's creature**: each side sends its avatar's species key and a hash of its body (`kMsgAvatarSpecies`,
  on change and every 10 s, the hash read again each time). The key is the creature key inside the avatar's species profile
  (`mCreatureKey`, which an edit may change) when a body can be read under it, else `avatar->mSpeciesKey`; a creature record the
  editor added to EditorSaves during its last visit wins over both (`game/OwnCreature.cpp`). Our body is read from every database
  holding it, the EditorSaves copy first (`OwnCreature::ReadFresh`, also when the other side asks for our model); the editor's close
  and a change of the profile in memory make it read again at once. The host announces
  the animals sharing that profile with the same key, and re-reads every profile's key instead of caching it forever.
  **The guest's own creature**: outside a co-op launch the avatar's key goes into `Games\CoopOwnCreature.txt` (else, in the main
  menu, the citizen species of the newest creature save); in a co-op launch the guest's avatar gets that profile and species key
  5 s after it appears and its model is reloaded (`SetModelKey` or `GrowUp`); `COOP_NO_OWN_CREATURE=1` turns it off.
  The puppet is built from that key. When the receiver holds
  another body under the same key (the guest joined with a copy of the host's save, then one of them edited his creature),
  it fetches the sender's body and registers it under an alias key (`Models::InstallAlias`, `CoopModels\m_<group>_<alias>`);
  the puppet and, on the guest, the mirrors of the host's species are built from the alias. A failed alias is logged and
  the key itself is used.
- **Host side of the world**: every 100 ms the `cCreatureAnimal`s (not the avatar, not the puppet) go into `HostWorld`
  (`core/coop_core.h`); deltas of 20 entities on channel 2, spawn/destroy events, a full snapshot every 10 s and all species
  definitions with every full snapshot on channel 3. An animal with an unknown species is held back up to 3 s. Every entity
  carries position, orientation, scale, age, health and the flags dead / alpha.
- **Mirrors** (the host's animals on the guest): each in its own empty, disabled herd of one, no nest (`World.cpp`
  `MakeMirror`), built with the host's age (rebuilt when age or species change), with scale, health and alpha applied on
  every update; AI skipped (`World::ShouldSkipAI`); within 150 m walked by `PuppetMotion::Follower`, farther ones
  teleported. A dead one is killed locally once (`KillLocally`: health 0); the game's own tick then runs on it for up to 2 s
  to make the corpse, else the mod sets `mbDead` itself. The corpse is placed once and again only when it drifts more than
  0.3 m; the host's food value (`kMsgCorpse`) is written into it. Animals within 100 m of the planet's centre (the game's
  spawn pool, `FACTS.md` section 8) are neither announced by the host nor removed by the guest. The guest's other own
  animals are removed as strays every 0.5 s. A species the guest cannot build waits up to 20 s for its model, then becomes a
  counted stand-in. The guest reports its counts every 2 s (`kMsgTelemetry`).
- **Nests**: when the host's list arrives the guest moves its own nests (drawn by the game) to the host's places, nearest
  pairs first: teleported with the host's orientation and scale, `mbResetNestEffects` set, the herd kept enabled with target
  size 0. The avatar's own nest stays and stands for the host entry within 30 m of it. Own nests left over are removed; host
  entries left without a nest are made with `CreateHerd(..., createNest = true)`, a few per half second. Only the guest's own
  record of what it moved or made counts.
- **Animations**: the same animation of the same creature is sent at most every 400 ms; a creature within 1 m of its target
  gets no stop/turn orders for 2.5 s after a remote animation was played on it (`PuppetMotion::Hold`).
- **Abilities, effects, voices** (`game/Actions.cpp`): detours of `cCreatureBase::PlayAbility`, `CreateEffectForPool1(Renamed)`,
  `CreateEffectForPool2Renamed`, `StartOrStopEffectFromPool1/2`, `StopEffectFromPools` and `PlayVoice`. Only the outermost call
  is sent; an attack ability is not (bites stay `PlayAnimation` + damage); inside a sent ability its animations, effects and
  voices are not sent (the replayed ability makes them). Effects and voices only of the avatars. The receiver plays the ability
  by type (`GetAbilityIndexByType`), starts the effect under the same pool id and stops it on the sender's stop or after 8 s.
  With `coop echo` the same message bytes are applied to the echo puppet, and our avatar's plain animations are replayed on it.
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
| `tests/run_core_tests.bat`, `tests/focus_test.ps1` | core, co-op launch folder, manifest and presence tests (no game), focus test |
