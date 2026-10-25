// RecentHosts.h : the hosts this PC has joined, so a player types "coopjoin last" instead of a Steam ID.
// Kept in %APPDATA%\Spore\Games\CoopHosts.txt (next to the saves, outside Game0, so a received world does not replace it).
#pragma once

#include <cstddef>
#include <cstdint>

namespace RecentHosts
{
	// Remembers a host that answered: it moves to the top of the list (at most 10). An empty name keeps the known one.
	void Remember(uint64_t steamId, const char* name);

	// What "coopjoin <arg>" means: "last", a list number (1..10), a Steam ID, or part of a remembered name
	// (case-insensitive). Returns 0 and a message in `error` when it matches nothing or more than one host.
	uint64_t Resolve(const char* arg, char* error, size_t errorSize);

	// `coop friends`: the list, newest first, with numbers for coopjoin.
	void Print();
}
