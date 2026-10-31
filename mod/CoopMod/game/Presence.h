// Presence.h : who is hosting (docs/features/JOIN_FLOW.md section 5, M3).
//
// Every running mod publishes its state in the Steam Rich Presence key `sporecoop` (`<protocol>|<state>|<planet>|<players>/<slots>`,
// core/coop_presence.h) and reads the same key of its Steam friends while the CO-OP window is open. Steam is started when the main
// menu first opens, so friends see us from the menu on. Besides that, the host can invite a friend: our own message on channel 3
// (kMsgInvite) over ISteamNetworkingMessages; Steam's own "Invite to game" would open Spacewar (FACTS section 5).
//
// All Steam calls go through CoopNet (dllmain.cpp); a missing export turns this off with a log line and never blocks joining.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../../core/coop_presence.h"

namespace Presence
{
	// first byte of the invitation message (ids in use: World.cpp 100-116, Actions.cpp 117)
	constexpr uint8_t kMsgInvite = 118;

	// a Steam friend who runs the mod (any state, any version that publishes the key)
	struct Friend
	{
		uint64_t id = 0;
		std::string name;  // UTF-8
		coop::presence::Info info;
	};

	// an invitation we received (newest first, one per host)
	struct Invite
	{
		uint64_t host = 0;
		std::string name;    // the host's Steam name
		std::string planet;  // may be empty
		unsigned long received = 0;  // GetTickCount() when it arrived
		bool toastDone = false;      // the toast was answered (Not now): the window still lists it
	};

	// every frame (NetUpdate): starts Steam in the main menu, publishes our state, scans the friends while watched, ages invitations
	void Update();
	void Shutdown();

	// our own state as the key would say it now
	coop::presence::Info Current();

	// the CO-OP window (kWatchWindow) or the invite list (kWatchInvite) is open: scan the friends every 5 s
	constexpr unsigned kWatchWindow = 1, kWatchInvite = 2;
	void WatchFriends(unsigned who, bool open);
	// scan now (Refresh button); starts Steam again if it is not up
	void Refresh();
	// Steam is up (false: not running / not logged in, the window says so: JOIN_FLOW E1)
	bool SteamOk();
	// the friends list and Rich Presence work (the exports exist)
	bool Available();
	// grows whenever the list of friends or of invitations changed
	unsigned Revision();
	const std::vector<Friend>& Friends();
	const std::vector<Invite>& Invites();
	const Friend* FindFriend(uint64_t id);
	// the player answered the toast (Not now) or joined: the invitation leaves the list
	void DismissInvite(uint64_t host, bool removeFromList);

	// host: invite one friend with our planet name; false if it could not be sent
	bool SendInvite(uint64_t friendId);
	// the invitation to this friend went out less than a minute ago
	bool InvitedRecently(uint64_t friendId);

	// from the message pump (dllmain.cpp): an invitation arrived
	void OnMessage(const uint8_t* data, uint32_t size);
}
