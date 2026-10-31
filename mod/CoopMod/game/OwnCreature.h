// OwnCreature.h : the player's own creature (STATUS 3c, PLAN 7.1).
//
// 1. Edits. The 2-PC test of 2026-10-31 showed that after the creature editor the avatar's species key, profile sequence and the
//    body read through ResourceManager.FindRecord all stay the same (FACTS section 7), so the other side never heard of the edit.
//    This file watches three other signals: the editor closing (game mode), the in-memory species profile of the avatar (a hash of
//    its plain fields) and the creature records that appear in EditorSaves.package while the editor is open. It also reads a body
//    from EVERY database that holds the key (a stale copy may win in FindRecord) and prefers the copy in EditorSaves.
// 2. The guest's own creature. Outside a co-op launch the avatar's key is written to Games\CoopOwnCreature.txt; in a co-op launch
//    (the host's world) the guest's avatar is switched to that species 5 s after it appears (ModAPI has no SetSpecies: the profile
//    pointer, species key and profile sequence are set and the model is reloaded, a guess). COOP_NO_OWN_CREATURE=1 turns it off.
// Log lines start with `own:`.
#pragma once

#include "../Models.h"

#include <cstdint>
#include <vector>

namespace OwnCreature
{
	// Every frame, also without a session. `inGame` = creature stage with an avatar.
	void Update(bool inGame);

	// True once after the editor closed or the avatar's species profile changed in memory: read the body again.
	bool TakeChange();

	// The creature key the editor wrote in its last visit (a record in EditorSaves that was not there when the editor opened).
	// False if there was none.
	bool EditedKey(uint32_t* key);

	// Like Models::Read, but looks at every database that holds the records and takes the copy in EditorSaves.package when there
	// is one (else the one FindRecord returns). Logs the copies when they changed. For our own avatar's body only.
	bool ReadFresh(uint32_t instance, uint32_t type, uint32_t group, std::vector<Models::Record>& out);

	// Dispose: frees the memory.
	void Shutdown();
}
