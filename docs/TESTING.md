# How we test

> Status: agreed · Owner: both · Updated: 2026-10-31
> Read when: before you test a change and before you call anything "done". Results go into `STATUS.md` section 5.

## 1. The two-PC test (the real test)
Both developers can test together on two real PCs almost every day; a change that touches sync is only done when it was seen
there. One PC alone proves only that nothing crashes.
1. Both: `git pull`, then `launch_spore_mod.bat` (builds, installs into `test\SPORE_A`, starts the game windowed, opens the console).
2. Host: load a game into the creature stage, `coop host`, send the printed Steam ID (or the friend uses `coopjoin last`).
3. Guest: stay in the main menu, `coopjoin <id>`; after "world received" the game restarts, joins again by itself; press Continue.
4. Do the scripted steps of the change (write them in the changelog entry), then compare both views.
5. Read `test\SPORE_A\SporebinEP1\CoopMod.log` (our log) and `spore_log.txt` (ModAPI). Paste verdict lines into `STATUS.md`,
   never whole logs (they contain Steam IDs).

Never run the guest's game minimized or fullscreen in the background: a minimized Spore runs no frames (`FACTS.md` section 2).

## 2. Tools that exist
| Tool | Use |
|---|---|
| `coop status` | connection, packets, puppet, mirror and stand-in counts, animations sent/played |
| `coop dump` + `scripts/worlddiff.ps1` | dump what exists on each machine, compare the two dumps |
| Telemetry (`kMsgTelemetry`) | the guest's counts in the host's log |
| `coop echo` | one-PC puppet test: a copy of you 0.4 s later, 4 m to the side |
| `coop motionlog`, `coop fps` | puppet motion and frame-rate diagnostics |
| `coop actionlog` | log every ability, creature effect and voice that is sent and replayed (`action:` lines) |
| `coop facing`, `coop run` | the puppet's steps back / sideways (`always`, `steps`, `off`) and its speed matching (on / off) |
| `tests\run_core_tests.bat` | the replication core with a fake world, and the co-op launch folder logic (`core/coop_launch.h`) and the world-transfer manifest (`core/coop_manifest.h`: diff, build, verify, cleanup) on temporary folders; no game, seconds |
| `crash:` lines in `CoopMod.log`, link map | find a crash (`FACTS.md` section 4) |

## 3. Rules
1. **Oracles are never loosened to make a test pass**: no duplicate netIds; every guest entity has a host id; counts per type
   and positions agree within tolerance; no guest-only NPC; health equal after each event; exactly one avatar per player on
   each side.
2. A pass with zero comparisons is not a pass. A missing species is a counted stand-in, never silent.
3. Every fix of a desync comes with a check that would have caught it, and the check is run once with the fix turned off
   (it must fail).
4. Logic that needs no game (ids, snapshots, deltas, ordering) goes into `core/` and gets a test in `tests/`.

## 4. Planned, not built
- A per-second side-by-side desync verdict of both machines in the host's log.
- Scripted scenarios run through the remote console on both PCs that print `RESULT: PASS` / `FAIL`.
- A flight recorder (time, netId, state hash) that finds the first divergence.
