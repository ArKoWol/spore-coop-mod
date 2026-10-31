// Respawn.cpp : see Respawn.h.
#include "stdafx.h"
#include "Respawn.h"
#include "../Log.h"
#include "../World.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace
{
	const DWORD kHideAfterMs = 1500;     // the friend's puppet stays this long after his death (a death animation may play)
	const DWORD kCheckAfterMs = 2000;    // after our respawn: when we look where the game put us
	const float kFarFromNest = 60.0f;    // farther than this from our own nest after a respawn: put the avatar beside it
	const float kBesideNest = 4.0f;      // how far beside the nest (or the friend) the avatar is put
	const DWORD kStillDeadMs = 60000;    // dead this long: log it once (no respawn came)

	// ---- our own avatar
	bool sOwnDead = false;
	DWORD sOwnDeadAt = 0;
	DWORD sOwnBackAt = 0;                // 0 = no check pending
	bool sStillDeadLogged = false;
	void* sDeadObject = nullptr;          // the avatar object that died (compared only, never read after the death)
	Vector3 sDeathPos;
	int sDeaths = 0;

	// ---- the other player
	bool sRemoteDead = false;
	DWORD sRemoteDeadAt = 0;
	bool sHidden = false;

	// ---- `coop die`
	enum class Test { Off, WaitDead, WaitHidden, WaitBack, WaitPlace, WaitPuppet };
	Test sTest = Test::Off;
	DWORD sTestAt = 0;
	int sPassed = 0, sFailed = 0;

	void Say(const char* format, ...)
	{
		char text[512];
		va_list args;
		va_start(args, format);
		vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		Logf("respawn-test: %s", text);
		App::ConsolePrintF("respawn-test: %s", text);
	}

	void Check(bool ok, const char* what, const char* detail)
	{
		ok ? ++sPassed : ++sFailed;
		Say("%s %s (%s)", ok ? "PASS" : "FAIL", what, detail);
	}

	void EndTest()
	{
		Say("RESULT: %s (%d passed, %d failed)", sFailed == 0 ? "PASS" : "FAIL", sPassed, sFailed);
		sTest = Test::Off;
	}

	bool IsDead(Simulator::cCreatureAnimal* avatar)
	{
		return avatar->mbDead || avatar->mHealthPoints <= 0.0f;
	}

	// The nest of the avatar's herd: on the host his own nest, on the guest the copy of the save's player nest (World.cpp keeps
	// it, GuestNests: "the avatar's nest covers a host entry"), which stands where the host's nest stands.
	bool OwnNest(Simulator::cCreatureAnimal* avatar, Vector3* pos)
	{
		Simulator::cHerd* herd = avatar->mHerd.get();
		if (!herd || !herd->mpNest) {
			return false;
		}
		*pos = herd->mpNest->GetPosition();
		return true;
	}

	// A point `distance` beside `at` on the planet's surface (the creature planet is a sphere around the origin).
	Vector3 Beside(const Vector3& at, float distance)
	{
		Vector3 up = at;
		float len = up.Length();
		if (len < 1.0f) {
			return at + Vector3(distance, 0.0f, 0.0f);
		}
		up = up / len;
		Vector3 side = up.Cross(Vector3(0.0f, 0.0f, 1.0f));
		if (side.Length() < 0.1f) {
			side = up.Cross(Vector3(1.0f, 0.0f, 0.0f));
		}
		side = side / side.Length();
		return at + side * distance;
	}

	bool ObjectExists(void* object)
	{
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			if ((void*)&*it == object) {
				return true;
			}
		}
		return false;
	}

	// 2 s after the respawn: where did the game put us? Beside the own nest when it is far from it; with no nest at all (should
	// not happen: the save has a player nest on both machines) beside the friend's creature.
	void CheckPlace(Simulator::cCreatureAnimal* avatar, void* puppet)
	{
		const Vector3 at = avatar->GetPosition();
		Vector3 nest;
		if (OwnNest(avatar, &nest)) {
			float d = (at - nest).Length();
			if (d > kFarFromNest) {
				Vector3 to = Beside(nest, kBesideNest);
				avatar->Teleport(to, avatar->GetOrientation());
				Logf("respawn: the game put our avatar %.1f m from its own nest: moved beside the nest (%.1f %.1f %.1f)", d, to.x, to.y, to.z);
			}
			else {
				Logf("respawn: our avatar is %.1f m from its own nest (%.1f %.1f %.1f): left there", d, nest.x, nest.y, nest.z);
			}
			return;
		}
		if (puppet) {
			auto friendCreature = static_cast<Simulator::cCreatureAnimal*>(puppet);
			Vector3 to = Beside(friendCreature->GetPosition(), kBesideNest);
			avatar->Teleport(to, avatar->GetOrientation());
			Logf("respawn: our avatar's herd has no nest: moved beside the friend's creature (fallback)");
			return;
		}
		Logf("respawn: our avatar's herd has no nest and no friend is here: left where the game put it (%.1f %.1f %.1f)", at.x, at.y, at.z);
	}

	void WatchOwn(DWORD now)
	{
		if (!Simulator::IsCreatureGame()) {
			sOwnDead = false;
			sOwnBackAt = 0;
			return;
		}
		Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
		if (!sOwnDead) {
			if (!avatar || !IsDead(avatar)) {
				return;
			}
			sOwnDead = true;
			sOwnDeadAt = now;
			sOwnBackAt = 0;
			sStillDeadLogged = false;
			sDeadObject = avatar;
			sDeathPos = avatar->GetPosition();
			++sDeaths;
			Vector3 nest;
			bool hasNest = OwnNest(avatar, &nest);
			Logf("respawn: our avatar died (%d): health %.1f, mbDead %d, at %.1f %.1f %.1f, own nest %s%.1f m away", sDeaths,
				avatar->mHealthPoints, avatar->mbDead ? 1 : 0, sDeathPos.x, sDeathPos.y, sDeathPos.z, hasNest ? "" : "NONE, ",
				hasNest ? (sDeathPos - nest).Length() : 0.0f);
			return;
		}
		if (!avatar || IsDead(avatar)) {
			if (!sStillDeadLogged && now - sOwnDeadAt >= kStillDeadMs) {
				sStillDeadLogged = true;
				Logf("respawn: our avatar is still dead after %u s (avatar %s): the game did not respawn it", (unsigned)(kStillDeadMs / 1000),
					avatar ? "dead" : "missing");
			}
			return;
		}
		sOwnDead = false;
		sOwnBackAt = now;
		const Vector3 at = avatar->GetPosition();
		Vector3 nest;
		bool hasNest = OwnNest(avatar, &nest);
		const bool same = (void*)avatar == sDeadObject;
		Logf("respawn: our avatar is back after %.1f s: %s object, health %.1f/%.1f, at %.1f %.1f %.1f, %.1f m from the death place, own nest %s%.1f m away%s",
			(now - sOwnDeadAt) / 1000.0f, same ? "the same" : "a new", avatar->mHealthPoints, avatar->mMaxHealthPoints, at.x, at.y, at.z,
			(at - sDeathPos).Length(), hasNest ? "" : "NONE, ", hasNest ? (at - nest).Length() : 0.0f,
			same ? "" : (ObjectExists(sDeadObject) ? "; the dead object is still in the world" : "; the dead object is gone"));
	}

	void WatchRemote(DWORD now, bool echo)
	{
		bool dead = echo ? sOwnDead : World::RemoteAlive() == 0;
		if (dead && !sRemoteDead) {
			sRemoteDead = true;
			sRemoteDeadAt = now;
			Logf("respawn: the other player%s died, his creature goes in %.1f s", echo ? " (echo)" : "", kHideAfterMs / 1000.0f);
		}
		else if (!dead && sRemoteDead) {
			sRemoteDead = false;
			Logf("respawn: the other player%s is alive again after %.1f s, his creature is built where he is", echo ? " (echo)" : "",
				(now - sRemoteDeadAt) / 1000.0f);
		}
		bool hide = sRemoteDead && now - sRemoteDeadAt >= kHideAfterMs;
		if (hide != sHidden) {
			sHidden = hide;
			if (hide) {
				Logf("respawn: the other player's creature removed while he is dead");
			}
		}
	}

	void TestStep(DWORD now, void* puppet)
	{
		const DWORD since = now - sTestAt;
		char detail[160];
		switch (sTest) {
		case Test::WaitDead:
			if (sOwnDead || since >= 5000) {
				Check(sOwnDead, "our avatar died", sOwnDead ? "health 0 or mbDead" : "still alive 5 s after the hit");
				if (!sOwnDead) {
					EndTest();
					return;
				}
				sTest = Test::WaitHidden;
				sTestAt = now;
			}
			break;
		case Test::WaitHidden:
			if (!puppet || since >= kHideAfterMs + 3000) {
				Check(!puppet, "the echo puppet is removed while we are dead", puppet ? "still there" : "gone");
				sTest = Test::WaitBack;
				sTestAt = now;
			}
			break;
		case Test::WaitBack:
			if (!sOwnDead || since >= 60000) {
				snprintf(detail, sizeof(detail), "%.1f s after the death", (now - sOwnDeadAt) / 1000.0f);
				Check(!sOwnDead, "the game respawned our avatar", sOwnDead ? "still dead after 60 s" : detail);
				if (sOwnDead) {
					EndTest();
					return;
				}
				sTest = Test::WaitPlace;
				sTestAt = now;
			}
			break;
		case Test::WaitPlace:
			if (sOwnBackAt == 0) {  // CheckPlace ran
				Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
				Vector3 nest;
				if (avatar && OwnNest(avatar, &nest)) {
					float d = (avatar->GetPosition() - nest).Length();
					snprintf(detail, sizeof(detail), "%.1f m", d);
					Check(d <= kFarFromNest, "our avatar is at its own nest", detail);
				}
				else {
					Check(false, "our avatar is at its own nest", avatar ? "its herd has no nest" : "no avatar");
				}
				sTest = Test::WaitPuppet;
				sTestAt = now;
			}
			break;
		case Test::WaitPuppet:
			if (puppet || since >= 8000) {
				Check(puppet != nullptr, "the echo puppet is back", puppet ? "built again" : "not there 8 s after the respawn");
				EndTest();
			}
			break;
		default:
			break;
		}
	}
}

namespace Respawn
{
	void Update(bool echo, void* puppet)
	{
		DWORD now = GetTickCount();
		WatchOwn(now);
		if (sOwnBackAt && now - sOwnBackAt >= kCheckAfterMs) {
			sOwnBackAt = 0;
			if (Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar()) {
				CheckPlace(avatar, echo ? nullptr : puppet);
			}
		}
		WatchRemote(now, echo);
		if (sTest != Test::Off) {
			if (!Simulator::IsCreatureGame()) {
				Say("FAIL left the creature stage");
				++sFailed;
				EndTest();
				return;
			}
			TestStep(now, puppet);
		}
	}

	float ReportedHealth(Simulator::cCreatureAnimal* avatar)
	{
		if (!avatar || avatar->mbDead || sOwnDead) {
			return 0.0f;
		}
		return avatar->mHealthPoints;
	}

	bool OwnDead() { return sOwnDead; }
	bool RemoteDead() { return sRemoteDead; }
	bool HidePuppet() { return sHidden; }

	void StartDieTest()
	{
		Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
		if (!Simulator::IsCreatureGame() || !avatar) {
			App::ConsolePrintF("respawn-test: needs the creature stage");
			return;
		}
		if (sOwnDead) {
			App::ConsolePrintF("respawn-test: our avatar is dead already, wait for the respawn");
			return;
		}
		sPassed = sFailed = 0;
		float damage = avatar->mMaxHealthPoints + 100.0f;
		Say("killing our avatar: %.1f damage with no attacker, as a hit from the host arrives (kMsgDamage)", damage);
		// through the TakeDamage detour: FilterDamage lets a hit on our own avatar through
		avatar->TakeDamage(damage, 0, Simulator::kMelee, Vector3(0.0f, 0.0f, 0.0f), nullptr);
		sTest = Test::WaitDead;
		sTestAt = GetTickCount();
	}
}
