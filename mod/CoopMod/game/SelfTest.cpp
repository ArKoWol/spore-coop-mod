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

	enum class Step { Off, WaitEcho, Ability, Anim, SingAnim, Effect, EffectStop, Keys, Done };

	Step sStep = Step::Off;
	DWORD sStepStart = 0;
	bool sStepBegun = false;
	Actions::Counters sBefore;
	int sAbilityIdx[32];
	int sAbilityType[32];
	int sAbilityCount = 0;
	int sAt = 0;
	int sPassed = 0, sFailed = 0;
	uint32_t sSingAnim = 0;                   // the animation of our sing ability, played as PlayAnimation (the player's path?)
	uint32_t sAvatarAnim = 0, sEchoAnim = 0;  // what both played at kSampleMs
	bool sSampled = false;
	HWND sWindow = nullptr;

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

	BOOL CALLBACK FindWindowProc(HWND hwnd, LPARAM)
	{
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
			sWindow = hwnd;
			return FALSE;
		}
		return TRUE;
	}

	// Presses a key as the player would: the window messages the game reads, sent to its window (it need not have the focus).
	void PressKey(int vk)
	{
		if (!sWindow) {
			EnumWindows(FindWindowProc, 0);
		}
		if (!sWindow) {
			return;
		}
		LPARAM scan = (LPARAM)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16;
		PostMessageA(sWindow, WM_KEYDOWN, vk, scan | 1);
		PostMessageA(sWindow, WM_CHAR, '0' + (vk - '0'), scan | 1);
		PostMessageA(sWindow, WM_KEYUP, vk, scan | 1 | (1u << 30) | (1u << 31));
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
		Say("started: wait for the echo puppet, then abilities, an animation, an effect, then keys 1-9 (stand still)");
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
						if (type == Simulator::cCreatureAbility::kSing) {
							uint32_t anim = avatar->GetAbility(i)->GetAnimationID(0);
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
				Next(Step::Ability);
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
				Next(Step::SingAnim);
			}
			else {
				Sample(now, avatar, echo);
			}
			break;

		case Step::SingAnim:
			// the player's song seems to arrive as plain animations (no ability was ever captured in a real test): play it that way
			if (!sSingAnim) {
				Say("no sing animation to test");
				Next(Step::Effect);
				break;
			}
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sBefore = c;
				avatar->PlayAnimation(sSingAnim);
			}
			else if (now - sStepStart >= kStepMs) {
				char what[64], detail[160];
				snprintf(what, sizeof(what), "sing animation %08x as PlayAnimation", sSingAnim);
				snprintf(detail, sizeof(detail), "captured %d, replayed %d, after %.1f s our avatar plays %08x, the echo %08x",
					c.anims - sBefore.anims, c.replayed - sBefore.replayed, kSampleMs / 1000.0f, sAvatarAnim, sEchoAnim);
				Check(c.anims > sBefore.anims && c.replayed > sBefore.replayed && sEchoAnim == sAvatarAnim, what, detail);
				Next(Step::Effect);
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
				Next(Step::Keys);
			}
			break;

		case Step::Keys:
			// research, no verdict: what does the game call when the player presses an ability key?
			if (sAt >= 9) {
				Say("RESULT: %s (%d passed, %d failed)", sFailed == 0 && sPassed > 0 ? "PASS" : "FAIL", sPassed, sFailed);
				sStep = Step::Done;
				break;
			}
			if (!sStepBegun) {
				sStepBegun = true;
				sStepStart = now;
				sBefore = c;
				PressKey('1' + sAt);
			}
			else if (now - sStepStart >= kStepMs) {
				Say("key %d: abilities %d, effects %d, stops %d, voices %d, animations %d, mode actions %d (last %08x), replayed %d", sAt + 1,
					c.abilities - sBefore.abilities, c.effects - sBefore.effects, c.stops - sBefore.stops, c.voices - sBefore.voices,
					c.anims - sBefore.anims, c.modeActions - sBefore.modeActions, c.lastModeAction, c.replayed - sBefore.replayed);
				++sAt;
				sStepBegun = false;
			}
			break;

		default:
			break;
		}
	}
}
