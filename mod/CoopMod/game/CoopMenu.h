// CoopMenu.h : the CO-OP button and window in the main menu (docs/features/JOIN_FLOW.md section 4, decisions D21, D24).
//
// In the main menu (GGE mode) a small plate with a CO-OP button sits in a corner of the screen (bottom right, off the game's
// own menu; the environment variable COOP_BUTTON_CORNER = tl|tr|bl|br moves it). Clicking it opens or closes the CO-OP window,
// anchored to the plate: friends to join (remembered hosts), Join by Steam ID, then connecting and downloading the friend's
// world. The game restarts by itself and opens the world as a co-op launch (the world is Game0 for that run, the player's own
// Game0 waits as Game0.own: game/CoopLaunch.h). The galaxy of the player is never touched: no star, no save is added to it.
//
// In a co-op launch the window opens by itself, the CO-OP star ("CO-OP: <host>", a copy of the host's newest creature-stage
// save, MenuWorlds::AddCoopStar) appears near the galaxy's centre and is selected from the mod once (spike J9,
// MenuWorlds::SelectStar); the window says "<host>'s world: press Play on the CO-OP star". If the galaxy drops the star
// (leaving a game without saving), the window names the host's own planet instead. The window has a Leave button (the game
// restarts in the own world).
#pragma once

#include <cstdint>
#include <string>

namespace CoopMenu
{
	// every frame: the button plate in the main menu, the window, the join and the co-op launch's start
	void Update();

	// coop menu: opens the window (also outside the main menu, for testing)
	void Open();

	// Join this host as the window's Join button does (the Join of an invitation's toast, game/InvitePanel.cpp). The window opens
	// on the join; ignored while a join or a co-op launch is running.
	void JoinHost(uint64_t steamId, const std::string& name);

	// Dispose: removes the plates, frees GDI+
	void Shutdown();
}
