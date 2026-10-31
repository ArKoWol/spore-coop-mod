# Status: what works, what is being done, by whom

> Status: live · Owner: both · Updated: 2026-10-31
> Read when: at the start of every work session (after `git pull`), and before you pick a task. Update it when you start or
> finish a piece of work and after every two-PC test.

## 1. Creature stage: the minimum for playable co-op
The owner's list of 2026-10-25. States: **works** (seen on two PCs), **to check** (built, not seen on two PCs since the last
change), **partly**, **not done**.

| # | Item | State |
|---|---|---|
| 1 | Session: connect, world transfer, automatic restart and rejoin, console | works |
| 2a | Same animals on both sides: model, size, age, health, alpha | works; in a fight a mirror stood off the real animal (2026-10-25), settle snap built 2026-10-31, to check |
| 2b | Nests (the host's nests on the guest) | not visible on the guest (2026-10-25); orientation, scale, model key sent since 2026-10-31: still not visible (2-PC test 2026-10-31) |
| 2c | Plants not flickering | to check (fixed 2026-10-25) |
| 2d | Fruit at the same places and gone when eaten | not done (fruit is generated per machine, 6 of 74 match) |
| 2e | Eggs and hatching | partly |
| 2f | Corpses both players can eat | the guest cannot eat a corpse (2026-10-25); the host's food value sent, corpse placed once: still cannot eat (2-PC test 2026-10-31). A newborn from an egg has the right species (works, 2026-10-25) |
| 2g | Species relations: extinction for both, allies | not done (plan: `PLAN.md` section 3) |
| 3a | The other player walks | works |
| 3b | Walking backwards, sideways, running | works (2-PC test 2026-10-31) |
| 3c | Each player's own creature model on the puppet | an edit by the host was not seen by the guest (2026-10-25); body hash and alias key since 2026-10-31: still not seen, the host's hash never changed (2-PC test 2026-10-31); the guest's own creature at start (`SetSpecies`) is not done |
| 3d | Real health and size of the puppet | to check |
| 3e | Action animations (eat, sing, bite) | partly (`kMsgAnim`, untested on two PCs) |
| 4a | Guest bites applied natively on his machine and reported to the host (PLAN rule 3) | partly: non-lethal bites native; the lethal bite goes through the host (no crash since, owner 2026-10-31), no DNA yet |
| 4b | Host damage and deaths | works |
| 4c | PvP, death and respawn at the own nest, no reward | not done |
| 4d | Hit and get-hit animations of mirrors | not done |
| 5a | Separate DNA / brain / parts per player, saved in the host's save | not done |
| 5b | Stage change when both are ready | not done |
| 6 | Stability: no crashes on connect, in combat, on restart; leaving and returning guest | main crashes fixed |

Animals that waited for their species were never announced (25 of 99 reached the guest): fixed 2026-10-25, to check.

## 2. Join flow from the main menu (`features/JOIN_FLOW.md`)
| Part | What | State |
|---|---|---|
| today | CO-OP star + window, Steam join, download, restart (seen on 2 PCs 2026-10-25, before M1) | works |
| M1 | co-op launch: host's world in `Coop\<id>` for one launch, own `Game0` restored at every start, migration question | built 2026-10-31, folder logic tested (63 checks), game side to check on 2 PCs |
| M2 + J9 | CO-OP button instead of the star (closes the GitHub issue), host's star selected in the co-op launch | not started |
| M3 + J3 | Steam Rich Presence: who hosts, version before connecting, invites | not started |
| M4 / M5 | manifest to skip downloads / join from the launcher without the restart (J4) | not started |
| J8 (path C) | a save from another universe on our star | failed 2026-10-25, parked |

## 3. Who does what
| Who | Area | Now |
|---|---|---|
| ArKoWol + Claude | world truth and lifecycle, the plan; since 2026-10-31 also the join flow (`features/JOIN_FLOW.md`) | creature-stage fixes (protocol 13, to check); join flow M1 next |
| exxecute + Claude | how the other player looks and acts (models, animation, walking), menu UI | - |

## 4. Known gaps in the repo
- `README.md`'s player table is kept short and points here.

## 5. Two-PC test log
Newest first. One line per test: date, build (commit), who, what was tested, result, verdict lines pasted from the log
(never whole logs: they contain Steam IDs).

| Date | Build | Who | What | Result |
|---|---|---|---|---|
| 2026-10-31 | b0039f2 (protocol 13) | both | M1 join, walking, nests, creature edit, corpses | walking (forward, back, sideways, running) looks right; host's nests still not visible on the guest; after the host's edit + mating, hundreds of clones in a ring around the avatar nest on the guest only (not selectable, no collision); host's edit seen on the host only, the guest sees the old body; the guest still cannot eat corpses. Host log: `body hash b7211470` never changed during the session (no key or profile-sequence change), host nest model key `00000000!00000000!87b2acc2`, host corpses `food 100.0` |
| 2026-10-31 | 3df9d61 (protocol 13) | exxecute + friend | the other player walking, running, sideways, backwards, standing | works: the puppet keeps up, keeps the facing, does not twitch when standing |
| 2026-10-25 | c785a3b era (protocol 12) | both | species change, mating, corpses, fruit, fights | host edit changes the look of everything of the host's species and of the guest's puppet on the host, the guest sees nothing new (key unchanged?); many animals gather at the nest after the mating call on the guest; the guest cannot eat a corpse, the host can; fruit at other places; the real animal stood elsewhere than its mirror in a fight |
| 2026-10-25 | 8d35044 era (protocol 11) | both | eggs, bites, eating, animations | newborn has the right species; guest crashed on the lethal bite (fixed f632199); fruit does not vanish when eaten (switched off, to redo); eating plays, carnivore vomit and mating-call effects do not; trees/fruit jump for a few seconds at the start |
| 2026-10-25 | 287fc81 era | both | nests, animals, plants, actions | endless nests and 25 of 99 animals found and fixed; guest bites reach the host; guest animations arrive |
