// SelfTest.h : `coop selftest`, an automatic one-PC test of what a creature does (game/Actions.cpp) with the echo puppet.
// First it checks the herd type repair of game/Kills.cpp (Kills::SelfCheck). In the creature stage it then plays, on our own avatar through the game's functions, each of its non-attack abilities, a known
// effect and a known animation, and checks that each was captured and replayed on the echo puppet (PASS / FAIL). Then it presses
// the ability keys 1-9 as a player would and logs what the game called for each (research: which path a song or dance takes).
// The verdict is the line "selftest: RESULT: PASS" or "... FAIL" in CoopMod.log and the console (tests/actions_test.ps1 reads it).
#pragma once

namespace SelfTest
{
	// The echo puppet must be on (the caller switches `coop echo` on first).
	void Start();
	// every frame
	void Update();
	bool Running();
}
