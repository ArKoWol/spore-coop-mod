// HostPanel.h : the in-game "Host co-op" button (creature stage, top right; look: docs/features/UI_STYLE.md).
//
// Not hosting: a small glossy plate with a green "Host co-op" button. Hosting: the plate says whether a friend is here,
// with "Copy my Steam ID" (for the friend's Join by Steam ID) and "Stop". Hidden while this game is a guest, outside the
// creature stage, and while the game is paused or in an editor.
#pragma once

namespace HostPanel
{
	// every frame
	void Update();
	// Dispose
	void Shutdown();
}
