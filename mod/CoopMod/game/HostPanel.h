// HostPanel.h : the in-game "Host co-op" button (creature stage, top right; look: docs/features/UI_STYLE.md).
//
// Not hosting: a small glossy plate with a green "Host co-op" button. Hosting: the plate says whether a friend is here,
// with "Copy my Steam ID" (for the friend's Join by Steam ID) and "Stop". A guest in a co-op launch (game/CoopLaunch.h)
// gets a small plate instead: "In <host>'s world" and Leave (the game restarts in the guest's own world). Hidden for any
// other guest, outside the creature stage, and while the game is paused or in an editor.
#pragma once

namespace HostPanel
{
	// Where the plate is on screen (right edge and bottom edge of its visible part), for the invite list under it; false if hidden
	bool Anchor(float& right, float& bottom);

	// every frame
	void Update();
	// Dispose
	void Shutdown();
}
