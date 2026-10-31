// FollowFriend.cpp : see FollowFriend.h.
#include "stdafx.h"
#include "FollowFriend.h"
#include "../Log.h"

#include <Spore/Simulator/SubSystem/GameViewManager.h>

namespace
{
	const DWORD kOrderIntervalMs = 200;  // how often a new move order is given
	const float kKeepDistance = 3.0f;    // stand still this close behind the friend
	const float kResumeDistance = 4.5f;  // start walking again when the friend is farther than this (no step-stop jitter)
	const DWORD kClickGraceMs = 300;     // the click that started following does not stop it

	bool sFollowing = false;
	bool sWalking = false;
	bool sMouseWasDown = false;
	DWORD sStarted = 0;
	DWORD sLastOrder = 0;

	bool Down(int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; }

	bool GameHasFocus()
	{
		DWORD pid = 0;
		HWND hwnd = GetForegroundWindow();
		GetWindowThreadProcessId(hwnd, &pid);
		return hwnd && pid == GetCurrentProcessId();
	}

	bool MoveKeysDown()
	{
		return Down('W') || Down('A') || Down('S') || Down('D') || Down(VK_UP) || Down(VK_DOWN) || Down(VK_LEFT) || Down(VK_RIGHT);
	}
}

namespace FollowFriend
{
	bool IsFollowing() { return sFollowing; }

	void Stop(const char* why)
	{
		if (!sFollowing) {
			return;
		}
		sFollowing = false;
		if (sWalking) {
			sWalking = false;
			if (auto avatar = GameNounManager.GetAvatar()) {
				avatar->StopMovement();
			}
		}
		Logf("follow: stopped (%s)", why);
	}

	void Update(void* puppetPtr)
	{
		auto puppet = static_cast<Simulator::cCreatureAnimal*>(puppetPtr);
		auto avatar = GameNounManager.GetAvatar();
		if (!puppet || !avatar || puppet->mbDead) {
			Stop("the friend's creature is gone");
			sMouseWasDown = false;
			return;
		}
		DWORD now = GetTickCount();
		bool focus = GameHasFocus();
		bool mouseDown = focus && Down(VK_LBUTTON);
		bool clicked = mouseDown && !sMouseWasDown;
		sMouseWasDown = mouseDown;

		if (clicked) {
			Simulator::cGameData* hovered = GameViewManager.GetHoveredObject();
			bool onFriend = hovered && hovered == static_cast<Simulator::cGameData*>(puppet);
			if (onFriend && !sFollowing) {
				sFollowing = true;
				sWalking = false;
				sStarted = now;
				sLastOrder = 0;
				Logf("follow: started");
			}
			else if (now - sStarted >= kClickGraceMs) {
				Stop(onFriend ? "clicked the friend again" : "clicked elsewhere");
			}
		}
		if (!sFollowing) {
			return;
		}
		if (focus && MoveKeysDown()) {
			Stop("movement key");
			return;
		}

		Vector3 own = avatar->GetPosition();
		Vector3 friendPos = puppet->GetPosition();
		Vector3 toFriend = friendPos - own;
		float distance = toFriend.Length();
		if (distance > (sWalking ? kKeepDistance : kResumeDistance)) {
			if (now - sLastOrder >= kOrderIntervalMs) {
				sLastOrder = now;
				// a point kKeepDistance short of the friend, on the line from us to him
				Vector3 goal = friendPos - toFriend * (kKeepDistance / distance);
				avatar->MoveTo(goal, 0.5f, 1.0f);
				sWalking = true;
			}
		}
		else if (sWalking) {
			sWalking = false;
			avatar->StopMovement();
		}
	}
}
