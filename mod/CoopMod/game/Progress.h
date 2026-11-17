// Progress.h : what the player earns, for the diagnosis of the guest's low DNA (features/SHARED_CREATURE_WORLD.md P4).
// Every change of our DNA (cCreatureGameData::mEvolutionPoints) is logged with the last species relation event before it
// (attacked, killed, impressed, nest events; reached the game's table or dropped by World::AllowRelationEvent). Run the same
// actions on the host and on the guest and compare the `dna:` lines. `coop dna` prints the totals. Log lines start with `dna:`.
#pragma once

namespace Progress
{
	// The species relation detour (game/Kills.cpp): one event on `creature` (a cCreatureBase*), `allowed` = it reached the game.
	void NoteRelation(int event, bool allowed, void* creature);

	// Every frame; `inGame` = creature stage with an avatar.
	void Update(bool inGame);

	// `coop dna`: DNA now, gained since the stage started, relation events by type (reached / dropped), in the log and the console.
	void Report();
}
