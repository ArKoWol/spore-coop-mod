# Co-op design v2: one shared world

Status: agreed plan, written 2026-10-24 (v2.1: owner's decisions folded in). Nothing here is implemented yet except where
marked. Read `../CLAUDE.md` first (how the mod works today, facts we already verified).

## 1. Why the current build is a dead end

Today both players run **their own local world** (same planet file, but each game spawns and simulates its own animals,
nests, plants). The only shared thing is the other player's avatar, shown as a puppet. So the animals you see are not the
animals your friend sees, nothing you do to the world happens in the other world, and the puppet is a ghost that
teleports around. It proved the plumbing (injection, Steam networking, world transfer, puppet spawning, remote console);
the next step is a different architecture, not more polish.

## 2. Product definition (decisions of the owner, 2026-10-24)

"Both play their own game, but in one realm." Two players, **one world, two different creatures.**

| Topic | Decision |
|---|---|
| World | One world, owned by the host. The guest plays inside the host's world. |
| Creatures | Each player designs **their own creature** in the editor. Different species. |
| Progress | **Each has their own** DNA, brain level and parts. Stages are shared. |
| Save | The **host's save holds everything**: the host's creature and progress and the guest's creature and progress. |
| Stage change | Happens only when **both have reached the goal**; then both go through the editor and the game continues only when **both are ready**. |
| PvP | Allowed. Killing a friend works like any death: **he respawns at his own nest**. |
| Session start | A **new game**; both create creatures; the **cell stage is skipped** (the mod leads both straight to the creature editor, then the creature stage). |
| Guest spawn | Next to the host, with **his own nest** next to the host's nest. |
| Pack ("socialize" followers) | **Not in v1.** Goals are done by hunting. Later. |
| Guest disconnects | His creature **disappears**; his progress stays in the host's save and is restored when he comes back. |
| Scope | Creature stage only until it is solid. |
| Testing | **On two real PCs from the start**, both developers can test together almost every day (so a second copy on one PC, spike S6, is low priority). |
| Killing a player | No reward, no corpse: the victim just disappears and respawns at his nest. |
| Audience | Private use between friends (no public distribution; the Steam API file comes from the players' own Steam games). |
| Work split | As proposed in section 12. |

## 3. Core architecture: the host owns the world, the guest mirrors it

Spore's simulation is not deterministic and has no netcode, so lockstep and "everybody simulates the same thing" are out.
The standard answer for single-player engines is **host-authoritative state replication**.

| Thing | Owner | Replicated as |
|---|---|---|
| Each player's own avatar: movement, aim, ability use | that player | state at 30 Hz (position, orientation, animation state) |
| NPC creatures, herds, their AI | host | state at 10-20 Hz, guest only mirrors |
| Nests (both players' nests), eggs, plants, fruit, food, corpses, world objects | host | create / destroy / state-change events (reliable) |
| Damage, deaths, eating, picking up | host decides | the guest sends a *request*, the host applies and broadcasts the result |
| Health / hunger / energy of an avatar | the avatar's owner, changed only through host-approved events | state + events |
| DNA, brain level, parts, abilities | the avatar's owner (local game) | sent to the host on change, stored in the host's save |
| World time, weather, day/night | host | state |
| Projectiles, effects, sounds | each side locally | triggered by events, cosmetic |

### What happens on each side
- **Host game**: the real simulation. It contains the host avatar (real) and the **guest avatar as a player puppet**:
  AI skipped, transform driven by the guest, flagged so NPCs can perceive and attack it, damage to it goes through the
  event path to the guest.
- **Guest game**: the same stage, but its simulation of shared things is neutered: the AI of every creature that is not
  a puppet or the local avatar is skipped (`NPCTickAI` detour exists), local spawning (herds, hatching, respawn timers) is
  blocked, and at world-ready the locally created animals/eggs/herds are removed and replaced by mirrors built from the
  host's snapshot. The guest's own avatar is real and local; the host avatar is a puppet.
- **Requests, not results**: a bite, eating, picking up is sent to the host ("player P used ability A on entity N").
  The outcome is shown when the host broadcasts it. Own movement is client-authoritative (no waiting for the host).

### Entities and ids
The host keeps a registry `netId -> game object` for every replicated noun (creatures, herds, nests, eggs, plants, ...).
`netId` is assigned by the host, never reused in a session. Packets carry netIds, never pointers. Avatars have fixed ids
per player (`player 0 = host`, `player 1 = guest`). In v1 the host owns every NPC for the whole session, so ownership
epochs (hub tool `ownership`) are not needed yet.

### Self-healing
Besides deltas the host sends a **full snapshot of the entity table every 10 s** (unreliable-sequenced, cheap at this
size); the guest reconciles (create missing, destroy extra, correct drift). A desync therefore heals itself, and the
telemetry (section 9) shows that it happened.

## 4. Session lifecycle

1. **Lobby**: host types `coop host`, guest `coopjoin <id>` (as today, from the main menu).
2. **New co-op game** (`coop new` on the host): the mod starts a new game that skips the cell stage and opens the creature
   editor on **both** machines (S7).
3. **Editors**: each player designs a creature. A barrier waits for "done" from both.
4. **World creation**: the host's game creates the creature-stage world with the host's creature. The guest's creature
   (species data and model, see S8) is sent to the host; the host creates the **guest nest next to its own** and the guest
   avatar puppet. The host sends the guest the world files (save transfer, exists), installs-at-start as today until
   S7 removes the restart, and a full entity snapshot.
5. **Play**: replication as in section 3. Both players evolve at their own nests (each opens the editor independently;
   the other keeps playing).
6. **Stage goal**: each avatar reports its goal state. When **both** reached it, the host starts the stage change:
   both go to the editor, a barrier waits for both "done", then the next stage starts for both. (Not in the first
   milestones: they work inside the creature stage.)
7. **Saving**: the host's save plus a sidecar `coop_players.json` in the same save folder (see section 5).
8. **Guest leaves/crashes**: the host destroys the guest puppet and its nest stays; progress is in the sidecar. A returning
   guest (same Steam ID) gets his creature and progress back. **Host leaves**: the session ends for the guest (message).

## 5. Data

Sidecar `Game0\coop_players.json` (written by the host on save and when a player's progress changes):
```
{ "version": 1,
  "players": [
    { "steamId": "7656...", "role": "host",  "species": "<resource key>", "creatureFile": "host.creature",
      "dna": 123, "brainLevel": 3, "parts": [...], "nest": {"x":..,"y":..,"z":..} },
    { "steamId": "7656...", "role": "guest", ... } ] }
```
The creature files live in the same folder so the save is self-contained. The guest's own `Game0` is untouched (backup).

## 6. Network protocol

| Channel | Content | Reliability | Rate |
|---|---|---|---|
| 0 | avatar state (position, orientation, animation state, speed) | unreliable | 30 Hz |
| 1 | save files (exists) | reliable | on join |
| 2 | NPC / entity deltas and the 10 s full snapshot | unreliable-sequenced | 10-20 Hz |
| 3 | events: create/destroy, damage requests/results, death, eat, ready flags, progress | reliable ordered | on event |
| 4 | telemetry (section 9): per-second state summary | unreliable | 1 Hz |

Budget: about 60 entities x 28 bytes x 15 Hz is roughly 25 KB/s, fine for Steam relay. Quantize positions and orientations
(16-bit per component relative to the planet radius is enough). Both sides interpolate remote entities with a **100 ms
buffer**. `kProtocolVersion` is bumped on every layout change (players with different versions refuse each other).

## 7. Spikes (small experiments that remove the unknowns), in this order

Each spike ends with a log/console result a script can check, and a note in `CLAUDE.md` "Hard-won facts".

| # | Question | Experiment | Done when |
|---|---|---|---|
| S1 | What entities exist; how to enumerate? | console `coop dump` lists nouns by type with position, species, health | counts per type printed; same save, same counts on both machines |
| S7 | How to start a new game without the cell stage and open the creature editor from the mod, and load the world without restarting? | `cGamePersistenceManager::LoadGame` / `GameLoadParameters` (mode = creature), `GameModeManager.SetActiveMode(kEditorMode)` | the mod takes a player from the main menu to the creature editor, then to the creature stage |
| S8 | How to give another machine a creature (species + model) so it can be created there? | send the `.creature` file / species data, register it with the resource manager, `GetSpeciesProfile(key)` finds it, `CreateHerd` spawns it | a creature designed on A appears on B with the same look |
| S2 | Can a guest stop local spawning and clear local animals? | detour `CreateHerd` / hatching / respawn on the guest; `DestroyInstance` all NPC herds at world-ready | the guest has zero local NPCs and none appear later |
| S9 | Can the host create a second nest and respawn the guest there? | `CreateNest`, and see how the game picks the respawn point of an avatar | the guest dies and respawns at his nest |
| S3 | Can a mirror animate like the original? | read the animal's animation state and replay it on the mirror; find why `WalkTo` crashed | a mirrored animal walks/idles/attacks visibly like the original |
| S4 | Where is damage applied? | find/detour the damage function of `cCombatant`; log attacker, target, amount | the log shows a bite with ids and amount |
| S5 | How does eating/pickup work? | trace plant/food/corpse consumption | a consume event with an entity id is logged |
| S10 | Do NPCs attack a player puppet in the host world? | spawn a puppet of another species, see if the NPC AI targets it | NPCs react to the guest puppet; if not, find the targeting hook |
| S11 | Save hook: when does the game save, how to write the sidecar at that moment? | hook `kMsgSaveGame` / `cGamePersistenceManager` | the sidecar is written with every save |
| S6 (optional) | Two games on one PC | `COOP_MULTI=1` (**done**: the second copy gets past "already running") plus a separate `APPDATA`; the second copy then crashes at `SporeApp.exe+0x29de23` (null read). Cause open. | two windowed games side by side |

S1 first (everything depends on enumeration), then S7/S8 (the new flow), then S2, S9, S3-S5, S10, S11.

## 8. Milestones and acceptance tests

- **M0 foundation.** Split the mod into modules (section 11). The replication core as a plain C++ library with **no Spore
  headers**, built into a test program with a fake world and a chaos layer (loss, delay, reorder, duplicates).
  Telemetry (section 9). S1 and the remote console work (exists).
- **M1 new co-op game flow.** S7 and S8: from the main menu both players get their own creature into one world. Test: both
  players see both creatures (own and other), correct looks.
- **M2 shared animals.** One-way mirrors of the host's animals (position, orientation, species, health) with the guest's
  AI and spawning neutered (S2), plus the 10 s snapshot. Test: after 10 s the entity tables agree (counts per type, positions
  within 1 m, health), checked by the telemetry.
- **M3 avatars both ways with animation.** S3. Test: remote avatars animate like their source.
- **M4 combat.** S4, S5, S9, S10: players and NPCs hurt each other, deaths and respawns at the right nests, eating, PvP kill and
  respawn. Test: the same events and equal health on both sides after every scripted step.
- **M5 world objects and saving.** Nests, eggs, plants, food on both sides; sidecar save/restore (S11); guest leaves and returns.
- **M6 stage barrier.** Both reach the goal, both edit, both ready, next stage. Late/early cases (one disconnects at the barrier).
- **M7 hardening.** Interpolation tuning, bandwidth limits and interest management, lag and loss runs, soak test (one hour).

## 9. Test strategy: two real PCs, with the hub skills' principles

Constraint: a test needs both machines running at once. To make every session count:

1. **In-band telemetry.** The guest sends a compact per-second state summary (entity counts per type, a hash of the sorted
   entity states, own position and health, the last events) to the host on channel 4; the host writes both views, side by
   side, with a **desync verdict**, into its own `CoopMod.log`. One machine's log then answers "did they diverge, when, in
   what". The friend's Claude and mine read the same log.
2. **Scripted scenarios through the remote console** on both PCs (`scripts/console.ps1 -Run`), with a known start state (prepared
   save, fixed positions). Each scenario prints `RESULT: PASS` or `FAIL` plus the reason. Scenario steps and expected
   results live in `TEST_PLAN.md` (to be written with M0) so both developers run the same thing.
3. **Invariants as oracles** (never loosened to make a test pass): no duplicate netIds; every guest entity has a host id;
   counts per type and positions agree within tolerance; no guest-only NPC; health equal after each event; exactly one
   avatar per player on each side.
4. **Flight recorder**: each machine logs `(time, netId, state hash)`; the first divergence is found automatically and
   becomes a regression test.
5. **Pure-core tests need no game**: the replication core with fake world and chaos runs in seconds on either PC, so most
   logic is verified without a live session.
6. **Every fix ships with a test and a mutation check** (turn the fix off once: the test must fail).
7. **Coordination** between the two Claudes: the repo is the channel. `CHANGELOG.md` (what changed), `CLAUDE.md` (facts),
   this file (plan), `TEST_PLAN.md` (what to run, last results). Test logs contain Steam IDs and are not committed; paste
   the verdict lines instead.

The hub's ready-made tools (`chaos`, `flight`, `fuzz`, `ownership`, `fakeworld`, `bots`, `animcheck`) are not on this
machine, only the skill descriptions; we write small equivalents as needed.

## 10. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| R1 NPCs ignore the guest puppet or attack it wrongly | no PvE for the guest | S10; if needed hook the NPC targeting so player puppets count as valid targets |
| R2 The game recreates animals through paths we cannot block (S2) | the guest fights local ghost animals | destroy any non-mirror NPC on the guest every frame as a fallback |
| R3 No usable way to start a game in the editor / skip the cell stage (S7) | clumsy start | fall back to: both start a normal new game manually, then the mod takes over at the creature stage |
| R4 A creature cannot be created from another machine's data (S8) | wrong look of the other creature | transfer via the resource system / Sporepedia files; worst case a stand-in model with the right name |
| R5 Animation state cannot be read/replayed (S3) | puppets slide | derive walk/run/idle from speed, play standard animations |
| R6 Spore's own save/progress cannot hold two players | progress of the guest lost on reload | the sidecar (section 5) is ours; the base save only has the host |
| R7 Combat latency feels bad for the guest | PvP/PvE annoying | prediction and reconciliation for own hits, later |
| R8 Two machines needed for every test | slow iteration | telemetry, scripted scenarios, pure-core tests, optional S6 |

## 11. Code structure

`mod/CoopMod/dllmain.cpp` is already about 1500 lines. Split before adding more:

- `net/` transport (Steam, UDP), framing, channels, telemetry;
- `core/` replication core with **no Spore headers**: entity table, snapshot/delta, ids, events, interest. Built into a test
  executable (`tests/`) with a fake world and a chaos layer;
- `game/` the adapter to Spore: enumerate entities, create/destroy mirrors, drive animation, hook damage/eating, nests,
  start flow, save sidecar, stage barrier;
- `world_transfer`, `console`, `indicator`, `crash_log` as they are today.

## 12. Work split between the two developers (proposal)

- **Developer A** (ArKoWol's Claude): the replication core and its tests (M0), entity enumeration and mirrors (S1, S2),
  telemetry, protocol.
- **Developer B** (exxecute's Claude): the start flow and editor barrier (S7, S6 if wanted), creature model exchange (S8),
  save sidecar (S11), stage barrier (M6).
- **Together**: nests/respawn (S9), animation (S3), combat (S4, S5, S10).
Rules stay as in `CLAUDE.md`: pull first, small commits, changelog for every change, bump the protocol version.

## 13. Remaining defaults (change them if you disagree)

- the guest receives a full entity snapshot every 10 s; the host leaving ends the session for the guest (with a message);
- two players only (the core is written for up to four, tested with two); host migration is not supported;
- in-game messages are English first (Russian later if wanted);
- the guest's own save is kept as `Game0.coopbackup`, never overwritten.

