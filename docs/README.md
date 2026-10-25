# Documents

> Status: reference · Owner: both · Updated: 2026-10-25
> Read when: you look for where something is written, or where to write something. The rules for writing are in
> `../CLAUDE.md`, section "Documents".

| File | What is in it | Read it when | Write into it when |
|---|---|---|---|
| `../CLAUDE.md` | rules for every change and for documents, reading order, workflow | always first | a rule changes (owner's OK) |
| `../CHANGELOG.md` | every change, newest first | after `git pull` | you change anything in the repo |
| `../README.md` | for players: install, play, commands, problems; short developer start | you need the player view | something players see changes |
| `STATUS.md` | what works, what is in work, who does what, two-PC test log | start of every session | you start or finish work, after a test |
| `PLAN.md` | the only live plan, every stage, order of work | before gameplay work | the owner changes direction (with `DECISIONS.md`) |
| `DECISIONS.md` | the owner's decisions with dates, what replaced what | before you question the direction | the owner decides something |
| `ARCHITECTURE.md` | how the mod works now: files, frame, protocol, transfer, console, puppet, sync rules, tools | before you change code | your change makes it wrong |
| `FACTS.md` | hard-won facts about the game, ModAPI, Steam, saves, creatures, combat, other stages | before game-facing code | you learn something about the game or the tools |
| `TESTING.md` | how to test on two PCs, tools, test rules | before you test or call anything done | the way of testing changes |
| `features/JOIN_FLOW.md` | joining from the main menu instead of the console (exxecute) | you work on joining or the menu | that feature's design changes |
| `features/UI_STYLE.md` | how every in-game window of the mod looks and is made (exxecute) | you make or change a window | the style changes |

A new large feature gets its own `features/<name>.md` and a row here. Nothing else is added to `docs/` without the owner's OK.
