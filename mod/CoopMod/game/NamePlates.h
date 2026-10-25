// NamePlates.h : the players' Steam names above their creatures in the creature stage (look: docs/features/UI_STYLE.md).
//
// While a session runs, the other player's puppet gets a plate with their name and the own avatar a smaller one. The plate is
// a picture (game/UiDraw.cpp) drawn once per name and moved every frame to the creature's screen position, found from the
// camera's rays (cViewer::GetCameraToPoint). Hidden behind the camera, off screen, far away, outside the creature stage.
#pragma once

namespace NamePlates
{
	// every frame
	void Update();
	// Dispose
	void Shutdown();
}
