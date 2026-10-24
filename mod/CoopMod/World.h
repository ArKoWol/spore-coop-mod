// World.h : the shared world (milestone M2 of docs/DESIGN.md). The host's animals are replicated to the guest.
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

	// Connection gone: forget the table and remove the mirrors.
	void Reset();
	// Dispose: also free the memory (no static destructors may run later).
	void Shutdown();

	void PrintStatus();
}
