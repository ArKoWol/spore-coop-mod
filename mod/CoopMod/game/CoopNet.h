// CoopNet.h : what the CO-OP window (game/CoopMenu.cpp) needs from the session code in dllmain.cpp: join a host and
// follow the join (answer, world download, restart).
#pragma once

#include <cstdint>
#include <string>

namespace CoopNet
{
	struct JoinState
	{
		bool joining = false;          // a Steam join is running (this run)
		bool hostSeen = false;         // the host answered
		bool versionMismatch = false;  // the other side runs another protocol version
		bool receiving = false;        // the host's world is arriving
		float progress = 0;            // 0..1 of the world
		bool staged = false;           // the world is complete; the game restarts by itself to open it
		bool installedAtStart = false; // this run started with a received world installed
		uint64_t host = 0;             // the host we join / whose world is installed
	};

	// starts a Steam join (what `coopjoin <id>` does); false if Steam failed
	bool Join(uint64_t hostId);
	// stops the session (Cancel in the window)
	void Cancel();
	JoinState State();
	// Steam name of a friend ("" if unknown)
	std::string Name(uint64_t steamId);
}
