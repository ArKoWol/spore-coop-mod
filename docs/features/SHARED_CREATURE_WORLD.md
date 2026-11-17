# Shared creature world: what the 2026-11-17 play session found

> Status: draft · Owner: exxecute · Updated: 2026-11-17
> Read when: you work on the guest's creature or progress, hosting (the world sent), nests, species relations or friendly
> fire in the creature stage. Decisions: `../DECISIONS.md` D3, D14, D25-D27. Priorities: `../PLAN.md` 7.1. State: `../STATUS.md` 1.

ArKoWol and exxecute played the demo (build 535e446, protocol 17) on 2 PCs on 2026-11-17. Six problems, each below with what
was seen, the cause as far as we know and the fix we plan. Causes marked *(guess)* were not checked in the code or the log yet.
When a part is built and seen working, cut it from this file.

## P1. The guest brings any creature from his own games
- **Seen**: the joining player starts with the creature of his last own game, not one of the host's world. Nothing of it is kept
  in the host's world, so he can bring the strongest creature he ever made into a fresh world.
- **Cause**: outside a co-op launch the mod writes the avatar's key to `Games\CoopOwnCreature.txt`; in a co-op launch that
  creature is loaded into the profile (`game/OwnCreature.h`, part 2). The host never stores it (STATUS 5a is not done).
- **Fix** (D26): the guest's creature belongs to the host's world, in the sidecar `Game0\coop_players.json` (`../PLAN.md` 8):
  1. First join of a Steam ID to this world: the guest gets no outside creature. He starts as the host's species and the game's
     creature editor opens for him with the parts of this world (his own species from then on).
  2. The host writes his creature (key and creature file), DNA and brain level into the sidecar on every save and when they change.
  3. A later join loads the creature from the sidecar; `CoopOwnCreature.txt` is no longer used in a co-op launch.
- **Open**: can the editor be opened for the guest at the start without breaking his profile (`../FACTS.md` 7)? Fallback: he
  plays the host's species until his first own editor visit.

## P2. The host sends an old world
- **Seen**: the host started a new game and hosted; the guest arrived in the previous world.
- **Cause**: hosting takes a snapshot of the `Game0` files on disk (`../ARCHITECTURE.md` 5) without saving first, and the
  guest's menu picks the newest creature-stage `.spo` (`MenuWorlds::HostPlanet`). An unsaved new game is not on disk.
- **Fix** (D27): hosting (CO-OP window and `coop host`) first saves the running game through the game's own save, waits until
  it is written, then takes the snapshot. The host also sends the planet he plays, so the guest's star does not depend on file
  dates. Hosting from the main menu, the editor or a loading screen is refused with a message (a save made while an editor
  opens cannot be loaded, `../FACTS.md` 6). Every later save of the host goes to the guest at the next join (M4 manifest).
- **Open**: which game function saves without the pause menu (`cGamePersistenceManager`, check with `coop` first).

## P3. Every nest comes back when the host respawns
- **Seen**: after the host's death and respawn the nests of all species are back, also the ones that were destroyed.
- **Cause** *(guess)*: the game's respawn (`kMsgRestartAtNest`, `../FACTS.md` 7) rebuilds the planet's herds and nests, or
  the guest's nest code (`GuestNests`) makes them again when the host's nest list arrives after the respawn.
- **Fix**: first find which machine makes them (`eco:` and `respawn:` lines on both sides around a host's death). A destroyed
  nest is then kept destroyed on both sides (P5); a respawn changes only the dead player's avatar.

## P4. The guest gets little DNA; his nest goals do not work
- **Seen**: the host earns DNA normally; the guest earns little. His goal cards for nests ("befriend / kill the nest") do not
  complete.
- **Cause** *(guess)*: on the guest the host's animals are mirrors, and the mod drops relation events on mirrors so that
  replayed deaths do not count (`../FACTS.md` 9). That also drops the guest's **own** hits and impressions, so his table and his
  goal cards never see them. The kill DNA works (native lethal bite, STATUS 4a), the nest and species goals do not.
- **Fix**: keep relation events that come from the guest's own action on a mirror (rule 3 of `../PLAN.md` 1), drop only the
  replayed ones. Check with the goal card of one nest on the guest.

## P5. Befriending and destroying a nest is not shared
- **Seen**: a nest befriended or killed by one player stays as it was for the other.
- **Fix** (D25): the result of a nest is shared. When a species becomes allied or extinct in one player's table, the other
  player's table gets the same value through the game's own path (`ApplyEvent`, rule 4 of `../PLAN.md` 1), so his game shows its
  own message and pays its own goal cards. Extinction already was shared by D14; allies are new. Each player keeps his own table
  for everything below that (hostile, impressed).
- **Open**: does the friend also get the DNA of the goal card (the card pays only when it sees the change itself, `../FACTS.md`
  9)? Both getting it is fine: co-op is not about balance.

## P6. The friend's creatures can be attacked
- **Seen**: a player can hit a member of the friend's family at the friend's nest; it then attacks him again and again, also
  inside his own nest.
- **Fix** (D25): the friend's species is allied in both tables (value 6, STATUS 2g), and attacks on it are refused: in the attack
  stance the friend's family is no target, a bite or a hit on it does no damage and sends nothing. The players' own avatars stay
  open to PvP (D1).
- **Open**: which check is the cheapest: the game's target selection or `TakeDamage` (0xBFCF10).

## Order
P2 and P1 first (they decide what world and creature a session starts with), then P6 and P4, then P5 and P3.
