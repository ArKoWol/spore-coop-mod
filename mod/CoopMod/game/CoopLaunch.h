// CoopLaunch.h : the co-op launch in the game (docs/features/JOIN_FLOW.md section 3).
//
// A friend's world is played in a "co-op launch": for one launch of the game the host's world (Games\Coop\<id>) is Game0 and
// the player's own Game0 waits as Game0.own; the next start puts everything back. The folder logic is core/coop_launch.h
// (tested without the game); this file calls it from DllMain and shows what the player must know:
//   - a notice in the main menu when a folder could not be moved (nothing is ever deleted);
//   - once, on an old install, the question "Your own saves are in a backup. Restore them?" (JOIN_FLOW D22).
// Both are small plates in the style of docs/features/UI_STYLE.md (the same drawing helpers as HostPanel).
#pragma once

#include <cstdint>
#include <string>

namespace CoopLaunch
{
	// DllMain, before the game opens anything in Games\Game0: restore after a co-op launch, the answer to the restore question,
	// the co-op launch itself. Logs every step.
	void Prepare();

	// this run is a co-op launch: Game0 is the host's world, the mod joins the host by itself, no hosting
	bool Active();
	// the host whose world this co-op launch plays, 0 if none
	uint64_t Host();
	// what the player must be told about the folder moves of this start ("" = nothing)
	const char* Notice();

	// the name of a host for the windows: the remembered name, the Steam name, or the id
	std::string HostName(uint64_t id);

	// every frame: the notice and the restore question in the main menu
	void Update();
	// Dispose
	void Shutdown();
}
