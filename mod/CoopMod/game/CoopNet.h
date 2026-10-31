// CoopNet.h : what the CO-OP window (game/CoopMenu.cpp) needs from the session code in dllmain.cpp: join a host and
// follow the join (answer, world download, restart).
#pragma once

#include <cstdint>
#include <string>

namespace Simulator { class cCreatureAnimal; }

namespace CoopNet
{
	struct JoinState
	{
		bool joining = false;          // a Steam join is running (this run)
		bool hostSeen = false;         // the host answered
		bool versionMismatch = false;  // the other side runs another protocol version
		bool receiving = false;        // the host's world is arriving
		float progress = 0;            // 0..1 of the world
		bool staged = false;           // the world is complete; the game restarts by itself to open it
		bool upToDate = false;         // staged without a download: the cache already was the host's world (JOIN_FLOW M4)
		std::string error;             // the download failed or was cut off (text for the player), "" = no error
		bool installedAtStart = false; // this run is a co-op launch: Game0 is the host's world (game/CoopLaunch.cpp)
		uint64_t host = 0;             // the host we join / whose world Game0 holds
	};

	struct HostState
	{
		bool hosting = false;      // a Steam host session runs
		bool guestHere = false;    // a guest is connected (packets arrived in the last seconds)
		uint64_t guest = 0;        // its Steam ID
		uint64_t myId = 0;         // our Steam ID (give it to the friend)
		bool joined = false;       // this game is a guest of somebody (no hosting then)
	};

	// starts hosting over Steam (what `coop host` does); false if Steam failed
	bool Host();
	HostState Hosting();

	// starts a Steam join (what `coopjoin <id>` does); false if Steam failed
	bool Join(uint64_t hostId);
	// stops the session (Cancel in the window, Stop hosting)
	void Cancel();
	JoinState State();
	// Leave: in a co-op launch, closes the game and starts it again without CoopLaunch.txt, so the next run is the player's
	// own world; false (nothing done) outside a co-op launch
	bool Leave();
	// closes the game and starts it again (the restore question of an old install)
	void RestartGame();
	// the other player of the session (host: the guest, guest: the host), 0 if none
	uint64_t PeerId();
	// the other player's creature in this game (the puppet), null if there is none yet
	Simulator::cCreatureAnimal* Puppet();

	// Steam name of a friend ("" if unknown)
	std::string Name(uint64_t steamId);

	// ---- Steam outside a session: the friends list, Rich Presence and invitations (JOIN_FLOW M3, game/Presence.cpp).
	// Steam is started once, when the main menu first opens, and stays up; host and join reuse it. The friends functions come
	// from optional exports of steam_api_coop.dll: without them FriendsApi() is false and they return nothing.
	bool StartSteam();     // starts Steam if it is not up (false: Steam is not running or not logged in)
	bool SteamUp();
	bool FriendsApi();     // the friends list and Rich Presence can be used
	unsigned ProtocolVersion();
	uint64_t MyId();
	bool SetRichPresence(const char* key, const char* value);  // value null = clear all keys
	int FriendCount();                                          // immediate friends
	uint64_t FriendAt(int index);
	uint32_t FriendAppId(uint64_t steamId);                     // the AppID the friend plays, 0 = none
	int FriendPersona(uint64_t steamId);                        // 0 offline, 1 online, ...
	void RequestFriendPresence(uint64_t steamId);
	std::string FriendPresence(uint64_t steamId, const char* key);
	// one reliable message on channel 3 to any Steam user (the invitation); false if Steam is down or its buffer is full
	bool SendTo(uint64_t steamId, const void* data, uint32_t size);
}
