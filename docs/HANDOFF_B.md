# Task split after the first two-PC test (2026-10-24)

Read `CLAUDE.md` (rules, hard-won facts) and `docs/DESIGN.md` (plan) first. Pull before you start.

## What the test showed
1. **Different worlds.** Both players are connected and the host's `Game0` is installed on the guest, but each side still runs its own
   simulation: its own NPC creatures, herds, plants, nests. The guest only receives the host avatar. Cause: nothing replicates or
   neuters the world yet (DESIGN S1, S2, M2).
2. **The remote creature "drifts"** instead of walking: the puppet is moved with `Teleport`, so it slides with no walking animation
   (DESIGN S3, M3).

## Split (the two tracks touch different code; keep it that way)

### Track A: ArKoWol + his Claude: the shared world (S1, S2, M2)
Goal: after the guest joins, the guest sees the host's animals, not its own.
- `coop dump` in a real creature stage on both machines: what entities exist, counts per type (S1).
- Game adapter `mod/CoopMod/game/`: enumerate the host's entities into `coop::HostWorld` (`core/coop_core.h`), send
  Delta/Full/events on a new channel 2/3, build mirrors on the guest from `GuestWorld` callbacks.
- Guest side: skip `NPCTickAI` for every non-puppet creature, block local spawning, destroy local NPC herds at world-ready (S2).
- Files: new `game/` and `net/` code, the packet/channel parts of `dllmain.cpp`. Bump `kProtocolVersion`.
- Done when: after 10 s the entity counts per type are equal on both machines (telemetry line in the host's `CoopMod.log`).

### Track B: exxecute + his Claude: avatars that walk, and the start flow (S3, S8, S7)
Goal 1 (do this first, it is what the tester saw): **the remote player's creature walks, runs and idles instead of drifting.**
- Find how the game animates a creature (`cCreatureBase`, `cCreatureAnimal`, `IAnimWorld`/animation ids; DESIGN S3). Read the local
  avatar's animation state / speed, send it in `StatePacket`, play the matching walk/run/idle on the puppet.
- Cheap fallback if the animation API fights back (DESIGN R5): derive the state from the speed between two received positions and
  start the standard walk/idle animation; also turn the body along the movement direction.
- The `coop anim` switch and the `WalkTo` crash notes in `CLAUDE.md` are the starting point.
- Done when: a person watching the other PC sees the puppet's legs move while it moves and stop when it stops.

Goal 2 (after 1): the other player's **own creature model** on the puppet (S8: send the `.creature` file or species data, register it,
`CreateHerd` with it). Then the start flow without the restart and the cell stage (S7), the sidecar save (S11).
- Files: the puppet code in `dllmain.cpp` (`NetUpdate`, puppet creation, `StatePacket`), new `game/` files for S7/S8.
- If you change `StatePacket`, bump `kProtocolVersion` and write it in the changelog (Track A reads the same packet).

## Rules so we do not collide
- `dllmain.cpp` is shared: keep edits small and local, pull right before and push right after. New logic goes into new files.
- Track A owns the entity table/channels; Track B owns the puppet and `StatePacket`. If you need something from the other
  side, write it in `CHANGELOG.md` / a commit message, do not edit their part.
- Every change: changelog entry under `## Unreleased`; new game facts into "Hard-won facts" in `CLAUDE.md`.
- Test on two real PCs; use the remote console (`console.bat "coop dump"`) and read `CoopMod.log`. Paste verdict lines, not logs.
- One release at a time, only after telling the other developer.
