# Spore co-op mod: project guide (read this first)

This file is for people and AI assistants (Claude Code) working on the project. Two developers, each with their own
Claude, share this repo. The documents are how we understand each other: keep them accurate and in the places below.

## Read first, in this order
1. This file.
2. `CHANGELOG.md`, the `## Unreleased` section (what changed since you last looked).
3. `docs/STATUS.md` (what works, what is in work, who does what).
4. `docs/PLAN.md` before gameplay work; `docs/DECISIONS.md` before you question the direction.
5. `docs/FACTS.md` before you touch code that talks to the game, ModAPI, Steam or saves (read it all once).
6. `docs/ARCHITECTURE.md` before you change the mod's code or protocol; `docs/TESTING.md` before you test.
7. The feature document in `docs/features/` for the area you work on.
`docs/README.md` lists every document with what it holds.

## Rules for every change
1. **Pull before you start** (`git pull`), commit small, push often.
2. **Record every change in `CHANGELOG.md`** under `## Unreleased` (what changed, why, and who/what to be careful of).
   `scripts\publish.ps1` turns the Unreleased section into the release notes and moves it under the new build tag.
3. If the packet layout, save transfer or anything both players must agree on changes, **bump `kProtocolVersion`** in
   `mod/CoopMod/dllmain.cpp` and say so in the changelog. Players with different versions refuse each other on purpose.
4. New knowledge about the game or ModAPI (a crash cause, an address, an API that works or does not) goes into
   `docs/FACTS.md`, not only into the changelog.
5. Never commit game files, saves, `steam_api.dll` (Valve's file), logs, or anything in `local/` (it is ignored).
   Commit author email must be the GitHub noreply address, not a personal one.
6. Only one person publishes a release at a time (`latest` is simply the newest release). Tell the other before you do.
7. Do not run destructive or outward-facing actions (publishing, deleting, force-push) without the owner's OK.
8. **Every in-game window of the mod follows `docs/features/UI_STYLE.md`** (the look of the CO-OP window: glossy light
   plate, orange outlined title, glossy pill buttons with hover). Reuse the drawing helpers of `game/UiDraw.cpp`.

## Documents: where to write what
| What happened | Write it into | Not into |
|---|---|---|
| you changed anything in the repo | `CHANGELOG.md` (one bullet: what, why, what to watch) | - |
| you learned a fact about the game, ModAPI, Steam, saves, tools | `docs/FACTS.md`, in its topic section, with a source tag | the changelog only, code comments only |
| the owner decided something | `docs/DECISIONS.md` (new numbered line, mark what it replaces), then update `docs/PLAN.md` | a new plan file |
| you started or finished a piece of work, or ran a two-PC test | `docs/STATUS.md` | the plan |
| your code change makes the description of the code wrong | `docs/ARCHITECTURE.md`, in the same commit | - |
| the way we test changes | `docs/TESTING.md` | - |
| a new large feature needs a design | a new `docs/features/<name>.md` plus a row in `docs/README.md` | the plan, the changelog |
| a new player-visible command or behaviour | `README.md` (player part) and `package/README.txt` | - |
| personal notes, scratch, drafts, research dumps | `local/` (ignored) or the chat | `docs/` |

Rules for every document:
1. **English**, plain words, short sentences. Tables for comparisons and lists of states. Absolute dates (`2026-10-25`), never
   "today" or "yesterday".
2. **Every file in `docs/` starts with a header block** right under its title:
   ```
   > Status: draft | agreed | reference | live · Owner: <name> · Updated: YYYY-MM-DD
   > Read when: <the situation in which someone must read it>
   ```
   Change `Updated` whenever you change the content.
3. **One thing lives in one place.** Other documents link to it (`FACTS.md` section 6) instead of repeating it.
4. **There is only one live plan** (`docs/PLAN.md`). When something replaces a part of it, change it there and log the
   decision; do not leave the old version anywhere. Git keeps the history.
5. **Facts carry a source tag**: *(verified YYYY-MM-DD)*, *(SDK)*, *(SporeCoop)*, *(guess)* (meanings in `docs/FACTS.md`).
6. **Never paste logs or Steam IDs** into documents: paste the verdict lines only.
7. **Do not create new documents** (in `docs/` or the root) except `docs/features/<name>.md`; anything else needs the owner's
   OK. Do not rename or move another developer's document without telling him in the changelog.
8. Update the documents **in the same commit** as the change that makes them necessary.

What every entry must say:
- **Changelog entry**: whether it was tested (`tested on 2 PCs` / `tested on 1 PC` / `not tested`), whether the protocol
  changed (`protocol N`), and what the other developer must do, if anything.
- **`STATUS.md` item**: its state and the date it was last checked.
- **`PLAN.md` part**: `verified` once it was seen working in the game; everything else counts as not verified.
- **`FACTS.md` fact**: its source tag; a `guess` that gets confirmed is rewritten with `verified YYYY-MM-DD`.

Keep the files small (git keeps the history, so deleting is safe):
- **Changelog**: at most 5 lines per entry: what, why, what to watch. Investigations and details go into `FACTS.md` or
  `ARCHITECTURE.md`; the entry links there.
- **`STATUS.md`**: the test log keeps the last 10 rows; finished work leaves "who does what"; an item stays "works" only
  while it is true.
- **`PLAN.md`** describes only what is still to do: a finished step is removed (how it works goes to `ARCHITECTURE.md`, its
  state to `STATUS.md`).
- **`FACTS.md`**: a wrong fact is corrected or deleted (no "we used to think" notes); facts about code that no longer exists
  are deleted.
- **`ARCHITECTURE.md`** describes only the current code: removed code, removed text.
- **`features/`**: when a feature is built, its document is cut to what is still open, or deleted.
- **`DECISIONS.md`** only grows: a replaced decision stays as one line marked "replaced by Dx".
- **Size limits**: `CLAUDE.md` at most 150 lines, any file in `docs/` at most 300 lines. If an edit would pass the limit,
  clean the file first.
- **Whenever you touch a document, remove what your change made out of date.**

## What this is
An experimental co-op mod for Spore: Galactic Adventures, GOG build (exe `SporebinEP1\SporeApp.exe`, 3.1.0.29, unencrypted).
Two players share the host's world in the creature stage; the plan covers every stage. How it works: `docs/ARCHITECTURE.md`.
What works today: `docs/STATUS.md`.

## Workflow
1. `.\scripts\setup.ps1 -GameDir "<your Spore folder>"` once.
2. `git pull`, read the Unreleased section of the changelog and `docs/STATUS.md`.
3. Edit the code under `mod/CoopMod` (new logic goes into new files, `dllmain.cpp` is shared: keep edits there small).
4. Run `launch_spore_mod.bat`: it builds, closes a running game (the DLL is locked while it runs, hence `staging`), installs
   the build, starts the game windowed and opens a console connected to it.
5. Test as in `docs/TESTING.md`; check `test\SPORE_A\SporebinEP1\CoopMod.log` and `spore_log.txt`.
6. Update `CHANGELOG.md` and the documents of the table above, commit, push. To ship: build, then `.\scripts\publish.ps1`
   (tell the other developer first). Players run `update.bat`.

## Repository layout
```
launch_spore_mod.bat    double-click: build, install, start the game, open the console (scripts\launch.ps1)
console.bat             remote game console (scripts\console.ps1)
update.bat              check GitHub for new commits / a new release and ask (package\coop_update\update.bat)
CLAUDE.md CHANGELOG.md README.md
docs/                   STATUS, PLAN, DECISIONS, ARCHITECTURE, FACTS, TESTING, features/ (index: docs/README.md)
mod/CoopMod/            the mod (C++), build.bat, make_assets.ps1
core/  tests/           replication core (plain C++, no game headers) and its tests
assets/                 files packed into Data\CoopMod.package (splash.png)
scripts/                developer tools (PowerShell)
package/                what players get, laid out as in coop_package.zip
local/                  ignored personal notes
```
Not in git: `SPORE/` (game), `test/SPORE_A` (test copy), `downloads/` (SDK, loader), `coop_package*`. Each tool is described
in `docs/ARCHITECTURE.md` section 9.
