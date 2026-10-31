// PuppetMotion.cpp : the remote player's puppet walks to the received positions instead of being teleported (S3).
#include "stdafx.h"
#include "PuppetMotion.h"
#include "../Log.h"

#include <cmath>

namespace
{
	PuppetMotion::Mode sMode = PuppetMotion::Mode::Move;
	bool sLogging = false;
	PuppetMotion::Facing sFacing = PuppetMotion::Facing::Always;
	bool sRunning = true;   // the puppet's desired speed is raised when the remote player is faster than its standard speed
	PuppetMotion::Follower sPuppet;  // the remote player's puppet
	DWORD sLastLog = 0;
	DWORD sLastSettleLog = 0;
	int sSettleLogged = 0;

	const float kHoldDistance = 1.6f;     // an animation is only protected from stop/turn orders while the creature is this close to its target
	                                      // (a standing creature may stand up to kWalkUpDistance away: the hold must cover that)
	const float kStopSpeed = 0.4f;        // the remote player counts as standing below this speed
	const float kArriveDistance = 1.0f;   // a creature walking up to a standing target stops this close (the game stops by itself at ~0.7 m)
	const float kWalkUpDistance = 1.5f;   // a standing creature starts walking up only when farther than this (no step-stop jitter)
	const DWORD kOrderIntervalMs = 100;   // how often a new move order is given
	const float kLeadSeconds = 0.35f;     // aim a bit ahead of the remote player so the puppet keeps walking smoothly
	const float kSettleDistance = 1.5f;   // the remote creature stands and ours is farther than this from it ...
	const DWORD kSettleMs = 600;          // ... for this long (blocked by a body, locked by a hit reaction): Teleport it there
	const float kMaxSpeedFactor = 2.0f;   // never more than twice the creature's standard speed
	const float kCatchUpPerMetre = 0.5f;  // m/s added for every metre the puppet is behind
	const float kOffFacingCos = 0.5f;     // walking more than 60 degrees away from the facing is a step back or sideways
	const float kStuckDistance = 1.5f;    // a creature this far behind that stands still ...
	const DWORD kStuckMs = 1500;          // ... for this long is stuck: teleport it

	// A creature's forward axis is its local +Y (FACTS.md section 7).
	Vector3 Forward(const Math::Quaternion& rotation)
	{
		return rotation.ToMatrix() * Vector3(0.0f, 1.0f, 0.0f);
	}

	// Each MoveTo resets the desired speed to the standard one, so this runs right after every order: a remote creature
	// faster than our standard speed (the player's avatar 4.75 m/s, the puppet 4.0) gets its speed, plus a little to catch up.
	void MatchSpeed(PuppetMotion::Follower& f, Simulator::cCreatureAnimal* creature, float behind)
	{
		float standard = creature->GetStandardSpeed();
		float needed = f.remoteSpeed + (behind > 1.0f ? (behind - 1.0f) * kCatchUpPerMetre : 0.0f);
		if (standard > 0.0f && needed > standard) {
			creature->SetDesiredSpeed(needed > standard * kMaxSpeedFactor ? standard * kMaxSpeedFactor : needed, 0);
		}
	}
}

namespace PuppetMotion
{
	void Reset()
	{
		sPuppet = Follower();
	}

	void Hold(Follower& follower, unsigned long ms) { follower.holdUntil = GetTickCount() + ms; }
	void HoldPuppet(unsigned long ms) { Hold(sPuppet, ms); }

	Mode GetMode() { return sMode; }

	void SetMode(Mode mode)
	{
		sMode = mode;
		Reset();
	}

	const char* ModeName(Mode mode)
	{
		switch (mode) {
		case Mode::Teleport: return "teleport";
		case Mode::Move: return "move";
		case Mode::Walk: return "walk";
		}
		return "?";
	}

	void SetFacing(Facing facing) { sFacing = facing; }
	Facing GetFacing() { return sFacing; }

	const char* FacingName(Facing facing)
	{
		switch (facing) {
		case Facing::Off: return "off";
		case Facing::Steps: return "steps";
		case Facing::Always: return "always";
		}
		return "?";
	}
	void SetRunning(bool on) { sRunning = on; }
	bool GetRunning() { return sRunning; }

	void SetLogging(bool on) { sLogging = on; }
	bool GetLogging() { return sLogging; }

	void Drive(Simulator::cCreatureAnimal* puppet, const Vector3& target, const Math::Quaternion& rotation)
	{
		sPuppet.facing = sFacing;
		sPuppet.matchSpeed = sRunning;
		Drive(sPuppet, puppet, target, rotation);
	}

	void Drive(Follower& f, Simulator::cCreatureAnimal* puppet, const Vector3& target, const Math::Quaternion& rotation)
	{
		DWORD now = GetTickCount();

		// remote speed and direction from the change of the target position
		Vector3 velocity;
		if (f.lastTargetTick && now != f.lastTargetTick) {
			float dt = (now - f.lastTargetTick) / 1000.0f;
			velocity = (target - f.lastTarget) / dt;
			float speed = velocity.Length();
			if (dt < 0.5f) {
				float a = 1.0f - expf(-dt * 8.0f);
				f.remoteSpeed += (speed - f.remoteSpeed) * a;
				f.remoteVelocity = f.remoteVelocity + (velocity - f.remoteVelocity) * a;
			}
		}
		f.lastTarget = target;
		f.lastTargetTick = now;

		Vector3 position = puppet->GetPosition();
		float distance = (target - position).Length();

		// pushed only while the remote creature really walks, so a standing one is always walked up to with MoveTo
		f.pushing = f.facing != Facing::Off && sMode == Mode::Move && f.remoteSpeed > kStopSpeed
			&& f.remoteVelocity.Length() > kStopSpeed
			&& (f.facing == Facing::Always || Forward(rotation).Normalized().Dot(f.remoteVelocity.Normalized()) < kOffFacingCos);

		// a creature that should walk but stands still (seen after experiments with its locomotion) is put in place
		if (f.started && f.moving && distance > kStuckDistance && puppet->GetVelocity().Length() < 0.2f) {
			if (!f.stuckSince) {
				f.stuckSince = now;
			}
			else if (now - f.stuckSince >= kStuckMs) {
				Logf("motion: stuck %.1f m behind for %u ms, teleported", distance, (unsigned)(now - f.stuckSince));
				f.started = false;  // the branch below teleports
			}
		}
		else {
			f.stuckSince = 0;
		}

		// an animation (not a walk) is playing and the remote player stands: do not interrupt it with stop/turn orders
		if (now < f.holdUntil && f.remoteSpeed < 0.6f && distance <= kHoldDistance) {
			return;
		}

		// Settle snap: the remote creature stands but ours stands off its place (blocked by an avatar body, locked by combat or
		// hit reactions, so MoveTo never gets it there and it never falls 6 m behind). Only counted outside kHoldDistance, so a
		// held animation is never cut. In a fight the animal would otherwise stand at two different places for the two players.
		bool settle = false;
		if (f.remoteSpeed < kStopSpeed && distance > kSettleDistance) {
			if (!f.farSince) {
				f.farSince = now ? now : 1;
			}
			else if (now - f.farSince >= kSettleMs) {
				settle = true;
			}
		}
		else {
			f.farSince = 0;
		}

		if (sMode == Mode::Teleport) {
			puppet->Teleport(target, rotation);
		}
		else if (settle && f.started) {
			if (sSettleLogged < 60 && now - sLastSettleLog >= 1000) {
				sLastSettleLog = now;
				++sSettleLogged;
				Logf("motion: settle snap of %s, %.1f m from the target after %d ms (moving %d)", &f == &sPuppet ? "the puppet" : "a mirror",
					distance, (int)(now - f.farSince), f.moving ? 1 : 0);
			}
			f.farSince = 0;
			f.moving = false;
			puppet->StopMovement();
			puppet->Teleport(target, rotation);
		}
		else if (!f.started || distance > f.snap) {
			f.started = true;
			f.moving = false;
			f.stuckSince = 0;
			puppet->StopMovement();
			puppet->Teleport(target, rotation);
		}
		else if (f.pushing) {
			// the remote facing and a velocity towards the target, no MoveTo (it would turn the creature)
			if (f.lastOrder != 0) {
				f.lastOrder = 0;
				puppet->StopMovement();
			}
			f.moving = true;
			Vector3 push = f.remoteVelocity + (target - position) * (kCatchUpPerMetre * 2.0f);
			float limit = puppet->GetStandardSpeed() * kMaxSpeedFactor;
			if (limit > 0.0f && push.Length() > limit) {
				push = push.Normalized() * limit;
			}
			puppet->SetOrientation(rotation);
			puppet->SetVelocity(push);
		}
		else if (f.remoteSpeed > kStopSpeed || distance > (f.moving ? kArriveDistance : kWalkUpDistance)) {
			// walking: give the game a new goal a little ahead of the remote player
			if (now - f.lastOrder >= kOrderIntervalMs) {
				f.lastOrder = now;
				Vector3 goal = target;
				if (f.remoteSpeed > kStopSpeed) {
					goal = target + velocity * kLeadSeconds;
				}
				if (sMode == Mode::Walk) {
					// speed state 1 is a run when the remote player is fast
					puppet->WalkTo(f.remoteSpeed > 6.0f ? 1 : 0, goal, goal, 0.3f, 0.6f);
				}
				else {
					puppet->MoveTo(goal, 0.3f, 0.6f);
				}
				if (f.matchSpeed) {
					MatchSpeed(f, puppet, distance);
				}
				f.moving = true;
			}
		}
		else if (f.moving) {
			// the remote player stopped and we are there: stand and face the same way
			f.moving = false;
			puppet->StopMovement();
			puppet->SetOrientation(rotation);
		}
		else {
			puppet->SetOrientation(rotation);
		}


		if (sLogging && &f == &sPuppet && now - sLastLog >= 500) {
			sLastLog = now;
			Vector3 own = puppet->GetVelocity();
			// angle between the remote facing and the remote walking (0 forward, 90 sideways, 180 backwards)
			float angle = -1.0f;
			if (f.remoteVelocity.Length() > 0.5f) {
				float c = Forward(rotation).Normalized().Dot(f.remoteVelocity.Normalized());
				c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
				angle = acosf(c) * 57.2958f;
			}
			auto avatar = GameNounManager.GetAvatar();
			Logf("motion: mode %s remote %.2f m/s puppet %.2f m/s behind %.2f m %s; facing/walking %.0f deg (%s%s);"
				" desired puppet %.2f (standard %.2f, state %d) avatar %.2f (standard %.2f, state %d, %.2f m/s)",
				ModeName(sMode), f.remoteSpeed, own.Length(), distance, f.moving ? "moving" : "standing", angle,
				FacingName(f.facing), f.pushing ? ", pushed" : "",
				puppet->GetDesiredSpeed(), puppet->GetStandardSpeed(), puppet->mSpeedState,
				avatar ? avatar->GetDesiredSpeed() : 0.0f, avatar ? avatar->GetStandardSpeed() : 0.0f,
				avatar ? avatar->mSpeedState : 0, avatar ? avatar->GetVelocity().Length() : 0.0f);
		}
	}
}
