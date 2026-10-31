// InvitePanel.h : the two small windows of the invitation (docs/features/JOIN_FLOW.md section 5, M3), in the style of
// docs/features/UI_STYLE.md.
//   - the host's invite list: opened by the Invite button of the host plate (game/HostPanel.cpp), under that plate; the Steam
//     friends who run the mod and sit in their main menu, each with Invite (Presence::SendInvite);
//   - the guest's toast in the main menu: "<host> invites you to <planet>  [Join] [Not now]" (Join = the normal join of the CO-OP
//     window, CoopMenu::JoinHost).
#pragma once

namespace InvitePanel
{
	// every frame: the list under the host plate, the toast in the main menu
	void Update();

	// the Invite button of the host plate
	void Toggle();
	bool IsOpen();

	// Dispose
	void Shutdown();
}
