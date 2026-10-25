# Hard-won facts

> Status: reference · Owner: both · Updated: 2026-10-25
> Read when: before you change any code that talks to the game, ModAPI, Steam or the save files. Read it all once; it is
> grouped by topic so you can come back to one section.

What we learned about the game, ModAPI, Steam and the tools, so nobody has to find it again. One fact lives in one place:
other documents link here instead of repeating it.

**Source tags** after a fact: *(verified YYYY-MM-DD)* = seen in our own test; *(SDK)* = read in the ModAPI headers;
*(SporeCoop)* = from the RE notes of WoodyWizard/SporeCoop (MIT, https://github.com/WoodyWizard/SporeCoop), found on the same
exe build as ours but not yet checked by us; *(guess)* = believed, never checked. Facts without a tag predate the tags and were
seen in our tests.

## 1. Game build and install
- **Use the GOG build** (`SporebinEP1\SporeApp.exe`, 3.1.0.29, unencrypted). The Steam build updated in October 2024 has an
  encrypted exe that breaks ModAPI (workaround: downgrade the exe to the March 2017 depot). ModAPI reports the GOG exe as
  platform `March2017`.
- **Our exe is SporeCoop's `march2017_dc04aee5` build** (SHA-256 starts with `dc04aee5`, both `SPORE/` and `test/SPORE_A`)
  *(verified 2026-10-25)*. So the addresses in their `data/symbols/overrides_dc04aee5.sym` (265 entries, each marked
  "verified port") are addresses in our exe.
- **The GOG install can leave the registry incomplete** (`HKLM\SOFTWARE\WOW6432Node\Electronic Arts\spore*`): alerts
  "Configuration Script Failed [2000]" and "data directory is missing or corrupt [1004]", even without mods. Fix:
  `package/fix_spore_registry.ps1 -GameDir <folder>` as administrator (shipped in the package).

## 2. Running the game
- **A minimized Spore runs no frames**: no update functions, no co-op networking, no remote console. Fullscreen Spore
  minimizes as soon as it loses focus; `SporeApp.exe -w` runs windowed and keeps running (throttled) without focus.
- Windowed Spore runs about 50 fps in the main menu with or without the focus (`coop fps`).
- A second game copy on one PC (`COOP_MULTI=1` gives it a private mutex past the "already running" check, plus a separate
  `APPDATA`) crashes at `SporeApp.exe+0x29de23` (null read) shortly after start; cause not found.
- **A crashed or killed Spore can leave an unkillable process** after the AMD driver resets (`LiveKernelEvent 117`); a new game
  then hangs at startup or says "Another instance of the game is already running". Only a reboot clears it.
- `Simulator::IsCreatureGame()` is false in the main menu, which is where the guest waits before the world arrives.
- Game saves live in `%APPDATA%\Spore\Games\Game0` (`.spo`, `stars.db`, `PlanetScripts.pld`, ...). Console: Ctrl+Shift+C.

## 3. ModAPI, the loader and the console
- **The loader unloads the mod with `FreeLibrary` right after `Dispose`** ("CoopMod detached (FreeLibrary)" in the log) while
  the game still runs: everything pointing into the DLL must be removed in `Dispose` (Steam, console thread, update listeners,
  cheats, detours). Do **not** pin the DLL: static destructors then run at process exit, and since **ModAPI routes our
  `new`/`delete` to the game's allocator**, a static `std::string`/`std::vector` holding memory crashes in the game's free
  (`SporeApp.exe+0x527270`) because the game's heap is already gone. `Dispose` also frees our static containers.
- A `member_detour` needs `GetAddress(Class, Method)`; example: `NPCTickAI_detour` in `AttachDetours()`.
- **ArgScript**: `line.GetArguments(n)` accepts exactly `n` arguments: fewer gives "Expecting at least n arguments", more
  "Expecting at most n arguments" (the game prints it, our code is not reached). So every command has a fixed argument count:
  `coop <word>`, `coopjoin <steamid>`, `coopudpjoin <ip>`. `GetArgumentsRange` was suspected of crashing and is avoided. A
  cheat with zero arguments is rejected by the game itself.
- The game prints its own console errors outside `ArgScript::PrintF`; `CheatManager.ProcessLine` returns false for them.
- **Never iterate `SpeciesManager.mSpeciesProfiles`**: ModAPI's hash map layout does not match the game (crash "reading
  3ea99f65"). The key of a `cSpeciesProfile` is `profile->mCreatureKey` (+0x504).

## 4. Crashes and debugging
- A crash in `SporeModAPI.dll` / `SporeApp.exe` leaves Windows Event 1000 in the Application log with the fault offset; a
  clean exit writes "Shutting Down" in `spore_log.txt`.
- **The "exit crash with Steam" is not a crash.** `SteamAPI_Init` itself makes Windows log Event 1000 (`0xC0000005`, offset
  `0xFFFFFFFF`, module unknown): Steam's `CSERHelper.dll` writes to `0xFFFFFFFF` on purpose, handles it and reports it, about
  a second after `coop host` / `coopjoin` while the game keeps running. Ignore it. A real exit crash comes after the close and
  shows `crash:` lines in `CoopMod.log` *(verified 2026-10-24)*.
- **Crash log**: a vectored exception handler (`CrashHandler`) writes access violations to `CoopMod.log` as `crash:` lines with
  the thread, the address and the first 3 modules on the stack (the first is usually the harmless Steam one).
- **Find the function of a crash in our DLL**: build with `set LINK=/MAP:<file>`; `CoopMod.dll+0x...` from the log plus the
  preferred base `0x10000000` is the address to look up in the map.

## 5. Steam networking
- Set env `SteamAppId` / `SteamGameId` = 480 before `SteamAPI_InitFlat`.
- `SteamNetworkingMessage_t` starts with `void* m_pData; int m_cbSize;` (only these are read).
- Unknown senders must be accepted through `SetGlobalCallback_MessagesSessionRequest` (the callback gets the identity as its
  first field).
- Reliable sends return `1` (`k_EResultOK`) or fail when the send buffer is full: retry next frame. That is why the world is
  sent a few 60 KB chunks per frame.
- Steam names: `SteamFriends_v017`, `GetFriendPersonaName`, `RequestUserInformation`, loaded as optional exports (an older
  `steam_api_coop.dll` without them still connects). A name can be empty or "[unknown]" right after joining: ask again after
  3 s and 10 s.
- Steam's own "Invite to game" opens Spacewar (AppID 480) for the friend, not Spore: unusable for invites.

## 6. Saves and the main menu
- **The running game keeps `planetRecords.pkp`, `planetRecords.pkt` and `PlanetScripts.pld` in `Game0` open** (read share
  only, `StarManager` databases) from startup, even in the main menu: `Game0` cannot be renamed or overwritten while the game
  runs (MoveFile fails, then error 183). The galaxy is already in memory too. Hence the install-on-restart of the world.
- **The main menu (GGE mode `0x2CCD1D2`, the galaxy) lists saved games live from `StarManager.mSavedGameStarRecords`** (stars
  with `kStarFlagSaveGame`; the slot's `.spo` is named after the star's played planet). A star pushed into that list while
  the menu is open appears at once with its card (name, stage, date, creature): no restart, no UI rebuild
  *(verified 2026-10-25, `coop addcenter`)*. A star far from the centre is off screen.
- **Do not call `GlobalGGEUI::InitializeUI` / `Initialize` a second time**: it changes nothing visible and the game crashes at
  exit (eip 0 / 0x28 / 0x2a0 after `CoopMod disposed`).
- Closing the game from the menu does not save `stars.db`.
- The **selected star** of the menu is the StarID at `[GlobalGGEUI::Get() + 0xFC] + 0x7C` (keeps the last selection when you
  click empty space; `+0x138` is something else) *(verified 2026-10-25, one PC)*.
- **Play** calls `cGameModeManager::SetActiveModeByName("LoadGame")` (then mode `0x1654C08`, then `Game_Creature`) and sends
  `kMessageLoadGame`; it never calls `cGamePersistenceManager::LoadGame`. Modes change through `SetActiveModeAt` /
  `SetActiveModeByName`, not `SetActiveMode`. A save star whose `.spo` was copied from another save loads normally by Play
  *(verified 2026-10-25)*.
- A save written while an editor is opening cannot be loaded *(SporeCoop)*.
- **Leaving a game without saving reloads the galaxy from disk** and **rebuilds the saved-game list by scanning the `.spo`
  files** inside the switch to the menu (`SetActiveModeAt(0)`): each save names its played planet in entry `0x0355C93A`
  (uncompressed, field `0x0355FA98`, the first value, at +72, big-endian PlanetID); the save goes on that planet's star and the
  star markers are built right then. A star added to the list later (even before the menu's `OnEnter`) is not drawn. A copied
  save therefore lands on the template's star, and an in-memory star vanishes *(verified 2026-10-25)*.
- A copied save whose planet ids were all rewritten to an in-memory planet (fields of the template's planets copied) is put on
  our star but **its load crashes** (`SporeApp.exe+0x78dc10`, reading 0x2c) *(verified 2026-10-25)*; a save of another universe
  copied as it is crashes too (`+0x7babe2`). Probably the load reads planet records from `planetRecords.pkp` *(guess)*.
- **`stars.db`** is a DBPF; entry type `0x03E4353C` holds all 42010 star records in one RefPack blob: each record is big-endian
  `[size][8-byte id][0x01A80D26][payload size][field count][(field id, size) table][values]`, name as UTF-16LE. The game reads
  the file uncompressed too; a record that changes size crashes the start. The save flag in it is not used for the list
  *(verified 2026-10-25)*. **`planetRecords.pkp`**: one entry per planet (instance = PlanetID), each a nested DBPF.
- The star card: its caption window holds the star name; its Play button is control id `0x0375408c` (an `IButton`), the bin
  and close buttons are next to it. **The game ignores synthetic clicks** (mouse down/up sent with `SendMsg`, or a button-click
  message) on it, and `SetActiveModeByName("LoadGame")` called by the mod crashes (`+0x293df0`): only the player's click works
  *(verified 2026-10-25)*. The card's 3D planet is drawn over all UI.
- Hooking `cMessageManager::MessageSend` (Detours) or the manager's vtable slots 0x14/0x18 crashes at the first message: the
  ModAPI layout of `IMessageManager` does not match this build *(verified 2026-10-25)*.

## 7. Creatures, herds, puppets and mirrors
- Creature stage is on a **sphere**: positions and orientations are full 3D values; copy them as they are, never assume a flat
  plane or `+Y` forward.
- **Move creatures with `Teleport(pos, rot)`, not `SetPosition`**: the creature's physics puts it back after `SetPosition` (the
  SDK says so on `cSpatialObject::Teleport`); `SetOrientation` alone does stick.
- **Every creature needs a herd.** `cCreatureAnimal::Create` without a herd crashes right after spawning (SporeCoop: an access
  violation at 0xD41A20 every frame). Use `GameNounManager.CreateHerd`.
- **Mirror animals**: make an empty herd (`CreateHerd(..., 0, ...)`, `mTargetHerdSize = 0`, `SetEnabled(false)`) and put the
  animal in with `cCreatureAnimal::Create(pos, profile, 1, herd)`. A herd of size 1 fills itself only when the game wants
  (23 of 99 animals in the first test). `CreateHerd` with an own species fills itself up to 5 unless the target size is 0
  *(SporeCoop)*.
- **A puppet walks with the game's own locomotion even with its AI skipped**: `cLocomotiveObject::MoveTo(goal, 0.3, 0.6)` every
  100 ms on the herd animal (whose `NPCTickAI` we skip) makes it walk with leg animation; `StopMovement` stops it. Puppet speed
  up to **4.0 m/s** (seems to be the walk speed; a sprinting player at ~4.8 m/s gets 2-4 m ahead), no teleports while
  following *(verified 2026-10-24, `coop echo`)*. `SetDesiredSpeed` is the knob to try for running. `Teleport` every frame
  gives no leg animation.
- **`cCreatureBase::WalkTo`** crashed in an early test (no herd then); it is `coop walk` now (untested with a herd).
- **Every creature needs a tuning object**: the game reads `*(creature + 0xE84) + 0x32C` (`cCreatureBase::field_E84`) without a
  null check (guest crashes "reading 0000032c" at `SporeApp.exe+0x948634` / `+0x76ca11`). It is filled only for creatures made
  as members of a nest herd; mirrors and the puppet get a blank object whose `+0x32C` is -1 (`World::EnsureTuning`), never
  freed because the game may still point at it after the DLL is gone.
- A newborn's species is set a moment after it appears: an animal seen with species 0 must wait (up to 3 s) before it is used.
- The game puts a nest made with `CreateHerd(..., createNest = true)` on the ground away from the asked position, or lists it
  later: never look a nest up again by position, keep your own record of what you made (else: 295 nests in minutes).
- `SetSpecies` on a creature during world load crashes: wait about 3 s after the load *(SporeCoop)*.
- A dead mirror flies away unless it is pinned to the corpse position every frame *(SporeCoop)*.
- `GameNounManager.SetAvatar(animal)` exists *(SDK)*; SporeCoop gives each player his own creature with `SetSpecies` on the
  avatar 3 s after the load *(SporeCoop)*.

## 8. What a creature-stage planet contains
- First real dump (host, owner's save, 2026-10-24): 4188 nouns: 101 CreatureAnimal, 123 Herd, **101 Nest** (one per herd, all
  over the planet), 74 Fruit, 1443 FruitGroup, 5 GamePlant, 1 Egg, 29 Rock, 1712 Obstacle, 534 GameplayMarker, 21
  InteractiveOrnament, 17 HitSphere, 1 Player.
- **The NPC species of a planet are stock data** (`Spore_Content.package`, type crt `0x2B978C46`, group `0x40626200`), not in
  the save; every GOG install has them. The avatar's own species lives in `EditorSaves.package`.
- **A save holds creature keys only, never bodies.** Bodies (`bem` `0x1A99B06B`) live in `EditorSaves.package` and the
  Sporepedia cache `Pollination.package` *(SporeCoop)*. A guest without them cannot build the species: transfer the `bem`
  records, write a DBPF package, register it (`new Resource::DatabasePackedFile(path)`, `ResourceManager.RegisterDatabase`,
  priority 1000+). `coop modeltest` does this: a 2890-byte `bem` gave a 3022-byte DBPF, the profile was found
  *(verified 2026-10-24)*. `CoopModels\m_*.package` (`Models.cpp`) is our safety net.
- **`cCreatureBase::mSpeciesKey` is zero for most NPC animals**; the species is `mpSpeciesProfile` (+0xB20, or the herd's
  `mpSpeciesProfile`), whose key is `profile->mCreatureKey`.
- **What differs between two machines that loaded the same save**: animals, herds, nests, eggs, obstacles, ornaments are made
  per machine when the stage starts (host 101 nests, friend 96 and 108, none at the same places). **Fruit matches only 6 of 74
  by position.** Plants, rocks and markers are identical. A `GamePlant` leaves and re-enters the game's noun list, so a scan
  that reports "gone" for it is wrong (that was the plant flicker) *(verified 2026-10-25)*.
- Creature-mode actions go through `cCreatureModeStrategy::ExecuteAction(actionId, data)` *(SDK)*: EatFruit `D335362B`,
  EatMeat `D335362D`, Pickup `D3353636`, Interact `D3353638` *(verified 2026-10-25, `action:` log lines)*.

## 9. Combat, DNA and species relations
- `TakeDamage`'s 4th argument is a push vector; pass `(0,0,0)` *(SporeCoop)*. The animal's `TakeDamage` is entered with `this` =
  the `cCombatant` part of the creature (creature + 0x5A8).
- **DNA for a kill is paid inside `TakeDamage` only when the attacker is the player's avatar** (or an ally) *(SporeCoop)*: a hit
  applied with a puppet as the attacker pays nobody.
- **The species relation table is one table "player versus species"**: `cSpeciesRelationshipManager` (`Get` 0xB3D5C0 in our
  exe), hash map at +0x20 of `tRelationshipData {relation, friendlyCount, numBefriended, numKilled, ...}` *(SDK, SporeCoop)*.
  Relation values in the creature stage: 6 allied (and the own species), 5 extinct, 3-4 impressed, 1-2 hostile; 0 = not set
  (the archetype tuning decides) *(SporeCoop)*.
- `GetRelationship(mgr, key*, a, b)` 0xBA40E0 and `ApplyEvent(mgr, event, key*, creature)` 0xBA4A00 in our exe; event 3 =
  impressed, 0 / 5 = killed, 2 = attacked, 6 / 7 nest events. Typical tuning: 3 kills make a species extinct, 3 impressions
  make it an ally *(SporeCoop)*.
- `TakeDamage` calls `ApplyEvent` for every hit and death, also without a player attacker: **replayed deaths of mirrors count as
  kills** and can make a species extinct on the guest. Drop relation events on mirrors and puppets *(SporeCoop)*.
- Extinction: every herd of the species is stopped; a species goal card pays its DNA only when it sees the change itself
  *(SporeCoop)*.

## 10. Other stages (research, nothing verified by us)
- **"The player's" is one political id per game**: every object has `cGameData::mPoliticalID` (+0x30) *(SDK)*;
  `IsPlayerOwned()` = "my political id == `GetPlayerPoliticalID()`" *(SporeCoop)*.
- Relations between factions are stored **per pair of political ids** in `cRelationshipManager`
  (`ApplyRelationship(polA, polB, event, scale)` 0xD06280) *(SDK, SporeCoop)*.
- Tribe: `GameNounManager.mpPlayerTribe` (+0x70; `GetPlayerTribe` 0xBFC730 is just `mov eax,[ecx+0x70]`). NPC tribes are
  the same class `cTribe` with their own species and a planner AI (`mpTribePlanner` +0x290); `SpawnNpcTribe(pos, archetype,
  members, food, ?, species)` exists *(SDK, SporeCoop)*. New NPC tribes appear during play at the same positions on both
  machines *(SporeCoop)*.
- Civilization: `cCivilization::mIsPlayerOwned` (+0x89), `cCity` +0x33C `mbIsPlayerCity`, money per civ (`mWealth`); the
  planet counts as united only when every city is the player's *(SDK, SporeCoop)*.
- Space: `SpacePlayerData` +0x18 player political id, +0x1C cached `cEmpire*`; `cStarManager` +0x1D8 the player's political
  id; `cEmpire` +0x50 flags (0x40 / 0x04 = player). Star and planet ids and the political ids of NPC empires are the same in
  two games loaded from one save *(SDK, SporeCoop)*.
- **Same random numbers do not come for free**: `Math::GetRNG()` is one global generator seeded from the clock *(SDK)*.
  Seeding it before a generation step gave "mostly the same" result; a private generator for the draws of one generation call
  gave equal planets in 8 of 9 runs *(SporeCoop)*.

## 11. Resources, intro and UI
- **Intro splash**: the whole intro (EA logo, Maxis, Spore logo) is **one movie**, `0x7af7df4f` (type `0x376840d7`, EA VP6:
  `MVhd` + `MV0K`/`MV0F` chunks, video only, 1280x720 @ 29.97, in `Spore_EP1_Data.package`), started through
  `MovieSystem::PlayMovie` about 5.7 s after launch; its first 4 s (the EA logo) cannot be skipped. Our splash pauses it at 4 s
  (`PauseMovie`), shows a UTFWin window with `coopmod!splash.png` (a UTFWin window **is drawn over a paused movie**) and
  continues after 2.5 s.
- **Detour movie methods on `Movie::IMovieSystem`, not `MovieSystem`**: `MovieSystem` derives from `ILayer` first, the game
  passes `this` as the `IMovieSystem` part, and virtual calls on a `MovieSystem*` crashed.
- **Own resources**: a DBPF package with uncompressed entries works when dropped into `Data\` (`make_assets.ps1` writes one:
  header version 3, index flags 4, entries `type, group, instance, offset, size|0x80000000, size, 0, 1`). Names are `id("...")`
  = FNV-1 of the lower-case name. A PNG of any size works for UTFWin images.
- UTFWin has no text window we can create from code; how the mod draws its windows is in `features/UI_STYLE.md`.
