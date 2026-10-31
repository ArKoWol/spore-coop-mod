# Co-op join flow: from the main menu into a friend's world

> Status: agreed · Owner: ArKoWol · Updated: 2026-10-31
> Read when: you work on hosting, joining, the main menu, the world transfer or the start of the game. Decisions: `../DECISIONS.md`
> D19-D22. What works today: `../STATUS.md` section 2. How the code works today: `../ARCHITECTURE.md` section 5.

## 0. Goals
1. Hosting and joining happen in the game's main menu, without console commands (the console stays as a second way).
2. **A friend's world never replaces your own saves**, not even for a moment that a crash could make permanent.
3. A first-time player gets through without a manual; every failure says what to do.
4. You see which friends are hosting before you click anything.

## 1. What limits the design (facts, `../FACTS.md` section 6)
- The running game keeps files of `Game0` open from its start, even in the main menu: the world cannot be switched while it runs.
  **Entering a friend's world therefore costs one restart** (until M5).
- A save from another universe does not load on a star of ours (spike J8 failed): the friend's world cannot be one more star in
  your own galaxy. That path (C) is parked; its research tool `game/LoadTrace.cpp` (`COOP_LOAD_TRACE`) stays, off by default. The
  star-making code (`AddWorld`, `SaveIds`, `COOP_MOVE_SAVE`) was removed with the CO-OP star in M2; git has it.
- The game ignores synthetic clicks on the card's Play, and `SetActiveModeByName("LoadGame")` from the mod crashes: the player
  presses Play himself.
- Our DLL runs before the game opens `Game0` (`InstallPendingWorld` works from `DllMain`): folders can be moved there.

## 2. How it looks for the players
**Host**: plays his own `Game0` as always; in the creature stage the "Host co-op" plate (`game/HostPanel.cpp`, exists) starts
hosting. Friends see him as hosting (M3).

**Guest**:
1. Main menu: a **CO-OP button** (our plate, `../features/UI_STYLE.md`) opens the co-op window: friends hosting (M3), recent
   sessions, Join by Steam ID.
2. Join: `Connecting to Artyom...`, `Downloading Artyom's world 42%` (or `Your copy is up to date` with M4), then `Opening
   Artyom's world. The game restarts once.`
3. **Co-op launch**: the game starts with Artyom's world in place of `Game0`; the mod joins Artyom again by itself. The menu
   shows Artyom's galaxy; our window opens by itself and says `Artyom's world: press Play on Leian`, and the host's star is
   selected (spike J9, to check).
4. In the game: a small plate `In Artyom's world · Leave`.
5. **Leave**, or simply closing the game: the next normal launch is always your own world. Your own `Game0` comes back at the
   start of the next launch, before the game opens it, so a crash or a killed process also brings it back.

## 3. M1: the co-op launch (first, fixes goal 2)
Files under `%APPDATA%\Spore\Games\`:

| Path | What |
|---|---|
| `Coop\<host steam id>\` | the host's world as last received (a cache: the host's save is the real one) |
| `Coop\<host steam id>.incoming\` | a download in progress; renamed to the folder above only when complete |
| `Game0.own\` | your own `Game0` while a co-op launch runs (exists only then) |
| `CoopNoOwn.txt` | marker instead of `Game0.own` when the player had no `Game0` at all: restore only moves the host's world away and invents no `Game0` |
| `CoopLaunch.txt` | written before the restart: host id. Read and deleted at the next start (replaces `CoopAutoJoin.txt`) |

What the mod does in `DllMain`, before the game opens anything, in this order:
1. **Restore**: if `Game0.own` exists, the last launch was a co-op launch: move `Game0` back to `Coop\<id>` (id from its
   `coop_host.txt`) and `Game0.own` to `Game0`.
2. **Co-op launch**: if `CoopLaunch.txt` names a host and `Coop\<id>` exists: move `Game0` to `Game0.own` and `Coop\<id>` to
   `Game0`; this run is a co-op launch (auto-join, no hosting, Leave plate).
3. A move that fails (a zombie process holds the folder, `../FACTS.md` section 2) is retried twice, then undone as far as it went,
   logged and shown (console at start, plate in the main menu); the co-op launch is not started and nothing is ever deleted.
   `CoopLaunch.txt` is consumed anyway, so a failure does not repeat at every start. A taken target name gets `.old2`, `.old3`.
The code is `core/coop_launch.h` (tested without the game, `tests/launch_tests.cpp`) and `game/CoopLaunch.cpp`.

Leave (in-game plate, co-op window in the menu, console `coop leave`): closes the game and starts it again without
`CoopLaunch.txt`; step 1 restores. A co-op launch cannot host (`coop host` is refused).
The guest's game may save inside the co-op launch: that lands in the cache only (only the host saves, `../PLAN.md` rule 7).
Replaced code: `Game0.coopready`, `InstallPendingWorld`'s swap into `Game0.coopbackupN`, `CoopAutoJoin.txt`.

**Migration of old installs** (D22): `Game0` holds `coop_host.txt` (a host's world) and a `Game0.coopbackup*` without it
exists. The first time the menu opens, our window asks once: `Your own saves are in a backup (Game0.coopbackup). Restore them?
[Restore] [Not now]`. Restore writes `CoopMigrate.txt` and restarts; at that start the mod moves `Game0` to `Coop\<id>` and the
oldest backup without `coop_host.txt` to `Game0` (this runs before step 1). Other backups stay (named in the log). The answer
is remembered (`CoopMigrateAsked.txt`); nothing is deleted. The question is not asked in a co-op launch.

Done when (two PCs): join, play, Leave -> own save is back; join, kill the game in the co-op launch -> next start is the own save;
an old install with backups is restored by the question.

## 4. M2: CO-OP button instead of the star (D21) - built, not tested on 2 PCs
- A plate in the main menu (GGE mode, `game/CoopMenu.cpp`) with a CO-OP button, bottom left (`COOP_BUTTON_CORNER=tl|tr|bl|br`
  moves it; the log line `menu: CO-OP button plate at ...` has its rectangle). Click: opens or closes the co-op window, which
  sits above (or, in a top corner, below) the plate. A running join keeps going while the window is closed.
- Removed: the CO-OP star and its `CO-OP.spo`, hiding the card's Play, following the card, `HoldPlay`, the "our Play presses the
  card's Play" code, and the star spike commands (`coop slots/addworld/addcenter/delworld/menureload/ggefind/holdplay/playnow/
  carddump`). The guest's galaxy is never changed. This closes the GitHub issue "CO-OP star vanishes after leaving a game without
  saving". `MenuWorlds::SelectedStar` stays.
- In a co-op launch the window opens by itself and says `<host>'s world: press Play on <planet>`. **The planet is found by the
  guest, not written by the host** (differs from the first plan, no protocol change needed): Game0 is the host's world, so the
  planet is the newest creature-stage save (newest `.spo`, named after its played planet) and its star is the saved-game star
  that save belongs to (`MenuWorlds::HostPlanet`; `menu: the host's world: planet ... on star ...`).
- **Spike J9** (to check on 2 PCs): once per co-op launch the mod writes that StarID at `[GlobalGGEUI + 0xFC] + 0x7C`
  (`MenuWorlds::SelectStar`) and reads it back after 0.1 s, 1 s and 3 s (`menu: J9:` lines, verdict `menu: J9 verdict`; `COOP_NO_SELECT=1` turns the write off). Look at
  the screen too: is the card open, is the star marked, does Play use it? If the value is overwritten or nothing shows, the
  window says `Click its star in the galaxy, then press Play on the card.` (the text is the fallback). No synthetic clicks and no
  `SetActiveModeByName("LoadGame")`.

## 5. M3: who is hosting (Steam Rich Presence)
- Every running mod sets the key `sporecoop` = `<protocol>|<state>|<planet>|<players>/<slots>`; state: `menu`, `hosting`,
  `guest`, `editor`, `busy` (minimized, loading). Exports, loaded as optional like the names (`../FACTS.md` section 5):
  `SetRichPresence`, `GetFriendCount`, `GetFriendByIndex`, `GetFriendGamePlayed`, `RequestFriendRichPresence`,
  `GetFriendRichPresence`. **Spike J3**: the key of one PC shows up on the other within seconds (both run AppID 480).
- The window lists: invitations, friends hosting, friends running the mod, recent sessions (offline). Join is disabled with the
  reason when the protocol differs (`ask Artyom to run update.bat`), the world is full, or the host is not in the creature stage.
- Invite: our own message over Steam networking (Steam's "Invite to game" opens Spacewar); the friend gets a toast in the menu.
  Protocol bump.

## 6. M4: rejoin without downloading again
Built (protocol 14, not yet seen on 2 PCs); how it works: `../ARCHITECTURE.md` section 5, code `core/coop_manifest.h` and
`game/WorldTransfer.cpp`. The host first sends a manifest (file names, sizes, CRC32 of its `Game0`); the guest compares it with
`Coop\<id>` and asks only for what differs. Also catches a truncated download (E10).
- Nothing differs: `JoinState.upToDate`, straight to `CoopLaunch.txt` and the restart (the window's text for it, `Your copy is up
  to date`, is wired by the menu code, not by this step).
- A check that fails (size or CRC32) deletes `.incoming` and retries the whole download once from a fresh manifest, then
  `JoinState.error`. The cache is never touched by a failed download.
- E9 is `NetStop` (cancel) and `GuestUpdate` (host silent 10 s, or nothing on the file channel for 30 s).
- Open: the window shows `JoinState.error` only after its menu code reads it; until then a failed download ends as `answered
  but sends no world`.

## 7. M5 (later): join without the restart, from the launcher
`launch_spore_mod.bat join last | <name>`: a small helper (or the launcher) fetches the world into `Coop\<id>` before the game
starts, then starts it with `CoopLaunch.txt`. **Spike J4**: the helper and then the game can use AppID 480 one after the other.

## 8. Edge cases
| # | Case | What happens |
|---|---|---|
| E1 | Steam off / not logged in | window: `Start Steam and log in, then press Refresh` |
| E2 | Same Steam account on both PCs | refused with that reason |
| E3 | Protocol differs | before connecting with M3; after connecting today; says who runs `update.bat` |
| E5 | Host in the editor, menu or loading | row says so, Join disabled (M3) |
| E7 | World already has a guest | `Artyom's world is full` |
| E9 | Host leaves during the download | `.incoming` is deleted, message, nothing else changed |
| E12 | Restart fails ("Another instance", zombie process) | message `Spore did not close, reboot the PC`; `CoopLaunch.txt` kept |
| E13 | Host gone after the restart | plate `Artyom is offline` with Leave |
| E14 | Game closed or crashed in the co-op launch | next start restores the own `Game0` (M1 step 1) |
| E15 | Host plays another planet since | M4 manifest differs, the changed files are sent |
| E17 | Which creature the guest plays | `../PLAN.md` section 3 (own species), not part of the join |
| E21 | Short network drop | 10 s grace (`kReconnectMs`), then E13 |
| E22 | UDP fallback | developer tool only, no world transfer |

## 9. Order and spikes
M1 -> M2 (with J9) -> M3 (with J3) -> M4 -> M5 (J4). Protocol bumps: M3 (invite), M4 (manifest). M1 and M2 need none.
