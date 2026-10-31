// Kills.h : what the game needs so that the guest's own lethal bite on a mirror can be native (PLAN rule 3: the game pays
// the DNA for the kill itself), and the relation events of mirrors (FACTS.md section 9).
//
// The guest crashed on a native lethal bite (SporeApp.exe+0x797779, reading 0x14, 2026-10-25): the kill raised the species'
// kill count to its extinction threshold, the extinction (0xBA4750, called from ApplyEvent 0xBA4A00) looks up the herd type of
// the dead animal's herd (cHerd::mArchetype, cSpeciesManager 0x4E00C0) and a mirror's herd (made by CreateHerd) has a type the
// lookup does not find. EnsureHerdType gives such a herd the type of a herd the game made itself.
#pragma once

#include <cstddef>

namespace Kills
{
	// The detour on the species relation manager's ApplyEvent (World::AllowRelationEvent decides).
	void Attach();
	void Detach();

	// True when the herd type of `herd` (a Simulator::cHerd*) is known to the game. If it is not, the type (mArchetype,
	// mArchetypeGroup) of a herd the game made is copied into it (one of the same species profile when there is one).
	// False only when no usable herd was found: then a kill of the herd's animal must not be native.
	bool EnsureHerdType(void* herd, const char* why);

	// `coop killcheck` and the self test: makes a herd the way a mirror's herd is made, checks its type, repairs it, removes it.
	// Writes a one-line verdict into `detail`; true = the repair works (or nothing needed repair).
	bool SelfCheck(char* detail, size_t size);
}
