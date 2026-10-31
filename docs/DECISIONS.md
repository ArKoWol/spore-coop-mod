# Decisions of the owner

> Status: agreed · Owner: ArKoWol · Updated: 2026-10-31
> Read when: before you question the direction or propose something that changes it. Add a line here for every new
> decision of the owner; never edit an old line except to mark it replaced.

Newest first. Each line: what was decided, and which earlier decision it replaces. `PLAN.md` always follows the newest
decisions.

## 2026-10-31: join flow (`features/JOIN_FLOW.md`)
| # | Decision | Replaces |
|---|---|---|
| D22 | Old installs whose `Game0` is a host's world: the menu asks once whether to restore the own save from `Game0.coopbackup*`; nothing is deleted | - |
| D21 | The guest starts joining from a CO-OP button in the main menu; the CO-OP star goes away | the CO-OP star of 2026-10-25 |
| D20 | A friend's world is played in a "co-op launch": swapped in place of `Game0` for one launch from `Games\Coop\<id>`, the own `Game0` restored at every start; entering costs one restart (path C parked) | the permanent swap into `Game0.coopbackupN` |
| D19 | The join flow is ArKoWol's area (with Claude); exxecute no longer works on it | D5 for the start flow |

## 2026-10-25: plan for every stage (long chat with the owner)
| # | Decision | Replaces |
|---|---|---|
| D18 | Docs are written in English and follow the structure and the rules in `CLAUDE.md` ("Documents") | - |
| D17 | Space: one shared empire (the civilization winner's), two ships; money, upgrades, missions, colonies, trade, terraforming shared | - |
| D16 | Civilization: two civilizations; PvP between the players **by war only**; whoever owns every city wins and his civilization becomes the space empire | D11 for the civilization stage |
| D15 | Tribe: two tribes on one planet, allied from the start (the friend's tribe counts as befriended); a tribe befriended or conquered by either counts for both | - |
| D14 | Creature: each player has his own species relation table; the two players' species are allies (maximum friendship); extinction is shared | D12 for the creature stage |
| D13 | **Checkpoints**: an important sub-stage (cell growth, nest move, stage change) starts only when both reached it | D4 (refines it) |
| D12 | Diplomacy with NPCs is shared by the team (tribe, civilization, space) | - |
| D11 | No PvP after the creature stage, the players are allied | replaced by D16 for the civilization stage |
| D10 | Only the host starts a stage change, and it sends **both** players into the editor | D4 (refines it) |
| D9 | Each player edits on his own inside a stage; if that breaks something, both go into the editor together | - |
| D8 | From the tribe stage on: two allied factions with their own species, if the game allows it; else one faction for both | - |
| D7 | Cell stage is played **together in one pool**, separate cells, separate DNA | D2 |
| D6 | The host simulates NPCs; each player simulates his own things; the actor decides the result of his own action. NPC "brain swap" between players is not part of the plan | the "host decides everything" model of 2026-10-24 (refines it) |

## 2026-10-24: design v2.1
| # | Decision | Status |
|---|---|---|
| D5 | Work split: ArKoWol + Claude = world truth and lifecycle (dump, objects, events, telemetry); exxecute + Claude = how the other player looks and acts (species/models, animation, walking) and the start flow | current; the start flow replaced by D19 |
| D4 | Stage change only when both reached the goal; both go through the editor; the game continues when both are ready | refined by D10, D13 |
| D3 | One world owned by the host; each player has his own creature and progress (DNA, brain, parts); the host's save holds both players (sidecar) | current |
| D2 | A new co-op game skips the cell stage | **replaced by D7** |
| D1 | Other decisions of v2.1, all current: PvP in the creature stage, a killed player respawns at his own nest with no reward and no corpse; the guest spawns next to the host with his own nest; no pack ("socialise" followers) in v1; a guest who leaves disappears and gets his progress back on return; creature stage first until it is solid; tests on two real PCs from the start; private use between friends; in-game texts English first; two players only, no host migration | current |

Also 2026-10-25 (exxecute): every in-game window of the mod follows `features/UI_STYLE.md`.
