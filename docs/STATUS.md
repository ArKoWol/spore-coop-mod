# Status: what works, what is being done, by whom

> Status: live · Owner: both · Updated: 2026-10-25
> Read when: at the start of every work session (after `git pull`), and before you pick a task. Update it when you start or
> finish a piece of work and after every two-PC test.

## 1. Creature stage: the minimum for playable co-op
The owner's list of 2026-10-25. States: **works** (seen on two PCs), **to check** (built, not seen on two PCs since the last
change), **partly**, **not done**.

| # | Item | State |
|---|---|---|
| 1 | Session: connect, world transfer, automatic restart and rejoin, console | works |
| 2a | Same animals on both sides: model, size, age, health, alpha | works, to re-check |
| 2b | Nests (the host's nests on the guest) | to check (endless-nest bug fixed 2026-10-25) |
| 2c | Plants not flickering | to check (fixed 2026-10-25) |
| 2d | Fruit at the same places and gone when eaten | not done (fruit is generated per machine, 6 of 74 match) |
| 2e | Eggs and hatching | partly |
| 2f | Corpses both players can eat | to check (eating reported by `kMsgEaten`); a newborn from an egg has the right species (works, 2026-10-25) |
| 2g | Species relations: extinction for both, allies | not done (plan: `PLAN.md` section 3) |
| 3a | The other player walks | works |
| 3b | Walking backwards, sideways, running | not done |
| 3c | Each player's own creature model on the puppet | to check (2026-10-25, protocol 12): built from the other avatar's species, rebuilt on change; the guest's own creature at start (`SetSpecies`) is not done |
| 3d | Real health and size of the puppet | to check |
| 3e | Action animations (eat, sing, bite) | partly (`kMsgAnim`, untested on two PCs) |
| 4a | Guest bites applied natively on his machine and reported to the host (PLAN rule 3) | partly: non-lethal bites native; the lethal native death crashed the guest (2026-10-25), so the kill goes through the host, no DNA yet |
| 4b | Host damage and deaths | works |
| 4c | PvP, death and respawn at the own nest, no reward | not done |
| 4d | Hit and get-hit animations of mirrors | not done |
| 5a | Separate DNA / brain / parts per player, saved in the host's save | not done |
| 5b | Stage change when both are ready | not done |
| 6 | Stability: no crashes on connect, in combat, on restart; leaving and returning guest | main crashes fixed |

Animals that waited for their species were never announced (25 of 99 reached the guest): fixed 2026-10-25, to check.

## 2. Join flow from the main menu (`features/JOIN_FLOW.md`)
| Spike | Question | State |
|---|---|---|
| main-menu world | a new save star shows up in the open menu | passed 2026-10-25 |
| J6 | Play on such a star loads its copied save | passed 2026-10-25 |
| J7 | does the galaxy save keep the star | open |
| J8 | a save from another universe works on our star | paused 2026-10-25: a save moved to our star stops before reading its planets (`FACTS.md` section 6) |
| J5 | our own window over the menu | works on 2 PCs 2026-10-25 (CO-OP star + window, real join through the restart) |
| - | CO-OP star after leaving a game without saving | known bug: it vanishes until the next start (GitHub issue) |
| J1-J4 | save-area redirect, `LoadGame` from the mod, Rich Presence, fetch before start | open |

## 3. Who does what
| Who | Area | Now |
|---|---|---|
| ArKoWol + Claude | world truth and lifecycle: dump, objects, events, telemetry, the plan | next: `PLAN.md` section 7 step 1 |
| exxecute + Claude | how the other player looks and acts (models, animation, walking), join flow and menu UI | CO-OP star bug (GitHub issue), join without the restart |

## 4. Known gaps in the repo
- `README.md`'s player table is kept short and points here.

## 5. Two-PC test log
Newest first. One line per test: date, build (commit), who, what was tested, result, verdict lines pasted from the log
(never whole logs: they contain Steam IDs).

| Date | Build | Who | What | Result |
|---|---|---|---|---|
| 2026-10-25 | c785a3b era (protocol 12) | both | species change, mating, corpses, fruit, fights | host edit changes the look of everything of the host's species and of the guest's puppet on the host, the guest sees nothing new (key unchanged?); many animals gather at the nest after the mating call on the guest; the guest cannot eat a corpse, the host can; fruit at other places; the real animal stood elsewhere than its mirror in a fight |
| 2026-10-25 | 8d35044 era (protocol 11) | both | eggs, bites, eating, animations | newborn has the right species; guest crashed on the lethal bite (fixed f632199); fruit does not vanish when eaten (switched off, to redo); eating plays, carnivore vomit and mating-call effects do not; trees/fruit jump for a few seconds at the start |
| 2026-10-25 | 287fc81 era | both | nests, animals, plants, actions | endless nests and 25 of 99 animals found and fixed; guest bites reach the host; guest animations arrive |
