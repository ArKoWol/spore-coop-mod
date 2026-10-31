// FollowFriend.h : a click on the other player's creature makes our own avatar follow it (the game's locomotion walks it,
// MoveTo as for the puppet). It ends with W/A/S/D or the arrow keys, a click elsewhere, a second click on the friend, or when
// the friend's creature is gone. Our own name plate shows "(following)" meanwhile.
#pragma once

namespace FollowFriend
{
	// Every frame in the creature stage; `puppet` is the other player's creature (cCreatureAnimal*), nullptr if none.
	void Update(void* puppet);

	bool IsFollowing();
	void Stop(const char* why);
}
