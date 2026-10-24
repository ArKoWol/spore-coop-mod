SPORE CO-OP TEST (CoopMod)
==========================

What you need
- Spore + Galactic Adventures, GOG "SPORE Collection" version (exe SporebinEP1\SporeApp.exe, version 3.1.0.29).
  The current Steam build does not work with mods; if you have Steam, ask for the downgrade steps.
- Steam installed, running and logged in (the connection goes through Steam as "Spacewar", no port forwarding).
- The game must start normally BEFORE installing the mod.

Install (first time)
1. Extract this package anywhere and run  coop_update\update.bat . Close the game first.
   It asks for your Spore folder (the one that contains SporebinEP1), downloads the newest mod build, installs it,
   and finds the Steam API file (steam_api_coop.dll) in your Steam games.
2. Start the game by running <game>\SporebinEP1\SporeApp.exe (or the GOG shortcut).
3. To update later, run  coop_update\update.bat  again (from the same place).
4. If you get "Configuration Script Failed [2000]" or "data directory is missing or corrupt [1004]":
   open PowerShell as Administrator and run
     .\fix_spore_registry.ps1 -GameDir "<game folder>"
   (it is installed next to update.bat)
Both players must run update.bat before playing: different mod versions refuse to talk to each other
("different mod version" message in the console).
Playing together (test version)
1. HOST: load your game and get into the CREATURE stage. Let the game save (it autosaves; leaving a nest also saves).
   Open the console (Ctrl+Shift+C) and type:   coop host
   It prints "Your Steam ID: 7656...". Send that number to the other player.
2. CLIENT: stay in the MAIN MENU (no game loaded). Open the console and type:   coopjoin 7656...
3. The host sends its save game (about 3 MB) to the client automatically. The client console says "coop: world received".
   **The game then restarts by itself** after 3 seconds (the running game keeps its save files locked), installs the received world
   at startup (your own save is kept in %APPDATA%\Spore\Games\Game0.coopbackup; a later join keeps it and uses coopbackup2, 3, ...)
   and **joins the host again automatically**. You do not have to type anything. (Set the environment variable COOP_NO_RESTART
   to turn the automatic restart off; then restart the game yourself and type  coopjoin 7656...  again.)
4. CLIENT: choose Continue / load the game and enter the creature stage. Now both players are in the same
   world: a copy of the other player's creature appears at their position and follows them.
   (The copy uses your own creature's look for now, and slides without a walking animation. "coop anim" tries
   to make it walk; it is experimental and may crash the game.)
5. If you load the world again after the client left, type  coopjoin 7656...  again.
   Host: "coop sendworld" sends the save again, "coop noworld" turns the automatic sending off.
6. If Steam does not work, there is a fallback over plain UDP (no world transfer, move the save by hand):
     host: coop udphost         client: coopudpjoin <host IP>      (UDP port 27777; needs VPN or port forwarding)
Commands: coop host | coopjoin <steamid> | coop udphost | coopudpjoin <ip> | coop abs | coop anim | coop noworld | coop sendworld | coop status | coop off
Log file: <game>\SporebinEP1\CoopMod.log - send it if something goes wrong.







