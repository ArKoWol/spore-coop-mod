// OwnCreature.h : the player's own creature (STATUS 3c, PLAN 7.1).
//
// 1. Edits. The 2-PC test of 2026-10-31 showed that after the creature editor the avatar's species key, profile sequence and the
//    body read through ResourceManager.FindRecord all stay the same (FACTS section 7), so the other side never heard of the edit.
//    This file watches three other signals: the editor closing (game mode), the in-memory species profile of the avatar (a hash of
//    its plain fields) and the creature records that appear in EditorSaves.package while the editor is open. It also reads a body
//    from EVERY database that holds the key (a stale copy may win in FindRecord) and prefers the copy in EditorSaves.
// 2. The guest's creature belongs to the host's world (D26, features/SHARED_CREATURE_WORLD.md P1). The host keeps the creature
//    the guest announces (kMsgAvatarSpecies, when it is not the host's own) in Game0\coop_players.json (core/coop_players.h); the
//    file travels with the world. In a co-op launch the guest loads his entry into the player's species profile 5 s after the
//    avatar appears, the way the game does it when the editor closes (exe 0x4D4F70, FACTS section 7). No entry (first join): he
//    plays the host's creature until his first editor visit. COOP_NO_OWN_CREATURE=1 turns the loading off.
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

	// coop abilities: logs our avatar's abilities (index, type, category, hot key), its default attack / socialize index and the
	// attack or social stance (`own: ab` lines).
	void DumpAbilities();

	// Host: the guest `guestId` announced his avatar's creature (instance, type, group). Kept in Game0\coop_players.json unless it
	// is the host's own creature `hostOwn` (the guest plays ours: nothing of his to keep).
	void OnFriendCreature(unsigned long long guestId, const uint32_t key[3], const uint32_t hostOwn[3]);

	// Dispose: frees the memory.
	void Shutdown();
}
