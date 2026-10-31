// CoopMenu.h : the CO-OP button and window in the main menu (docs/features/JOIN_FLOW.md section 4, owner's decision D21).
//
// In the main menu (GGE mode) a small plate with a CO-OP button sits in a corner of the screen (bottom left; the environment
// variable COOP_BUTTON_CORNER = tl|tr|bl|br moves it). Clicking it opens or closes the CO-OP window, anchored to the plate:
// friends to join (remembered hosts), Join by Steam ID, then connecting and downloading the friend's world. The game restarts
// by itself and opens the world as a co-op launch (the world is Game0 for that run, the player's own Game0 waits as
// Game0.own: game/CoopLaunch.h). The galaxy of the player is never touched: no star, no save is added to it.
//
// In a co-op launch the window opens by itself and says "<host>'s world: press Play on <planet>"; the star of that planet
// (the newest creature-stage save of Game0) is selected from the mod once (spike J9, MenuWorlds::SelectStar). The window then
// has a Leave button (the game restarts in the own world).
#pragma once

namespace CoopMenu
{
	// every frame: the button plate in the main menu, the window, the join and the co-op launch's start
	void Update();

	// coop menu: opens the window (also outside the main menu, for testing)
	void Open();

	// Dispose: removes the plates, frees GDI+
	void Shutdown();
}
