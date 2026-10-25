// PuppetMotion.cpp : the remote player's puppet walks to the received positions instead of being teleported (S3).
#include "stdafx.h"
#include "PuppetMotion.h"
#include "../Log.h"

#include <cmath>

namespace
{
	PuppetMotion::Mode sMode = PuppetMotion::Mode::Move;
	bool sLogging = false;
	PuppetMotion::Follower sPuppet;  // the remote player's puppet
	DWORD sLastLog = 0;

	const float kSnapDistance = 6.0f;     // farther than this behind: Teleport
	const float kStopSpeed = 0.4f;        // the remote player counts as standing below this speed
	const float kArriveDistance = 0.6f;   // close enough to stand still
	const DWORD kOrderIntervalMs = 100;   // how often a new move order is given
	const float kLeadSeconds = 0.35f;     // aim a bit ahead of the remote player so the puppet keeps walking smoothly
}

namespace PuppetMotion
{
	void Reset()
	{
		sPuppet = Follower();
	}

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

	void SetLogging(bool on) { sLogging = on; }
	bool GetLogging() { return sLogging; }

	void Drive(Simulator::cCreatureAnimal* puppet, const Vector3& target, const Math::Quaternion& rotation)
	{
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
			}
		}
		f.lastTarget = target;
		f.lastTargetTick = now;

		Vector3 position = puppet->GetPosition();
		float distance = (target - position).Length();

		if (sMode == Mode::Teleport) {
			puppet->Teleport(target, rotation);
		}
		else if (!f.started || distance > kSnapDistance) {
			f.started = true;
			f.moving = false;
			puppet->StopMovement();
			puppet->Teleport(target, rotation);
		}
		else if (f.remoteSpeed > kStopSpeed || distance > kArriveDistance) {
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
			Logf("motion: mode %s remote %.2f m/s puppet %.2f m/s behind %.2f m %s", ModeName(sMode), f.remoteSpeed,
				own.Length(), distance, f.moving ? "moving" : "standing");
		}
	}
}
