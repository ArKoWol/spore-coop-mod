# Changelog

Newest first. Every change goes under `## Unreleased` while you work; `publish.ps1` moves it under the release tag.
Format (rules in `CLAUDE.md`, "Documents"): one bullet per change, at most 5 lines: what, why, what to watch; whether it was
tested (`tested on 2 PCs` / `tested on 1 PC` / `not tested`) and whether the protocol changed. Details go into `docs/`.

## Unreleased

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
