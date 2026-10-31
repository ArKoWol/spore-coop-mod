// Actions.h : what a creature does besides walking and plain animations (STATUS 3e): its abilities (sing, dance, pose,
// charm), the effects started on it (vomit after the wrong food, notes, mating-call waves) and its voice. PlayAnimation
// alone showed only the first frames of these (FACTS.md section 8). Each is captured at the game's own function and replayed
// on the other side's copy of the creature through the same function (ARCHITECTURE.md section 8, rule 3).
#pragma once

#include <cstdint>

namespace Actions
{
	// Detours of cCreatureBase::PlayAbility, the effect pools and PlayVoice; called inside the detour transaction.
	void Attach();
	void Detach();

	// A message from the other side; false when it is not ours.
	bool OnMessage(const uint8_t* data, uint32_t size);

	// Every frame while a session runs: stops replayed effects the other side never stopped.
	void Update();

	// True while the game runs one of our avatar's non-attack abilities: its animations are not sent on their own.
	bool InAbility();
	// True while we replay something that came from the other side (nothing of it is sent back).
	bool Replaying();

	// `coop echo`: our avatar's actions are replayed on this creature at once (one-PC test); nullptr = off.
	void SetEchoTarget(void* creature);

	// Writes "action:" research lines (what is captured, what is replayed) to CoopMod.log; `coop actionlog`.
	void SetLogging(bool on);
	bool GetLogging();
}
