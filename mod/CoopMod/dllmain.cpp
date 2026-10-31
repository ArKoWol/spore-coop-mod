// dllmain.cpp : entry point for the CoopMod DLL.
#include "stdafx.h"
#include "Entities.h"
#include "Log.h"
#include "game/PuppetMotion.h"
#include "RecentHosts.h"
#include "game/MenuWorlds.h"
#include "game/CoopMenu.h"
#include "game/CoopNet.h"
#include "game/LoadTrace.h"
#include "game/HostPanel.h"
#include "game/NamePlates.h"
#include "game/CoopLaunch.h"
#include "../../core/coop_launch.h"

static HMODULE sModule = nullptr;  // our DLL (set in DllMain)
#include "World.h"
#include "Models.h"
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/Simulator/SubSystem/UIEventLog.h>
#include <Spore/Movie/MovieSystem.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

// The puppet must not run the game's wandering AI, otherwise it fights the position we set.
static Simulator::cCreatureAnimal* sPuppetRaw = nullptr;

member_detour(NPCTickAI_detour, Simulator::cCreatureAnimal, void(float))
{
	void detoured(float deltaTime)
	{
		if (this == sPuppetRaw || World::ShouldSkipAI(this)) {
			return;
		}
		original_function(this, deltaTime);
	}
};

// Every hit on a creature goes through here first (World::FilterDamage): the host decides about the shared world, so a
// hit of the guest on an animal becomes a request, and a hit on the guest's puppet is sent to the guest.
// The animal's TakeDamage is entered with `this` = the cCombatant part of the creature (creature + 0x5A8).
member_detour(TakeDamage_detour, Simulator::cCombatant, int(float, uint32_t, int, const Vector3&, Simulator::cCombatant*))
{
	int detoured(float damage, uint32_t attackerId, int type, const Vector3& push, Simulator::cCombatant* attacker)
	{
		if (World::FilterDamage(static_cast<Simulator::cCreatureAnimal*>(reinterpret_cast<Simulator::cCombatant*>(this)), damage, type, attacker)) {
			return 0;
		}
		return original_function(this, damage, attackerId, type, push, attacker);
	}
};

// Research (gate G1): every action of the creature mode (eat fruit, eat meat, pick up, mate, ...) is logged with its target.
member_detour(ModeAction_detour, App::cCreatureModeStrategy, void(uint32_t, void*))
{
	void detoured(uint32_t actionId, void* actionData)
	{
		World::OnModeAction(actionId, actionData);
		original_function(this, actionId, actionData);
	}
};

// Every animation a creature starts (bite, eat, sing, ...) is announced to the other side (World::OnAnimation).
member_detour(PlayAnimation_detour, Simulator::cCreatureBase, int(uint32_t, int, bool))
{
	int detoured(uint32_t animationId, int block, bool disableBlendIn)
	{
		int result = original_function(this, animationId, block, disableBlendIn);
		World::OnAnimation(reinterpret_cast<Simulator::cCreatureBase*>(this), animationId);
		return result;
	}
};

// ---------------------------------------------------------------- intro splash
// Our own picture for a few seconds after the EA logo. The intro is a single movie (EA logo, Maxis, Spore
// logo; the first 4 s, the EA logo, cannot be skipped), so the movie is paused when the EA part is over, a
// UTFWin window with the picture is shown on top, and the movie continues afterwards. The picture is a PNG in
// Data\CoopMod.package (group "coopmod", instance "splash"); without it there is no splash.
static const uint32_t kIntroMovie = 0x7af7df4f;   // Spore_EP1_Data, type 0x376840d7 (EA VP6)
static const DWORD kSplashAfterMs = 4000;         // into the intro movie: the end of the EA logo
static const DWORD kSplashShowMs = 2500;

static struct Splash
{
	int state = 0;  // 0 waiting for the intro, 1 intro playing, 2 showing our picture, 3 done
	DWORD since = 0;
	UTFWin::IWindow* backdrop = nullptr;
} sSplash;

static bool SplashShow(Movie::IMovieSystem* movies)
{
	ImagePtr image;
	if (!UTFWin::Image::GetImage(ResourceKey(id("splash"), TypeIDs::png, id("coopmod")), image) || !image) {
		Logf("splash: no picture in CoopMod.package, skipped");
		return false;
	}
	UTFWin::IWindow* main = WindowManager.GetMainWindow();
	if (!main) {
		return false;
	}
	const Math::Rectangle& screen = main->GetArea();
	float w = screen.GetWidth(), h = screen.GetHeight();
	// fit 16:9 into the window, centred, black around it
	float pw = w, ph = w * 9.0f / 16.0f;
	if (ph > h) {
		ph = h;
		pw = h * 16.0f / 9.0f;
	}
	auto backdrop = new UTFWin::Window();
	backdrop->SetFillColor(Math::Color(0, 0, 0, 255));
	backdrop->SetArea(Math::Rectangle(0, 0, w, h));
	auto picture = new UTFWin::Window();
	auto drawable = new UTFWin::ImageDrawable();
	drawable->SetImage(image.get());
	picture->SetDrawable(drawable);
	picture->SetArea(Math::Rectangle((w - pw) / 2, (h - ph) / 2, (w + pw) / 2, (h + ph) / 2));
	backdrop->AddWindow(picture);
	main->AddWindow(backdrop);
	main->BringToFront(backdrop);
	sSplash.backdrop = backdrop;
	movies->PauseMovie();
	Logf("splash: shown (%.0fx%.0f in %.0fx%.0f)", pw, ph, w, h);
	return true;
}

static void SplashHide(Movie::IMovieSystem* movies)
{
	if (sSplash.backdrop) {
		if (UTFWin::IWindow* main = WindowManager.GetMainWindow()) {
			main->RemoveWindow(sSplash.backdrop);
		}
		sSplash.backdrop = nullptr;
	}
	movies->ContinueMovie();
	Logf("splash: hidden");
}

// The game calls these with `this` pointing at the IMovieSystem part of MovieSystem (its second base class),
// so the detours are declared on IMovieSystem; declared on MovieSystem, virtual calls on `this` crashed.
member_detour(PlayMovie_detour, Movie::IMovieSystem, bool(const ResourceKey&, const Movie::cMoviePlayInfo&))
{
	bool detoured(const ResourceKey& key, const Movie::cMoviePlayInfo& info)
	{
		bool result = original_function(this, key, info);
		Logf("movie: play %08x!%08x.%08x -> %d", key.groupID, key.instanceID, key.typeID, result ? 1 : 0);
		if (result && key.instanceID == kIntroMovie && sSplash.state == 0) {
			sSplash.state = 1;
			sSplash.since = GetTickCount();
		}
		return result;
	}
};

member_detour(MovieUpdate_detour, Movie::IMovieSystem, void(uint32_t))
{
	void detoured(uint32_t deltaMS)
	{
		original_function(this, deltaMS);
		DWORD now = GetTickCount();
		if (sSplash.state == 1 && now - sSplash.since >= kSplashAfterMs) {
			sSplash.since = now;
			sSplash.state = (this->MovieIsPlaying() && SplashShow(this)) ? 2 : 3;
		}
		else if (sSplash.state == 2 && now - sSplash.since >= kSplashShowMs) {
			sSplash.state = 3;
			SplashHide(this);
		}
	}
};

class CoopHelloCommand : public ArgScript::ICommand
{
public:
	void ParseLine(const ArgScript::Line& line) override
	{
		App::ConsolePrintF("CoopMod: hello from the co-op mod.");
		Logf("coophello cheat ran");
	}

	const char* GetDescription(ArgScript::DescriptionMode mode) const override
	{
		return "Prints a test line from the co-op mod.";
	}
};

// Usage: stage <cell|creature|tribe|civ|space>
class StageCommand : public ArgScript::ICommand
{
public:
	void ParseLine(const ArgScript::Line& line) override
	{
		auto args = line.GetArguments(1);
		if (!args) {
			return;
		}

		uint32_t mode = 0;
		const char* name = args[0];
		if (strcmp(name, "cell") == 0) mode = GameModeIDs::kGameCell;
		else if (strcmp(name, "creature") == 0) mode = GameModeIDs::kGameCreature;
		else if (strcmp(name, "tribe") == 0) mode = GameModeIDs::kGameTribe;
		else if (strcmp(name, "civ") == 0) mode = GameModeIDs::kGameCiv;
		else if (strcmp(name, "space") == 0) mode = GameModeIDs::kGameSpace;
		else {
			App::ConsolePrintF("Unknown stage '%s'.", name);
			return;
		}

		Logf("stage %s -> SetActiveMode(0x%X)", name, mode);
		bool ok = GameModeManager.SetActiveMode(mode);
		App::ConsolePrintF("SetActiveMode(%s) = %d", name, ok ? 1 : 0);
	}

	const char* GetDescription(ArgScript::DescriptionMode mode) const override
	{
		return "Switches the game mode: stage <cell|creature|tribe|civ|space>.";
	}
};

// ---------------------------------------------------------------- puppet

// The remote player is shown as a puppet: a one-animal herd whose AI is skipped (detour above)
// and whose transform is set from network packets.
static cCreatureAnimalPtr sPuppet;
static cHerdPtr sPuppetHerd;
static int sPuppetWaitFrames = 0;
static bool sPuppetInit = false;
static Vector3 sPuppetAnchor;

static void ReleasePuppet()
{
	sPuppetRaw = nullptr;
	if (sPuppetHerd) {
		GameNounManager.DestroyInstance(sPuppetHerd.get());
	}
	sPuppet = nullptr;
	sPuppetHerd = nullptr;
}

static ResourceKey sPuppetKey;  // the species the puppet was built from

// Returns true when the herd was created; the animal itself shows up in later frames (see PickUpPuppet).
static bool SpawnPuppet()
{
	auto avatar = GameNounManager.GetAvatar();
	if (!avatar) {
		return false;
	}
	// The puppet has the species of the OTHER player's avatar (each player has his own creature); until it is known and its model
	// is here, or when it does not come, the local avatar's species stands in.
	ResourceKey key = avatar->mSpeciesKey;
	uint32_t remote[3] = { 0, 0, 0 };
	int state = World::RemoteSpecies(remote);
	if (state == 1) {
		return false;  // wait for the model
	}
	if (state == 2) {
		key = ResourceKey(remote[0], remote[1], remote[2]);
	}
	auto species = SpeciesManager.GetSpeciesProfile(key);
	if (!species) {
		return false;
	}
	sPuppetKey = key;
	ReleasePuppet();
	sPuppetAnchor = avatar->GetPosition() + Vector3(8.0f, 0.0f, 0.0f);
	// cCreatureAnimal::Create without a herd crashed the game, so let the game build the herd.
	sPuppetHerd = GameNounManager.CreateHerd(sPuppetAnchor, species, 1, false, 0, false);
	sPuppetWaitFrames = 0;
	sPuppetInit = false;
	Logf(sPuppetHerd ? "puppet: herd created" : "puppet: CreateHerd returned null");
	return sPuppetHerd != nullptr;
}

static void PickUpPuppet()
{
	if (!sPuppetHerd || sPuppet) {
		return;
	}
	++sPuppetWaitFrames;
	if (!sPuppetHerd->mHerd.empty()) {
		sPuppet = sPuppetHerd->mHerd[0];
		sPuppetRaw = sPuppet.get();
		World::EnsureTuning(sPuppetRaw);
		Logf("puppet animal found after %d frames", sPuppetWaitFrames);
	}
}

// ---------------------------------------------------------------- steam (Spacewar, AppID 480)

// The Steam API is used through its flat C exports, loaded by hand from steam_api_coop.dll (a modern
// 32-bit steam_api.dll renamed, so it does not clash with the old stub the GOG build ships as steam_api.dll).
// Peers find each other by Steam ID; Steam relays the traffic, so no port forwarding is needed.
typedef int(__cdecl* SteamInitFlat_t)(char* errMsg);
typedef void*(__cdecl* SteamGetInterface_t)();
typedef uint64_t(__cdecl* SteamUserGetSteamID_t)(void* self);
typedef void(__cdecl* SteamVoid_t)();
typedef void(__cdecl* SteamSocketsRunCallbacks_t)(void* self);
typedef void(__cdecl* SteamIdentitySetSteamID64_t)(void* identity, uint64_t id);
typedef int(__cdecl* SteamMsgSend_t)(void* self, const void* identity, const void* data, uint32_t size, int flags, int channel);
typedef int(__cdecl* SteamMsgReceive_t)(void* self, int channel, void** outMessages, int maxMessages);
typedef bool(__cdecl* SteamMsgAccept_t)(void* self, const void* identity);
typedef void(__cdecl* SteamMsgCloseSession_t)(void* self, const void* identity);
typedef const char*(__cdecl* SteamGetPersonaName_t)(void* self, uint64_t steamId);
typedef bool(__cdecl* SteamRequestUserInfo_t)(void* self, uint64_t steamId, bool nameOnly);
typedef void(__cdecl* SteamMessageRelease_t)(void* message);
typedef void(__cdecl* SteamSessionRequestCallback_t)(void* request);
typedef void(__cdecl* SteamSetSessionRequestCallback_t)(void* utils, SteamSessionRequestCallback_t callback);
typedef void(__cdecl* SteamInitRelay_t)(void* utils);

static struct SteamApi
{
	HMODULE dll = nullptr;
	bool ready = false;
	uint64_t myId = 0;
	void* messages = nullptr;
	void* sockets = nullptr;
	void* utils = nullptr;

	SteamVoid_t RunCallbacks = nullptr;
	SteamSocketsRunCallbacks_t SocketsRunCallbacks = nullptr;
	SteamIdentitySetSteamID64_t SetSteamID64 = nullptr;
	SteamMsgSend_t Send = nullptr;
	SteamMsgReceive_t Receive = nullptr;
	SteamMsgAccept_t Accept = nullptr;
	SteamMsgCloseSession_t CloseSession = nullptr;
	SteamMessageRelease_t Release = nullptr;
	// optional (only for the names in the list of remembered hosts): an older steam_api without them still works
	void* friends = nullptr;
	SteamGetPersonaName_t GetPersonaName = nullptr;
	SteamRequestUserInfo_t RequestUserInfo = nullptr;
} sSteam;

static SteamVoid_t sSteamShutdown = nullptr;
static SteamSetSessionRequestCallback_t sSteamSetRequestCallback = nullptr;

// Called by Steam when an unknown user first messages us; the identity is the first field of the request.
static void __cdecl OnSessionRequest(void* request)
{
	if (sSteam.messages && sSteam.Accept) {
		bool ok = sSteam.Accept(sSteam.messages, request);
		Logf("steam: session request accepted = %d", ok ? 1 : 0);
	}
}

template <typename T>
static bool SteamLoadProc(T& fn, const char* name)
{
	fn = (T)GetProcAddress(sSteam.dll, name);
	if (!fn) {
		Logf("steam: missing export %s", name);
	}
	return fn != nullptr;
}

static bool SteamStart()
{
	if (sSteam.ready) {
		return true;
	}
	if (!sSteam.dll) {
		sSteam.dll = LoadLibraryA("steam_api_coop.dll");
		if (!sSteam.dll) {
			Logf("steam: steam_api_coop.dll not found (error %lu)", GetLastError());
			return false;
		}
	}
	// Spacewar. Setting the environment is enough, steam_appid.txt next to the exe also works.
	SetEnvironmentVariableA("SteamAppId", "480");
	SetEnvironmentVariableA("SteamGameId", "480");

	SteamInitFlat_t initFlat = nullptr;
	SteamGetInterface_t getUser = nullptr, getMessages = nullptr, getSockets = nullptr, getUtils = nullptr;
	SteamUserGetSteamID_t getSteamId = nullptr;
	SteamSetSessionRequestCallback_t setRequestCallback = nullptr;
	SteamInitRelay_t initRelay = nullptr;
	bool ok = SteamLoadProc(initFlat, "SteamAPI_InitFlat")
		&& SteamLoadProc(getUser, "SteamAPI_SteamUser_v023")
		&& SteamLoadProc(getSteamId, "SteamAPI_ISteamUser_GetSteamID")
		&& SteamLoadProc(getMessages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002")
		&& SteamLoadProc(getSockets, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012")
		&& SteamLoadProc(getUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004")
		&& SteamLoadProc(sSteam.RunCallbacks, "SteamAPI_RunCallbacks")
		&& SteamLoadProc(sSteam.SocketsRunCallbacks, "SteamAPI_ISteamNetworkingSockets_RunCallbacks")
		&& SteamLoadProc(sSteam.SetSteamID64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64")
		&& SteamLoadProc(sSteam.Send, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser")
		&& SteamLoadProc(sSteam.Receive, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel")
		&& SteamLoadProc(sSteam.Accept, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser")
		&& SteamLoadProc(sSteam.CloseSession, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser")
		&& SteamLoadProc(sSteam.Release, "SteamAPI_SteamNetworkingMessage_t_Release")
		&& SteamLoadProc(setRequestCallback, "SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionRequest")
		&& SteamLoadProc(initRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess")
		&& SteamLoadProc(sSteamShutdown, "SteamAPI_Shutdown");
	if (!ok) {
		return false;
	}

	char errMsg[1024] = {};
	int result = initFlat(errMsg);
	if (result != 0) {
		Logf("steam: InitFlat failed (%d): %s", result, errMsg);
		return false;
	}

	sSteam.myId = getSteamId(getUser());
	sSteam.messages = getMessages();
	sSteam.sockets = getSockets();
	sSteam.utils = getUtils();
	sSteamSetRequestCallback = setRequestCallback;
	setRequestCallback(sSteam.utils, OnSessionRequest);
	initRelay(sSteam.utils);
	SteamGetInterface_t getFriends = (SteamGetInterface_t)GetProcAddress(sSteam.dll, "SteamAPI_SteamFriends_v017");
	sSteam.GetPersonaName = (SteamGetPersonaName_t)GetProcAddress(sSteam.dll, "SteamAPI_ISteamFriends_GetFriendPersonaName");
	sSteam.RequestUserInfo = (SteamRequestUserInfo_t)GetProcAddress(sSteam.dll, "SteamAPI_ISteamFriends_RequestUserInformation");
	sSteam.friends = getFriends ? getFriends() : nullptr;
	sSteam.ready = true;
	Logf("steam: ready, my Steam ID %llu", (unsigned long long)sSteam.myId);
	return true;
}

// Called from Dispose. Steam keeps its own threads and holds a callback into this DLL, and the loader unloads
// the DLL right after Dispose: without shutting Steam down, a Steam thread calls into freed memory and the
// game process crashes or hangs on exit (Windows Event 1000, fault offset 0xffffffff).
static void SteamStop()
{
	if (!sSteam.ready) {
		return;
	}
	sSteam.ready = false;
	if (sSteamSetRequestCallback) {
		sSteamSetRequestCallback(sSteam.utils, nullptr);
	}
	if (sSteamShutdown) {
		sSteamShutdown();
	}
	sSteam.messages = sSteam.sockets = sSteam.utils = nullptr;
	Logf("steam: shut down");
}

// ---------------------------------------------------------------- network

// Bump when the packet layout or the save transfer changes; players with different versions refuse each other.
static const uint32_t kProtocolVersion = 13;

#pragma pack(push, 1)
struct StatePacket
{
	uint32_t magic;  // 'SPCO'
	uint32_t version;
	uint32_t type;   // 0 = ping, 1 = state
	uint32_t seq;
	uint64_t sender; // sender's Steam ID (0 over plain UDP)
	float pos[3];
	float rot[4];
};
#pragma pack(pop)

static const uint32_t kMagic = 0x4F435053;
static const uint16_t kDefaultPort = 27777;
static const DWORD kSendIntervalMs = 33;

enum class Transport { None, Udp, Steam };

static Transport sTransport = Transport::None;
static SOCKET sSocket = INVALID_SOCKET;
static sockaddr_in sPeer;
static uint64_t sPeerSteamId = 0;
static bool sHavePeer = false;
static bool sIsHost = false;
static uint32_t sSeq = 0;
static DWORD sLastSend = 0;
static uint32_t sReceived = 0;
static DWORD sLastReceive = 0;
static bool sHostSeen = false;    // client: the host has answered since the last join
static const DWORD kReconnectMs = 10000;

// Absolute mode (default) puts the puppet exactly where the remote avatar is, which only makes sense when
// both players are on the same planet (the host sends its save, see "world transfer"). Relative mode shows
// the remote movement around an anchor next to the local player, for two unrelated games.
static bool sAbsolute = true;
static bool sSendWorld = true;   // host: send the save game to a client when it connects
static bool sGotFirst = false;
static Vector3 sPuppetPos;
static float sPuppetRot[4];
static DWORD sPuppetLast = 0;
// `coop echo`: a puppet that repeats our own avatar 0.4 s later, 4 m to the side, through the same puppet code as the
// network. Tests the puppet's movement on one PC.
static bool sEcho = false;
static bool sEchoStarted = false;  // the echo has its offset and an empty sample buffer
static Vector3 sRemoteFirst;
static StatePacket sLatest;
static bool sHaveLatest = false;

static void NetStop()
{
	World::Reset();
	if (sTransport == Transport::Udp && sSocket != INVALID_SOCKET) {
		closesocket(sSocket);
		sSocket = INVALID_SOCKET;
		WSACleanup();
	}
	if (sTransport == Transport::Steam && sSteam.ready && sPeerSteamId) {
		alignas(8) unsigned char identity[256] = {};
		sSteam.SetSteamID64(identity, sPeerSteamId);
		sSteam.CloseSession(sSteam.messages, identity);
	}
	sTransport = Transport::None;
	sHavePeer = false;
	sPeerSteamId = 0;
	sPuppetInit = false;
	sGotFirst = false;
	sHaveLatest = false;
	sReceived = 0;
	sHostSeen = false;
}

static bool UdpStart(bool host, const char* ip, uint16_t port)
{
	NetStop();
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		Logf("net: WSAStartup failed");
		return false;
	}
	sSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sSocket == INVALID_SOCKET) {
		Logf("net: socket failed (%d)", WSAGetLastError());
		WSACleanup();
		return false;
	}
	u_long nonBlocking = 1;
	ioctlsocket(sSocket, FIONBIO, &nonBlocking);

	sockaddr_in local = {};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(host ? port : 0);
	if (bind(sSocket, (sockaddr*)&local, sizeof(local)) != 0) {
		Logf("net: bind failed (%d)", WSAGetLastError());
		closesocket(sSocket);
		sSocket = INVALID_SOCKET;
		WSACleanup();
		return false;
	}

	sTransport = Transport::Udp;
	sIsHost = host;
	sHavePeer = false;
	if (!host) {
		sPeer = {};
		sPeer.sin_family = AF_INET;
		sPeer.sin_port = htons(port);
		if (inet_pton(AF_INET, ip, &sPeer.sin_addr) != 1) {
			Logf("net: bad address '%s'", ip);
			NetStop();
			return false;
		}
		sHavePeer = true;
	}
	Logf("net: udp %s on port %u%s%s", host ? "hosting" : "joining", (unsigned)port, host ? "" : " -> ", host ? "" : ip);
	return true;
}

static bool SteamHostStart()
{
	NetStop();
	if (!SteamStart()) {
		return false;
	}
	sTransport = Transport::Steam;
	sIsHost = true;
	sHavePeer = false;
	return true;
}

static bool SteamJoinStart(uint64_t hostId)
{
	NetStop();
	if (!SteamStart()) {
		return false;
	}
	sTransport = Transport::Steam;
	sIsHost = false;
	sPeerSteamId = hostId;
	sHavePeer = true;
	Logf("steam: joining %llu", (unsigned long long)hostId);
	return true;
}

static void NetSend(uint32_t type, const Vector3& pos, const Math::Quaternion& rot)
{
	StatePacket p;
	p.magic = kMagic;
	p.version = kProtocolVersion;
	p.type = type;
	p.seq = ++sSeq;
	p.sender = sTransport == Transport::Steam ? sSteam.myId : 0;
	p.pos[0] = pos.x; p.pos[1] = pos.y; p.pos[2] = pos.z;
	p.rot[0] = rot.x; p.rot[1] = rot.y; p.rot[2] = rot.z; p.rot[3] = rot.w;
	if (sTransport == Transport::Udp) {
		sendto(sSocket, (const char*)&p, sizeof(p), 0, (sockaddr*)&sPeer, sizeof(sPeer));
	}
	else if (sTransport == Transport::Steam) {
		alignas(8) unsigned char identity[256] = {};
		sSteam.SetSteamID64(identity, sPeerSteamId);
		// 5 = k_nSteamNetworkingSend_UnreliableNoDelay (unreliable + no nagle + no delay)
		sSteam.Send(sSteam.messages, identity, &p, sizeof(p), 5, 0);
	}
}

static bool sWarnedVersion = false;

static void WarnVersion(const char* detail)
{
	if (!sWarnedVersion) {
		sWarnedVersion = true;
		Logf("net: version mismatch (%s), mine is %u", detail, (unsigned)kProtocolVersion);
		App::ConsolePrintF("coop: the other player has a different mod version. Both run update.bat, then restart.");
	}
}

static void HandlePacket(const StatePacket& p)
{
	++sReceived;
	if (p.type == 1) {
		sLatest = p;
		sHaveLatest = true;
	}
}

// ---------------------------------------------------------------- world transfer (Steam only)

// The host sends the files of its save game folder (Games\Game0, about 3 MB) so the client can load the
// same world. Files travel as reliable messages on channel 1, a few chunks per frame because Steam's send
// buffer is small. The client keeps the received world in Games\Coop\<host id>.
// The running game keeps planetRecords.pkp/.pkt and PlanetScripts.pld open (read share only) from startup,
// so Game0 cannot be replaced while it runs: the world waits in Games\Coop\<host id> and CoopLaunch::Prepare()
// swaps it in for the next launch (the "co-op launch"), before the game opens its save. The client's own Game0
// is never touched for good (core/coop_launch.h).
static const uint32_t kFileMagic = 0x4C465053;  // 'SPFL'
static const uint32_t kChunk = 60 * 1024;
static const int kFileChannel = 1;

#pragma pack(push, 1)
struct FilePacket
{
	uint32_t magic;
	uint32_t fileIndex;
	uint32_t fileCount;
	char name[64];
	uint32_t offset;
	uint32_t totalSize;
	uint32_t dataLen;
	unsigned char data[kChunk];
};
#pragma pack(pop)
static const size_t kFileHeader = offsetof(FilePacket, data);

// Written into a received world (Coop\<id>, in a co-op launch Game0): the host's Steam ID.
static const char* kSaveHostFile = "coop_host.txt";

static std::string GamesDir()
{
	const char* appdata = getenv("APPDATA");
	return std::string(appdata ? appdata : "") + "\\Spore\\Games";
}

static void DeleteDir(const std::string& dir)
{
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			std::string name = fd.cFileName;
			if (name == "." || name == "..") {
				continue;
			}
			std::string path = dir + "\\" + name;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				DeleteDir(path);
			}
			else {
				DeleteFileA(path.c_str());
			}
		} while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryA(dir.c_str());
}

// Set when this run is a co-op launch (CoopLaunch::Prepare put the host's world in place of Game0): a world sent again
// (the client joins again after the restart and the host sends on every first contact) is then ignored instead of
// asking for another restart.
static bool sWorldInstalledAtStart = false;
static float sWorldProgress = -1;  // client: share of the world received (0..1), -1 = none arriving (CoopNet::State)
static bool sWorldStaged = false;
static bool sWorldIgnoredNotice = false;

// After a world is received the mod restarts the game by itself (the running game keeps Game0 locked, so the world is
// swapped in at the next start) and joins the same host again at that start. The host's Steam ID travels in
// Games\CoopLaunch.txt, which the next start reads once and deletes (core/coop_launch.h). A restart without that file is
// Leave. COOP_NO_RESTART turns the automatic restart after a download off.
static DWORD sRestartAt = 0;  // GetTickCount() deadline of the restart, 0 = none planned
static bool sRestartDone = false;

static struct WorldSend
{
	bool active = false;
	std::vector<std::string> names;
	size_t file = 0;
	uint32_t offset = 0;
	bool loaded = false;
	std::vector<unsigned char> data;
} sWorld;

static void WorldSendStart()
{
	sWorld = WorldSend();
	std::string dir = GamesDir() + "\\Game0";
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) {
		Logf("world: no save folder %s", dir.c_str());
		return;
	}
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strlen(fd.cFileName) < sizeof(((FilePacket*)0)->name)
			&& fd.nFileSizeLow <= 8u * 1024 * 1024) {
			sWorld.names.push_back(fd.cFileName);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	sWorld.active = !sWorld.names.empty();
	Logf("world: sending %d files from %s", (int)sWorld.names.size(), dir.c_str());
	if (sWorld.active) {
		App::ConsolePrintF("coop: sending the world (%d files)...", (int)sWorld.names.size());
	}
}

static void WorldSendPump()
{
	if (!sWorld.active || !sSteam.ready || !sPeerSteamId) {
		return;
	}
	alignas(8) unsigned char identity[256] = {};
	sSteam.SetSteamID64(identity, sPeerSteamId);
	static FilePacket packet;

	for (int budget = 0; budget < 3 && sWorld.active; ++budget) {
		const std::string& name = sWorld.names[sWorld.file];
		if (!sWorld.loaded) {
			sWorld.data.clear();
			if (FILE* f = fopen((GamesDir() + "\\Game0\\" + name).c_str(), "rb")) {
				fseek(f, 0, SEEK_END);
				long size = ftell(f);
				fseek(f, 0, SEEK_SET);
				sWorld.data.resize(size > 0 ? size : 0);
				if (size > 0) {
					fread(sWorld.data.data(), 1, size, f);
				}
				fclose(f);
			}
			sWorld.loaded = true;
			sWorld.offset = 0;
		}

		uint32_t total = (uint32_t)sWorld.data.size();
		uint32_t len = total - sWorld.offset;
		if (len > kChunk) {
			len = kChunk;
		}
		packet.magic = kFileMagic;
		packet.fileIndex = (uint32_t)sWorld.file;
		packet.fileCount = (uint32_t)sWorld.names.size();
		memset(packet.name, 0, sizeof(packet.name));
		strcpy(packet.name, name.c_str());
		packet.offset = sWorld.offset;
		packet.totalSize = total;
		packet.dataLen = len;
		if (len) {
			memcpy(packet.data, sWorld.data.data() + sWorld.offset, len);
		}
		// 8 = k_nSteamNetworkingSend_Reliable; the result 1 is k_EResultOK. Anything else: try again next frame.
		int result = sSteam.Send(sSteam.messages, identity, &packet, (uint32_t)(kFileHeader + len), 8, kFileChannel);
		if (result != 1) {
			return;
		}
		sWorld.offset += len;
		if (sWorld.offset >= total) {
			sWorld.loaded = false;
			if (++sWorld.file >= sWorld.names.size()) {
				sWorld.active = false;
				Logf("world: all files sent");
				App::ConsolePrintF("coop: world sent.");
			}
		}
	}
}

static void WorldReceiveChunk(const unsigned char* bytes, int size)
{
	if (size < (int)kFileHeader) {
		return;
	}
	const FilePacket& p = *(const FilePacket*)bytes;
	if (p.magic != kFileMagic || p.dataLen > kChunk || (int)(kFileHeader + p.dataLen) > size || p.fileIndex >= p.fileCount) {
		return;
	}
	char name[65];
	memcpy(name, p.name, 64);
	name[64] = 0;
	if (!name[0] || strpbrk(name, "\\/:") || strstr(name, "..")) {
		Logf("world: rejected file name");
		return;
	}

	// Only take a world in the main menu of a run that has not just installed one; the host also sends it
	// when it thinks the client reconnected (see SteamReceive), which can happen in the middle of a game.
	if (sWorldInstalledAtStart || Simulator::IsCreatureGame()) {
		if (!sWorldIgnoredNotice) {
			sWorldIgnoredNotice = true;
			Logf("world: ignoring the host's copy (%s)", sWorldInstalledAtStart ? "installed at start" : "in a game");
		}
		return;
	}

	std::string games = GamesDir();
	// Coop\<host id>.incoming while the download runs; renamed to Coop\<host id> only when it is complete
	std::string coopDir = games + "\\Coop";
	std::string worldDir = coopDir + "\\" + std::to_string((unsigned long long)sPeerSteamId);
	std::string incoming = worldDir + ".incoming";
	if (p.fileIndex == 0 && p.offset == 0) {
		CreateDirectoryA(coopDir.c_str(), nullptr);
		DeleteDir(incoming);
		CreateDirectoryA(incoming.c_str(), nullptr);
		App::ConsolePrintF("coop: receiving the world...");
	}
	std::string path = incoming + "\\" + name;
	if (FILE* f = fopen(path.c_str(), p.offset == 0 ? "wb" : "r+b")) {
		fseek(f, (long)p.offset, SEEK_SET);
		if (p.dataLen) {
			fwrite(p.data, 1, p.dataLen, f);
		}
		fclose(f);
	}
	else {
		Logf("world: cannot write %s", path.c_str());
		return;
	}

	if (p.fileCount && p.totalSize) {
		sWorldProgress = (p.fileIndex + (float)(p.offset + p.dataLen) / p.totalSize) / p.fileCount;
	}
	bool last = p.fileIndex + 1 == p.fileCount && p.offset + p.dataLen >= p.totalSize;
	if (last) {
		// remember whose world this is, so the co-op indicator can tell the host's world from the player's own
		if (FILE* f = fopen((incoming + "\\" + kSaveHostFile).c_str(), "w")) {
			fprintf(f, "%llu\n", (unsigned long long)sPeerSteamId);
			fclose(f);
		}
		// the older copy of this host's world is only a cache (the host's save is the real one): replaced by the new one
		DeleteDir(worldDir);
		bool staged = MoveFileA(incoming.c_str(), worldDir.c_str()) != 0 && coop::launch::WriteLaunch(games, sPeerSteamId);
		sWorldStaged = staged;
		Logf("world: received %d files, staged %d (error %lu)", (int)p.fileCount, staged ? 1 : 0, staged ? 0 : GetLastError());
		if (staged && !getenv("COOP_NO_RESTART")) {
			sRestartAt = GetTickCount() + 3000;
			App::ConsolePrintF("coop: world received. The game restarts by itself in 3 s and opens the host's world (your own saves stay as they are).");
		}
		else if (staged) {
			App::ConsolePrintF("coop: world received. Restart the game to open it (your own saves stay as they are); the new start joins the host again,");
			App::ConsolePrintF("coop: then choose Continue.");
		}
		else {
			App::ConsolePrintF("coop: could not store the received world (see CoopMod.log).");
		}
	}
}

static void UdpReceive()
{
	for (;;) {
		StatePacket p;
		sockaddr_in from;
		int fromLen = sizeof(from);
		int n = recvfrom(sSocket, (char*)&p, sizeof(p), 0, (sockaddr*)&from, &fromLen);
		if (n < 0) {
			return;  // nothing waiting (WSAEWOULDBLOCK)
		}
		if (n != (int)sizeof(p)) {
			WarnVersion("packet size");
			continue;
		}
		if (p.magic != kMagic) {
			continue;
		}
		if (p.version != kProtocolVersion) {
			WarnVersion("packet version");
			continue;
		}
		if (sIsHost && !sHavePeer) {
			sPeer = from;
			sHavePeer = true;
			char text[64];
			inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
			Logf("net: client connected from %s:%u", text, (unsigned)ntohs(from.sin_port));
			App::ConsolePrintF("coop: client connected from %s", text);
		}
		HandlePacket(p);
	}
}

static void RememberHost(uint64_t steamId, bool first);  // below, next to NetUpdate

static void SteamReceive()
{
	void* messages[16];
	for (;;) {
		int n = sSteam.Receive(sSteam.messages, 0, messages, 16);
		if (n <= 0) {
			return;
		}
		for (int i = 0; i < n; ++i) {
			// SteamNetworkingMessage_t starts with: void* m_pData; int m_cbSize;
			const unsigned char* msg = (const unsigned char*)messages[i];
			const void* data = *(void* const*)msg;
			int size = *(const int*)(msg + sizeof(void*));
			if (size != (int)sizeof(StatePacket)) {
					WarnVersion("message size");
				}
				else {
				StatePacket p;
				memcpy(&p, data, sizeof(p));
				if (p.magic == kMagic && p.version != kProtocolVersion) {
						WarnVersion("message version");
					}
					else if (p.magic == kMagic) {
					DWORD now = GetTickCount();
					// A client that was silent for a while (it restarted the game, e.g. to install the world) or a
					// different client counts as a new connection and gets the world again; a client that has just
					// installed it ignores the copy.
					bool reconnect = sHavePeer && (p.sender != sPeerSteamId || now - sLastReceive > kReconnectMs);
					if (sIsHost && p.sender && (!sHavePeer || reconnect)) {
						sPeerSteamId = p.sender;
						sHavePeer = true;
						Logf("steam: client %s, Steam ID %llu", reconnect ? "reconnected" : "connected", (unsigned long long)p.sender);
						App::ConsolePrintF("coop: client %s (Steam ID %llu)", reconnect ? "reconnected" : "connected",
							(unsigned long long)p.sender);
						if (sSendWorld) {
							WorldSendStart();
						}
						World::HostPeerConnected();
					}
					if (!sIsHost && !sHostSeen) {
						sHostSeen = true;
						Logf("steam: host answered");
						App::ConsolePrintF("coop: connected to the host.");
						RememberHost(sPeerSteamId, true);
						if (sWorldInstalledAtStart) {
							App::ConsolePrintF("coop: the host's world is installed. Choose Continue and enter the creature stage.");
						}
					}
					sLastReceive = now;
					HandlePacket(p);
				}
			}
			sSteam.Release(messages[i]);
		}
	}
}

static void SteamReceiveFiles()
{
	void* messages[16];
	for (;;) {
		int n = sSteam.Receive(sSteam.messages, kFileChannel, messages, 16);
		if (n <= 0) {
			return;
		}
		for (int i = 0; i < n; ++i) {
			const unsigned char* msg = (const unsigned char*)messages[i];
			const unsigned char* data = *(const unsigned char* const*)msg;
			int size = *(const int*)(msg + sizeof(void*));
			if (!sIsHost) {
				WorldReceiveChunk(data, size);
			}
			sSteam.Release(messages[i]);
		}
	}
}

// The shared world (World.cpp): entity deltas on channel 2 (unreliable), the rest on channel 3 (reliable, ordered).
static bool WorldSendOn(const void* data, uint32_t size, int flags, int channel)
{
	if (!sSteam.ready || !sPeerSteamId) {
		return false;
	}
	alignas(8) unsigned char identity[256] = {};
	sSteam.SetSteamID64(identity, sPeerSteamId);
	return sSteam.Send(sSteam.messages, identity, data, size, flags, channel) == 1;
}

static bool WorldSendUnreliable(const void* data, uint32_t size) { return WorldSendOn(data, size, 5, World::kChannelDelta); }
static bool WorldSendReliable(const void* data, uint32_t size) { return WorldSendOn(data, size, 8, World::kChannelReliable); }

static void SteamReceiveWorld()
{
	void* messages[16];
	for (int channel : { World::kChannelDelta, World::kChannelReliable }) {
		for (;;) {
			int n = sSteam.Receive(sSteam.messages, channel, messages, 16);
			if (n <= 0) {
				break;
			}
			for (int i = 0; i < n; ++i) {
				const unsigned char* msg = (const unsigned char*)messages[i];
				const unsigned char* data = *(const unsigned char* const*)msg;
				int size = *(const int*)(msg + sizeof(void*));
				if (size > 0) {
					World::OnMessage(channel, data, (uint32_t)size, sIsHost);
				}
				sSteam.Release(messages[i]);
			}
		}
	}
}

// ---------------------------------------------------------------- co-op indicator
// Shows the co-op state where the player sees it: in the game window's title, and as an event log message
// (the alerts on the left of the screen) when the creature stage starts. Most important for the client:
// whether the loaded save really is the host's world or the player's own.
static HWND sGameWindow = nullptr;
static std::wstring sBaseTitle;
static std::string sIndicator;
static uint32_t sEventId = 0;
static bool sEventTried = false;

// Steam ID of the host whose world Games\Game0 holds (written when the world is received), 0 if none.
static uint64_t SaveHostId()
{
	unsigned long long id = 0;
	if (FILE* f = fopen((GamesDir() + "\\Game0\\" + kSaveHostFile).c_str(), "r")) {
		if (fscanf(f, "%llu", &id) != 1) {
			id = 0;
		}
		fclose(f);
	}
	return id;
}

static BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
		sGameWindow = hwnd;
		return FALSE;
	}
	return TRUE;
}

// The event log item is borrowed from the space stage popups; guarded in case it misbehaves outside space.
static uint32_t ShowEventGuarded(const char16_t* text)
{
	uint32_t event = 0;
	__try {
		event = UIEventLog.ShowEvent(id("GenericMissionProgress"), GroupIDs::SpacePopups);
		if (event) {
			UIEventLog.SetDescription(event, text);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		event = 0;
	}
	return event;
}

static void UpdateIndicator(const std::string& text, bool inGame)
{
	if (text != sIndicator) {
		sIndicator = text;
		if (!sGameWindow) {
			EnumWindows(FindGameWindow, 0);
		}
		if (sGameWindow) {
			if (sBaseTitle.empty()) {
				wchar_t title[256] = {};
				GetWindowTextW(sGameWindow, title, 256);
				sBaseTitle = title;
			}
			std::wstring full = sBaseTitle;
			if (!text.empty()) {
				full += L"  |  " + std::wstring(text.begin(), text.end());
			}
			SetWindowTextW(sGameWindow, full.c_str());
		}
		sEventTried = false;  // a new state gets a new message
	}
	if (!inGame) {
		sEventTried = false;  // show it again on the next entry into the creature stage
		sEventId = 0;
		return;
	}
	if (!sEventTried && !text.empty()) {
		sEventTried = true;
		std::u16string text16(text.begin(), text.end());
		if (sEventId) {
			UIEventLog.SetDescription(sEventId, text16.c_str());
		}
		else {
			sEventId = ShowEventGuarded(text16.c_str());
			Logf("indicator: event log item %u", sEventId);
		}
	}
}

static std::string IndicatorText()
{
	if (sTransport == Transport::None) {
		return "";
	}
	if (sIsHost) {
		return sHavePeer && sReceived ? "CO-OP HOST: a player is connected" : "CO-OP HOST: waiting for a player";
	}
	if (!sHostSeen) {
		return "CO-OP: connecting to the host...";
	}
	uint64_t saveHost = SaveHostId();
	if (sTransport == Transport::Steam && saveHost != sPeerSteamId) {
		return "CO-OP: connected, but this save is YOUR OWN world, not the host's";
	}
	return "CO-OP: you are in the HOST's world";
}

static bool sDisposed = false;

// Frame statistics (`coop fps`): game frames per second and whether the game window has the focus. A game that
// stops running when it is not in the foreground freezes the other player's mirror of the host's world.
static bool sFpsLog = false;
static DWORD sFpsStart = 0;
static int sFpsFrames = 0;
static int sFps = 0;

static bool GameWindowFocused()
{
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

static void FrameStats()
{
	DWORD now = GetTickCount();
	if (!sFpsStart) {
		sFpsStart = now;
	}
	++sFpsFrames;
	if (now - sFpsStart >= 2000) {
		sFps = (int)(sFpsFrames * 1000 / (now - sFpsStart));
		if (sFpsLog) {
			Logf("fps: %d (game window %s)", sFps, GameWindowFocused() ? "focused" : "NOT focused");
		}
		sFpsFrames = 0;
		sFpsStart = now;
	}
}

// The repo's console script (scripts\console.ps1): the launcher's COOP_CONSOLE_PS1 if it points to a file, else looked for
// up to 5 folders above the game folder (test\SPORE_A\SporebinEP1 -> <repo>\scripts\console.ps1). Empty on a player's install.
static std::wstring FindConsoleScript()
{
	const wchar_t* fromEnv = _wgetenv(L"COOP_CONSOLE_PS1");
	if (fromEnv && *fromEnv && GetFileAttributesW(fromEnv) != INVALID_FILE_ATTRIBUTES) {
		return fromEnv;
	}
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring up = exe;
	for (int level = 0; level < 6; ++level) {
		size_t cut = up.find_last_of(L'\\');
		if (cut == std::wstring::npos) {
			break;
		}
		up.erase(cut);
		if (level == 0) {
			continue;  // the folder of the exe itself
		}
		for (const wchar_t* tail : { L"\\scripts\\console.ps1", L"\\console.ps1" }) {
			std::wstring candidate = up + tail;
			if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
				return candidate;
			}
		}
	}
	return std::wstring();
}

// Every start of the game (also after the automatic restart) opens the console window, unless COOP_NO_CONSOLE is set
// (launch.ps1 -NoConsole) or there is no console script (a player's install).
static void OpenConsoleWindow()
{
	if (getenv("COOP_NO_CONSOLE")) {
		return;
	}
	std::wstring script = FindConsoleScript();
	if (script.empty()) {
		Logf("console: no console.ps1 found, no console window");
		return;
	}
	std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + script + L"\"";
	STARTUPINFOW startup = { sizeof(startup) };
	PROCESS_INFORMATION process = {};
	if (CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process)) {
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		Logf("console: window opened (%ls)", script.c_str());
	}
	else {
		Logf("console: could not open the window (error %lu)", GetLastError());
	}
}

// Starts a hidden PowerShell that waits for this game to end and then starts it again (with `-w`). The new game opens
// its own console window (OpenConsoleWindow).
static void LaunchRestartHelper()
{
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dir = exe;
	dir.erase(dir.find_last_of(L'\\'));
	wchar_t temp[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, temp);
	std::wstring script = std::wstring(temp) + L"coop_restart.ps1";

	FILE* f = _wfopen(script.c_str(), L"w, ccs=UTF-8");
	if (!f) {
		Logf("restart: cannot write the helper script");
		return;
	}
	fwprintf(f,
		L"param([int]$GamePid, [string]$Exe, [string]$Dir)\n"
		L"Wait-Process -Id $GamePid -Timeout 90 -ErrorAction SilentlyContinue\n"
		L"Start-Sleep -Seconds 1\n"
		L"Start-Process -FilePath $Exe -ArgumentList '-w' -WorkingDirectory $Dir\n");
	fclose(f);

	std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\" -GamePid " +
		std::to_wstring(GetCurrentProcessId()) + L" -Exe \"" + exe + L"\" -Dir \"" + dir + L"\"";
	STARTUPINFOW startup = { sizeof(startup) };
	PROCESS_INFORMATION process = {};
	if (CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, temp, &startup, &process)) {
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		Logf("restart: helper started, the game will start again after it exits");
	}
	else {
		Logf("restart: could not start the helper (error %lu)", GetLastError());
	}
}
// Main thread: when the planned time has come, start the helper and close the game the normal way (WM_CLOSE, so it saves).
static void RunRestartIfDue()
{
	if (!sRestartAt || sRestartDone || (int)(GetTickCount() - sRestartAt) < 0) {
		return;
	}
	sRestartDone = true;
	LaunchRestartHelper();
	if (!sGameWindow) {
		EnumWindows(FindGameWindow, 0);
	}
	if (sGameWindow) {
		PostMessageW(sGameWindow, WM_CLOSE, 0, 0);
	}
	else {
		Logf("restart: no game window found, close the game by hand");
		App::ConsolePrintF("coop: close the game by hand, it will start again by itself.");
	}
}

// Spawns the puppet when remote states arrive and moves it to the newest one.
static void DrivePuppet(DWORD now, bool inGame)
{
	if (!inGame) {
		return;
	}
	// the other player came back from the editor with another creature: build his puppet again
	if (sPuppetHerd) {
		uint32_t remote[3] = { 0, 0, 0 };
		if (World::RemoteSpecies(remote) == 2 && (remote[0] != sPuppetKey.instanceID || remote[2] != sPuppetKey.groupID)) {
			Logf("puppet: the other player's species changed, building the puppet again");
			ReleasePuppet();
		}
	}
	if (sHaveLatest && !sPuppetHerd) {
		if (SpawnPuppet()) {
			App::ConsolePrintF("coop: remote player appeared.");
		}
	}
	PickUpPuppet();
	if (sHaveLatest && sPuppet) {
		Vector3 remote(sLatest.pos[0], sLatest.pos[1], sLatest.pos[2]);
		if (!sGotFirst) {
			sGotFirst = true;
			sRemoteFirst = remote;
		}
		Vector3 target = sAbsolute ? remote : sPuppetAnchor + (remote - sRemoteFirst);

		// Smooth the movement between packets (exponential approach); snap on the first frame and on jumps.
		float dt = (float)(now - sPuppetLast) / 1000.0f;
		sPuppetLast = now;
		if (dt < 0.0f || dt > 0.25f) {
			dt = 0.016f;
		}
		const float* tr = sLatest.rot;
		if (!sPuppetInit || (target - sPuppetPos).Length() > 8.0f) {
			sPuppetInit = true;
			PuppetMotion::Reset();
			sPuppetPos = target;
			memcpy(sPuppetRot, tr, sizeof(sPuppetRot));
		}
		else {
			float a = 1.0f - expf(-dt * 14.0f);
			sPuppetPos = sPuppetPos + (target - sPuppetPos) * a;
			// normalized lerp of the rotation, taking the short way round
			float dot = sPuppetRot[0] * tr[0] + sPuppetRot[1] * tr[1] + sPuppetRot[2] * tr[2] + sPuppetRot[3] * tr[3];
			float sign = dot < 0.0f ? -1.0f : 1.0f;
			float q[4], len = 0.0f;
			for (int i = 0; i < 4; ++i) {
				q[i] = sPuppetRot[i] * (1.0f - a) + tr[i] * sign * a;
				len += q[i] * q[i];
			}
			len = sqrtf(len);
			if (len > 0.0001f) {
				for (int i = 0; i < 4; ++i) {
					sPuppetRot[i] = q[i] / len;
				}
			}
		}

		// the game's locomotion walks the puppet there (leg animation), see game/PuppetMotion.cpp
		PuppetMotion::Drive(sPuppet.get(), sPuppetPos, Math::Quaternion(sPuppetRot[0], sPuppetRot[1], sPuppetRot[2], sPuppetRot[3]));
	}}

// `coop echo`: feeds sLatest from our own avatar, delayed and shifted, instead of from the network.
static void EchoFeed(DWORD now)
{
	struct Sample { DWORD tick; Vector3 pos; Math::Quaternion rot; };
	static Sample samples[64];
	static int count = 0, next = 0;
	static Vector3 side;
	auto avatar = GameNounManager.GetAvatar();
	if (!avatar) {
		return;
	}
	if (!sEchoStarted) {
		sEchoStarted = true;
		// a fixed sideways offset, taken from the avatar's facing when the echo starts
		Vector3 right = avatar->mOrientation.ToMatrix() * Vector3(1.0f, 0.0f, 0.0f);
		side = right * 4.0f;
		count = next = 0;
	}
	samples[next] = { now, avatar->GetPosition(), avatar->mOrientation };
	next = (next + 1) % 64;
	if (count < 64) {
		++count;
	}
	// the newest sample that is at least 400 ms old
	const Sample* pick = nullptr;
	for (int k = 1; k <= count; ++k) {
		const Sample& s = samples[(next - k + 64) % 64];
		if (now - s.tick >= 400) {
			pick = &s;
			break;
		}
	}
	if (!pick) {
		return;
	}
	Vector3 pos = pick->pos + side;
	sLatest.type = 1;
	sLatest.pos[0] = pos.x; sLatest.pos[1] = pos.y; sLatest.pos[2] = pos.z;
	sLatest.rot[0] = pick->rot.x; sLatest.rot[1] = pick->rot.y; sLatest.rot[2] = pick->rot.z; sLatest.rot[3] = pick->rot.w;
	sHaveLatest = true;
}

// The host's Steam name, or "" while Steam does not know it yet (it is fetched in the background).
static std::string SteamName(uint64_t steamId)
{
	if (!sSteam.ready || !sSteam.friends || !sSteam.GetPersonaName) {
		return "";
	}
	const char* name = sSteam.GetPersonaName(sSteam.friends, steamId);
	if (!name || !*name || strcmp(name, "[unknown]") == 0) {
		return "";
	}
	return name;
}

// Puts the host into the list of remembered hosts (RecentHosts) with its Steam name; if the name is not known yet,
// asks Steam for it and stores it again 3 s and 10 s later.
static uint64_t sNameHost = 0;
static DWORD sNameRetryAt[2] = {};
static void RememberHost(uint64_t steamId, bool first)
{
	std::string name = SteamName(steamId);
	if (first || !name.empty()) {
		RecentHosts::Remember(steamId, name.c_str());
	}
	if (first && name.empty() && sSteam.friends && sSteam.RequestUserInfo) {
		sSteam.RequestUserInfo(sSteam.friends, steamId, true);
		sNameHost = steamId;
		sNameRetryAt[0] = GetTickCount() + 3000;
		sNameRetryAt[1] = GetTickCount() + 10000;
	}
	else if (!name.empty()) {
		sNameHost = 0;
	}
}

static void RememberHostRetry()
{
	DWORD now = GetTickCount();
	for (DWORD& at : sNameRetryAt) {
		if (sNameHost && at && (int)(now - at) >= 0) {
			at = 0;
			RememberHost(sNameHost, false);
		}
	}
}

namespace CoopNet
{
	bool Join(uint64_t hostId)
	{
		sWarnedVersion = false;
		sWorldProgress = -1;
		sWorldStaged = false;
		sHostSeen = false;
		return SteamJoinStart(hostId);
	}

	void Cancel()
	{
		NetStop();
		sWorldProgress = -1;
	}

	bool Leave()
	{
		if (!sWorldInstalledAtStart) {
			return false;
		}
		if (!sRestartAt) {
			Logf("leave: closing the game, the next start is the player's own world");
			App::ConsolePrintF("coop: leaving the friend's world: the game restarts in your own world...");
			sRestartDone = false;
			sRestartAt = GetTickCount() + 500;
		}
		return true;
	}

	void RestartGame()
	{
		sRestartDone = false;
		sRestartAt = GetTickCount() + 500;
		Logf("restart: requested by the window");
	}

	bool Host()
	{
		if (sWorldInstalledAtStart) {
			Logf("host: refused, this run is a co-op launch");
			return false;
		}
		bool ok = SteamHostStart();
		Logf("host: started from the in-game button: %s", ok ? "ok" : "Steam failed");
		return ok;
	}

	HostState Hosting()
	{
		HostState s;
		s.hosting = sTransport == Transport::Steam && sIsHost;
		s.joined = sTransport == Transport::Steam && !sIsHost;
		s.guestHere = s.hosting && sHavePeer && sReceived > 0 && GetTickCount() - sLastReceive < kReconnectMs;
		s.guest = s.guestHere ? sPeerSteamId : 0;
		s.myId = sSteam.myId;
		return s;
	}

	JoinState State()
	{
		JoinState s;
		s.joining = sTransport == Transport::Steam && !sIsHost;
		s.hostSeen = sHostSeen;
		s.versionMismatch = sWarnedVersion;
		s.receiving = sWorldProgress >= 0;
		s.progress = sWorldProgress < 0 ? 0 : sWorldProgress;
		s.staged = sWorldStaged;
		s.installedAtStart = sWorldInstalledAtStart;
		s.host = s.joining ? sPeerSteamId : SaveHostId();
		return s;
	}

	std::string Name(uint64_t steamId)
	{
		return SteamName(steamId);
	}

	uint64_t PeerId()
	{
		return sTransport == Transport::Steam && sHavePeer ? sPeerSteamId : 0;
	}

	Simulator::cCreatureAnimal* Puppet()
	{
		return sPuppet.get();
	}
}

static void NetUpdate()
{
	if (sDisposed) {
		return;
	}
	FrameStats();
	RunRestartIfDue();
	bool inGame = Simulator::IsCreatureGame() && GameNounManager.GetAvatar();
	UpdateIndicator(IndicatorText(), inGame);
	MenuWorlds::Update();
	MenuWorlds::HookMenuEnter(sModule);
	LoadTrace::HookDatabases(sModule);
	CoopMenu::Update();
	CoopLaunch::Update();
	HostPanel::Update();
	NamePlates::Update();
	if (sEcho) {
		if (inGame) {
			EchoFeed(GetTickCount());
			DrivePuppet(GetTickCount(), inGame);
		}
		return;
	}
	if (sTransport == Transport::None) {
		return;
	}
	if (sTransport == Transport::Steam) {
		sSteam.RunCallbacks();
		sSteam.SocketsRunCallbacks(sSteam.sockets);
		RememberHostRetry();
		SteamReceive();
		SteamReceiveFiles();
		SteamReceiveWorld();
		WorldSendPump();
		if (sHavePeer) {
			World::SetTransport(WorldSendUnreliable, WorldSendReliable);
			World::Update(sIsHost, inGame, sPuppetRaw);
		}
	}
	else {
		UdpReceive();
	}

	DWORD now = GetTickCount();
	if (sHavePeer && now - sLastSend >= kSendIntervalMs) {
		sLastSend = now;
		if (inGame) {
			auto avatar = GameNounManager.GetAvatar();
			NetSend(1, avatar->GetPosition(), avatar->mOrientation);
		}
		else {
			NetSend(0, Vector3(), Math::Quaternion());
		}
	}

	DrivePuppet(now, inGame);
}

// ---------------------------------------------------------------- remote console
// A local TCP port (127.0.0.1 only) that mirrors the game console: each line a client sends is run as a
// console command, and everything printed to the console is sent back. Used by scripts\console.ps1 in the repo so
// commands can be typed (or scripted) from PowerShell instead of in the game.
// The socket lives on its own thread and queues the lines; the commands run on the main thread each frame.
// A minimized game runs no frames (nor does the co-op networking), so commands wait until it is restored.
static const uint16_t kConsolePort = 27780;
static SOCKET sConsoleListen = INVALID_SOCKET;
static SOCKET sConsoleClient = INVALID_SOCKET;
static CRITICAL_SECTION sConsoleLock;
static std::vector<std::string> sConsoleQueue;
static ArgScript::PrintF_ptr sOrigPrintF = nullptr;
static HANDLE sConsoleThread = nullptr;

static void ConsoleSend(const char* text)
{
	std::string line = std::string(text) + "\n";
	EnterCriticalSection(&sConsoleLock);
	if (sConsoleClient != INVALID_SOCKET) {
		// the socket has a short send timeout: a stuck client is dropped rather than stalling the game
		if (send(sConsoleClient, line.c_str(), (int)line.size(), 0) == SOCKET_ERROR) {
			shutdown(sConsoleClient, SD_BOTH);
		}
	}
	LeaveCriticalSection(&sConsoleLock);
}

// Detour of the game's ArgScript PrintF: copies console output to the remote console.
static void* PrintF_detour(ArgScript::FormatParser* object, const char* str, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, str);
	vsnprintf(buffer, sizeof(buffer), str, args);
	va_end(args);
	if (sConsoleClient != INVALID_SOCKET && object == App::GetConsoleStream()) {
		ConsoleSend(buffer);
	}
	return sOrigPrintF(object, "%s", buffer);
}

static void ConsoleDropClient()
{
	EnterCriticalSection(&sConsoleLock);
	SOCKET old = sConsoleClient;
	sConsoleClient = INVALID_SOCKET;
	LeaveCriticalSection(&sConsoleLock);
	if (old != INVALID_SOCKET) {
		closesocket(old);
	}
}

static DWORD WINAPI ConsoleThread(void*)
{
	std::string input;
	for (;;) {
		SOCKET listener = sConsoleListen;
		SOCKET client = sConsoleClient;
		if (listener == INVALID_SOCKET) {
			ConsoleDropClient();
			return 0;  // closed by Dispose
		}
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(listener, &readable);
		if (client != INVALID_SOCKET) {
			FD_SET(client, &readable);
		}
		timeval wait = { 0, 500 * 1000 };
		if (select(0, &readable, nullptr, nullptr, &wait) == SOCKET_ERROR) {
			if (sConsoleListen == INVALID_SOCKET) {
				ConsoleDropClient();
				return 0;
			}
			Sleep(100);
			continue;
		}

		if (FD_ISSET(listener, &readable)) {
			SOCKET incoming = accept(listener, nullptr, nullptr);
			if (incoming != INVALID_SOCKET) {
				// a new client replaces the old one (e.g. a forgotten interactive console)
				if (client != INVALID_SOCKET) {
					ConsoleSend("(another console connected; this one is closed)");
					ConsoleDropClient();
				}
				DWORD timeout = 200;
				setsockopt(incoming, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
				EnterCriticalSection(&sConsoleLock);
				sConsoleClient = incoming;
				LeaveCriticalSection(&sConsoleLock);
				input.clear();
				ConsoleSend("CoopMod remote console. Type console commands, e.g. coop status");
			}
			continue;
		}

		if (client != INVALID_SOCKET && FD_ISSET(client, &readable)) {
			char buffer[1024];
			int n = recv(client, buffer, sizeof(buffer), 0);
			if (n <= 0) {
				ConsoleDropClient();
				continue;
			}
			input.append(buffer, n);
			size_t end;
			while ((end = input.find('\n')) != std::string::npos) {
				std::string line = input.substr(0, end);
				input.erase(0, end + 1);
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (!line.empty()) {
					EnterCriticalSection(&sConsoleLock);
					sConsoleQueue.push_back(line);
					LeaveCriticalSection(&sConsoleLock);
				}
			}
		}
	}
}

static void ConsoleStart()
{
	InitializeCriticalSection(&sConsoleLock);
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return;
	}
	sConsoleListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in local = {};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	local.sin_port = htons(kConsolePort);
	if (sConsoleListen == INVALID_SOCKET || bind(sConsoleListen, (sockaddr*)&local, sizeof(local)) != 0 ||
		listen(sConsoleListen, 1) != 0) {
		// a second game on the same PC: only the first one gets the remote console
		Logf("console: port %u unavailable (%d)", (unsigned)kConsolePort, WSAGetLastError());
		if (sConsoleListen != INVALID_SOCKET) {
			closesocket(sConsoleListen);
			sConsoleListen = INVALID_SOCKET;
		}
		WSACleanup();
		return;
	}
	sConsoleThread = CreateThread(nullptr, 0, ConsoleThread, nullptr, 0, nullptr);
	Logf("console: listening on 127.0.0.1:%u", (unsigned)kConsolePort);
	OpenConsoleWindow();
}

static void ConsoleStop()
{
	if (sConsoleListen == INVALID_SOCKET) {
		return;
	}
	SOCKET listener = sConsoleListen;
	sConsoleListen = INVALID_SOCKET;  // the thread sees it within half a second, drops the client and ends
	closesocket(listener);
	// The thread's code lives in this DLL: wait until it has really ended, the DLL is unloaded after Dispose.
	if (sConsoleThread) {
		WaitForSingleObject(sConsoleThread, 3000);
		CloseHandle(sConsoleThread);
		sConsoleThread = nullptr;
	}
}

// Main thread, every frame: run the queued commands.
static void ConsoleUpdate()
{
	if (sConsoleListen == INVALID_SOCKET) {
		return;
	}
	std::vector<std::string> lines;
	EnterCriticalSection(&sConsoleLock);
	lines.swap(sConsoleQueue);
	LeaveCriticalSection(&sConsoleLock);
	for (const std::string& line : lines) {
		Logf("console: %s", line.c_str());
		ConsoleSend(("> " + line).c_str());
		// the game prints its own parse errors ("Expecting at least 1 arguments") outside PrintF
		if (!CheatManager.ProcessLine(line.c_str())) {
			ConsoleSend("(the game rejected the command; its message is in the in-game console)");
		}
	}
}

static const char* kUsage =
	"coop host | coop leave | coopjoin <steamid> | coopjoin last | coop friends | coop udphost | coopudpjoin <ip> | coop abs | coop move | coop walk | coop teleport | coop motionlog | coop echo | coop dump | coop hit | coop kill | coop fps | coop restart | coop noworld | coop sendworld | coop status | coop off";

class CoopCommand : public ArgScript::ICommand
{
public:
	void ParseLine(const ArgScript::Line& line) override
	{
		// Only GetArguments(n) is used: it is the call known to work with this ModAPI build, and the game
		// itself rejects a call without arguments, so there is always at least the sub-command.
		auto args = line.GetArguments(1);
		if (!args) {
			return;
		}
		const char* sub = args[0];
		Logf("coop command: %s", sub);

		if (strcmp(sub, "leave") == 0) {
			if (!CoopNet::Leave()) {
				App::ConsolePrintF("coop: this is not a co-op launch (you are in your own world), nothing to leave");
			}
		}
		else if (strcmp(sub, "host") == 0) {
			if (sWorldInstalledAtStart) {
				App::ConsolePrintF("coop: this run plays a friend's world (co-op launch), it cannot host. coop leave first.");
			}
			else if (SteamHostStart()) {
				App::ConsolePrintF("coop: hosting over Steam. Your Steam ID: %llu", (unsigned long long)sSteam.myId);
				App::ConsolePrintF("coop: give it to the other player: coopjoin %llu", (unsigned long long)sSteam.myId);
			}
			else {
				App::ConsolePrintF("coop: Steam failed (see CoopMod.log). Fallback: coop udphost");
			}
		}
		else if (strcmp(sub, "join") == 0 || strcmp(sub, "udpjoin") == 0) {
			// The game's command parser accepts exactly the argument count we ask for, so joining is a
			// separate one-argument command.
			App::ConsolePrintF("coop: use  coopjoin <steamid>  (or  coopudpjoin <ip>)  without the space after coop");
		}
		else if (strcmp(sub, "udphost") == 0) {
			bool ok = UdpStart(true, nullptr, kDefaultPort);
			App::ConsolePrintF("coop: %s", ok ? "hosting over UDP (port 27777), waiting for a client" : "failed to start (see CoopMod.log)");
		}
		else if (strcmp(sub, "noworld") == 0) {
			sSendWorld = !sSendWorld;
			App::ConsolePrintF("coop: sending the world to clients %s", sSendWorld ? "on" : "off");
		}
		else if (strcmp(sub, "sendworld") == 0) {
			if (sTransport == Transport::Steam && sHavePeer) {
				WorldSendStart();
			}
			else {
				App::ConsolePrintF("coop: needs a Steam connection with a client");
			}
		}
		else if (strcmp(sub, "restart") == 0) {
			// the same restart that follows a received world, without the world: closes the game and starts it again
			sRestartDone = false;
			sRestartAt = GetTickCount() + 1000;
			App::ConsolePrintF("coop: restarting the game in 1 s...");
		}
		else if (strcmp(sub, "fps") == 0) {
			sFpsLog = !sFpsLog;
			App::ConsolePrintF("coop: %d frames per second now (game window %s); logging fps %s", sFps,
				GameWindowFocused() ? "focused" : "NOT focused", sFpsLog ? "on" : "off");
		}
		else if (strcmp(sub, "dump") == 0) {
			DumpEntities();
		}
		else if (strcmp(sub, "hit") == 0 || strcmp(sub, "kill") == 0) {
		World::TestHit(strcmp(sub, "kill") == 0);
	}
	else if (strcmp(sub, "modeltest") == 0) {
			// one-PC check of the model transfer: read the avatar's species from the resource manager, write it as a
			// CoopModels package, register it (the game already knows the key, so nothing visible changes)
			auto avatar = GameNounManager.GetAvatar();
			if (!avatar) {
				App::ConsolePrintF("coop: modeltest needs the creature stage (an avatar)");
			}
			else {
				const ResourceKey& k = avatar->mSpeciesKey;
				std::vector<Models::Record> records;
				bool read = Models::Read(k.instanceID, k.typeID, k.groupID, records);
				bool installed = read && Models::Install(k.instanceID, k.typeID, k.groupID, records);
				App::ConsolePrintF("coop: modeltest species %08x/%08x/%08x: read %s (%d records), install %s", k.instanceID,
					k.typeID, k.groupID, read ? "ok" : "FAILED", (int)records.size(), installed ? "ok" : "FAILED");
			}
		}
		else if (strcmp(sub, "move") == 0 || strcmp(sub, "walk") == 0 || strcmp(sub, "teleport") == 0 || strcmp(sub, "anim") == 0) {
			// how the puppet follows the remote player (game/PuppetMotion.cpp); "anim" is the old name of "walk"
			PuppetMotion::Mode mode = sub[0] == 'm' ? PuppetMotion::Mode::Move
				: sub[0] == 't' ? PuppetMotion::Mode::Teleport : PuppetMotion::Mode::Walk;
			PuppetMotion::SetMode(mode);
			App::ConsolePrintF("coop: puppet motion: %s", PuppetMotion::ModeName(mode));
		}
		else if (strcmp(sub, "friends") == 0) {
			RecentHosts::Print();
		}
		else if (strcmp(sub, "slots") == 0) {
			MenuWorlds::Dump();
		}
		else if (strcmp(sub, "addworld") == 0 || strcmp(sub, "addcenter") == 0) {
			MenuWorlds::AddTestWorld(strcmp(sub, "addcenter") == 0);
		}
		else if (strcmp(sub, "delworld") == 0) {
			MenuWorlds::RemoveTestWorlds();
		}
		else if (strcmp(sub, "holdplay") == 0) {
			MenuWorlds::SetHoldPlay(sModule);
		}
		else if (strcmp(sub, "menu") == 0) {
			CoopMenu::Open();
		}
		else if (strcmp(sub, "carddump") == 0) {
			CoopMenu::DumpCard();
		}
		else if (strcmp(sub, "ggefind") == 0) {
			MenuWorlds::FindSelected();
		}
		else if (strcmp(sub, "playnow") == 0) {
			MenuWorlds::PlayNow();
		}
		else if (strcmp(sub, "menureload") == 0) {
			MenuWorlds::ReloadMenu();
		}
		else if (strcmp(sub, "motionlog") == 0) {
			PuppetMotion::SetLogging(!PuppetMotion::GetLogging());
			App::ConsolePrintF("coop: motion log in CoopMod.log %s", PuppetMotion::GetLogging() ? "on" : "off");
		}
		else if (strcmp(sub, "echo") == 0) {
			sEcho = !sEcho;
			sEchoStarted = false;
			if (!sEcho) {
				ReleasePuppet();
				sHaveLatest = false;
				sPuppetInit = false;
			}
			App::ConsolePrintF("coop: echo puppet (repeats you 0.4 s later, 4 m to the side) %s", sEcho ? "on" : "off");
		}
		else if (strcmp(sub, "abs") == 0) {
			sAbsolute = !sAbsolute;
			App::ConsolePrintF("coop: absolute positions %s", sAbsolute ? "on" : "off");
		}
		else if (strcmp(sub, "status") == 0) {
			const char* mode = sTransport == Transport::None ? "off" : (sTransport == Transport::Steam ? "steam" : "udp");
			App::ConsolePrintF("coop: %s %s, peer %s, received %u, puppet %s", mode,
				sTransport == Transport::None ? "" : (sIsHost ? "host" : "client"),
				sHavePeer ? "known" : "none", (unsigned)sReceived, sPuppet ? "active" : "none");
			if (sSteam.ready) {
				App::ConsolePrintF("coop: your Steam ID %llu", (unsigned long long)sSteam.myId);
			}
			World::PrintStatus();
		}
		else if (strcmp(sub, "off") == 0) {
			NetStop();
			ReleasePuppet();
			App::ConsolePrintF("coop: stopped");
		}
		else {
			App::ConsolePrintF("Usage: %s", kUsage);
		}
	}

	const char* GetDescription(ArgScript::DescriptionMode mode) const override
	{
		return kUsage;
	}
};

// coopjoin <steamid>: join a Steam host, from the main menu or from a game.
class CoopJoinCommand : public ArgScript::ICommand
{
public:
	void ParseLine(const ArgScript::Line& line) override
	{
		auto args = line.GetArguments(1);
		if (!args) {
			return;
		}
		Logf("coopjoin %s", args[0]);
		// a Steam ID, or a remembered host: "last", its number in `coop friends`, or part of its name
		char error[200];
		uint64_t id = RecentHosts::Resolve(args[0], error, sizeof(error));
		if (id == 0) {
			App::ConsolePrintF("coop: %s", error);
			return;
		}
		if (SteamJoinStart(id)) {
			App::ConsolePrintF("coop: connecting over Steam, your Steam ID: %llu", (unsigned long long)sSteam.myId);
		}
		else {
			App::ConsolePrintF("coop: Steam failed (see CoopMod.log). Fallback: coopudpjoin <ip>");
		}
	}

	const char* GetDescription(ArgScript::DescriptionMode mode) const override
	{
		return "coopjoin <steamid> | last | <number> | <name>: join a co-op host over Steam (coop friends lists the hosts you joined).";
	}
};

// coopudpjoin <ip>: join a host over plain UDP (port 27777).
class CoopUdpJoinCommand : public ArgScript::ICommand
{
public:
	void ParseLine(const ArgScript::Line& line) override
	{
		auto args = line.GetArguments(1);
		if (!args) {
			return;
		}
		Logf("coopudpjoin %s", args[0]);
		bool ok = UdpStart(false, args[0], kDefaultPort);
		App::ConsolePrintF("coop: %s", ok ? "connecting over UDP" : "failed to start (see CoopMod.log)");
	}

	const char* GetDescription(ArgScript::DescriptionMode mode) const override
	{
		return "coopudpjoin <ip>: join a co-op host over plain UDP.";
	}
};

static eastl::intrusive_ptr<App::UpdateMessageListener> sNetListener;
static eastl::intrusive_ptr<App::UpdateMessageListener> sConsoleListener;

void Initialize()
{
	CheatManager.AddCheat("coop", new CoopCommand());
	CheatManager.AddCheat("coopjoin", new CoopJoinCommand());
	CheatManager.AddCheat("coopudpjoin", new CoopUdpJoinCommand());
	sNetListener = App::AddUpdateFunction(NetUpdate);
	CheatManager.AddCheat("stage", new StageCommand());
	CheatManager.AddCheat("coophello", new CoopHelloCommand());
	ConsoleStart();
	sConsoleListener = App::AddUpdateFunction(ConsoleUpdate);
	Logf("CoopMod initialized (protocol %u)", (unsigned)kProtocolVersion);
	if (CoopLaunch::Notice()[0]) {
		App::ConsolePrintF("coop: %s", CoopLaunch::Notice());
	}
	// A co-op launch (the host's world is Game0, DllMain): join the same host again.
	if (sWorldInstalledAtStart) {
		uint64_t autoJoin = CoopLaunch::Host();
		Logf("autojoin: co-op launch, joining host %llu", (unsigned long long)autoJoin);
		if (SteamJoinStart(autoJoin)) {
			App::ConsolePrintF("coop: co-op launch: the host's world is open, joining the host again automatically. Leave: coop leave");
		}
		else {
			App::ConsolePrintF("coop: co-op launch, but joining failed (see CoopMod.log): coopjoin %llu", (unsigned long long)autoJoin);
		}
	}
}

// Test aid: Spore refuses to start a second copy ("other instance of the game is already running", a named mutex).
// With the environment variable COOP_MULTI set, a second copy gets an unnamed mutex instead, so two games can run
// on one PC (give the second one its own APPDATA so the saves do not clash).
static HANDLE(WINAPI* sOrigCreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR) = CreateMutexA;
static bool sMultiInstance = false;

// Always windowed. A fullscreen Spore is minimized by Windows as soon as it loses the focus (Alt+Tab) and then runs no
// frames at all: for the host that freezes the whole shared world for the other player. The game takes `-w` from its
// command line, so the mod adds it (the hooks hand the game a command line with " -w" appended). Set the environment
// variable COOP_FULLSCREEN to keep the game's own mode. Static buffers, no std::string: see the allocator note in Dispose.
static LPSTR(WINAPI* sOrigGetCommandLineA)() = GetCommandLineA;
static LPWSTR(WINAPI* sOrigGetCommandLineW)() = GetCommandLineW;
static bool sForceWindowed = false;

static LPSTR WINAPI GetCommandLineA_hook()
{
	static char line[4096];
	if (!line[0]) {
		strncpy_s(line, sOrigGetCommandLineA(), _TRUNCATE);
		if (!strstr(line, " -w")) {
			strncat_s(line, " -w", _TRUNCATE);
		}
	}
	return line;
}

static LPWSTR WINAPI GetCommandLineW_hook()
{
	static wchar_t line[4096];
	if (!line[0]) {
		wcsncpy_s(line, sOrigGetCommandLineW(), _TRUNCATE);
		if (!wcsstr(line, L" -w")) {
			wcsncat_s(line, L" -w", _TRUNCATE);
		}
	}
	return line;
}

static HANDLE WINAPI CreateMutexA_hook(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name)
{
	HANDLE handle = sOrigCreateMutexA(attributes, initialOwner, name);
	if (name && handle && GetLastError() == ERROR_ALREADY_EXISTS) {
		Logf("multi: mutex '%s' already existed, using a private one", name);
		CloseHandle(handle);
		handle = sOrigCreateMutexA(attributes, initialOwner, nullptr);
	}
	return handle;
}

void Dispose()
{
	// The loader unloads this DLL right after Dispose. Everything that still points into it must go first,
	// otherwise the game or Steam would call freed code.
	NetStop();
	World::SetTransport(nullptr, nullptr);
	World::Shutdown();
	CoopMenu::Shutdown();
	CoopLaunch::Shutdown();
	HostPanel::Shutdown();
	NamePlates::Shutdown();
	MenuWorlds::Shutdown();
	SteamStop();
	ConsoleStop();
	App::RemoveUpdateFunction(sNetListener);
	App::RemoveUpdateFunction(sConsoleListener);
	sNetListener = nullptr;
	sConsoleListener = nullptr;
	for (const char* cheat : { "coop", "coopjoin", "coopudpjoin", "stage", "coophello" }) {
		CheatManager.RemoveCheat(cheat);
	}
	PrepareDetours(sModule);
	NPCTickAI_detour::detach();
	TakeDamage_detour::detach();
	PlayAnimation_detour::detach();
	ModeAction_detour::detach();
	PlayMovie_detour::detach();
	MovieUpdate_detour::detach();
	MenuWorlds::DetachHook();
	LoadTrace::Detach();
	DetourDetach((PVOID*)&sOrigPrintF, (PVOID)PrintF_detour);
	if (sMultiInstance) {
		DetourDetach((PVOID*)&sOrigCreateMutexA, (PVOID)CreateMutexA_hook);
	}
	if (sForceWindowed) {
		DetourDetach((PVOID*)&sOrigGetCommandLineA, (PVOID)GetCommandLineA_hook);
		DetourDetach((PVOID*)&sOrigGetCommandLineW, (PVOID)GetCommandLineW_hook);
	}
	CommitDetours();
	sPuppet = nullptr;
	sPuppetHerd = nullptr;
	// ModAPI routes our new/delete to the game's allocator. Static destructors normally run at FreeLibrary, while
	// the game still lives; if they ever ran at process exit instead (DLL pinned or not unloaded), a static
	// std::string/std::vector still holding memory would crash in the game's free. Free them now to be safe.
	sDisposed = true;
	sWorld = WorldSend();
	std::string().swap(sIndicator);
	std::wstring().swap(sBaseTitle);
	EnterCriticalSection(&sConsoleLock);
	std::vector<std::string>().swap(sConsoleQueue);
	LeaveCriticalSection(&sConsoleLock);
	Logf("CoopMod disposed");
}

void AttachDetours()
{
	NPCTickAI_detour::attach(GetAddress(Simulator::cCreatureAnimal, NPCTickAI));
	TakeDamage_detour::attach(GetAddress(Simulator::cCreatureAnimal, TakeDamage));
	PlayAnimation_detour::attach(GetAddress(Simulator::cCreatureBase, PlayAnimation));
	ModeAction_detour::attach(GetAddress(App::cCreatureModeStrategy, ExecuteAction));
	PlayMovie_detour::attach(GetAddress(Movie::MovieSystem, PlayMovie));
	MovieUpdate_detour::attach(GetAddress(Movie::MovieSystem, Update));
	MenuWorlds::AttachStartupHooks();
	LoadTrace::Attach();
	sOrigPrintF = ArgScript::PrintF;
	DetourAttach((PVOID*)&sOrigPrintF, (PVOID)PrintF_detour);
	if (getenv("COOP_MULTI")) {
		sMultiInstance = true;
		DetourAttach((PVOID*)&sOrigCreateMutexA, (PVOID)CreateMutexA_hook);
	}
	if (!getenv("COOP_FULLSCREEN")) {
		sForceWindowed = true;
		DetourAttach((PVOID*)&sOrigGetCommandLineA, (PVOID)GetCommandLineA_hook);
		DetourAttach((PVOID*)&sOrigGetCommandLineW, (PVOID)GetCommandLineW_hook);
	}
}

// ---------------------------------------------------------------- crash log
// Logs access violations into CoopMod.log with the thread, the faulting address and the modules found on the
// stack, so a crash (for example the one on exit after Steam was started) can be traced without a debugger.
static PVOID sCrashHandler = nullptr;
static int sCrashLogged = 0;

static void DescribeAddress(DWORD address, char* out, size_t size)
{
	HMODULE module = nullptr;
	char path[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)(uintptr_t)address, &module) && module) {
		GetModuleFileNameA(module, path, MAX_PATH);
		const char* name = strrchr(path, '\\');
		snprintf(out, size, "%s+0x%lx", name ? name + 1 : path, (unsigned long)(address - (DWORD)(uintptr_t)module));
	}
	else {
		snprintf(out, size, "%08lx", (unsigned long)address);
	}
}

static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* info)
{
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || sCrashLogged >= 3) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	++sCrashLogged;
	const CONTEXT* c = info->ContextRecord;
	char where[MAX_PATH + 32];
	DescribeAddress((DWORD)(uintptr_t)info->ExceptionRecord->ExceptionAddress, where, sizeof(where));
	Logf("crash: access violation in thread %lu at %s (eip %08lx esp %08lx, %s %08lx)", GetCurrentThreadId(), where,
		c->Eip, c->Esp, info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
		(unsigned long)info->ExceptionRecord->ExceptionInformation[1]);
	// return addresses on the stack that point into a loaded module
	const DWORD* stack = (const DWORD*)(uintptr_t)c->Esp;
	int shown = 0;
	for (int i = 0; i < 256 && shown < 16; ++i) {
		DWORD value;
		__try {
			value = stack[i];
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			break;
		}
		HMODULE module = nullptr;
		if (value > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)(uintptr_t)value, &module) && module) {
			DescribeAddress(value, where, sizeof(where));
			Logf("crash:   [esp+%03x] %s", i * 4, where);
			++shown;
		}
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		sModule = hModule;
		sCrashHandler = AddVectoredExceptionHandler(1, CrashHandler);
		CoopLaunch::Prepare();
		sWorldInstalledAtStart = CoopLaunch::Active();
		ModAPI::AddPostInitFunction(Initialize);
		ModAPI::AddDisposeFunction(Dispose);

		PrepareDetours(hModule);
		AttachDetours();
		CommitDetours();
		break;

	case DLL_PROCESS_DETACH:
		// lpReserved is null when the DLL is unloaded with FreeLibrary, non-null when the process ends
		// lpReserved is null when the loader unloads us with FreeLibrary (it does, right after Dispose)
		Logf("CoopMod detached (%s)", lpReserved ? "process exit" : "FreeLibrary");
		if (sCrashHandler) {
			RemoveVectoredExceptionHandler(sCrashHandler);
		}
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}





