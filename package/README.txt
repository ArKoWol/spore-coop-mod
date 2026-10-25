SPORE CO-OP (test build)
========================

Two players play the creature stage of Spore on the same planet and see each other's creature.
This is an early test made by two friends: it can crash the game. Back up your saves
(%APPDATA%\Spore\Games) before you try it.


WHAT YOU NEED
-------------
- Spore + Galactic Adventures, GOG "SPORE Collection" (SporebinEP1\SporeApp.exe, version 3.1.0.29).
  The current Steam version of Spore does NOT work with mods.
- Steam installed, running and logged in on both PCs. The connection goes through Steam
  (as the free test app "Spacewar"), no port forwarding.
- The game must start normally BEFORE you install the mod.


INSTALL / UPDATE
----------------
1. Close the game.
2. Run  coop_update\update.bat  (from the folder where you extracted this package).
   It finds your Spore folder, installs the newest build and copies the Steam API file
   (steam_api_coop.dll) from one of your Steam games. Run it again any time to update.
3. Both players must have the same version. Otherwise the console says "different mod version"
   and the games refuse each other: both run update.bat.


PLAY TOGETHER
-------------
The game always starts in a window. Do not minimize it while you play: a minimized game stops,
and so does the co-op. The console opens with Ctrl+Shift+C.

1. HOST: load your game and go into the CREATURE stage. Press "Host co-op" (top right of the screen),
   then "Copy my Steam ID" and send that number to your friend. (Or type   coop host   in the console.)
2. GUEST: stay in the MAIN MENU. Click the CO-OP star in the middle of the galaxy, copy the host's
   Steam ID and press "Join by Steam ID" (or "Join" next to a friend you joined before).
   (Or type   coopjoin 7656...   in the console.)
3. GUEST: the host's world arrives ("coop: world received"). Your game closes and starts again
   by itself, installs the world and connects to the host again. You do not type anything.
   Your own save is kept in %APPDATA%\Spore\Games\Game0.coopbackup (later joins: coopbackup2, 3, ...).
4. GUEST: press Continue and enter the creature stage. The window title says
   "CO-OP: you are in the HOST's world".

What you see: the other player's creature walks next to you (for now it has YOUR creature's look).
What is not shared yet: eating, fighting, health, plants, fruit, eggs and nests.

If the guest leaves and comes back, just type  coopjoin last  (the mod remembers the hosts you joined).


COMMANDS
--------
coop host              host over Steam (prints your Steam ID)
coopjoin <steamid>     join a host, from the main menu
coopjoin last          join the host you joined last time (no need to look up the Steam ID again)
coopjoin <name>        join a host you joined before by part of their Steam name
coopjoin <number>      ... or by their number in coop friends
coop friends           the list of hosts you joined, with numbers
coop status            what is connected
coop off               leave the session
coop sendworld         host: send the world to the guest again
coop noworld           host: do not send the world automatically
coop udphost           fallback without Steam: host over UDP port 27777 (needs VPN or port forwarding,
coopudpjoin <ip>       no world transfer: copy the save by hand)
coop echo              try it alone: a copy of your creature follows you


PROBLEMS
--------
- "Configuration Script Failed [2000]" or "data directory is missing or corrupt [1004]":
  open PowerShell as Administrator in the coop_update folder and run
     .\fix_spore_registry.ps1 -GameDir "<your Spore folder>"
- "Another instance of the game is already running" right after a crash: restart Windows.
- To turn the automatic restart off, set the environment variable COOP_NO_RESTART=1;
  then restart the game yourself after "world received" and type coopjoin again.
- Anything else: send  <Spore folder>\SporebinEP1\CoopMod.log  to the developers.
