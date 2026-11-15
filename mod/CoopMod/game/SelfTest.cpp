// SelfTest.cpp : see SelfTest.h.
#include "stdafx.h"
#include "SelfTest.h"
#include "Actions.h"
#include "Kills.h"
#include "../Log.h"

#include <cstdarg>
#include <cstdio>

namespace
{
	const DWORD kEchoWaitMs = 8000;    // the echo puppet must appear within this
	const DWORD kStepMs = 3000;        // each step waits this long for the capture and the replay
	const uint32_t kVomitEffect = 0xE55DC391;  // seen after a carnivore ate fruit (FACTS.md section 8)
	const uint32_t kVomitAnim = 0x05261D56;    // seen right after it
	const uint32_t kTestPool = 0x5E1F7E57;     // our own pool id for the test effect

	const DWORD kSampleMs = 1500;      // when the echo puppet must still play what our avatar plays (not cut after the first frames)
	const uint32_t kNoAnimation = 0x4330667;   // cCreatureAbility::GetAnimationID when an ability has none

	enum class Step { Off, WaitEcho, Ability, Anim, OwnSong, Effect, EffectStop, Done };

	Step sStep = Step::Off;
	DWORD sStepStart = 0;
	bool sStepBegun = false;
	Actions::Counters sBefore;
	int sAbilityIdx[32];
	int sAbilityType[32];
	int sAbilityCount = 0;
	int sAt = 0;
	int sPassed = 0, sFailed = 0;
	int sSingIdx = -1;                        // our sing ability, used through its button (the player's own path)
	uint32_t sSingAnim = 0;                   // its animation
	uint32_t sAvatarAnim = 0, sEchoAnim = 0;  // what both played at kSampleMs
	bool sSampled = false;

	void Say(const char* format, ...)
	{
		char text[512];
		va_list args;
		va_start(args, format);
		vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		Logf("selftest: %s", text);
		App::ConsolePrintF("selftest: %s", text);
	}

	void Check(bool ok, const char* what, const char* detail)
	{
		ok ? ++sPassed : ++sFailed;
		Say("%s %s (%s)", ok ? "PASS" : "FAIL", what, detail);
	}

	void Next(Step step)
	{
		sStep = step;
		sStepStart = GetTickCount();
		sStepBegun = false;
		sSampled = false;
		sAt = 0;
	}

	// At kSampleMs into a step: what our avatar and the echo puppet play now.
	void Sample(DWORD now, void* avatar, void* echo)
	{
		if (!sSampled && now - sStepStart >= kSampleMs) {
			sSampled = true;
			sAvatarAnim = Actions::CurrentAnimation(avatar);
			sEchoAnim = Actions::CurrentAnimation(echo);
		}
	}

	// Skill abilities (mating call, jump, glide, sprint, sneak, ...) cannot be played with PlayAbility (SDK), attacks are not sent.
	bool Testable(int type, int category)
	{
		using A = Simulator::cCreatureAbility;
		if (category == A::kCategoryAttack || category == A::kCategoryPassive || category == A::kCategorySuperPower) {
			return false;
		}
		switch (type) {
		case A::kMatingCall: case A::kJump: case A::kGlide: case A::kSprint: case A::kStealth: case A::kSpeed: case A::kSight:
		case A::kBlock: case A::kSprintBurst: case A::kHover: case A::kStealthField: case A::kJumpJet:
			return false;
		}
		return true;
	}
}

namespace SelfTest
{
	bool Running() { return sStep != Step::Off && sStep != Step::Done; }

	void Start()
	{
		sPassed = sFailed = 0;
		sAbilityCount = 0;
		sSingIdx = -1;
		sSingAnim = 0;
		Say("started: wait for the echo puppet, then our song through its button, abilities, an animation, an effect (stand still)");
		Next(Step::WaitEcho);
	}

	void Update()
	{
		if (!Running()) {
			return;
		}
		DWORD now = GetTickCount();
		auto avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			Say("RESULT: FAIL (no avatar: go into the creature stage)");
			sStep = Step::Done;
			return;
		}
		auto echo = static_cast<Simulator::cCreatureAnimal*>(Actions::EchoTarget());
		Actions::Counters c = Actions::GetCounters();

		switch (sStep) {
		case Step::WaitEcho:
			if (echo) {
				// game/Kills.cpp: a herd made like a mirror's must get a herd type the game knows (else a native lethal bite crashes)
				char herdDetail[256];
				Check(Kills::SelfCheck(herdDetail, sizeof(herdDetail)), "herd type of a mirror-like herd", herdDetail);
				for (int i = 0; i < 32 && sAbilityCount < 32; ++i) {
					int type = 0, category = 0;
					if (Actions::AbilityInfo(avatar, i, &type, &category)) {
						Say("our ability %d: type %d, category %d%s", i, type, category, Testable(type, category) ? "" : " (not tested)");
						using A = Simulator::cCreatureAbility;
						if (sSingIdx < 0 && (type == A::kSing || type == A::kHarmoniousSong || type == A::kInspiringSong)) {
							uint32_t anim = avatar->GetAbility(i)->GetAnimationID(0);
							sSingIdx = i;
							sSingAnim = anim == kNoAnimation ? 0 : anim;
							Say("the sing ability's animation: %08x", sSingAnim);
						}
						if (Testable(type, category)) {
							sAbilityIdx[sAbilityCount] = i;
							sAbilityType[sAbilityCount] = type;
							++sAbilityCount;
						}
					}
				}
				Next(Step::OwnSong);
			}
			else if (now - sStepStart > kEchoWaitMs) {
				Say("RESULT: FAIL (no echo puppet within %u s)", kEchoWaitMs / 1000);
				sStep = Step::Done;
			}
			break;

		case Step::Ability:
			if (sAt >= sAbilityCount) {
				if (sAbilityCount == 0) {
					Say("no ability to test with PlayAbility");
				}
				Next(Step::Anim);
				break;
			}
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sSampled = false;
				sBefore = c;
				avatar->PlayAbility(sAbilityIdx[sAt]);
			}
			else if (now - sStepStart >= kStepMs) {
				char what[64], detail[160];
				snprintf(what, sizeof(what), "ability type %d", sAbilityType[sAt]);
				snprintf(detail, sizeof(detail), "captured %d, replayed %d, after %.1f s our avatar plays %08x, the echo %08x",
					c.abilities - sBefore.abilities, c.replayed - sBefore.replayed, kSampleMs / 1000.0f, sAvatarAnim, sEchoAnim);
				Check(c.abilities > sBefore.abilities && c.replayed > sBefore.replayed && sEchoAnim == sAvatarAnim, what, detail);
				++sAt;
				sStepBegun = false;
			}
			else {
				Sample(now, avatar, echo);
			}
			break;

		case Step::Anim:
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sBefore = c;
				avatar->PlayAnimation(kVomitAnim);
			}
			else if (now - sStepStart >= kStepMs) {
				char detail[160];
				snprintf(detail, sizeof(detail), "captured %d, replayed %d, after %.1f s our avatar plays %08x, the echo %08x",
					c.anims - sBefore.anims, c.replayed - sBefore.replayed, kSampleMs / 1000.0f, sAvatarAnim, sEchoAnim);
				Check(c.anims > sBefore.anims && c.replayed > sBefore.replayed && sEchoAnim == sAvatarAnim, "animation 05261d56 (vomit)",
					detail);
				Next(Step::Effect);
			}
			else {
				Sample(now, avatar, echo);
			}
			break;

		case Step::OwnSong:
			// first, while the song does not recharge: the player's song never passes PlayAbility, it goes button -> 0xD49560 ->
			// the ability use 0xD1E930 (FACTS.md section 8)
			if (sSingIdx < 0) {
				Say("no sing ability to test through its button");
				Next(Step::Ability);
				break;
			}
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sBefore = c;
				if (!Actions::PressAbilityButton(sSingIdx)) {
					Check(false, "our song through its button", "the game's button code is not found");
					Next(Step::Ability);
				}
			}
			else if (now - sStepStart >= kStepMs) {
				char detail[192];
				snprintf(detail, sizeof(detail), "captured %d, replayed %d, after %.1f s our avatar plays %08x (sing %08x), the echo %08x",
					c.abilities - sBefore.abilities, c.replayed - sBefore.replayed, kSampleMs / 1000.0f, sAvatarAnim, sSingAnim, sEchoAnim);
				Check(c.abilities > sBefore.abilities && c.replayed > sBefore.replayed && sEchoAnim == sAvatarAnim, "our song through its button",
					detail);
				Next(Step::Ability);
			}
			else {
				Sample(now, avatar, echo);
			}
			break;

		case Step::Effect:
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sBefore = c;
				avatar->CreateAndStartEffectForPool1Renamed(kVomitEffect, kTestPool);
			}
			else if (now - sStepStart >= kStepMs) {
				bool onEcho = echo && static_cast<Simulator::cCreatureBase*>(echo)->GetEffectFromPools(kTestPool) != nullptr;
				char detail[128];
				snprintf(detail, sizeof(detail), "captured %d, replayed %d, on the echo puppet %s", c.effects - sBefore.effects,
					c.replayed - sBefore.replayed, onEcho ? "yes" : "no");
				Check(c.effects > sBefore.effects && c.replayed > sBefore.replayed && onEcho, "effect e55dc391 (vomit)", detail);
				Next(Step::EffectStop);
			}
			break;

		case Step::EffectStop:
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sBefore = c;
				avatar->StopEffectFromPools(kTestPool, 0);
			}
			else if (now - sStepStart >= 1000) {
				char detail[96];
				snprintf(detail, sizeof(detail), "captured %d, replayed %d", c.stops - sBefore.stops, c.replayed - sBefore.replayed);
				Check(c.stops > sBefore.stops && c.replayed > sBefore.replayed, "effect stop", detail);
				Say("RESULT: %s (%d passed, %d failed)", sFailed == 0 && sPassed > 0 ? "PASS" : "FAIL", sPassed, sFailed);
				sStep = Step::Done;
			}
			break;

		default:
			break;
		}
	}
}
