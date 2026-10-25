// Objects.h : the world's objects that the players eat, pick up or that hatch (gate G1, docs/PLAN.md section 10).
//
// Fruit, fruit groups, plants and eggs exist on both machines from the same save and are not streamed. Each side watches
// them; when one of them disappears (eaten, hatched, picked up) the side tells the other (reliable `kMsgRemove`: kind and
// position), which destroys the matching object. When the guest enters the world it also sends a manifest of its objects, the
// host answers with a remove for everything the guest has and the host does not (eaten since the save was written).
#pragma once

#include <cstdint>

namespace Objects
{
	// Every frame while a connection exists; `inGame` = creature stage with an avatar.
	void Update(bool host, bool inGame);

	// Returns true if the message was ours (types 109 manifest, 110 remove).
	bool OnMessage(const uint8_t* data, uint32_t size, bool host);

	void Reset();
	void PrintStatus();

	constexpr uint8_t kMsgManifest = 109;
	constexpr uint8_t kMsgRemove = 110;
}
