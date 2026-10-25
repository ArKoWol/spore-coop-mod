// PuppetMotion.h : how the remote player's puppet follows the received positions (spike S3 of docs/DESIGN.md).
// Teleporting the puppet every frame makes it slide with its legs still; letting the game's own locomotion walk it
// to the received position makes the game play the walk/run/idle animations itself.
#pragma once

namespace PuppetMotion
{
	enum class Mode
	{
		Teleport,  // the old way: Teleport every frame (no leg animation)
		Move,      // the game walks it: cLocomotiveObject::MoveTo, Teleport only when far behind (default)
		Walk,      // like Move, but with cCreatureBase::WalkTo (crashed once without a herd, kept for comparison)
	};

	// What one followed creature remembers between frames. The remote player's puppet uses a built-in one; every mirrored
	// animal of the shared world (World.cpp) owns its own.
	struct Follower
	{
		bool started = false;
		bool moving = false;
		Vector3 lastTarget;
		DWORD lastTargetTick = 0;
		float remoteSpeed = 0.0f;  // m/s, smoothed, from the change of the target position
		DWORD lastOrder = 0;
	};

	// Called every frame with the newest remote state (target position and orientation, already smoothed).
	void Drive(Simulator::cCreatureAnimal* puppet, const Vector3& target, const Math::Quaternion& rotation);
	void Drive(Follower& follower, Simulator::cCreatureAnimal* creature, const Vector3& target, const Math::Quaternion& rotation);

	// A new puppet or a jump in the remote position: the next Drive starts with a Teleport.
	void Reset();

	Mode GetMode();
	void SetMode(Mode mode);
	const char* ModeName(Mode mode);

	// Writes a "motion:" line to CoopMod.log twice a second (remote speed, puppet speed, distance to the target).
	void SetLogging(bool on);
	bool GetLogging();
}
