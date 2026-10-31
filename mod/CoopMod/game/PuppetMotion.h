// PuppetMotion.h : how the remote player's puppet follows the received positions (spike S3, docs/PLAN.md section 10).
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

	// How the puppet keeps the remote facing (steps back and sideways, STATUS 3b). MoveTo turns a creature towards its
	// goal; turned back every frame, or with its turn rate at 0, it does not walk (FACTS.md section 7). Pushing means: no
	// MoveTo, the remote facing every frame and the velocity set directly.
	enum class Facing
	{
		Off,     // MoveTo only: the puppet turns the way it walks
		Steps,   // pushed only while the remote creature walks more than 60 degrees off its facing (turns on the way into it)
		Always,  // pushed whenever the remote creature walks (default)
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
		Vector3 remoteVelocity;    // smoothed as well, for the direction of walking
		DWORD lastOrder = 0;
		float snap = 6.0f;           // farther than this behind the target: teleport (the puppet 6 m, mirrors near the avatar less)
		DWORD holdUntil = 0;       // GetTickCount() until which a standing creature is left alone (see Hold)
		DWORD farSince = 0;        // since when the remote creature stands and ours stands off its place (settle snap), 0 = not
		Facing facing = Facing::Off;  // what to do when the remote creature walks backwards or sideways
		bool matchSpeed = false;      // raise the desired speed when the remote creature is faster than our standard speed
		bool pushing = false;         // this frame the creature is pushed, not walked by MoveTo (see Facing)
		DWORD stuckSince = 0;         // GetTickCount() since which the creature should walk but stands (0 = it does not)
	};

	// A non-walking animation (mating call, eating, singing) has just been started on this creature: for `ms` milliseconds,
	// while the remote player stands, no stop/turn orders are given, otherwise they cut the animation after its first frames.
	void Hold(Follower& follower, unsigned long ms);
	void HoldPuppet(unsigned long ms);

	// Called every frame with the newest remote state (target position and orientation, already smoothed).
	void Drive(Simulator::cCreatureAnimal* puppet, const Vector3& target, const Math::Quaternion& rotation);
	void Drive(Follower& follower, Simulator::cCreatureAnimal* creature, const Vector3& target, const Math::Quaternion& rotation);

	// A new puppet or a jump in the remote position: the next Drive starts with a Teleport.
	void Reset();

	Mode GetMode();
	void SetMode(Mode mode);
	const char* ModeName(Mode mode);

	// The puppet's steps back and sideways and its running (STATUS 3b), each can be switched off to compare.
	void SetFacing(Facing facing);
	Facing GetFacing();
	const char* FacingName(Facing facing);
	void SetRunning(bool on);
	bool GetRunning();

	// Writes a "motion:" line to CoopMod.log twice a second (remote speed, puppet speed, distance to the target,
	// the desired and standard speeds of the puppet and of our own avatar, the angle between facing and walking).
	void SetLogging(bool on);
	bool GetLogging();
}
