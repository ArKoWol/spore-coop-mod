# Co-op design v2: one shared world

Status: proposal, written 2026-10-24 after the first working test. Nothing here is implemented yet except where marked.
Read `CLAUDE.md` first (how the mod works today, facts we already verified).

## 1. Why the current build is a dead end

Today both players run **their own local world** (same planet file, but each game spawns and simulates its own animals,
nests, plants). The only shared thing is the other player's avatar, shown as a puppet. So:

- the animals each player sees are different animals; you cannot fight, eat or hunt together;
- anything that happens in one world (a kill, a nest, a pickup) never happens in the other;
- the puppet is a ghost that teleports around: it is a "presence" demo, not co-op.

It proved the plumbing (injection, Steam networking, world transfer, puppet spawning, remote console). The next step is
a different architecture, not more polish on this one.

## 2. Target

Two players in **one** creature-stage world: the same animals in the same places doing the same things, both can attack,
be attacked, eat, and see each other walk and fight with proper animation. Scope: creature stage first. Other stages
and stage transitions come after the creature stage works (section 7).

## 3. Core decision: the host owns the world, the guest mirrors it

Spore's simulation is not deterministic and has no netcode, so lockstep and "everybody simulates the same thing" are out.
The standard answer for single-player engines: **host-authoritative state replication**.

| Thing | Owner | Replicated as |
|---|---|---|
| Each player's own avatar (movement, aim, ability use) | that player | state at 30 Hz (position, orientation, animation state) |
| NPC creatures, herds, their AI | host | host sends state at 10-20 Hz; guest only mirrors |
| Nests, eggs, plants, fruit, food, corpses, world objects | host | created / destroyed / state-changed events (reliable) |
| Damage, deaths, eating, picking up | host decides | guest sends a *request*, host applies and broadcasts the result |
| Health / hunger / DNA of an avatar | the avatar's owner, changed only through host-approved events | state + events |
| Projectiles, effects, sounds | each side locally | triggered by events, purely cosmetic |
| The save game, the planet | host (canonical) | world transfer at join (done) |

### What the guest's game must do
1. **Stop simulating shared things.** On the guest, skip the AI of every creature that is not a puppet or the local avatar
   (`NPCTickAI` detour, exists), and block local spawning (herds, hatching eggs, respawn timers) so the guest never
   creates an animal on its own.
2. **Clear and mirror.** When the world is ready, remove the animals/herds/eggs the save created locally and create
   mirrors from the host's snapshot (puppets: AI skipped, transform from the network, same species).
3. **Send requests, not results.** Bite, eat, pick up: send "player P used ability A on entity N" to the host; show
   the outcome when the host broadcasts it.

### Entities and ids
The host keeps a registry `netId -> game object` for every replicated noun (creatures, herds, nests, eggs, plants...). A
`netId` is assigned by the host and never reused in a session. The guest keeps `netId -> mirror`. Packets carry netIds,
never pointers. Ownership with epochs (see the `coop-sync-testing` skill, `actor-ownership`) is only needed later if an
NPC can change owner; in v1 the host owns every NPC for the whole session.

### Late join, reconnect
Same path as the first join: world transfer (exists) and then a full entity snapshot (all entities, reliable), then deltas.

### Progression and saves (needs the owner's decision, see section 9)
Proposal: the host's save is the only saved game. The guest plays with their own creature but their progress (DNA, brain
level, parts) is **not** written to the host's world, and their own save stays untouched (`Game0.coopbackup`). Stage
transitions are host-driven with a barrier (everybody ready), as described in the design discussion.

## 4. Platform facts that shape this (verified vs unknown)

Verified in this project: DLL injection works on the GOG build; `NPCTickAI` can be detoured; `CreateHerd` + `mHerd[0]` makes
a creature; `Teleport(pos, rot)` moves it (`SetPosition` alone is undone by physics); `DestroyInstance` removes it;
console commands run on the main thread; Steam messaging works (channel 0 state, channel 1 files); a remote console exists
(`console.ps1`) so the game can be driven from scripts.

From the ModAPI headers (not yet used): `GameNounManager.GetData(...)`/`mNounMap` (entities by noun id), `cHerd`, `cNest`,
`cEgg`, `cGamePlant`, `cCombatant` (`mHealthPoints`), `cCreatureBase` (`mHunger`, `mEnergy`, `WalkTo`, `PlayAnimation`).

Unknown, to be found by reverse engineering (`coop-reverse-engineer` skill; the SDK ships Ghidra data in `SDKtoGhidra`):
where animals are spawned/respawned, how damage is applied, how eating works, how to read and set animation state.

## 5. Spikes (small experiments that remove the unknowns), in this order

Each spike ends with a log/console result a script can check, and a note in `CLAUDE.md` "Hard-won facts".

| # | Question | Experiment | Done when |
|---|---|---|---|
| S1 | What entities exist and how do we enumerate them? | console command `coop dump` lists nouns by type with position, species, health | counts per type printed; the same save gives the same counts on both machines |
| S2 | Can a guest stop local spawning and clear local animals? | detour `CreateHerd` / hatch / respawn on the guest; `DestroyInstance` all NPC herds at world-ready | guest has zero local NPCs, none appear later |
| S3 | Can a mirror animate like the original? | read the host animal's animation state, replay it on the mirror (`PlayAnimation`, or `WalkTo` with a velocity); find why `WalkTo` crashed before | a mirrored animal walks/idles/attacks visibly like the original |
| S4 | Where is damage applied? | find/detour the damage function of `cCombatant`; log attacker, target, amount | the log shows a player bite on an NPC with ids and amount |
| S5 | How does eating/pickup work? | trace plant/food/corpse consumption | a consume event with entity id is logged |
| S6 | Two games on one PC for testing | `COOP_MULTI=1` makes the mod give a second copy a private mutex (**done**: it gets past "already running"), plus a separate `APPDATA`. The second copy then crashes at `SporeApp.exe+0x29de23` (null read), the crash log has the stack. Open: find the other shared resource; if it cannot be solved, use Windows Sandbox/a VM or a second Windows user | two windowed games running side by side, each with its own console |
| S7 | Join without restarting | `cGamePersistenceManager::LoadGame` (undocumented) from the mod | client loads the host world straight from the main menu |

S6 comes first: without two local games every later test needs a second person.

## 6. Milestones and acceptance tests

- **M0 test rig.** (a) Split the mod into modules (section 8). (b) A pure C++ replication core with a fake-world test
  program (no game): entity table, snapshot/delta, ids, loss/reorder/duplicate handling. (c) A flight recorder: every
  machine logs `(time, netId, state hash)` so the first divergence is found automatically. (d) S6 so two games run on one PC.
- **M1 shared animals.** Host animals mirrored one-way (position, orientation, species, health). Test: after 10 s the entity
  tables of host and guest agree (same count per type, positions within 1 m, same health).
- **M2 avatars both ways.** Each player sees the other's real creature (species key and model sent at join) walking with
  animation. Test: animation checker compares the remote avatar's animation with its source.
- **M3 PvE combat.** Players hit NPCs, NPCs hit players, deaths, eating, hunger. Test: the same events happen on both sides,
  health values equal after each step.
- **M4 world objects.** Nests, eggs, plants, food, pickups created/consumed on both sides.
- **M5 session lifecycle.** Late join, reconnect, disconnect handling, host leaving, saves, stage transition barrier.
- **M6 hardening.** Interpolation buffer (about 100 ms), bandwidth limits and interest management, chaos tests with lag/loss.

## 7. Later: other stages, transitions

Tribe and civilization stages replicate more kinds of entities (villagers, buildings, vehicles); space needs a galaxy-level
model. They reuse the same ownership/replication core, so the creature stage is the template. Not planned in detail yet.

## 8. Code structure

`mod/CoopMod/dllmain.cpp` is already about 1400 lines. Split before adding more:

- `net/` transport (Steam, UDP), message framing, reliability channels;
- `core/` replication core with **no Spore headers**: entity table, snapshot/delta, ownership, interest. Built into a test
  executable (`tests/`) with a fake world and a chaos layer (loss, delay, reorder, duplicates);
- `game/` the adapter to Spore: enumerate entities, create/destroy mirrors, drive animation, hook damage/eating;
- `world_transfer`, `console`, `indicator`, `crash_log` as they are today.

## 9. Test strategy (from the `coop-qa` and `coop-sync-testing` skills)

1. Model the world first: the fake-world tests above, with invariants as oracles (no duplicate netIds; every guest entity
   has a host id; counts and positions agree within tolerance; no guest-only NPC). Fix failures at the root, never loosen an oracle.
2. Flight recorder on every machine; every desync report becomes a shrunk trace and a regression test.
3. Real clients: two isolated game copies (S6), a launcher for host + guest, scripted through the remote console
   (`stage`, `coop`, commands to move/attack to be added), prepared saves as known start states, PASS/FAIL printed.
4. Lag and loss: a UDP chaos proxy in front of the UDP transport; Steam path tested for connect/disconnect.
5. Every fix ships with a test and a mutation check (turn the fix off once: the test must fail).

The hub's ready-made tools (`tools/chaos`, `flight`, `fuzz`, `ownership`, `fakeworld`, `gameslot`, `gamepool`, `bots`,
`animcheck`) are not on this machine; only the skill descriptions are. If we want them, the skill's playbooks name the
source project (fnv-coop); otherwise we write small equivalents in C++/PowerShell.

## 10. Decisions needed from the owner

1. **Progression model**: host save canonical, guest progress not saved into it (proposed), or both keep their own progress?
2. **Scope**: creature stage only until M5 is solid (proposed)?
3. **PvP**: players can hurt each other, or co-op only (proposed: co-op only; friendly fire off)?
4. **Guest experience**: while joined, the guest plays inside the host's world and loses nothing of their own save (the
   backup stays); OK?
5. **Test machines**: is a second PC always available, or do we invest in two games on one PC (S6) first (proposed)?

## 11. Honest assessment

This is a multi-week project even with a good test rig: the hard parts are the unknown hooks (S2-S5) and animation
mirroring (S3). The risky one is S2: if Spore re-creates animals through paths we cannot block, the guest will fight its
own animals; the fallback is to hide/destroy any local NPC every frame on the guest. Combat feel (latency between a bite on the guest and
the result from the host) will need a prediction layer later. None of this is blocked by missing information that we could
not get with the tools we already have (SDK, Ghidra data, the remote console, the crash log).
