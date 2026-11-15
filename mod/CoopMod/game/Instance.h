// Instance.h : settings of this game copy when two copies run on one PC (the one-PC test, scripts\launch_two.ps1).
// Everything comes from environment variables the launcher sets; without them the game behaves as a normal single copy.
//   COOP_MULTI=1          a second copy gets past Spore's "another instance is already running" check (its named mutex)
//   COOP_APPDATA=<dir>    the folder the game uses as %APPDATA% (its saves, caches, GraphicsCache.package); the launcher
//                         sets APPDATA to the same folder, which is what the mod's own code reads
//   COOP_CONSOLE_PORT=N   the remote console port (default 27780)
//   COOP_LOCAL_PORT=N     the local co-op port of this copy, also its id in a local session (default 27790)
//   COOP_NO_STEAM=1       the mod never starts Steam (two copies of one Steam user would share one identity)
#pragma once

#include <cstdint>

namespace Instance
{
	// called from AttachDetours / Dispose, inside the detour transaction
	void AttachHooks();
	void DetachHooks();

	uint16_t ConsolePort();
	uint16_t LocalPort();
	bool NoSteam();
}
