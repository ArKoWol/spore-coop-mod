# Co-op join flow: from the main menu into a friend's world

> Status: draft · Owner: exxecute · Updated: 2026-10-25
> Read when: you work on hosting, joining, the main menu or the world transfer. Spike results are also in `../STATUS.md`.

Proposal for discussion with the owner. Every "spike" below is a small experiment that ends in a log line; the design changes
if a spike fails.

## 0. Goals (from the second developer)

1. Hosting and joining happen **in the game's main menu**, not by typing Steam IDs into a console.
2. The friend's world must **never look like "Continue" of your own progress**: it is visibly the friend's world, and your own
   saves are never touched by it.
3. A player who starts the mod **for the first time** gets through it without reading a manual.
4. **Friends list**: previous sessions, plus Steam friends who are running the mod right now.
5. The terminal (`console.bat`) keeps working as a second way to do the same things (developers, fallback).

## 0a. Spike result (2026-10-25): a world can be added to the open universe

Tried on one PC in the main menu (`game/MenuWorlds.cpp`, `coop addcenter`): a free star got `kStarFlagSaveGame`, the stage and save
version of an existing save, a planet named `test1` with that save's terrain key, a copy of the `.spo` named `test1.spo`, and was
pushed into `StarManager.mSavedGameStarRecords`. **The galaxy showed it at once** with the normal card ("test1, Creature Stage",
date, creature). No restart, no UI rebuild (calling `GlobalGGEUI::InitializeUI` again is not needed and crashes the game at exit).

This opens **path C** below: the friend's world as one more star in your own galaxy, marked as co-op, removed when you leave.

**Built (2026-10-25, tested on 2 PCs):** the CO-OP star near the centre at every start and our window next to its card
(`game/CoopMenu.cpp`): remembered hosts, Join by Steam ID (clipboard), the real join (path A: download, restart, install),
then the star is a copy of the host's newest creature save and the card's own Play (hidden until ready) enters it. J6 passed; **J8 failed** and the star vanishes after leaving a game without saving (both explained in
`../FACTS.md` section 6, tracked in a GitHub issue). Ideas for the star: find why a save on our planet crashes the load
(fixes J8 too); write our planets into `planetRecords.pkp` before the start and remove them after exit; leave the co-op world
through a quick restart; a CO-OP button in the left menu instead of a star (needs what the card's Play prepares).
Still unknown (next spikes): **J6** Play / `LoadGame` on such a star loads the copied save (creature, planet, terrain); **J7** the
game keeps or drops the star when it saves the galaxy (we want it dropped, or removed by us before every save); **J8** the
received `.spo` of another universe works on a star of ours (its planet and star ids belong to the host's galaxy).

### Path C: the friend's world as a star in your galaxy (no restart, if J6-J8 pass)
1. Guest presses Join; the host sends its `.spo` (and planet record data), not the whole `Game0`.
2. The mod picks a free star near the galaxy centre (visible), makes the planet and save star as in the spike, named
   "CO-OP <friend>", and pushes it into the saved-game list. The menu shows it at once; our overlay marks it in the co-op colour.
3. The guest clicks it (or the mod loads it), plays; the host's world is canonical, the local copy is a cache.
4. On leave / exit / next start: the star is taken out of the list and its `.spo` deleted, so it never stays among your own saves.
If J6-J8 fail, path A/B (separate universe per friend) stays the plan.

Results of the same day: **J6 passed** (Play on the test star loaded its copied save into the creature stage). Play does not call
`LoadGame`; it is `SetActiveModeByName("LoadGame")` + `kMessageLoadGame` (a place to hold the load, if ever needed). The
selected star is readable at `[GlobalGGEUI + 0xFC] + 0x7C`, so the flow becomes (idea of the second developer):
1. The CO-OP star stands near the centre at every start.
2. **Selecting it** (not Play) opens our window: friends hosting, recent sessions, Steam ID.
3. The chosen friend's world downloads into the CO-OP star's save while the window shows progress.
4. The player presses the game's own **Play**; the load is the normal one. No hook on the load at all.
Still open: J7 (galaxy save keeps the star?), J8 (a save from another universe on our star), J5 (our window over the menu).

## 1. What is wrong today

| Today | Why it hurts |
|---|---|
| Guest types `coopjoin 7656...` in a console | hidden, needs the Steam ID, nothing tells a new player it exists |
| The received world **replaces `Game0`**, own save moves to `Game0.coopbackupN` | the host's world shows up as *your* saves; your own progress seems gone; backups pile up (8 on this PC) |
| After the restart the guest presses **Continue** | looks exactly like continuing your own game |
| No list of who is playing | you have to ask the friend in chat whether they are hosting |
| Version mismatch is found only after connecting | "nothing happens" for the guest |

## 2. Key ideas

### 2.1 A separate universe per friend, not a replacement of `Game0`
The game reads saves from the save area `Resource::SaveAreaID::GamesGame0` (`%APPDATA%\Spore\Games\Game0`), created at start
through `Resource::Paths::CreateSaveAreaDirectoryDatabase` (ModAPI `Resource/Paths.h`). A detour there, active only in a
co-op start, points it at

```
%APPDATA%\Spore\Games\Coop\<host steam id>\      the friend's world (a cache; the host's save is the real one)
%APPDATA%\Spore\Games\Coop\<host steam id>\coop_session.txt   host id, host name, planet name, received time, hash
```

- Your own `Game0` is never moved, renamed or overwritten. No more `coopbackupN`.
- The main menu of a co-op start lists **only** the friend's world, so it cannot be mistaken for your own saves.
- Rejoining the same friend reuses the folder; the host sends the world again only if its hash changed.
- Spike **J1**: log the start order (save area created, `StarManager` opens `stars.db`/`planetRecords`, intro starts, our
  `Initialize`), then redirect to `Games\Coop\test` and check that the galaxy **and** the slots come from there (if
  `StarManager` builds its own path to `Game0`, that path needs a second detour).

The game cannot switch universes while it runs (the galaxy is in memory and its databases stay open from the start: see
`../FACTS.md` section 6). So entering the friend's universe means **one start of the game in co-op mode**. Two ways to make that
start invisible, section 4.

### 2.2 Skip "Continue" entirely
After the co-op start the mod loads the friend's planet itself with `cGamePersistenceManager::LoadGame` (spike **J2**, same as
S7 in `../PLAN.md` section 10). The player sees: "Joining Artyom's world" screen, then the creature stage. No main menu in between, no
Continue button. If J2 fails, fallback: the main menu shows the single slot with our banner "CO-OP: Artyom's world, press
Play" (section 5.4).

### 2.3 "Who is playing" through Steam Rich Presence
- Every running mod sets Rich Presence keys (`ISteamFriends::SetRichPresence`, synchronous, no call results needed):
  `sporecoop` = `<protocol>|<state>|<planet>|<players>/<slots>` where state is `menu`, `hosting`, `guest`, `busy`.
- The guest reads them for every friend (`GetFriendCount(k_EFriendFlagImmediate)`, `GetFriendByIndex`,
  `GetFriendPersonaState`, `GetFriendGamePlayed` = AppID 480, `RequestFriendRichPresence`, `GetFriendRichPresence`), every
  few seconds while the co-op panel is open.
- Friends who run real Spacewar (also AppID 480) have no `sporecoop` key and are not shown as mod players.
- Steam lobbies (`ISteamMatchmaking`) are **not needed for v1**: they need asynchronous call results (`CreateLobby`,
  `RequestLobbyList`), i.e. `SteamAPI_ManualDispatch_*` or `CCallResult` glue. Keep them for public/stranger sessions later.
- Spike **J3**: the exports above exist in `steam_api_coop.dll`, a friend's key shows up on the other PC within a few seconds.

### 2.4 Invites through our own packet, not Steam's "Invite to game"
Steam's invite opens **Spacewar** for the friend (AppID 480), not Spore: unusable. Instead the host's mod sends an `Invite`
message over `ISteamNetworkingMessages` (channel 3) to a friend whose `sporecoop` key says the mod is running. The friend's
menu shows a toast "Artyom invites you to Leian  [Join] [Not now]". A friend without the mod running gets nothing; the
panel says "Artyom is not running the mod".

## 3. The host's path, step by step

```
Main menu
  [CO-OP] button (our UTFWin overlay, bottom-right; new-player hint the first time: "Play with a friend")
    -> Co-op panel, tab HOST
         Your worlds (from Game0's slots):
           Leian       creature stage   played 25.10 07:36   [Host this world]
           Ferteptak   cell stage       (co-op works in the creature stage only)  greyed
         [x] Friends can join without asking        (default on)
```
1. Host presses **Host this world**. The mod loads that slot (J2; fallback: "open Leian and press Play, hosting starts by
   itself when the creature stage loads").
2. When the creature stage is ready: hosting starts (today's `coop host`), Rich Presence `hosting|Leian|1/2`.
3. In-game status strip (top, small): `CO-OP HOST · Leian · waiting for a friend · [Invite]`. `[Invite]` opens the friends
   list (section 5.3) with an Invite button next to friends running the mod.
4. A guest knocks: if "without asking" is off, a popup "Valeria wants to join [Allow] [Refuse]". Allowed: the world is sent;
   strip shows `sending world to Valeria 42%`, then `Valeria is here`.
5. Host leaves (menu/quit/`coop off`): the guest gets "Artyom closed the world" and is taken back (section 4, step 7).

## 4. The guest's path, step by step

```
Main menu
  [CO-OP] button, with a badge when a friend is hosting or an invite is waiting
    -> Co-op panel, tab JOIN
         Invitations        Artyom invites you to Leian                        [Join] [x]
         Friends playing    Artyom      hosting Leian (creature) 1/2           [Join]
                            Max         in the menu, not hosting
                            Kirill      mod is older (protocol 9): ask him to run update.bat
         Recent sessions    Artyom      Leian, last played 25.10 07:36   offline
         [Join by Steam ID ...]        (advanced, for friends not on your Steam friends list)
```
1. Guest presses **Join** next to Artyom. Before connecting, from Rich Presence: protocol equal? slot free? host in the
   creature stage? Any "no" is shown on the row and the button is disabled, with the reason.
2. Connecting screen (our overlay, menu stays visible behind): `Connecting to Artyom...` then `Downloading Artyom's world
   1.2 / 3.1 MB` (progress from the file chunks that exist today). `[Cancel]` aborts and leaves nothing behind (the download
   goes to `Coop\<id>\.incoming`, renamed only when complete and verified).
3. When the world is complete: `Opening Artyom's world. The game restarts once, about 20 s.` Then the restart (path A) or,
   with the pre-start helper, no restart (path B, below).
4. The co-op start: the save area points at `Coop\<id>`; our splash after the EA logo becomes **"Joining Artyom's world"**
   (same splash code, a second picture or text); the mod auto-joins the host (today's `CoopAutoJoin.txt`) and loads the planet
   by itself (J2).
5. In the creature stage: the status strip `CO-OP · Artyom's world · ping 40 ms · [Leave]`, in a colour of its own (the host
   sees the same strip in another colour). Window title as today.
6. Leaving: **Leave** in the strip or in the pause menu: "Back to your own worlds? The game restarts once." The next start is
   a normal one (own `Game0`). Closing the game also ends the co-op start: the next normal launch is always your own world.
7. The host leaves or the connection is lost for 10 s: popup "Artyom closed the world / connection lost  [Wait] [Back to your
   worlds]". Wait = keep playing alone in the cached copy, rejoin automatically when the host is back (Rich Presence says so).

### Path A: restart (works with what we know)
The mod writes `Coop\<id>\coop_session.txt` + `CoopAutoJoin.txt`, closes the game with `WM_CLOSE` and starts it again (exists
today). With the splash text above and J2 the player sees "Joining Artyom's world", never the menu or Continue.

### Path B: no restart, world fetched before the game starts (launcher / terminal)
A small 32-bit helper (`coophelper.exe`, same `steam_api_coop.dll`, AppID 480) or the launcher itself:
`launch_spore_mod.bat join last` / `join artyom`, or the terminal menu (section 6):
1. checks Steam, the friend's Rich Presence and the protocol;
2. downloads the world into `Coop\<id>` (the host sees an ordinary guest; a `HelloFetchOnly` flag tells it not to wait for an
   avatar), shows the same progress in the terminal;
3. starts `SporeApp.exe -w` with `COOP_SESSION=<id>`; the mod redirects the save area at start, joins, loads the planet.
One start, no restart. Spike **J4**: Steam lets the helper and then the game use AppID 480 one after the other (the helper
must call `SteamAPI_Shutdown` and exit first), and the host accepts the same Steam ID again without resending (hash check).

Recommendation: build path A first (it reuses everything), add path B for the terminal and shortcuts. The menu always uses A
(the game is already running when the player clicks Join).

## 5. Interface pieces

5.1 **CO-OP button**: our UTFWin layout (`.spui` made in SporeModder FX, packed into `Data\CoopMod.package` by
`make_assets.ps1`), shown only in the main menu mode (spike **J5**: detect the main menu: `GameModeManager.GetActiveModeID()`
value there; load a layout with `UTFWin::UILayout`; a button click handler; text with `IText`).
5.2 **Co-op panel**: tabs HOST / JOIN as in sections 3 and 4. English text first (Spore's fonts and Russian are a later
question).
5.3 **Friends list**: one row per friend: avatar (optional, `GetSmallFriendAvatar` + `ISteamUtils::GetImageRGBA`, later),
Steam name, state from Rich Presence, last session from `CoopHosts.txt`, one action button. Sorted: invitations, hosting,
running the mod, recent, the rest hidden behind "Show all friends".
5.4 **Banner fallback** if J2 fails: in the co-op start's main menu a coloured strip "CO-OP: Artyom's world, press Play on
Leian. Your own worlds are safe and come back after you leave."
5.5 **First run**: the first time the main menu opens with the mod, a one-time hint next to the CO-OP button: "New: play
with a friend. Both of you need the mod; one hosts, the other joins." `Games\CoopFirstRun.txt` remembers it was shown.

## 6. Terminal (`console.bat`) equivalents

Same actions, numbered menus instead of IDs (console.ps1 already does Tab completion):
```
spore> coop menu
  1  Host the current world
  2  Join a friend:  a) Artyom (hosting Leian)  b) Max (menu)  c) recent: Kirill (offline)
  3  Invite a friend (while hosting)
  4  Leave the co-op world
```
`coopjoin last | <number> | <name>` stay. Outside the game: `launch_spore_mod.bat join <name>` (path B).

## 7. Edge cases and blockers

| # | Case | What the player sees / what we do |
|---|---|---|
| E1 | Steam not running / not logged in / offline mode | panel: "Start Steam and log in, then press Refresh"; no connect attempt |
| E2 | Same Steam account on both PCs | refused at once: "You are signed in as the same Steam user as the host" |
| E3 | Protocol mismatch | known before connecting (Rich Presence); row says who must update and how (`update.bat`) |
| E4 | Friend runs Spacewar, not the mod | not listed as a mod player |
| E5 | Host not in the creature stage (menu, editor, loading, other stage) | row: "Artyom is in the editor, wait"; Join disabled; enabled again by itself |
| E6 | Host's game minimized (fullscreen alt-tab runs no frames) | host's mod sets `busy`; guest sees "Artyom's game is paused"; launcher always starts `-w` |
| E7 | World already has a guest (1 slot) | "Artyom's world is full" |
| E8 | Guest is hosting while trying to join | "Stop hosting first?" [Stop and join] |
| E9 | Host leaves during the download | download stays in `.incoming`, deleted; message; nothing else changed |
| E10 | Download corrupted / truncated | per-file size + CRC32 in the manifest; retry once, then error |
| E11 | Disk full / `AppData` not writable / OneDrive-redirected `AppData` | checked before download (free space > 2x world size); clear error |
| E12 | Restart fails: "Another instance" (zombie process after a driver reset) | helper waits 20 s, then: "Spore did not close, reboot the PC"; `CoopAutoJoin.txt` kept |
| E13 | Restart works but the host is gone by then | strip "Artyom is offline", [Wait] [Back to your worlds] |
| E14 | Guest closes the game while in the friend's world | next normal launch is the own world (the co-op start is per launch, never sticky) |
| E15 | Host loaded another save / another planet since | hash differs, world re-sent; the old cache folder is replaced (it is only a cache) |
| E16 | Guest's progress in the host's world | lives in the host's save + sidecar (`../PLAN.md` section 8); the guest's cache copy is never treated as progress |
| E17 | Which creature the guest plays | v1: as today; later "Choose your creature" step from own creations (S7/S8, G2) before the join completes |
| E18 | Old installs with `Game0` = a host's world (`coop_host.txt` inside, like this PC now) | one-time migration: move it to `Coop\<id>`, restore the newest `Game0.coopbackupN` without `coop_host.txt`; ask first |
| E19 | Friend not on the Steam friends list | "Join by Steam ID" field (advanced), or add them as a Steam friend |
| E20 | Two invites at once / invite while already in a world | newest wins; while in a world: "Leave Artyom's world and join Max?" |
| E21 | Host's PC sleeps / network drops for a moment | 10 s grace (exists, `kReconnectMs`), then E13 |
| E22 | UDP fallback (`coop udphost`) | stays a developer tool, not in the menu (no world transfer there) |
| E23 | Mod updated while a co-op start is pending | protocol checked again after the restart; mismatch -> back to own world with a message |
| E24 | Splash/overlay over a loading screen crashes on some PCs | every UI piece is guarded and can be turned off (`COOP_NO_MENU`), the terminal path keeps working |

**Blockers, in order of risk** (each is a spike before building on it):
- **J1** save-area redirect at start (if impossible: keep the swap of `Game0`, but always restore it on the next normal start,
  so the friend's world still never stays as "your" saves).
- **J5** our own menu UI: layout loading, buttons, text, main-menu detection.
- **J2** `LoadGame` from the mod (if impossible: banner fallback 5.4).
- **J3** Rich Presence and friends exports (if missing: only "Recent sessions" and "Join by Steam ID"; a newer
  `steam_api.dll` is the fix).
- **J4** pre-start fetch (only for path B; path A does not need it).

## 8. Build order

1. J1 + migration (E18): separate universes. Already fixes goal 2 with today's console commands.
2. J2: no Continue after the co-op start; splash says whose world it is.
3. J3: Rich Presence + friends list in the terminal (`coop menu`, `coop friends` with live states).
4. J5: CO-OP button and panel in the main menu (host tab, join tab, invitations).
5. Path B (`launch_spore_mod.bat join <name>`), invites, first-run hint, polish.

Protocol changes: the `Invite`, `HelloFetchOnly` and world hash messages bump `kProtocolVersion` (steps 2-5).
