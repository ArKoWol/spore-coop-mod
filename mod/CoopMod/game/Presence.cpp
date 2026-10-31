// Presence.cpp : see Presence.h.
#include "stdafx.h"
#include "Presence.h"
#include "CoopNet.h"
#include "CoopLaunch.h"
#include "MenuWorlds.h"
#include "../Log.h"

#include <Spore/Simulator/cPlanetRecord.h>
#include <Spore/Simulator/SubSystem/SpacePlayerData.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace coop::presence;

namespace
{
	const char* const kKey = "sporecoop";
	const uint32_t kAppId = 480;                 // Spacewar: a friend on any other AppID is not playing with us
	const DWORD kMinSetGapMs = 2000;             // the key is written at most this often
	const DWORD kScanEveryMs = 5000;             // friends list while a window watches it
	const DWORD kSteamRetryMs = 10000;           // a watching window asks Steam again (until kSteamTries)
	const int kSteamTries = 3;                   // after that only the Refresh button tries again
	const DWORD kInviteLifeMs = 10 * 60 * 1000;  // an invitation we did not answer fades after 10 minutes
	const DWORD kInvitedShownMs = 60 * 1000;     // "Invited" stays on a friend's row this long
	const size_t kMaxInvites = 4;
	const size_t kMaxPlanetName = 64;

	struct Sent
	{
		uint64_t id = 0;
		DWORD at = 0;
	};

	// heap object, freed in Shutdown (no static destructors: ModAPI routes new/delete to the game's heap)
	struct S
	{
		std::vector<Presence::Friend> friends;
		std::vector<Presence::Invite> invites;
		std::vector<Sent> sent;
		std::string published;   // what the key says now ("" = never written)
		std::string planet;      // the planet we play, once known
		std::string scanLog;     // the last scan summary that was logged
	};
	S* sS = nullptr;

	bool sSteamTried = false;     // Steam was started because the main menu opened
	int sSteamFailures = 0;
	DWORD sSteamTryAt = 0;
	bool sApiLogged = false;
	bool sWasUp = false;
	DWORD sSetAt = 0;
	unsigned sWatchMask = 0;  // who watches: 1 = the CO-OP window, 2 = the host's invite list
	bool sScanNow = false;
	DWORD sScanAt = 0;
	unsigned sRevision = 1;
	bool sPlanetKnown = false;
	DWORD sPlanetTriedAt = 0;

	S& St()
	{
		if (!sS) {
			sS = new S();
		}
		return *sS;
	}

	uint32_t Mode()
	{
		return GameModeManager.GetActiveModeID();
	}

	// the planet of the active game as UTF-8 ("" if the game does not say); guarded, the record may not exist yet
	bool ReadActivePlanet(char16_t* out, size_t size)
	{
		bool ok = false;
		__try {
			if (Simulator::cPlanetRecord* record = Simulator::GetActivePlanetRecord()) {
				size_t i = 0;
				for (; i + 1 < size && i < record->mName.size(); ++i) {
					out[i] = record->mName[i];
				}
				out[i] = 0;
				ok = i > 0;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			ok = false;
		}
		return ok;
	}

	std::string Utf8(const char16_t* text)
	{
		int n = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)text, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1) {
			return "";
		}
		std::string out(n - 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)text, -1, &out[0], n, nullptr, nullptr);
		return out;
	}

	// The planet we play, looked up once per stay in the creature stage (the game's active planet; the newest creature-stage
	// save of Game0 as the second try). Not found: the key simply has no planet.
	void LearnPlanet(DWORD now)
	{
		if (sPlanetKnown || now - sPlanetTriedAt < 3000) {
			return;
		}
		sPlanetTriedAt = now;
		char16_t name[kMaxPlanetName + 1] = {};
		std::string planet;
		if (ReadActivePlanet(name, kMaxPlanetName + 1)) {
			planet = Utf8(name);
		}
		if (planet.empty()) {
			uint32_t star = 0;
			MenuWorlds::HostPlanet(star, planet);
		}
		if (!planet.empty()) {
			sPlanetKnown = true;
			St().planet = planet;
			Logf("presence: our planet is '%s'", planet.c_str());
		}
	}

	std::string FriendName(uint64_t id)
	{
		std::string name = CoopNet::Name(id);
		return name.empty() || name == "[unknown]" ? std::to_string(id) : name;
	}

	bool IsFriend(uint64_t id)
	{
		int count = CoopNet::FriendCount();
		for (int i = 0; i < count; ++i) {
			if (CoopNet::FriendAt(i) == id) {
				return true;
			}
		}
		return false;
	}

	// ------------------------------------------------------------------ publish

	void Publish(DWORD now)
	{
		if (!CoopNet::FriendsApi()) {
			if (!sApiLogged) {
				sApiLogged = true;
				Logf("presence: the friends exports are missing in steam_api_coop.dll: no presence, no friends list, no invitations (joining by Steam ID still works)");
			}
			return;
		}
		std::string text = Format(Presence::Current());
		if (text == St().published || (sSetAt && now - sSetAt < kMinSetGapMs)) {
			return;
		}
		if (CoopNet::SetRichPresence(kKey, text.c_str())) {
			St().published = text;
			sSetAt = now;
			Logf("presence: set %s=%s", kKey, text.c_str());
		}
	}

	// ------------------------------------------------------------------ the friends

	void Scan(DWORD now)
	{
		if (!sWatchMask || !CoopNet::FriendsApi() || (!sScanNow && now - sScanAt < kScanEveryMs)) {
			return;
		}
		sScanNow = false;
		sScanAt = now;
		S& s = St();
		std::vector<Presence::Friend> found;
		int count = CoopNet::FriendCount();
		int onApp = 0;
		for (int i = 0; i < count; ++i) {
			uint64_t id = CoopNet::FriendAt(i);
			if (!id || CoopNet::FriendAppId(id) != kAppId) {
				continue;  // not in Spacewar: not playing with us (E4)
			}
			++onApp;
			CoopNet::RequestFriendPresence(id);
			std::string text = CoopNet::FriendPresence(id, kKey);
			Info info;
			if (text.empty() || !Parse(text, info)) {
				continue;  // plain Spacewar, or a key we cannot read (E4)
			}
			Presence::Friend f;
			f.id = id;
			f.name = FriendName(id);
			f.info = info;
			found.push_back(f);
		}
		std::sort(found.begin(), found.end(), [](const Presence::Friend& a, const Presence::Friend& b) {
			return Before(a.info, a.name, a.id, b.info, b.name, b.id);
		});
		std::string summary = "presence: " + std::to_string(count) + " friends, " + std::to_string(onApp) + " on AppID 480, "
			+ std::to_string(found.size()) + " run the mod";
		for (size_t i = 0; i < found.size(); ++i) {
			summary += (i ? ", " : ": ") + found[i].name + " " + Format(found[i].info);
		}
		bool changed = found.size() != s.friends.size();
		for (size_t i = 0; !changed && i < found.size(); ++i) {
			changed = found[i].id != s.friends[i].id || found[i].name != s.friends[i].name || Format(found[i].info) != Format(s.friends[i].info);
		}
		if (summary != s.scanLog) {
			s.scanLog = summary;
			Logf("%s", summary.c_str());  // only when something changed: the scan runs every 5 s
		}
		if (changed) {
			s.friends.swap(found);
			++sRevision;
		}
	}

	void AgeInvites(DWORD now)
	{
		S& s = St();
		for (size_t i = 0; i < s.invites.size();) {
			if (now - s.invites[i].received > kInviteLifeMs) {
				Logf("invite: from %s faded after 10 minutes", s.invites[i].name.c_str());
				s.invites.erase(s.invites.begin() + i);
				++sRevision;
			}
			else {
				++i;
			}
		}
	}

	bool InMainMenu()
	{
		return Mode() == (uint32_t)GameModeIDs::kGGEMode;
	}
}

namespace Presence
{
	Info Current()
	{
		Info info;
		info.protocol = CoopNet::ProtocolVersion();
		CoopNet::HostState host = CoopNet::Hosting();
		bool guest = CoopLaunch::Active() || host.joined;
		uint32_t mode = Mode();
		bool creature = false;
		switch (mode) {
		case (uint32_t)GameModeIDs::kGGEMode:
			info.state = guest ? State::Guest : State::Menu;
			break;
		case (uint32_t)GameModeIDs::kGameCreature:
			if (Simulator::IsCreatureGame() && GameNounManager.GetAvatar()) {
				creature = true;
				info.state = host.hosting ? State::Hosting : guest ? State::Guest : State::Playing;
			}
			else {
				info.state = State::Busy;  // the stage is loading
			}
			break;
		case (uint32_t)GameModeIDs::kEditorMode:
		case (uint32_t)GameModeIDs::kGameEditMode:
			info.state = State::Editing;
			break;
		default:
			info.state = State::Busy;  // loading, another stage, a cinematic
			break;
		}
		if (host.hosting) {
			info.slots = 2;
			info.players = host.guestHere ? 2 : 1;
		}
		if (creature) {
			LearnPlanet(GetTickCount());
		}
		else if (!host.hosting) {
			sPlanetKnown = false;  // looked up again at the next stay in the creature stage
			sPlanetTriedAt = 0;
			if (sS) {
				sS->planet.clear();
			}
		}
		if (sS && (creature || host.hosting)) {
			info.planet = sS->planet;
		}
		return info;
	}

	void Update()
	{
		DWORD now = GetTickCount();
		// Steam early: once, when the main menu first opens (the init writes the harmless CSERHelper crash line, FACTS section 4)
		if (!sSteamTried && InMainMenu()) {
			sSteamTried = true;
			bool ok = CoopNet::StartSteam();
			Logf("presence: Steam started in the main menu: %s", ok ? "ok" : "not available (not running or not logged in)");
			if (!ok) {
				++sSteamFailures;
				sSteamTryAt = now;
			}
		}
		else if (!CoopNet::SteamUp() && sWatchMask && sSteamFailures < kSteamTries && now - sSteamTryAt >= kSteamRetryMs) {
			sSteamTryAt = now;  // the window is open and Steam is down: the player may have started it just now
			if (CoopNet::StartSteam()) {
				Logf("presence: Steam is up now");
			}
			else {
				++sSteamFailures;
			}
		}
		bool up = CoopNet::SteamUp();
		if (up != sWasUp) {
			sWasUp = up;
			++sRevision;  // the window's banner (Steam is not running) follows
		}
		if (!up) {
			return;
		}
		Publish(now);
		Scan(now);
		AgeInvites(now);
	}

	void Shutdown()
	{
		delete sS;
		sS = nullptr;
	}

	void WatchFriends(unsigned who, bool open)
	{
		if (open && !sWatchMask) {
			sScanNow = true;  // the first look comes at once, not after 5 s
		}
		sWatchMask = open ? (sWatchMask | who) : (sWatchMask & ~who);
	}

	void Refresh()
	{
		sSteamFailures = 0;
		sScanNow = true;
		if (!CoopNet::SteamUp()) {
			bool ok = CoopNet::StartSteam();
			Logf("presence: Refresh: Steam %s", ok ? "is up now" : "is still not available");
			sSteamTryAt = GetTickCount();
			++sRevision;
		}
	}

	bool SteamOk() { return CoopNet::SteamUp(); }
	bool Available() { return CoopNet::FriendsApi(); }
	unsigned Revision() { return sRevision; }
	const std::vector<Friend>& Friends() { return St().friends; }
	const std::vector<Invite>& Invites() { return St().invites; }

	const Friend* FindFriend(uint64_t id)
	{
		for (const Friend& f : St().friends) {
			if (f.id == id) {
				return &f;
			}
		}
		return nullptr;
	}

	void DismissInvite(uint64_t host, bool removeFromList)
	{
		S& s = St();
		for (size_t i = 0; i < s.invites.size(); ++i) {
			if (s.invites[i].host == host) {
				if (removeFromList) {
					s.invites.erase(s.invites.begin() + i);
				}
				else {
					s.invites[i].toastDone = true;
				}
				++sRevision;
				return;
			}
		}
	}

	// message: [118][protocol u32][host Steam ID u64][planet length u8][planet, UTF-8]
	bool SendInvite(uint64_t friendId)
	{
		Info me = Current();
		if (me.state != State::Hosting) {
			Logf("invite: not sent, we are not hosting");
			return false;
		}
		std::string planet = CleanPlanet(me.planet);
		uint8_t buffer[16 + kMaxPlanet] = {};
		buffer[0] = kMsgInvite;
		uint32_t protocol = me.protocol;
		uint64_t myId = CoopNet::MyId();
		memcpy(buffer + 1, &protocol, 4);
		memcpy(buffer + 5, &myId, 8);
		buffer[13] = (uint8_t)planet.size();
		memcpy(buffer + 14, planet.data(), planet.size());
		bool ok = CoopNet::SendTo(friendId, buffer, (uint32_t)(14 + planet.size()));
		Logf("invite: sent to %s (%llu), world '%s': %s", FriendName(friendId).c_str(), (unsigned long long)friendId, planet.c_str(),
			ok ? "ok" : "FAILED");
		if (ok) {
			S& s = St();
			bool found = false;
			for (Sent& e : s.sent) {
				if (e.id == friendId) {
					e.at = GetTickCount();
					found = true;
				}
			}
			if (!found) {
				Sent e;
				e.id = friendId;
				e.at = GetTickCount();
				s.sent.push_back(e);
			}
			++sRevision;
		}
		return ok;
	}

	bool InvitedRecently(uint64_t friendId)
	{
		DWORD now = GetTickCount();
		for (const Sent& e : St().sent) {
			if (e.id == friendId && now - e.at < kInvitedShownMs) {
				return true;
			}
		}
		return false;
	}

	void OnMessage(const uint8_t* data, uint32_t size)
	{
		if (size < 14 || data[0] != kMsgInvite) {
			Logf("invite: ignored a message of %u bytes that does not parse", (unsigned)size);
			return;
		}
		uint32_t protocol = 0;
		uint64_t host = 0;
		memcpy(&protocol, data + 1, 4);
		memcpy(&host, data + 5, 8);
		uint32_t planetLen = data[13];
		if (14 + planetLen > size || planetLen > kMaxPlanet || !host) {
			Logf("invite: ignored a damaged message (%u bytes)", (unsigned)size);
			return;
		}
		std::string planet((const char*)data + 14, planetLen);
		std::string name = FriendName(host);
		if (protocol != CoopNet::ProtocolVersion()) {
			Logf("invite: received from %s, ignored: other mod version (%u, mine %u)", name.c_str(), (unsigned)protocol,
				(unsigned)CoopNet::ProtocolVersion());
			return;
		}
		// only a Steam friend can invite us (the host's id travels inside the message)
		if (host == CoopNet::MyId() || !IsFriend(host)) {
			Logf("invite: received from %s (%llu), ignored: not a Steam friend", name.c_str(), (unsigned long long)host);
			return;
		}
		// in a game, in a co-op launch or in the middle of a join: logged and ignored (E20; the newest one wins in the menu)
		Info me = Current();
		if (me.state != State::Menu || CoopLaunch::Active() || CoopNet::State().joining || CoopNet::Hosting().hosting) {
			Logf("invite: received from %s to '%s', ignored: we are %s", name.c_str(), planet.c_str(), StateWord(me.state));
			return;
		}
		Logf("invite: received from %s (%llu) to '%s'", name.c_str(), (unsigned long long)host, planet.c_str());
		S& s = St();
		for (size_t i = 0; i < s.invites.size(); ++i) {
			if (s.invites[i].host == host) {
				s.invites.erase(s.invites.begin() + i);
				break;
			}
		}
		Invite invite;
		invite.host = host;
		invite.name = name;
		invite.planet = planet;
		invite.received = GetTickCount();
		s.invites.insert(s.invites.begin(), invite);
		if (s.invites.size() > kMaxInvites) {
			s.invites.resize(kMaxInvites);
		}
		++sRevision;
	}
}
