# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format (rules in `CLAUDE.md`, "Documents"): one bullet per change, at most 5 lines: what, why, what to watch; whether it was
tested (`tested on 2 PCs` / `tested on 1 PC` / `not tested`) and whether the protocol changed. Details go into `docs/`.

## Unreleased

- **The guest's lethal bite is native again (DNA for his kill) and the guest can eat corpses, protocol 17** (exxecute; `game/Kills.cpp`,
  STATUS 4a, 2f). Crash cause found: the extinction reads the herd type of the mirror's herd, unknown to the game (`FACTS.md` 9); it is now
  copied from a game herd first, relation events of mirrors are dropped unless our own bite. The mirror corpse was removed at the first
  bite (`mbHasBeenEaten` is set when eating starts): now food left goes both ways (`kMsgCorpseFood` 119), `kMsgEaten` when used up.
  Not tested; **protocol 17, both must update**; run `coop killcheck` once; watch `kills:`, `relation:`, `damage: ... LETHAL`, `eat:`.

- **Creature-stage priorities (MoSCoW) and a new split of the work, agreed (D23)** (exxecute; docs only, `PLAN.md` 7.1,
  `STATUS.md` 3). Seven Musts for playable co-op; exxecute takes the guest's own creature, corpses, the guest's DNA for a kill
  and respawn; join flow M5 waits. Proposed by exxecute, agreed by ArKoWol on 2026-10-31. Not tested (no code); no protocol change.

- **Automatic one-PC test of creature actions: `coop selftest` and `tests\actions_test.ps1`** (exxecute; `game/SelfTest.cpp`). With
  the echo puppet (now fed through the network message bytes, plain animations too) it plays abilities, the vomit animation and
  effect on our avatar and checks capture and replay, then presses keys 1-9. Run on 1 PC: all PASS except superpowers (excluded
  since); keys reach nothing. Not yet run: the 1.5 s "still playing" check and the block-index log. STATUS 3e parked (FACTS 8).

- **Fix: replayed animations were cut on the other player's creature; research for songs and dances** (exxecute). A standing
  puppet may stand up to 1.5 m off since the walking change, but an animation was only protected within 1 m (now 1.6 m). In the
  first test no ability went through `PlayAbility`: unknown creature-mode actions are logged with their data (`action: mode action`)
  and replays are logged (`action: replay`). Tested: not yet; no protocol change. Next test: both sing, dance, vomit; send both logs.
- **M3 of the join flow: friends who host, and invitations, protocol 16** (ArKoWol; `game/Presence.cpp`, `game/InvitePanel.cpp`,
  `core/coop_presence.h`). Steam now starts when the main menu opens and publishes Rich Presence `sporecoop`; the CO-OP window lists
  invitations, friends hosting (Join, grey with the reason), friends with the mod, recent sessions; the host plate has Invite (new
  message `kMsgInvite` 118). Not tested on 2 PCs (55 core checks pass); **protocol 16, both must update**; spike J3: `presence:` / `invite:` lines.

- **Protocol 15**: M4 (manifest) and exxecute's abilities/effects/voices both changed the packets as protocol 14 at the same time;
  the two kinds of 14 build do not fit together, so the merged build is 15 and refuses both. Both must update. Built, not tested.
- **M2 of the join flow: a CO-OP button in the main menu replaces the CO-OP star** (ArKoWol; `game/CoopMenu.cpp`, `game/MenuWorlds.cpp`).
  The star, `CO-OP.spo`, the hidden card Play, `HoldPlay`, `SaveIds` and the star dev commands (`coop slots/addworld/addcenter/delworld/
  menureload/ggefind/holdplay/playnow/carddump`) are gone, which closes the issue "CO-OP star vanishes after leaving a game without saving".
  In a co-op launch the window opens by itself (`<host>'s world: press Play on <planet>`) and tries to select the host's star (spike J9,
  `menu: J9` lines). Not tested (2 PCs); no protocol change. Guests: click the CO-OP plate (bottom left; `COOP_BUTTON_CORNER` moves it).
- **M4 of the join flow: a rejoin downloads only what differs, protocol 14** (ArKoWol; `core/coop_manifest.h`, `game/WorldTransfer.cpp`).
  The host sends a manifest (name, size, CRC32); the guest asks only for changed or missing files, builds `Coop\<id>.incoming` from
  cache + received files, checks every file, then replaces the cache; a failed check retries once, then `CoopNet::State().error`
  (also `.upToDate`); a cancelled or cut-off download deletes `.incoming`. Logic: 85 checks in `tests\run_core_tests.bat`; not tested
  on 2 PCs; **protocol 14, both must update**; watch the `world:` lines in `CoopMod.log`.
- **Click on the friend's creature to follow it** (exxecute; `game/FollowFriend.cpp`). Our avatar walks after it with `MoveTo`
  (stays 3 m behind); W/A/S/D, arrows, a click elsewhere or on the friend again stop it; the own name plate shows "(following)".
  The hovered object comes from `GameViewManager.GetHoveredObject()`. Not tested (works with `coop echo` too). No protocol change.

- **Abilities, effects and voices of creatures reach the other side, protocol 14** (exxecute; `game/Actions.cpp`, STATUS 3e).
  `PlayAnimation` alone showed only the first frames of a song, dance or mating call and no vomit. Now `PlayAbility` (non-attack),
  the creature effect pools and `PlayVoice` are captured and replayed on the copy; effects and voices only of the avatars.
  `coop actionlog` logs it; `coop echo` replays it on one PC. Not tested; **both must update** (protocol 14).

- **Fixes from the 2026-10-31 test: clones, corpses, nests, editor** (ArKoWol; facts in `FACTS.md` sections 7-8). The guest no longer
  removes the game's pool at the planet's centre (destroyed pool animals came back as hundreds of "picture" clones); a killed mirror
  gets the game's tick for 2 s, else `mbDead` is set (it never became a corpse); the guest moves its own drawn nests to the host's
  places; the avatar's key comes from its species profile (`world: avatar profile ...` logs what the editor changes). Not tested;
  no protocol change; both need the new build.
- **M1 of the join flow: the co-op launch, a friend's world never replaces your saves** (ArKoWol; `core/coop_launch.h`, `game/CoopLaunch.cpp`).
  The world goes to `Games\Coop\<id>`; the next start swaps it in as `Game0` for one run (own save: `Game0.own`), every start puts it
  back. Leave: plate in the creature stage, CO-OP window button, `coop leave`. Old installs get a one-time restore question.
  Replaces `Game0.coopready`, `Game0.coopbackupN`, `CoopAutoJoin.txt`. Folder logic tested by `tests\run_core_tests.bat` (63 checks);
  the game side is `not tested` on 2 PCs; `no protocol change`. Players run `update.bat`; watch the `launch:` lines in `CoopMod.log`.
- **The puppet steps back and sideways and keeps up with the player** (exxecute; `game/PuppetMotion.cpp`, STATUS 3b). A walking
  puppet keeps the friend's facing and gets its velocity set directly (`coop facing`: `always` / `steps` / `off`); turning it every
  frame or turn rate 0 froze it (`FACTS.md` section 7). Stuck 1.5 s: teleported. Standing creatures (also mirrors) stop within 1 m
  and walk up again beyond 1.5 m (a `MoveTo` every 100 ms at 0.7 m made them twitch). Tested on 2 PCs (2026-10-31): walking, running,
  sideways, backwards and standing look right. No protocol change (merged into protocol 13).

- **Join flow re-planned, now ArKoWol's area** (docs only; owner's decisions D19-D22). **exxecute:** `features/JOIN_FLOW.md` was
  rewritten from your draft: a friend's world is played in a "co-op launch" (swapped in for one launch from `Games\Coop\<id>`, own
  `Game0` restored at every start), a CO-OP button replaces the star, Rich Presence and manifests come later (M1-M5). Path C is
  parked (J8 failed). Nothing in the code changed yet.

- **Four fixes from the 2026-10-25 test, protocol 13** (ArKoWol). (1) Creature edits: `kMsgAvatarSpecies` carries a hash of the body; the
  same key with another body is fetched and built under an alias key (puppet, host-species mirrors). (2) Corpses: the host sends each
  corpse's food value (`kMsgCorpse` 116), the corpse is placed once, not every frame. (3) Fights: "settle snap" in `PuppetMotion`.
  (4) Nests: the host sends orientation, scale and model key, the guest sets them. Built, not tested; **both must update**. Look for
  log lines `world: ... alias`, `corpse:`, `eat:`, `motion: settle snap`, `nests: made one` (details: `ARCHITECTURE.md` section 7).

- **Cause of the "bodiless dummies" found in the dumps; nests try to show** (ArKoWol). 65 of the 101 animals sit at the planet's centre (the game's pool for
  mating calls and nests); the host mirrored them, so the guest saw 65 stacked copies that flew out of the ground. The host no longer announces
  animals within 100 m of the centre. The herds of nests we make stay enabled (nests were present in the guest's dump but invisible).
  Details in `FACTS.md` section 8. Tested: not yet; no protocol change; both need the new build.

- **Research logs: bodiless mirrors and invisible nests** (ArKoWol). Owner: after the mating call "bodiless dummies" appear around the guest's nest
  (half of the animals without a body, the sound comes from under the nest) and the host's nests are still not seen on the guest. The guest now logs how
  many mirrors have no body model (`mpAnimatedCreature` null) and, after each batch of nests, how many `Nest` objects the game holds and how
  many of them have a herd. Tested: not yet; no protocol change. Next test: pass the guest's `CoopMod.log` and, while the dummies are there, `coop dump`.

- **Players' Steam names above their creatures** (exxecute; `game/NamePlates.cpp`): while a session runs, a small plate over
  the other player's puppet and a smaller one over the own avatar, moved every frame to the creature's screen position (from
  the camera's rays); hidden off screen, behind the camera, beyond 120 m and outside the creature stage. `coop echo` shows it on
  one PC. Tested on 1 PC (with `coop echo`). No protocol change.

- **Fix: an enemy mirror stood still while the real enemy stepped aside in a fight** (ArKoWol). Cause is my own change: every replayed animation
  (a fight is full of attack and hit animations) froze the mirror for 2.5 s (`PuppetMotion::Hold`), while the real enemy moved up to 6 m. The
  freeze now only holds while the creature is within 1 m of its target (`kHoldDistance`), so a mirror follows as soon as the enemy moves. Tested: not
  yet; no protocol change.

- **Research logs for species change, corpses and mirror offset** (ArKoWol). The faster snapping of mirrors near the player was a
  wrong guess and is removed (the animal did not run away: it stood in front of the guest but at the host's side). New log lines:
  `mirror: N is X m from the host's position ...` (how far a mirror next to the player stands from the real animal), `world: our avatar is species ... profile sequence` (does the host's editor change the key or only the profile?
  the host's edit changed the guest's puppet look on the host only) and `kill: mirror ... dead ... eaten ...` (why the guest cannot eat a
  corpse). Tested: not yet; no protocol change.

- **"Host co-op" button in the creature stage** (exxecute; `game/HostPanel.cpp`): a small plate top right; hosting shows
  "waiting for a friend" / "<name> is here", your Steam ID, Copy my Steam ID and Stop; hidden in the menu, editors, pause and
  while you are a guest. The drawing helpers of the CO-OP window moved to `game/UiDraw.cpp` (shared by both windows).
  Tested on 1 PC (a guest joining through it not tried yet). No protocol change.
- **Research logging for the "hundreds of puppets after a mating call" report** (ArKoWol): the guest logs `world: census: N animals (M mirrors,
  K strays), H herds` whenever more animals exist than mirrors (at most every 5 s). The cause is not known yet (the host's log shows
  99 animals and no runaway; the guest's own game or our nest herds are suspected). Needs a two-PC test; tested: not yet; no protocol change.

- **The log is rotated by size** (ArKoWol): `CoopMod.log` was only ever appended to (7000 lines, days of tests). When the game starts and the
  log is over 300 KB it is moved to `CoopMod.old.log` (the older one is deleted). Within a test, including the automatic restart, nothing
  is lost. No protocol change; tested on 1 PC: not yet.

- **Research: loading a friend's save on our star without a restart, paused** (exxecute). New research tools, off by default:
  `COOP_LOAD_TRACE=1` (`game/LoadTrace.cpp`, `load:` lines) and `COOP_MOVE_SAVE=1` (`game/SaveIds.cpp`: save and planet records
  moved to the CO-OP star, records written into the game's planet database). A moved save still stops before reading its planets;
  findings in `FACTS.md` section 6, next steps in `features/JOIN_FLOW.md`. Default behaviour unchanged. Not tested on 2 PCs.

- **The puppet has the OTHER player's creature** (ArKoWol, PLAN section 3). Each side sends the species of its avatar (`kMsgAvatarSpecies`,
  on change and every 10 s); the model transfer now works both ways (the guest's creature can travel to the host); the puppet is
  built from that species and rebuilt when it changes (a player back from the editor). Until the model is here (up to 15 s) the puppet
  waits, then the local species stands in. Not tested on 2 PCs, **protocol 12**: both must update. Test: one player changes the
  creature in the editor, the other must see the new body. The guest's SetSpecies at start is still open.

- **The CO-OP window joins for real** (exxecute): Join / Join by Steam ID start the Steam join (`game/CoopNet.h`, as `coopjoin`),
  show the real world download, the game restarts and installs the host's world; then the CO-OP star is a copy of the host's
  newest creature save, ready, and the card's Play enters it while the mod rejoins. Errors: no answer (20 s), other mod version,
  no world (30 s), Steam off. Test friend and the fake download are gone (`coop menuready` too). Tested on 2 PCs. No protocol change.
- **Docs: results of the 2026-10-25 two-PC test** (ArKoWol). `STATUS.md` test log and `FACTS.md` sections 8: newborns right, fruit and
  plants regenerate at stage start, eating plays but vomit and mating-call effects do not (next try: replay `PlayAbility`). Docs only.

- **Fix: the guest's game crashed on the lethal bite of a mirror** (ArKoWol). Friend's log: three bites on a mirror, `action: 045ab96e`, then
  `SporeApp.exe+0x797779, reading 0x14` in game code only: the game's native death of a mirror (no nest herd, no tuning) is not safe.
  Non-lethal bites stay native; the lethal one is only reported, the host kills the real animal and the mirror dies by the replay. The DNA
  for a kill is open again (`STATUS.md` 4a). Hosts and guests: `git pull`; not tested; no protocol change.

- **CO-OP star and window in the main menu, with stubs** (exxecute; `game/CoopMenu.cpp`, plan `docs/features/JOIN_FLOW.md`): a
  "CO-OP" save star near the galaxy's centre at every start; selecting it opens our window next to its card (remembered hosts,
  Test friend, Join by Steam ID from the clipboard, a fake 3 s download, then the card's own Play loads the world). The card's
  Play is hidden until a world is "ready". Known bug: the star vanishes after leaving a game without saving (GitHub issue; facts:
  `FACTS.md` section 6). Tested on 1 PC. No protocol change. `coop menu`, `menuready`, `carddump`, `ggefind` are dev commands.
- **Combat by PLAN rule 3, "the actor decides"** (ArKoWol). The guest's own bite on a mirror is applied natively on his machine (health,
  death, DNA reward from the game) and reported to the host (`kMsgDamageReq`), which applies it with the guest's puppet as attacker;
  an eaten mirror is reported with `kMsgEaten` and the host removes the real animal. Why: the old way (block, ask the host) gave the
  guest no native death or reward and a corpse that could not be eaten. Not tested; **protocol 11**: both must update; see `ARCHITECTURE.md` section 7.

- **Documents reorganised** (docs only; owner's request). One live plan (`docs/PLAN.md`) instead of four; new `docs/README.md`
  (index), `STATUS.md`, `DECISIONS.md`, `ARCHITECTURE.md`, `FACTS.md`, `TESTING.md`; `CLAUDE.md` now has the reading order and the
  rules for documents (where to write what, header, what to mark, what to delete, size limits). Deleted: `DESIGN.md`,
  `SYNC_REDESIGN.md`, `HANDOFF_B.md` (content moved). **exxecute:** `JOIN_FLOW.md` and `UI_STYLE.md` moved to `docs/features/`.
  This changelog was cut to 5 lines per entry; the details are in `docs/`. Code comments point to `PLAN.md` section 10.
- **Spike: a new world in the open main menu, no restart** (exxecute): `coop slots`, `coop addworld` / `addcenter`, `coop
  delworld`, `coop menureload` (`game/MenuWorlds.cpp`). The menu shows the new star at once; Play loads its copied save (J6
  passed); the selected star is readable. Facts: `docs/FACTS.md` section 6; plan: `docs/features/JOIN_FLOW.md`. Tested on 1 PC.
  No protocol change.
- **Fix: endless nests on the guest and animals that were never announced** (25 of 99 reached the guest). The guest now trusts
  only its own record of the nests it made; an animal waiting for its species keeps its waiting time. Log: `world: host sees N
  animals, announces M, K wait`. Found in the logs of a 2-PC test; fix not tested. No protocol change.
- **Fix: plants flickering on the guest; research detour on creature-mode actions.** Removal broadcast and manifest apply are
  switched off (`Objects.cpp` only logs `eco:` lines); `action:` lines log eat fruit / eat meat / pick up / interact; a remote
  animation is no longer cut by stop orders (`PuppetMotion::Hold`). Facts: `FACTS.md` section 8. Not tested. No protocol change.
- **Fix: host crash when the guest connected** (`KeyOfProfile` iterated `SpeciesManager.mSpeciesProfiles`): the key is now read
  from the profile (`mCreatureKey`). Raw reads of nests are guarded (`ReadNest`). Facts: `FACTS.md` section 3. Not tested.
- **Animation events and nest sync, first versions** (`kMsgAnim`, `kMsgNests`; `ARCHITECTURE.md` section 7). `anim:` lines
  list the animation ids seen. **Protocol 10.** Not tested.
- **Eaten fruit, plants, hatched eggs removed on the other machine, first version** (`Objects.cpp`, `kMsgManifest`,
  `kMsgRemove`; now switched off, see above). **Protocol 8.**
- **Fix: two guest crashes "reading 0000032c" and newborns with the wrong species**: blank tuning object for mirrors and the
  puppet (`World::EnsureTuning`); an animal with an unknown species waits up to 3 s. Facts: `FACTS.md` section 7. Not tested.
- **Damage, deaths, the puppet's real health, walking mirrors**: guest hits become requests to the host (`kMsgDamageReq`),
  host hits on the puppet go to the guest (`kMsgDamage`), avatar health/size both ways (`kMsgAvatar`), dead mirrors pinned,
  mirrors within 150 m walk. `coop hit` / `coop kill` for 1-PC checks. **Protocol 7.** Damage requests seen on 2 PCs.
- **Console window: Tab completion and history** (`scripts/console.ps1`). Tested on 1 PC.
- **Mirrors copy size, age, health and alpha** of the host's animals. **Protocol 6.**
- **Remembered hosts**: `coopjoin last | <number> | <part of name>`, `coop friends` (`RecentHosts.cpp`). Tested on 1 PC.
- **Launcher progress spinners**; **several console commands in one line** separated by `;`.
- **The console window opens by itself at every start** (also after the automatic restart). Tested on 1 PC.
- **Fix: stand-in models after a second join**: the host repeats all species definitions with every full snapshot.
- **Fix: all animals had our species' model and only a quarter appeared**: species key from the species profile; mirrors as an
  empty quiet herd plus `Create` (`FACTS.md` section 7).
- **README rewritten** (what works, install, commands, troubleshooting); `package/README.txt` updated.
- **Creature models travel with the session**: the guest asks for models it cannot build and registers them as packages
  (`Models.cpp`); `coop dump` lists species, `coop modeltest`, `scripts/worlddiff.ps1`. **Protocol 5.**
- **The puppet walks instead of drifting** (game locomotion, `game/PuppetMotion.cpp`); `coop move/walk/teleport`,
  `coop motionlog`, `coop echo`. Tested on 1 PC.
- **Restart helper finds the console script by itself.** Tested on 1 PC.
- **Shared world, first version**: the host's animals replicated to the guest (`World.cpp`), guest AI and own animals off,
  telemetry in `coop status`. **Protocol 4.**
- **`update.bat` in the repo root**; the updater finds the game through the registry too.
- **Repository tidied up**: tools to `scripts/`, player files to `package/`. Pull before you edit a moved file.

## build-20261024-1210

- **Automatic restart and auto-join after a received world** (`CoopAutoJoin.txt`, `COOP_NO_RESTART`, `coop restart`).
  Tested on 1 PC with a simulated world. No protocol change.
- **Intro splash**: our picture for 2.5 s after the EA logo (`assets/splash.png`, `make_assets.ps1`, `CoopMod.package`);
  exxecute's art is in.
- **The updater checks and asks** (pull / install the release / both / skip); `launch.ps1` asks before building when GitHub has
  new commits; the launcher no longer runs `git pull` by itself.
- **Replication core M0** (`core/coop_core.h`) with tests under loss, duplicates and reordering (`tests\run_core_tests.bat`).
- **`coop dump`** (`Entities.cpp`), **`coop fps`**, **the game always starts windowed** (`COOP_FULLSCREEN` to keep fullscreen).
- **Design v2.1** (then `DESIGN.md`, now `docs/DECISIONS.md` D1-D5). **Test aid `COOP_MULTI=1`** (`FACTS.md` section 2).
- **Exit crash with Steam: was a misreading** (`FACTS.md` section 4). **Crash log** (`crash:` lines); `Dispose` frees static
  containers, the DLL is not pinned.
- **One-step test launcher** (`launch_spore_mod.bat`); **cleanup on exit** in `Dispose`.
- **Fix: the puppet only turned on the spot** (`Teleport` instead of `SetPosition`). **Co-op indicator** in the window title.
- **Fix: the host sent the world only once per session**; it resends to a client silent for 10 s or a new Steam ID.
- `console.bat` (bypasses the execution policy); a new console connection replaces a forgotten one.
- **Fix: a second world install deleted the player's own save backup** (`coopbackup2`, `coopbackup3`, ...).
- **Fix: the client could not install the received world** (error 183): stored as `Game0.coopready`, installed at the next
  start (`FACTS.md` section 6).
- **Remote console** on `127.0.0.1:27780` (`console.ps1`); launcher modes `dev` / `release` / `play`.

## build-20261024-0925

- Fixed joining: `coop join <id>` was rejected ("Expecting at most 1 arguments"); now `coopjoin <steamid>` / `coopudpjoin <ip>`.
- Fixed `publish.ps1` moving the Unreleased heading to the wrong place.

## build-20261024-0906

- Added `CLAUDE.md` and this changelog (both ship in the package under `docs\`); `setup.ps1`; relative build paths.

## build-20261024-0851 (first release)

- Steam (Spacewar, AppID 480) transport with a UDP fallback; the host sends `Games\Game0` to a joining client (with a backup).
- Remote player puppet (one-animal herd, AI skipped). Protocol 3: different versions refuse each other.
- `update.bat` installs from the latest release; `publish.ps1` publishes one.

## Before the first release

- The GOG build works with ModAPI (the Steam build of Oct 2024 does not); fixed the incomplete GOG registry entries.
- Toolchain (VS 2022 Build Tools, ModAPI SDK, SporeModLoader); `cCreatureAnimal::Create` without a herd crashes.
