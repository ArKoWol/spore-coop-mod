// CoopMenu.h : the CO-OP star in the main menu and its window (docs/JOIN_FLOW.md, path C).
//
// At every start the galaxy gets a star "CO-OP" near its centre (game/MenuWorlds.cpp). Selecting it opens our window:
// friends to join (remembered hosts plus a test entry), then "downloading the friend's world", then "press Play". The
// world is stored as the CO-OP star's save, so the game's own Play loads it. Play on the CO-OP star before a world is
// chosen opens the window instead of loading.
//
// Stub stage (one PC): "downloading" copies a local save (a save of another universe if there is one, to test that it
// loads on our star) instead of receiving the host's save over Steam.
#pragma once

namespace CoopMenu
{
	// every frame: makes the CO-OP star in the main menu, opens the window when it is selected, runs the window
	void Update();

	// makes sure the CO-OP star is in the galaxy's saved-game list (also called just before the game enters the main
	// menu, so the menu is built with it: leaving a game without saving reloads the galaxy from disk)
	void EnsureStar();

	// Play pressed in the main menu: true = do not load (the window opens instead)
	bool HoldPlay();

	// coop menu: opens the window (also outside the main menu, for testing)
	void Open();

	// coop carddump: logs the window tree around the CO-OP star's card (looking for its Play button)
	void DumpCard();

	// coop menuready: debug, lets Play load the CO-OP star with the copy of the own save it starts with
	void MarkReady();

	// Dispose: removes the window, the star and its save, frees GDI+
	void Shutdown();
}
