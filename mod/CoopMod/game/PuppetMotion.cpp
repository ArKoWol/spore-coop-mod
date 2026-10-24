// PuppetMotion.cpp : the remote player's puppet walks to the received positions instead of being teleported (S3).
#include "stdafx.h"
#include "PuppetMotion.h"
#include "../Log.h"

#include <cmath>

namespace
{
	PuppetMotion::Mode sMode = PuppetMotion::Mode::Move;
	bool sLogging = false;
	bool sStarted = false;
	bool sMoving = false;
	Vector3 sLastTarget;
	DWORD sLastTargetTick = 0;
	float sRemoteSpeed = 0.0f;   // m/s, smoothed, from the change of the received position
	DWORD sLastOrder = 0;
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
		sStarted = false;
		sMoving = false;
		sRemoteSpeed = 0.0f;
		sLastTargetTick = 0;
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
		DWORD now = GetTickCount();

		// remote speed and direction from the change of the target position
		Vector3 velocity;
		if (sLastTargetTick && now != sLastTargetTick) {
			float dt = (now - sLastTargetTick) / 1000.0f;
			velocity = (target - sLastTarget) / dt;
			float speed = velocity.Length();
			if (dt < 0.5f) {
				float a = 1.0f - expf(-dt * 8.0f);
				sRemoteSpeed += (speed - sRemoteSpeed) * a;
			}
		}
		sLastTarget = target;
		sLastTargetTick = now;

		Vector3 position = puppet->GetPosition();
		float distance = (target - position).Length();

		if (sMode == Mode::Teleport) {
			puppet->Teleport(target, rotation);
		}
		else if (!sStarted || distance > kSnapDistance) {
			sStarted = true;
			sMoving = false;
			puppet->StopMovement();
			puppet->Teleport(target, rotation);
		}
		else if (sRemoteSpeed > kStopSpeed || distance > kArriveDistance) {
			// walking: give the game a new goal a little ahead of the remote player
			if (now - sLastOrder >= kOrderIntervalMs) {
				sLastOrder = now;
				Vector3 goal = target;
				if (sRemoteSpeed > kStopSpeed) {
					goal = target + velocity * kLeadSeconds;
				}
				if (sMode == Mode::Walk) {
					// speed state 1 is a run when the remote player is fast
					puppet->WalkTo(sRemoteSpeed > 6.0f ? 1 : 0, goal, goal, 0.3f, 0.6f);
				}
				else {
					puppet->MoveTo(goal, 0.3f, 0.6f);
				}
				sMoving = true;
			}
		}
		else if (sMoving) {
			// the remote player stopped and we are there: stand and face the same way
			sMoving = false;
			puppet->StopMovement();
			puppet->SetOrientation(rotation);
		}
		else {
			puppet->SetOrientation(rotation);
		}

		if (sLogging && now - sLastLog >= 500) {
			sLastLog = now;
			Vector3 own = puppet->GetVelocity();
			Logf("motion: mode %s remote %.2f m/s puppet %.2f m/s behind %.2f m %s", ModeName(sMode), sRemoteSpeed,
				own.Length(), distance, sMoving ? "moving" : "standing");
		}
	}
}
