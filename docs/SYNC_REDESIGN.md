# Sync redesign (2026-10-24, after the first real two-PC test of the shared world)

Read `DESIGN.md` for the product decisions (they stay). This file replaces its sections 3 and 7 to 8 where they disagree:
**how** the world is kept equal, and **in which order** we build it. Sources: the hub playbook pages `how-coop-mods-work`,
`effect-capture-story-sync`, `animation-sync-checks`, `desync-hunting`, `lessons` (summarised below, not copied).

## 1. What the test showed, and the cause of each symptom

| Symptom (guest = friend) | Cause | Confidence |
|---|---|---|
| Objects lie in the same places but do not disappear when eaten | We replicate only `cCreatureAnimal`. Fruit, plants, eggs, nests, food exist on both sides from the same save but nothing says "this one was eaten/destroyed". | high |
| Every animal has the model of our own species | **Revised after the first `coop dump` (host, 2026-10-24): the 7 species of the scene all live in the stock `Spore_Content.package`, so the guest has them too; "models missing on the guest" is NOT the cause for this scene** (the model transfer stays as a safety net for custom creatures). Still open: either the species definition did not reach the mirror code (then the guest log says `has no model here` and `coop status` shows stand-in models > 0), or the game builds animals from the herd's species, not from the profile we pass. Needs the friend's `CoopMod.log` (`world:` lines) and `coop dump`. | species location: verified; cause: unknown |
| Nests are missing on the guest | Nests of the creature stage are made by the game at runtime, not stored in the save we send; and we do not replicate them. | medium (check with `coop dump` on both) |
| Host sees the guest sliding | That test ran a build before the walking puppet (S3) or on one side only. Retest with the current build; not a design problem. | medium |
| Only walking is animated; no eating, singing, hitting, no HP | We stream positions only. Animations, abilities, damage and health are events of the game that nobody captures. | high |

Common root: **we mirror a snapshot of positions, but the game's world changes through events** (create, destroy, damage, eat,
play animation). The playbook's rule: "copy results, not events" for story, and "capture the primitive at the write and apply
it directly on the other side" for everything else.

## 2. Principles (from the hub, adapted to Spore)

1. **Measure before building.** We still have no real dump of what exists in a creature stage on two machines. The first step
   is a whole-world dump and diff (lessons 34), not another feature.
2. **One authority per kind of thing**, written down: world objects, animals and their lifecycle = host; each avatar's
   movement/abilities = its owner; damage/eating outcomes = host decides, owner of the target is told.
3. **State for what is continuous, events for what happens.** Continuous: avatar and animal transforms (the existing
   `coop_core.h` snapshot/delta path). Events: create, destroy, damage, heal, eat, animation/ability started, death,
   nest placed, egg hatched. Events are reliable and ordered (channel 3) and carry exact ids.
4. **Capture at the primitive, not by polling.** The engine funnels creation and destruction through
   `GameNounManager::CreateInstance / DestroyInstance / CreateHerd / CreateNest`, damage through `cCombatant::TakeDamage`,
   animation through `cCreatureBase::PlayAnimation / PlayAbility`. Detour these on the host (and on the owner of an avatar),
   emit an event, and apply it on the other side **through the same primitive with an echo guard** so the receiving side does
   not send it back and does not run the game's own follow-up logic twice.
5. **Never fall back silently.** A missing species shows a visible placeholder and a log line plus a counter in telemetry;
   a PASS with zero comparisons is not a pass.
6. **Identity of objects that exist on both sides from the save.** Host and guest start from the same `Game0`, so plants,
   rocks and most static objects exist on both. Do not stream them: give each a stable key
   `(noun, species/model, position rounded to 0.25 m)`, send one **manifest hash per noun type**, and on mismatch a diff
   (keys to remove, keys to add). Everything created later gets a host `netId` through the event path.
7. **Every fix with a test and a mutation twin; every desync becomes a regression test** (`coop_core.h` tests today;
   the dump diff below for the game).

## 3. Architecture

```
HOST game                                          GUEST game
 detours: CreateInstance/DestroyInstance/           mirrors (herd of 1 + transform) for moving things,
  CreateHerd/CreateNest/TakeDamage/PlayAnimation    objects created/destroyed by events, no local AI/spawn
        |                                                   ^
        v  events (reliable, ch 3)  +  state (unreliable, ch 2)  +  manifests/digests (ch 3, 1 Hz)
   Replication core (coop_core.h)  ------------------>  Guest world table
        ^                                                   |
        +----- requests (guest bite/eat/pickup, ch 3) ------+
```

Layers, each with its own test:
- **L0 truth**: `coop worlddump` (both machines) writes `WorldDump_<role>.txt`: per noun type the count, the keys of section 2.6,
  the species keys, HP, nests; `scripts/worlddiff.ps1` compares two dumps; the guest also sends a per-noun manifest hash to the
  host once per second and the host logs the verdict (`world: OK` or `DIFF noun X host N guest M`).
- **L1 objects**: manifest + diff for static objects; create/destroy events for dynamic ones. Fixes "eaten things stay".
- **L2 species and models**: before a species is used on the guest, the guest must hold its creature data. Host sends the
  species key; the guest asks `SpeciesManager.GetSpeciesProfile(key)`; if null, it requests the creature file from the host
  (reliable file channel that already exists for the save) and installs it in the place the game reads (S8). No fallback to our
  species except an explicit grey placeholder.
- **L3 movement and animation**: positions as today (mirrors move with the puppet motion code of `game/PuppetMotion.cpp`);
  `PlayAnimation(id)` / `PlayAbility(index)` on a creature (any, including avatars) is captured and replayed on its mirror/puppet.
  Locomotion keeps coming from speed, not from events.
- **L4 combat and health**: `TakeDamage` on the host is the only place health decreases. The guest's attack is a request
  (attacker netId, ability, target netId); the host applies it, and health goes out as state plus a `Damage`/`Death` event for
  the animation. Eating a fruit/plant/corpse is the same pattern (S5). HP of both avatars is shown on both sides through the
  puppet's `cCombatant` and a low-rate stats message (hunger, energy, DNA points).
- **L5 per-player progress** and **stage barrier**: as in `DESIGN.md` (sidecar, editor barrier). Later.

## 4. Build order (a gate between steps: do not start the next before the check passes on two real PCs)

| Gate | Work | Pass condition |
|---|---|---|
| G0 | `coop worlddump` + `worlddiff` + manifest hash telemetry; run `coop dump` on both PCs in the same save | we know exactly which nouns differ (counts per type) and where species keys come from |
| G1 | Static objects: manifest diff and destroy events from `DestroyInstance` (eating fruit/plants shows on the other side) | eat 5 fruits on one side: the other side's manifest equals, nothing reappears |
| G2 | Species/models (S8) and nests (host nests and the guest's own nest, `CreateNest` event) | the friend sees animals with their real models and the nests; `species fallback = 0` in telemetry |
| G3 | Animation events (`PlayAnimation`/`PlayAbility`) for avatars first, then animals; walking from PuppetMotion | a person watching sees eat, sing, bite animations on the other PC |
| G4 | Damage, death, eating outcomes, HP/stats; respawn at own nest | scripted fight: equal health on both sides after each step |
| G5 | Sidecar, editor barrier, stage change | per `DESIGN.md` M5/M6 |

## 5. Spikes needed now (small, each ends in a log line)

| # | Question | How |
|---|---|---|
| S12 | Which nouns differ between host and guest after the world transfer? | G0 dump/diff on two PCs |
| S13 | Where does an NPC species' data live (resource key, group/type, file or in-memory generated)? Can the guest load it from a transferred file? | host: log `mSpeciesKey` of each animal with `ResourceManager` lookup result; try `GetSpeciesProfile` for each on the guest |
| S14 | Is eating a fruit/plant `DestroyInstance`, a state change, or a script? | log detours of `DestroyInstance`, `SetHealthPoints`, plant fields while eating |
| S15 | Do `PlayAnimation`/`PlayAbility` fire for the avatar's own eat/sing/bite, and does calling them on a puppet play the clip? | detour both, log ids; call them on the puppet from the console |
| S16 | Can `TakeDamage` be detoured and called on a mirror? | log attacker/target/amount; apply on the guest |

## 6. Work split (changes the earlier proposal in `HANDOFF_B.md`)

- **ArKoWol + Claude (world truth and lifecycle)**: G0, G1, S12, S14, the event path (detours, echo guard), manifests, telemetry.
- **exxecute + Claude (what the other player looks like and does)**: G2 part species/models (S13, S8), G3 animation events (S15),
  walking (done), the guest's own creature model.
- **Together**: nests (G2), combat (G4, S16).
- The old `World.cpp` (animal mirrors) stays as the L3 movement carrier; its species fallback must be replaced by the placeholder
  rule in G2, and its stray-removal stays until G1 makes the guest's own spawning irrelevant.

## 6a. Measured facts (first real dump, host PC, creature stage of the owner's save)

- 4188 nouns: 101 CreatureAnimal, 123 Herd, **101 Nest** (one per herd, spread over the planet), 74 Fruit, 1443 FruitGroup,
  5 GamePlant, 1 Egg, 29 Rock, 1712 Obstacle, 534 GameplayMarker, 21 InteractiveOrnament, 17 HitSphere, 1 Player.
- The 7 creature species of the scene are stock (`Spore_Content.package`, type crt 0x2B978C46, group 0x40626200); the avatar's
  species was the owner's own creation. So the NPC models exist on every GOG install.
- `coop modeltest` works: read a `bem` record (2890 bytes), wrote a 3022-byte DBPF, registered it, the profile is found.

## 6b. What the other mod (WoodyWizard/SporeCoop, MIT) taught us (read 2026-10-24)

- **The save holds only creature keys, never bodies.** Bodies (`bem` 0x1A99B06B; `crt` type 0x2B978C46, group 0x40626200)
  live in `EditorSaves.package` and the Sporepedia cache `Pollination.package`. A guest without them cannot build the
  species: this is our "everybody has our model" bug. They transfer the `bem` records, write a DBPF package and register it
  (`new Resource::DatabasePackedFile(path)`, `ResourceManager.RegisterDatabase`, priority 1000+). **G2 is built the same way.**
  Keep the original key for NPC species (their remap is for players' own creations whose ids collide).
- Pitfalls to respect: every creature needs a herd (null herd = AV at 0xD41A20 every frame); `SetSpecies` during world
  load crashes (wait ~3 s); a dead mirror flies away unless pinned to the corpse position each frame; `TakeDamage`'s 4th argument
  is a push vector (pass 0,0,0); `CreateHerd` with an own species fills itself up to 5 members unless the target size is 0;
  a save written while an editor opens cannot be loaded.
- Combat pattern: guest hit on a mirror = request to the host, the host applies it and credits the kill to its own avatar
  (the game pays rewards only to the real avatar); a hit on the guest's puppet is forwarded to the guest.
- NPC interest radius 110 m (+20 m hysteresis), 10 Hz state: use when animal counts grow.
- They do **not** cover eating, plants/fruit, nests as objects, or a list of animation ids: those stay our spikes (S14, S15).

## 6c. Minimum for playable creature-stage co-op (owner's list, 2026-10-25; status in brackets)

1. Session: connect, world transfer, auto restart and join, console. [works]
2. Same world: animals with model, size, age, HP, alpha [works, to re-check]; nests [being fixed]; plants not flickering [fixed, to check];
   fruit at the same places and gone when eaten [not done: fruit differs per machine, 6 of 74 match]; eggs and hatching [partly];
   corpses that both can eat [not done]; **species relations: when enough animals of a species are killed the species is extinct for
   both players (also befriended/allied state)** [not done].
3. Players see each other: walking puppet [works]; walking backwards/sideways/running [not done]; each player's own creature model
   [not done, the puppet is built from the local avatar's species]; real HP and size of the puppet [to check]; action animations [partly].
4. Combat: guest damage as request to the host [request works]; host damage and deaths [works]; PvP, death and respawn at own nest,
   no rewards [not done]; hit/get-hit animations of mirrors [not done].
5. Progress: separate DNA / brain / parts per player, saved in the host's save [not done]; stage change when both are ready [not done].
6. Stability: no crashes on connect, in combat, on restart; leaving and returning guest. [main crashes fixed]

Species extinction (item 2): the game keeps it in `cSpeciesRelationshipManager` (SporeCoop notes for their exe: `Get` 0xB3D590, `GetRelationship`
0xBA40C0, `ApplyEvent` 0xBA49E0, event 0/5 = killed, 3 = impressed; relation values 6 allied, 5 extinct; tuning: 3 kills make a species
extinct, 3 impressed make it an ally). Kills by the guest are applied by the host with its own avatar as the attacker, so the host's table
counts both players' kills; the plan is to send that table to the guest (SporeCoop: every 10 s, guest writes it every 0.5 s) and to
remove the herds of an extinct species. The addresses must be checked against our exe (they were found on another build).

## 7. Risks that remain

- Spore may not let us call creation/destroy primitives from outside its own flow at arbitrary times (crash on
  `cCreatureAnimal::Create` without a herd is an example). Each primitive gets a spike before it is used.
- Creature data of NPC species may be generated and not exist as a file (S13). Fallback: send the species' genome/parts list if
  the API exposes it, else a named stand-in model; this must be decided with the owner when S13 is known.
- The simulation is not deterministic: any local random thing (new fruit, respawns) must be blocked on the guest or it
  drifts; G1 measures how much of that happens before we block it.
