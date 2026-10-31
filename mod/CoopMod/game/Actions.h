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

	// Research: a creature-mode action of our avatar (cCreatureModeStrategy::ExecuteAction). The social abilities (sing, dance)
	// do not go through cCreatureBase::PlayAbility: the unknown actions are logged with their data and the abilities it may name.
	void OnModeAction(uint32_t actionId, void* data);

	// Every frame while a session runs: stops replayed effects the other side never stopped.
	void Update();

	// True while the game runs one of our avatar's non-attack abilities: its animations are not sent on their own.
	bool InAbility();
	// True while we replay something that came from the other side (nothing of it is sent back).
	bool Replaying();

	// cCreatureBase::PlayAnimation started `animationId` on `creature` (with its block index and blend flag): counted and logged when
	// it is our avatar, and replayed on the echo puppet with the same arguments.
	void OnAnimation(void* creature, uint32_t animationId, int block, bool disableBlendIn);

	// The animation a creature (cCreatureAnimal*) plays now (AnimatedCreature::GetCurrentAnimation), 0 if unknown.
	uint32_t CurrentAnimation(void* creature);

	// `coop echo`: our avatar's actions are replayed on this creature (one-PC test), through the same message bytes as over the
	// network; nullptr = off.
	void SetEchoTarget(void* creature);
	void* EchoTarget();

	// The type and category of ability `index` of a creature (cCreatureAnimal*); false when it has none there.
	bool AbilityInfo(void* creature, int index, int* type, int* category);

	// What was captured from our own avatar and replayed here, since the start (for `coop selftest`, game/SelfTest.cpp).
	struct Counters
	{
		int abilities = 0, effects = 0, stops = 0, voices = 0, anims = 0;  // captured
		int replayed = 0;                                                 // played on a copy or the echo puppet and taken by the game
		int modeActions = 0;                                              // creature-mode actions of our avatar
		uint32_t lastModeAction = 0;
	};
	Counters GetCounters();

	// Writes "action:" research lines (what is captured, what is replayed) to CoopMod.log; `coop actionlog`.
	void SetLogging(bool on);
	bool GetLogging();
}
