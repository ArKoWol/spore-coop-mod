// World.h : the shared world (milestone M2, docs/PLAN.md section 10). The host's animals are replicated to the guest.
//
// Host: every 100 ms the animals of the simulation are copied into coop::HostWorld; its deltas (unreliable, channel 2),
// events and a full snapshot every 10 s (reliable, channel 3) go to the guest.
// Guest: coop::GuestWorld rebuilds the table; every entity gets a mirror (a one-animal herd whose AI is skipped and
// whose transform is set from the table); the animals the guest's own game made are removed.
#pragma once

#include <cstdint>

namespace World
{
	// Sends one message to the peer; false when the send buffer is full (reliable messages are then retried).
	typedef bool (*SendFn)(const void* data, uint32_t size);

	constexpr int kChannelDelta = 2;     // unreliable
	constexpr int kChannelReliable = 3;  // reliable, ordered

	void SetTransport(SendFn unreliable, SendFn reliable);

	// A guest connected (or came back): the host sends a full snapshot at once.
	void HostPeerConnected();

	// Called every frame while a Steam connection exists. `host` is the role, `inGame` is true in the creature stage
	// with an avatar, `puppet` is the player puppet (not an NPC), nullptr if none.
	void Update(bool host, bool inGame, void* puppet);

	// A message that arrived on kChannelDelta / kChannelReliable.
	void OnMessage(int channel, const uint8_t* data, uint32_t size, bool host);

	// True on the guest for every animal except its own avatar: the game's AI must not move it.
	bool ShouldSkipAI(void* animal);

	// The game's TakeDamage detour asks here first. True = swallow the call: on the guest a hit on a mirror (or on the
	// host's puppet) becomes a request to the host, and on the host a hit on the guest's puppet is sent to the guest, which
	// applies it to its own avatar. `victim` is a cCreatureAnimal*, `attacker` a cCombatant* (may be null).
	bool FilterDamage(void* victim, float damage, int type, void* attacker);

	// The game's PlayAnimation detour tells us which creature starts which animation (bite, eat, sing, ...); it is sent to the
	// other side, which plays the same animation on its copy. `creature` is a cCreatureBase*.
	void OnAnimation(void* creature, uint32_t animationId);

	// Research: the game's creature-mode action (EatFruit, EatMeat, Pickup, Mate, ...) with its data; only logged for now.
	void OnModeAction(uint32_t actionId, void* data);

	// Console tests of the damage code on one PC: `coop hit` bites, `coop kill` kills the nearest animal.
	void TestHit(bool kill);

	// A creature we made ourselves (a mirror, the player puppet) lacks the object the game reads at creature + 0xE84 (+ 0x32C)
	// without a null check: give it a blank one, otherwise the game crashes ("reading 0000032c").
	void EnsureTuning(void* creature);

	// The species of the OTHER player's avatar, for building his puppet: 2 = known and buildable here (key = instance, type, group),
	// 1 = known, its model is on the way (wait), 0 = unknown or not coming (use the local species).
	int RemoteSpecies(uint32_t* key);

	// Queues a reliable, ordered message for the other side (used by Objects.cpp).
	void SendReliable(const uint8_t* data, size_t size);

	// Connection gone: forget the table and remove the mirrors.
	void Reset();
	// Dispose: also free the memory (no static destructors may run later).
	void Shutdown();

	void PrintStatus();
}
