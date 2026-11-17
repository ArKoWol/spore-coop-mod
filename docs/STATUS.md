# Status: what works, what is being done, by whom

> Status: live · Owner: both · Updated: 2026-11-17
> Read when: at the start of every work session (after `git pull`), and before you pick a task. Update it when you start or
> finish a piece of work and after every two-PC test.

## 1. Creature stage: the minimum for playable co-op
The owner's list of 2026-10-25. States: **works** (seen on two PCs), **to check** (built, not seen on two PCs since the last
change), **partly**, **not done**.

| # | Item | State |
|---|---|---|
| 1 | Session: connect, world transfer, automatic restart and rejoin, console | partly (2026-11-17): works, but the host sends the last save on disk; after a new unsaved game the guest joined the previous world (`features/SHARED_CREATURE_WORLD.md` P2) |
| 2a | Same animals on both sides: model, size, age, health, alpha | works; in a fight a mirror stood off the real animal (2026-10-25), settle snap built 2026-10-31, to check |
| 2b | Nests (the host's nests on the guest) | **works on 2 PCs 2026-11-08**: same places (0.0 m), visible, upright. They were never missing: the guest read the host's positions with x and z swapped (`FACTS.md` 2) |
| 2c | Plants not flickering | to check (fixed 2026-10-25) |
| 2d | Fruit at the same places and gone when eaten | not done (fruit is generated per machine, 6 of 74 match) |
| 2e | Eggs and hatching | partly |
| 2f | Corpses both players can eat | the guest cannot eat a corpse (2026-10-25); the host's food value sent, corpse placed once: still cannot eat (2-PC test 2026-10-31). Works on 2 PCs 2026-11-01: the guest eats a host's corpse to the end, the host's eating lowers the guest's corpse. The host's eating animation is not seen on the guest (3e). A newborn from an egg has the right species (works, 2026-10-25) |
| 2g | Species relations: extinction for both, allies | not done (plan: `PLAN.md` section 3); 2026-11-17: a nest befriended or destroyed by one player stays unchanged for the other (`features/SHARED_CREATURE_WORLD.md` P5) |
| 2h | The friend's species and family cannot be attacked | not done; 2026-11-17: a player could hit the friend's nest members, who then attacked him again and again, also in his own nest (`features/SHARED_CREATURE_WORLD.md` P6) |
| 2i | Nests stay as they are when a player respawns | fails (2026-11-17): after the host's respawn the nests of all species came back (`features/SHARED_CREATURE_WORLD.md` P3) |
| 3a | The other player walks | works |
| 3b | Walking backwards, sideways, running | works (2-PC test 2026-10-31) |
| 3c | Each player's own creature model on the puppet | the guest's creature kept in the host's world (D26, `features/SHARED_CREATURE_WORLD.md` P1): works on 1 PC 2026-11-17 (two games), protocol 18, to check on 2 PCs (on 2026-11-17 he brought the creature of his last own game). Models **work on 2 PCs 2026-11-08**: models both ways, each sees the other's own creature, editor changes seen. The guest's creature is loaded into the player's profile as the editor does: own buttons, portrait, sight, family, mate and editor, also after a respawn |
| 3d | Real health and size of the puppet | to check |
| 3e | Action animations (eat, sing, bite) | parked 2026-10-31 (exxecute): songs, dances, vomit not seen on 2 PCs. `coop selftest` passes for what the mod itself plays (ability via `PlayAbility`, vomit animation and effect); the player's own song never goes through `PlayAbility`. Its path was found and captured 2026-11-15 (exxecute, `FACTS.md` section 8): on 1 PC a clicked song reaches the echo puppet in full; to check on 2 PCs. The mating call breaks off on the echo: only its first animation is sent (`FACTS.md` section 8), left as it is (minor) |
| 3f | Click on the friend's creature: our creature follows it | built 2026-11-08: only in the social stance (in the attack stance the click is a bite), to check on 2 PCs |
| 4a | Guest bites applied natively on his machine and reported to the host (PLAN rule 3) | **works on 2 PCs 2026-11-01**: the guest's lethal bite is native (`LETHAL, applied here (native death)`), no crash, herd type repaired; each player gets his own DNA |
| 4b | Host damage and deaths | works |
| 4c | PvP, death and respawn at the own nest, no reward | **works on 2 PCs 2026-11-01**: both killed each other, both respawned at their own nests and saw each other again; one exception fixed the same day: after the host's respawn the guest's puppet of him stayed invisible (the stray sweep removed it before it was picked up) |
| 4d | Hit and get-hit animations of mirrors | not done |
| 4e | The guest's DNA from nests and species goals | fails (2026-11-17): the host earns normal DNA, the guest little; his nest goal cards do not complete (`features/SHARED_CREATURE_WORLD.md` P4); 2026-11-17 on 1 PC the guest's kills pay like the host's (`dna:` lines); social and nest bonus not checked |
| 5a | Separate DNA / brain / parts per player, saved in the host's save | not done; needed for 3c (D26) |
| 5b | Stage change when both are ready | not done |
| 6 | Stability: no crashes on connect, in combat, on restart; leaving and returning guest | main crashes fixed |

Animals that waited for their species were never announced (25 of 99 reached the guest): fixed 2026-10-25, to check.

## 2. Join flow from the main menu (`features/JOIN_FLOW.md`)
| Part | What | State |
|---|---|---|
| today | CO-OP star + window, Steam join, download, restart (seen on 2 PCs 2026-10-25, before M1) | works |
| M1 | co-op launch: host's world in `Coop\<id>` for one launch, own `Game0` restored at every start, migration question | partly seen on 2 PCs 2026-10-31: the co-op launch moved the folders and joined. Leave and restore work on 1 PC 2026-11-01 (own `Game0` back, cache back in `Coop\<id>`); the migration answer "Restore" still to see |
| M2 + J9 | CO-OP button (bottom right since 2026-11-01), CO-OP star only in the co-op launch (D24), that star selected (J9) | works on 2 PCs 2026-11-01 (bottom-right plate, star, Play on it). J9 does not hold: the game resets the selection after 1 s (`FACTS.md` 6), the window's text is the way |
| M3 + J3 | Steam Rich Presence: who hosts, version before connecting, invites | works on 2 PCs 2026-11-01: the host was listed as hosting, his invitation reached the guest, Join from the window |
| M4 | manifest: a rejoin downloads only what differs, every file checked (protocol 14) | works on 2 PCs 2026-11-01: the rejoin needed 1 of 10 files (414 bytes) |
| M5 | join from the launcher without the restart (J4) | not started |
| J8 (path C) | a save from another universe on our star | failed 2026-10-25, parked |

## 3. Who does what
| Who | Area | Now |
|---|---|---|
| ArKoWol + Claude | world truth and lifecycle, the plan; since 2026-10-31 also the join flow (`features/JOIN_FLOW.md`) | proposed 2026-11-17 (`PLAN.md` 7.1, to confirm): save at hosting (P2), sidecar (5a), friendly species not attackable (2h), nest results shared (2g), nests after a respawn (2i); join flow M1-M4 to check on 2 PCs |
| exxecute + Claude | how the other player looks and acts (models, animation, walking), menu UI | 2026-11-17: the guest's creature from the host's world (3c, P1) works on 1 PC, to check on 2 PCs; the guest's DNA (4e, P4): diagnosis built, to run, then the fix |

## 4. Known gaps in the repo
- `README.md`'s player table is kept short and points here.

## 5. Two-PC test log
Newest first. One line per test: date, build (commit), who, what was tested, result, verdict lines pasted from the log
(never whole logs: they contain Steam IDs).

| Date | Build | Who | What | Result |
|---|---|---|---|---|
| 2026-11-17 | working copy over 9ba03de (protocol 18), **one PC, two games** | exxecute | P1: the guest's creature kept in the host's world | works: first join `the host's world keeps no creature for us`, after B's edit A `the guest's creature 292620f1 ... is kept`, rejoin needed 2 of 13 files and B played 292620f1 again. Before that the game closed at start on this PC: the registry values had gone (`FACTS.md` 1) |
| 2026-11-17 | 535e446 (protocol 17) | both | a short real play session of the demo | fails: the guest started with his last own creature; after the host's new game the guest got the previous world; all nests came back at the host's respawn; the guest earned little DNA (nest goals do not complete); nest friendship and kills not shared; the friend's nest members can be hit and then keep attacking. Details and fixes: `features/SHARED_CREATURE_WORLD.md` |
| 2026-11-15 | working copy over 78314ff (protocol 17), **one PC, two games** | exxecute | tuning getter guard after ArKoW's host crash (`+0x76ca11`) | works: `kills: creature ... had no tuning object ...: the blank one is given` on the guest for a fresh mirror, both in the creature stage, no crash; A no longer touches Steam's mutexes, B uses its own `%TEMP%` |
| 2026-11-15 | working copy over cbbb7d5 (protocol 17), **one PC, two games** (`launch_two.bat -Connect`) | exxecute | the local transport: join, world transfer, co-op launch, creature stage | works: B got past the mutex with its own APPDATA, `local: connected`, `co-op launch: the world of 27790 is Game0`, `puppet: herd created` on both, the guest's eating reached the host (`eat: the guest eats animal 18`) |
| 2026-11-08 | working copy over 9a9b0b3 (protocol 17), host on 9a9b0b3 | both | nests, mating call, editor, own creature | works: nests at the host's places (`covers a host entry: 1`), mate and editor for the guest's own creature, portrait and buttons his, the host keeps his own look (alias). Host's side of the new fixes (leave, follow, fruit) not seen yet |
| 2026-11-01 | a326dfa (protocol 17) | both | models, own creatures, editor, buttons, lethal bite, PvP and respawn | works: models in both directions (`models: installed`), each sees the other's own creature, the host's edit seen by the guest, own ability buttons, the guest's native lethal bite, own DNA each, PvP deaths and respawns. Fails: after the host's respawn the guest could not see him but was hit by him (`puppet: herd created`, no `animal found`; fixed after the test) |
| 2026-11-01 | cc9d6ee (protocol 17) | both | join from the menu, star, own creature, edits, corpses, bites, respawn, Leave | works: invite, list, manifest (1 of 10 files), star and Play, the guest plays his own creature, eats the host's corpse to the end, respawns at his nest, Leave. Fails: no creature model arrived either way (each saw a stand-in; host's edit not seen), after the respawn the guest had the save's creature again, the ability buttons were mixed (could not bite), the host's eating animation not seen; guest log: `strays removed so far` 1518 in a session |
| 2026-10-31 | b0039f2 (protocol 13) | both | M1 join, walking, nests, creature edit, corpses | walking (forward, back, sideways, running) looks right; host's nests still not visible on the guest; after the host's edit + mating, hundreds of clones in a ring around the avatar nest on the guest only (not selectable, no collision); host's edit seen on the host only, the guest sees the old body; the guest still cannot eat corpses. Host log: `body hash b7211470` never changed during the session (no key or profile-sequence change), host nest model key `00000000!00000000!87b2acc2`, host corpses `food 100.0` |
| 2026-10-31 | 3df9d61 (protocol 13) | exxecute + friend | the other player walking, running, sideways, backwards, standing | works: the puppet keeps up, keeps the facing, does not twitch when standing |
| 2026-10-25 | c785a3b era (protocol 12) | both | species change, mating, corpses, fruit, fights | host edit changes the look of everything of the host's species and of the guest's puppet on the host, the guest sees nothing new (key unchanged?); many animals gather at the nest after the mating call on the guest; the guest cannot eat a corpse, the host can; fruit at other places; the real animal stood elsewhere than its mirror in a fight |
