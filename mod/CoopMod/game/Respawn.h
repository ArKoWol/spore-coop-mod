// Respawn.h : a dead player comes back at his own nest (STATUS 4c), and the friend's creature goes away while he is dead.
// Our side: the game's own death and respawn run on our avatar; we watch it (`respawn:` lines), tell the friend through the
// health in kMsgAvatar (0 while dead) and, 2 s after the respawn, put the avatar beside its own nest when the game left it
// more than 60 m away (on the guest the own nest is his copy of the save's player nest, the place of the host's nest).
// The friend's side: while the other player is dead his puppet is removed (after 1.5 s, room for a death animation) and it is
// built again where he respawned. `coop die` is the one-PC test with the echo puppet (`respawn-test: RESULT: PASS / FAIL`).
#pragma once

namespace Simulator { class cCreatureAnimal; }

namespace Respawn
{
	// every frame, also without a session; `echo` = `coop echo` is on, `puppet` = the friend's (or the echo) puppet or null
	void Update(bool echo, void* puppet);
	// the health of our avatar for kMsgAvatar: 0 while it is dead (also when the game marks it dead with health left)
	float ReportedHealth(Simulator::cCreatureAnimal* avatar);
	// our avatar is dead now (between the death and the respawn)
	bool OwnDead();
	// the other player is dead now (from his kMsgAvatar health, or our own state for the echo puppet)
	bool RemoteDead();
	// true while the puppet must not exist: the other player has been dead for 1.5 s
	bool HidePuppet();
	// `coop die`: kills our avatar as a hit from the host would (no attacker) and checks death, respawn and the echo puppet
	void StartDieTest();
}
