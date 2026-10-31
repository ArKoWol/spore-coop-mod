# Co-op plan for every stage

> Status: agreed with the owner (2026-10-25) · Owner: ArKoWol · Updated: 2026-10-31
> Read when: before you start any gameplay work, and before you propose a change of direction. This is the only live plan;
> the decisions behind it, with dates, are in `DECISIONS.md`. What is done is in `STATUS.md`.

The most realistic plan, chosen in a long discussion with the owner. Everywhere it takes the reliable option, not the most
ambitious one. **Nothing in it beyond the creature-stage basics is verified in the game yet.**

## 1. Rules for every stage
1. **The host simulates the NPCs and the world.** The guest shows mirrors. NPC "brains" are not moved between players.
2. **Each player simulates his own things**: his cell, creature, tribe members, vehicles, ship. The other sees puppets.
3. **The actor decides the result of his own action** (hit, eat, pick up): it is applied on his machine at once and the result
   goes to the other. Nobody waits for his own actions, and the game pays its own rewards (DNA and so on) natively.
4. **Shared things are replayed through the game's own functions**, never by writing memory: totems, extinction, money,
   missions. The game then shows its own messages and cinematics.
5. **Checkpoints**: an important event is held until both players reached it. The first one plays on and sees "waiting for
   <friend>". When both are there, the host starts the event on both machines at the same time.
6. **Each player has his own editor.** Guest in the editor: the host plays on. Host in the editor: the guest's world probably
   freezes; the guest sees "host is in the editor" and may open his own editor.
7. **Save**: the world in the host's save; the guest's personal data in a sidecar file next to it (section 8). Only the host
   saves. Joining: world transfer and one restart into a "co-op launch" (`features/JOIN_FLOW.md`).
8. **Leaving**: the guest leaves = his creature disappears, his tribe or cities stay and are invulnerable while he is away;
   the host leaves = the session ends.

## 2. Cell
- **One pool.** Fish, food and predators are the host's, the guest sees mirrors. Each has his own cell, DNA, species, editor.
- **Checkpoints**: every growth of the cell (the pool rescales, so both are always the same size) and going onto land.
- **Transition**: both reached the end -> creature editor for both -> both land on the host's planet, nests next to each other.

## 3. Creature
- **Each has his own**: species, DNA, brain level, parts, pack, nest, species relation table.
- **Species relations**:
  - the guest's table is native on his machine, his game keeps it (replayed deaths of mirrors never go into it);
  - the host's game only knows its own table; a detour on `GetRelationship` (0xBA40E0, `FACTS.md` section 9) answers from the
    guest's table (sent by the guest) when the NPC is looking at the guest's puppet;
  - each player's species is an ally (value 6) in the other's table;
  - extinction is shared: an extinct species' herds disappear for both.
- **DNA for a kill**: the game pays only the attacking avatar, so the guest applies his own lethal hit on his machine.
- **Checkpoints**: every nest move (both nests move together) and the transition to the tribe stage.
- **Weak spot**: the social mini-game for the guest. His herd is a mirror without AI, so the guest sings and dances and the host
  counts the impression by the same rules; the guest may not see the native mini-game interface.

## 4. Tribe
- **Two tribes next to each other**, each with its own species, food, tools, members, outfits.
- Right after the start **the friend's tribe is marked allied** (a free totem is fine: co-op is not about balance).
- **NPC tribes are the host's.** A tribe befriended or conquered by either player counts for both.
- **Limits**: the host's AI probably does not raid the guest's village (to it, it is a foreign NPC tribe): v1 raids only the
  host, or the mod starts raids on the guest now and then. The guest's friendship with tribes (music, gifts) is simplified as in
  the creature stage. Main technical risk: making the guest's tribe "his own" on his machine (`FACTS.md` section 10).
- **Transition**: all NPC tribes belong to the team -> the host starts it -> both in their editors -> civilization.

## 5. Civilization: PvP, the winner becomes home
- **Two civilizations**, each with its own money, city, vehicles, buildings.
- **Against NPC cities**: the host can do everything, as in the normal game; the guest captures reliably with military
  vehicles; religion and trade for the guest may work through the same path as damage, no promise.
- **Against the friend only war**: military vehicles capture his cities. While the friend is offline his cities cannot be attacked.
- **Victory**: whoever owns every city of the planet wins. On the winner's machine the game sees "all cities are mine" by itself
  and starts its own transition, so the win check needs no change. On the loser's machine the mod suppresses "game over" and
  takes him into space after the winner.
- **Transition**: victory -> both in the ship editor -> space.

## 6. Space
- **One empire, the winner's**: his species, name, colour, homeworld. The loser flies his own ship in that empire.
- **Shared**: money, upgrades and tools, colonies, buying planets, terraforming, trade and alliances, missions. A mission taken
  by one is taken for both; done by one, both get the reward. A used charge is used on both.
- **Own**: ship position, health and energy.
- **Who simulates a system**: a player alone in a system simulates it on his machine; when both are in one system, the host
  simulates its NPC ships and the guest sees mirrors. AI empires across the galaxy: only the host.

## 7. Order of work
1. **Creature stage, finished**: the guest's own creature, "the actor decides his own action result", the guest's relation
   table, extinction, the alliance between the players, nest checkpoints, editors, the guest's data in the save.
2. **Checkpoint system** as a common mechanism: hold, report, start on both.
3. **Cell in one pool** and the cell -> creature transition.
4. **Creature -> tribe transition**, two tribes, shared totems.
5. **Civilization**: two civilizations, PvP, the winner becomes home.
6. **Space**: one empire, two ships, shared money and missions.

### 7.1 Creature stage priorities (MoSCoW)
**Proposed by exxecute on 2026-10-31, not yet agreed by ArKoWol.** Once agreed, it gets a line in `DECISIONS.md`. The test:
can two players play the creature stage for several evenings without losing progress? Numbers are `STATUS.md` section 1.

| Priority | Mechanic | STATUS | Who |
|---|---|---|---|
| Must | stability: no clones at the nest, the guest leaves and returns | 1, 6 | ArKoWol |
| Must | the guest eats corpses | 2f | exxecute |
| Must | the guest's lethal hit applied on his machine, he gets the DNA | 4a | exxecute |
| Must | the guest's own creature from the start (`SetSpecies`), editor changes seen by the friend | 3c | exxecute |
| Must | the guest's progress in the host's save (sidecar, section 8) | 5a | ArKoWol |
| Must | a dead player respawns at his own nest | 4c | exxecute |
| Must | the two players' species are allies | 2g | ArKoWol |
| Should | the host's nests on the guest | 2b | ArKoWol |
| Should | species relation tables, shared extinction | 2g | ArKoWol |
| Should | checkpoints: nest move together, transition to the tribe stage | 5b | ArKoWol |
| Should | the guest's social mini-game, simplified (section 3, weak spot) | 3e | exxecute |
| Should | check: the puppet's health and size, mirrors in fights | 3d, 2a | exxecute, ArKoWol |
| Could | shared fruit; song and dance animations; hit animations; eggs; plants; follow the friend; PvP rewards; join flow M5 | 2c-2e, 3e, 3f, 4c, 4d | - |
| Won't | pack followers (D1), more than two players, host migration, exact sync of every animation | - | - |

Join flow M5 waits until the Musts are done; M1-M4 are only checked on 2 PCs.

## 8. Save model
The host's save is the world. The guest's personal data (species and creature file, DNA, brain level, parts, relation table,
and later his tribe/civ identity) is written by the host into a sidecar `Game0\coop_players.json` next to the save, on every
save and when the guest's progress changes. A returning guest (same Steam ID) gets it back. The guest's own `Game0` is never
overwritten: the friend's world is swapped in for one launch and the own `Game0` comes back at the next start
(`features/JOIN_FLOW.md` M1, built, not yet verified on 2 PCs).

## 9. Least certain parts and their fallbacks

| Part | If it does not work |
|---|---|
| The host's animals react to the guest's puppet through the guest's relation table | one relation table shared by both |
| The guest's tribe or civilization can be made "his own" on his machine | one faction for both, units split between the players |
| The guest's world freezes while the host is in the editor | both go into the editor together |

## 10. Ids used in code comments
Older code comments name spikes, gates and milestones of the plans this file replaced. What they mean now:

| Id | Meaning | Where now |
|---|---|---|
| M0 | replication core as plain C++ with tests | `core/coop_core.h`, `tests/` |
| M2 | shared animals: the host's animals mirrored on the guest | section 1, rule 1 |
| S1 / G0 | world dump and diff (`coop dump`, `worlddiff.ps1`) | `TESTING.md` |
| S3 | the puppet walks with the game's locomotion | `ARCHITECTURE.md` section 7 |
| S7 | start / load a world from the mod | `features/JOIN_FLOW.md` |
| S8 / G2 | creature models sent to the guest; nests | `FACTS.md` section 8 |
| G1 | eaten fruit, plants, hatched eggs on the other machine | `STATUS.md` |
| R2 | the game recreating animals on the guest: remove strays | `ARCHITECTURE.md` section 7 |
