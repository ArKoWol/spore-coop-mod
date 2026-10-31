// World.cpp : the shared world, see World.h.
#include "stdafx.h"
#include "World.h"
#include "Log.h"
#include "Models.h"
#include "Objects.h"
#include "game/PuppetMotion.h"
#include "../../core/coop_core.h"

#include <deque>
#include <map>
#include <set>
#include <vector>

namespace
{
	using namespace coop;

	const DWORD kHostTickMs = 100;
	const DWORD kFullEveryMs = 10000;
	const DWORD kNeuterEveryMs = 500;
	const DWORD kTelemetryEveryMs = 2000;
	const size_t kDeltaPerMessage = 20;
	const uint8_t kMsgSpecies = 100;    // [100][instance][type][group]: which species a hash stands for
	const uint8_t kMsgTelemetry = 101;  // guest -> host: [101][table][mirrors][strays]
	const uint8_t kMsgModelReq = 102;   // guest -> host: [102][instance][type][group]: I cannot build this species
	const uint8_t kMsgModel = 103;      // host -> guest: [103][instance][type][group][index u8][count u8][recType][total][offset][len][bytes]
	const uint8_t kMsgModelNone = 104;  // host -> guest: [104][instance]: the host has no record for it either
	const uint8_t kMsgAvatar = 106;     // both ways, unreliable: [106][health][maxHealth][scale] of the sender's own avatar
	const uint8_t kMsgDamageReq = 107;  // guest -> host: [107][netId][damage f32][type u8]; netId 0 = the host's avatar
	const uint8_t kMsgDamage = 108;     // host -> guest: [108][damage f32][type u8]: apply to your own avatar
	const uint8_t kMsgAvatarSpecies = 115;  // both ways: [115][instance][type][group] of the sender's avatar species (changes in the editor)
	const uint8_t kMsgEaten = 114;      // guest -> host: [114][netId]: the guest ate this animal's corpse
	const uint8_t kMsgCorpse = 116;     // host -> guest, once per dead animal: [116][netId][food value f32][general flags u32] (why can the guest not eat a mirror's corpse?)
	const uint8_t kMsgNests = 113;      // host -> guest: [113][chunk][chunks][n] then n x 56 bytes (x y z f32, species instance/type/group, qx qy qz qw f32, model instance/type/group, scale f32)
	const DWORD kNestsEveryMs = 20000;
	const uint8_t kMsgAnim = 112;       // both ways: [112][who][animation id]; who: 0 = the host's avatar, 0xFFFFFFFF = the guest's avatar, else a netId
	const DWORD kAnimRepeatMs = 400;     // the same animation of the same creature is sent at most this often
	const DWORD kAvatarEveryMs = 200;
	const float kPoolRadius = 100.0f;    // animals closer than this to the planet's centre are the game's pool: the host does not announce them, the guest does not remove them
	const DWORD kDeathWaitMs = 2000;     // how long the game's own tick may turn a 0-health mirror into a corpse before we mark it dead
	const float kFollowRadius = 150.0f;  // mirrors farther than this from our avatar are only teleported, not walked
	const uint32_t kModelChunk = 40 * 1024;
	const DWORD kModelWaitMs = 20000;   // after this long a missing model is replaced by a stand-in (and counted)

	struct Mirror
	{
		cHerdPtr herd;
		cCreatureAnimalPtr animal;
		int waitFrames = 0;
		bool placed = false;
		Vector3 pos;
		float rot[4] = { 0, 0, 0, 1 };
		DWORD last = 0;
		uint8_t age = 0;  // the age step the body was made for: a different one needs a new body
		float lastHostHealth = -1.0f;  // the host's health we applied last
		DWORD lastOffsetLog = 0;
		uint32_t species = 0;  // the species the body was made for (a hatchling's species becomes known a moment after it appears)
		uint32_t built = 0;    // the instance of the profile it was meant to be built from (the host's avatar species may be an alias)
		bool viaAlias = false; // that instance is an alias, not the species key itself
		PuppetMotion::Follower follower;  // the game's locomotion walks the mirror (legs move) instead of sliding it
		bool dead = false;         // the animal's own mbDead is set (a real corpse)
		DWORD killAt = 0;          // when KillLocally was called (0 = not yet); the game's tick may finish the death for kDeathWaitMs
		bool deathLogged = false;  // the log line that tells which way the death came was written
		bool pinned = false;       // the corpse was placed once after the death (afterwards only when it drifts)
		bool foodApplied = false;  // the host's food value was written into the corpse
	};

	// Everything lives in one heap object that Shutdown() frees: no static destructor may run at process exit.
	struct State : IWorldSink
	{
		std::deque<Message> reliableQueue;  // waits for a free send buffer, keeps the order

		// host
		HostWorld host;
		std::map<void*, uint32_t> ids;             // game object -> netId
		std::map<uint32_t, ResourceKey> speciesSent;
		std::map<void*, ResourceKey> profileKeys;  // species profile -> its key (see SpeciesKeyOf)
		std::map<void*, DWORD> unresolvedSince;    // animal -> when we first saw it without a species
		DWORD lastTick = 0, lastFull = 0;
		bool fullNow = false;
		int hostAnimals = 0;
		std::set<uint32_t> corpseSent;  // netIds whose corpse info went to the guest (cleared when a guest connects)

		// guest
		GuestWorld* guest = nullptr;
		std::map<uint32_t, ResourceKey> species;
		struct ModelWait
		{
			DWORD since = 0;
			bool requested = false, failed = false, done = false;
			std::vector<Models::Record> records;
			std::vector<uint32_t> got;
		};
		std::map<uint32_t, ModelWait> models;  // species instance -> transfer state
		int standIns = 0;                      // mirrors that had to use the stand-in species
		std::map<uint32_t, Mirror> mirrors;
		std::set<uint32_t> consumed;   // netIds of animals eaten on this machine (the host removes the real ones)
		struct CorpseInfo { float food = 0; uint32_t flags = 0; };
		std::map<uint32_t, CorpseInfo> corpses;  // what the host told about its dead animals (kMsgCorpse), by netId
		bool poolLogged = false;                 // the pool animals that GuestNeuter leaves alone were logged once
		bool strayTuningLogged = false;         // the research log of the strays' tuning objects was written
		bool haveWorld = false;  // a snapshot arrived: the guest's own animals are strays now
		DWORD lastNeuter = 0, lastTelemetry = 0, lastLog = 0;
		int strays = 0, removedStrays = 0;
		std::set<uint32_t> warnedSpeciesIds;
		// telemetry received by the host
		int guestTable = -1, guestMirrors = -1, guestStrays = -1;

		// both roles
		bool isHost = false;
		ResourceKey remoteKey;               // species of the other player's avatar
		bool remoteKnown = false;
		DWORD remoteSince = 0;
		// The same key can stand for two bodies (a guest joins with a copy of the host's save, then one of them edits his
		// creature: the key stays, the body changes). The hash of the body travels with the key; when ours differs, the other
		// player's body is fetched and filed under an alias key.
		uint32_t remoteHash = 0;             // hash of the other player's body as announced (0 = unknown)
		bool remoteDirty = false;            // key or hash changed: compare again
		bool remoteResolved = false;         // the last comparison found the key buildable here
		uint32_t cmpStamp = 0;               // ownStamp at the last comparison
		bool aliasWanted = false, aliasFailed = false, aliasReady = false;
		ResourceKey remoteAlias;
		DWORD aliasSince = 0;
		std::map<uint32_t, ResourceKey> pendingAlias;  // species instance -> alias its incoming model chunks are filed under
		uint32_t sentHash = 0;               // hash of our own avatar's body (0 = unreadable)
		bool sentHashValid = false;
		uint32_t ownStamp = 0;               // counts changes of our own key / body
		ResourceKey sentKey;                // species of our own avatar as last sent
		uint32_t sentSeq = 0;                // its profile sequence (changes when the editor changed the creature)
		DWORD lastSpeciesSend = 0;
		void* puppet = nullptr;              // the other player's puppet (a cCreatureAnimal*)
		DWORD lastAvatarSend = 0;
		struct RemoteAvatar { float health = 0, maxHealth = 0, scale = 0; bool valid = false; } remoteAvatar;
		int damageLogged = 0, damageSent = 0, damageApplied = 0;
		std::map<uint64_t, DWORD> lastAnim;   // (who, animation) -> last time sent
		std::map<uint32_t, int> animSeen;     // animation id -> how often the game played it (log for spike S15)
		int animSent = 0, animPlayed = 0;

		// nests (G2): the host's list, and the nests the guest made from it
		struct NestInfo { Vector3 pos; ResourceKey key; float rot[4] = { 0, 0, 0, 1 }; ResourceKey model; float scale = 1.0f; };
		struct NestMirror { cHerdPtr herd; cNestPtr nest; Vector3 pos; };
		DWORD lastNests = 0;
		bool nestsNow = false;
		std::vector<NestInfo> nestIncoming;      // guest: list being received
		uint32_t nestChunksGot = 0, nestChunks = 0;
		std::vector<NestInfo> nestList;          // guest: the last complete list
		bool nestWiped = false;                  // guest: its own nests were removed
		std::vector<NestMirror> nestMirrors;
		int nestsCreated = 0, nestsRemoved = 0, nestsFailed = 0;

		void OnCreate(const EntityState&) override {}
		void OnUpdate(const EntityState&) override {}
		void OnDestroy(uint32_t) override {}
	};

	State* sState = nullptr;
	World::SendFn sSendUnreliable = nullptr;  // set once by the transport, survives Reset()
	World::SendFn sSendReliable = nullptr;

	State& S()
	{
		if (!sState) {
			sState = new State();
			sState->guest = new GuestWorld(sState);
		}
		return *sState;
	}

	bool IsAvatar(Simulator::cGameData* object)
	{
		return object == GameNounManager.GetAvatar();
	}

	void QueueReliable(State& s, const Message& m)
	{
		s.reliableQueue.push_back(m);
	}

	// true while we apply damage or a death that came over the network: the TakeDamage detour must let it through
	bool sApplyingRemote = false;
	bool sApplyingAnim = false;  // true while we play an animation that came over the network

	void DealDamage(Simulator::cCreatureAnimal* victim, float damage, int type, Simulator::cCombatant* attacker, uint32_t attackerId)
	{
		sApplyingRemote = true;
		victim->TakeDamage(damage, attackerId, type, Vector3(0.0f, 0.0f, 0.0f), attacker);
		sApplyingRemote = false;
	}

	// Found in the friend's log (crashes "reading 0000032c", SporeApp+0x948634 and +0x76ca11) and in SporeCoop's notes: the game
	// reads `*(creature + 0xE84) + 0x32C` for every creature (field_E84, filled when a creature is made as a member of a nest
	// herd) without a null check. A mirror or a puppet made by us has it empty and crashes the game the first time something
	// touches it. We give such creatures a blank object whose +0x32C is -1.
	char* sBlankTuning = nullptr;

	void EnsureTuning(Simulator::cCreatureBase* creature)
	{
		if (!creature || creature->field_E84) {
			return;
		}
		if (!sBlankTuning) {
			sBlankTuning = new char[0x800]();
			*(int*)(sBlankTuning + 0x32C) = -1;
		}
		creature->field_E84 = sBlankTuning;
	}

	// The host says this animal is dead: let the game play the death (a lethal hit with no attacker, no rewards).
	void KillLocally(Simulator::cCreatureAnimal* animal)
	{
		static int logged = 0;
		bool before = animal->mbDead;
		DealDamage(animal, animal->mMaxHealthPoints + 100.0f, Simulator::kMelee, nullptr, 0);
		if (logged++ < 20) {
			// TakeDamage brings the health to 0 but leaves mbDead false (the SDK has no death function): the game's own tick makes the
			// corpse (ShouldSkipAI lets it run for kDeathWaitMs), else GuestMirrors marks the animal dead. "kill: mirror N died/marked" tells which.
			Logf("kill: mirror %p dead %d -> %d, health %.1f, eaten %d, flags %x%s", (void*)animal, before ? 1 : 0, animal->mbDead ? 1 : 0,
				animal->mHealthPoints, animal->mbHasBeenEaten ? 1 : 0, (unsigned)animal->mGeneralFlags,
				animal->mbDead ? " (dead at once)" : ", waiting for the game's tick");
		}
	}

	void FlushReliable(State& s)
	{
		while (!s.reliableQueue.empty() && sSendReliable) {
			const Message& m = s.reliableQueue.front();
			if (!sSendReliable(m.data(), (uint32_t)m.size())) {
				return;  // buffer full, try again next frame (the queue keeps the order)
			}
			s.reliableQueue.pop_front();
		}
	}

	// ------------------------------------------------------------ host

	// The species of an NPC animal. Found by the first real test (friend's log: "species 00000000"): `mSpeciesKey` is
	// zero for most NPC animals, only the species profile pointer (cCreatureBase +0xB20, or the herd's) is set. The key
	// the game accepts in GetSpeciesProfile is the key of that profile in SpeciesManager.mSpeciesProfiles.
	ResourceKey KeyOfProfile(State& s, Simulator::cSpeciesProfile* profile);

	// guarded read of the key stored in a species profile (no C++ objects with destructors in a function with __try)
	bool ReadProfileKey(Simulator::cSpeciesProfile* profile, uint32_t* out)
	{
		__try {
			out[0] = profile->mCreatureKey.instanceID;
			out[1] = profile->mCreatureKey.typeID;
			out[2] = profile->mCreatureKey.groupID;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	ResourceKey SpeciesKeyOf(State& s, Simulator::cCreatureAnimal* animal)
	{
		if (animal->mSpeciesKey.instanceID) {
			return animal->mSpeciesKey;
		}
		Simulator::cSpeciesProfile* profile = animal->mpSpeciesProfile;
		if (!profile && animal->mHerd) {
			profile = animal->mHerd->mpSpeciesProfile;
		}
		return KeyOfProfile(s, profile);
	}

	ResourceKey KeyOfProfile(State& s, Simulator::cSpeciesProfile* profile)
	{
		if (!profile) {
			return ResourceKey();
		}
		auto cached = s.profileKeys.find(profile);
		if (cached != s.profileKeys.end()) {
			return cached->second;
		}
		// Iterating SpeciesManager.mSpeciesProfiles crashed the host (CoopMod.dll+0x159d0 in HostNests, "reading 3ea99f65": the
		// ModAPI layout of that hash map does not match this game build). The key of a profile is stored in the profile itself
		// (mCreatureKey, +0x504); a key is accepted only if the game maps it back to the same profile.
		ResourceKey found;
		uint32_t raw[3] = { 0, 0, 0 };
		if (ReadProfileKey(profile, raw)) {
			ResourceKey candidate(raw[0], raw[1], raw[2]);
			if (candidate.instanceID && SpeciesManager.GetSpeciesProfile(candidate) == profile) {
				found = candidate;
			}
		}
		s.profileKeys[profile] = found;
		Logf("world: species profile %p -> key %08x/%08x/%08x%s", (void*)profile, found.instanceID, found.typeID, found.groupID,
			found.instanceID ? "" : " (unknown)");
		return found;
	}

	void HostTick(State& s, DWORD now, void* puppet)
	{
		if (now - s.lastTick < kHostTickMs) {
			return;
		}
		s.lastTick = now;

		// the animals that exist now
		std::map<void*, EntityState> current;
		std::set<void*> present;  // every animal that exists, announced or not
		int pooled = 0;           // animals at the planet's centre (the game's pool), not announced
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != Simulator::kCreatureAnimal || IsAvatar(object) || (void*)object == puppet) {
				continue;
			}
			auto animal = object_cast<Simulator::cCreatureAnimal>(object);
			if (!animal) {
				continue;
			}
			present.insert(object);
			// Found in the dumps (2026-10-25): 65 of the planet's 101 animals sit at the planet's centre (0, 0, 0) with 1 HP and no species
			// key: the game's pool of animals that a nest or a mating call "spawns" later. Mirroring them showed 65 copies stacked in one
			// place at the guest, which then flew out of the ground when the call used them ("bodiless dummies, the sound comes from under
			// the nest"). They are not announced until they have a place on the surface (the planet's radius is about 505).
			if (animal->GetPosition().Length() < kPoolRadius) {
				++pooled;
				continue;
			}
			EntityState st;
			st.kind = Kind::Creature;
			const ResourceKey speciesKey = SpeciesKeyOf(s, animal);
			st.species = speciesKey.instanceID;
			if (!st.species) {
				// a newborn's species is set a moment after it appears; announcing it before would make the guest build the
				// stand-in body (an egg of another species hatched as the guest's own species). One animal of the planet never
				// gets a species: after 3 s it is announced as it is.
				DWORD& since = s.unresolvedSince[object];
				if (!since) {
					since = now;
				}
				if (now - since < 3000) {
					continue;
				}
			}
			const Vector3& p = animal->GetPosition();
			const Math::Quaternion& q = animal->GetOrientation();
			st.pos = { p.x, p.y, p.z };
			st.rot = { q.x, q.y, q.z, q.w };
			st.health = animal->mHealthPoints;
			st.maxHealth = animal->mMaxHealthPoints;
			st.scale = animal->GetScale();
			st.age = (uint8_t)animal->mAge;
			st.flags = (animal->mbDead ? kFlagDead : 0) | ((animal->mGeneralFlags & Simulator::kCreatureFlagIsAlpha) ? kFlagAlpha : 0);
			current[object] = st;
			if (st.species && !s.speciesSent.count(st.species)) {
				s.speciesSent[st.species] = speciesKey;
				Writer w;
				w.U8(kMsgSpecies); w.U32(speciesKey.instanceID); w.U32(speciesKey.typeID); w.U32(speciesKey.groupID);
				QueueReliable(s, w.buf);
			}
		}
		s.hostAnimals = (int)current.size();
		// forget the waiting times of animals that no longer exist (NOT of those still held back: they are not in `current`;
		// erasing them reset their clock every tick, so an animal without a species was never announced: 25 of 99 reached the guest)
		for (auto it = s.unresolvedSince.begin(); it != s.unresolvedSince.end();) {
			it = present.count(it->first) ? std::next(it) : s.unresolvedSince.erase(it);
		}
		static DWORD lastCountLog = 0;
		if (now - lastCountLog > 10000) {
			lastCountLog = now;
			Logf("world: host sees %d animals, announces %d, %d in the pool at the planet's centre, %d wait for their species", (int)present.size(),
				(int)current.size(), pooled, (int)(present.size() - current.size()) - pooled);
		}

		s.host.AdvanceTick();
		// gone
		for (auto it = s.ids.begin(); it != s.ids.end();) {
			if (!current.count(it->first)) {
				s.host.Destroy(it->second);
				s.corpseSent.erase(it->second);
				it = s.ids.erase(it);
			}
			else {
				++it;
			}
		}
		// new and changed
		for (auto& c : current) {
			auto found = s.ids.find(c.first);
			if (found == s.ids.end()) {
				s.ids[c.first] = s.host.Spawn(c.second);
			}
			else {
				s.host.Update(found->second, c.second);
			}
		}

		for (auto& m : s.host.TakeEvents()) {
			QueueReliable(s, m);
		}
		if (s.fullNow || now - s.lastFull >= kFullEveryMs) {
			s.fullNow = false;
			s.lastFull = now;
			// The species definitions go out again with every full snapshot: a guest that reset its world state (it joins a
			// second time, or restarts) would otherwise never learn them, and every mirror would get the stand-in.
			for (auto& known : s.speciesSent) {
				Writer w;
				w.U8(kMsgSpecies); w.U32(known.second.instanceID); w.U32(known.second.typeID); w.U32(known.second.groupID);
				QueueReliable(s, w.buf);
			}
			QueueReliable(s, s.host.MakeFull());
		}
		// Research + fix attempt (the guest cannot eat a mirror's corpse): the food value of a dead animal is filled from its tuning, which
		// a mirror lacks. Each dead animal's value goes to the guest once. After the Full snapshot, so the guest knows the animal.
		for (auto& c : current) {
			if (!(c.second.flags & kFlagDead)) {
				continue;
			}
			auto id = s.ids.find(c.first);
			if (id == s.ids.end() || !s.corpseSent.insert(id->second).second) {
				continue;
			}
			auto animal = static_cast<Simulator::cCreatureAnimal*>(static_cast<Simulator::cGameData*>(c.first));
			Writer w;
			w.U8(kMsgCorpse); w.U32(id->second); w.F32(animal->mFoodValue); w.U32((uint32_t)animal->mGeneralFlags);
			QueueReliable(s, w.buf);
			static int corpseLogged = 0;
			if (corpseLogged++ < 20) {
				Logf("corpse: host animal %u dead, food %.1f, flags %x, eaten %d", id->second, animal->mFoodValue, (unsigned)animal->mGeneralFlags,
					animal->mbHasBeenEaten ? 1 : 0);
			}
		}
		if (sSendUnreliable) {
			for (auto& m : s.host.MakeDeltas(kDeltaPerMessage)) {
				sSendUnreliable(m.data(), (uint32_t)m.size());
			}
		}
	}

	// ------------------------------------------------------------ guest

	// ------------------------------------------------------------ the other player's body (both roles)

	// Asks the other side for the model of `key` (the answer is filed under `key` or, with a pending alias, under the alias).
	void RequestModel(State& s, const ResourceKey& key)
	{
		Writer w; w.U8(kMsgModelReq); w.U32(key.instanceID); w.U32(key.typeID); w.U32(key.groupID);
		QueueReliable(s, w.buf);
	}

	// The alias profile exists here: puppets and mirrors of that species are built from it from now on.
	void RefreshAlias(State& s)
	{
		if (s.aliasWanted && !s.aliasReady && !s.aliasFailed &&
			Models::Has(s.remoteAlias.instanceID, s.remoteAlias.typeID, s.remoteAlias.groupID)) {
			s.aliasReady = true;
			s.pendingAlias.erase(s.remoteKey.instanceID);
			Logf("world: the other player's creature %08x is ready under alias %08x", s.remoteKey.instanceID, s.remoteAlias.instanceID);
		}
	}

	void FailAlias(State& s, const char* why)
	{
		if (!s.aliasFailed) {
			s.aliasFailed = true;
			s.aliasReady = false;
			s.pendingAlias.erase(s.remoteKey.instanceID);
			Logf("world: ALIAS %08x FAILED (%s), showing the other player's creature %08x with OUR body", s.remoteAlias.instanceID, why,
				s.remoteKey.instanceID);
		}
	}

	// Decides which key the other player's avatar is built from: his key itself, or an alias when his body differs from the one
	// we hold under the same key. Runs when his key / hash changed, when our own body changed, and (cheaply) while his model is
	// still missing.
	void ResolveRemote(State& s)
	{
		s.remoteDirty = false;
		s.cmpStamp = s.ownStamp;
		const ResourceKey k = s.remoteKey;
		DWORD now = GetTickCount();
		Models::RegisterCached();
		if (!Models::Has(k.instanceID, k.typeID, k.groupID)) {
			// we have no body for this key at all: fetch it and file it under the key itself
			s.aliasWanted = s.aliasReady = false;
			s.remoteResolved = false;
			State::ModelWait& wait = s.models[k.instanceID];
			if (!wait.requested && !wait.done && !wait.failed) {
				wait.requested = true;
				wait.since = now;
				RequestModel(s, k);
				Logf("world: the other player's creature %08x is not buildable here, asking for its model", k.instanceID);
			}
			return;
		}
		s.remoteResolved = true;
		// our body for the same key: our own avatar's hash, or read it (this runs on a change, not every frame)
		uint32_t local = 0;
		if (s.sentHashValid && k.instanceID == s.sentKey.instanceID && k.groupID == s.sentKey.groupID) {
			local = s.sentHash;
		}
		else {
			std::vector<Models::Record> records;
			if (Models::Read(k.instanceID, k.typeID, k.groupID, records)) {
				local = Models::Hash(records);
			}
		}
		if (!s.remoteHash || !local || s.remoteHash == local) {
			if (s.aliasWanted) {
				Logf("world: the other player's creature %08x equals ours again (hash %08x), using the key itself", k.instanceID, s.remoteHash);
			}
			s.aliasWanted = s.aliasFailed = s.aliasReady = false;
			s.pendingAlias.erase(k.instanceID);
			return;
		}
		ResourceKey alias(Models::AliasInstance(k.instanceID, s.remoteHash), k.typeID, k.groupID);
		if (!s.aliasWanted || s.remoteAlias.instanceID != alias.instanceID) {
			Logf("world: the other player's creature %08x differs from ours (hash %08x vs %08x), using alias %08x", k.instanceID,
				s.remoteHash, local, alias.instanceID);
			s.aliasWanted = true;
			s.aliasFailed = s.aliasReady = false;
			s.remoteAlias = alias;
			s.aliasSince = now;
		}
		if (s.aliasFailed || s.aliasReady) {
			return;
		}
		if (Models::Has(alias.instanceID, alias.typeID, alias.groupID)) {
			RefreshAlias(s);  // cached from an earlier run
			return;
		}
		auto pending = s.pendingAlias.find(k.instanceID);
		if (pending == s.pendingAlias.end() || pending->second.instanceID != alias.instanceID) {
			// the model of K is fetched again; the chunks are filed under the alias (the wait state of K may say "done" from before)
			s.pendingAlias[k.instanceID] = alias;
			State::ModelWait& wait = s.models[k.instanceID];
			wait = State::ModelWait();
			wait.requested = true;
			wait.since = now;
			RequestModel(s, k);
			Logf("world: asking for the model of %08x again, to file it under alias %08x", k.instanceID, alias.instanceID);
		}
	}

	// The instance a mirror of the host's species `species` is to be built from (0 = unknown species). The host's avatar species
	// can be an alias when the guest holds another body under the same key.
	uint32_t MirrorInstance(State& s, uint32_t species)
	{
		auto key = s.species.find(species);
		if (key == s.species.end()) {
			return 0;
		}
		const ResourceKey& k = key->second;
		if (s.aliasReady && s.remoteKnown && k.instanceID == s.remoteKey.instanceID && k.groupID == s.remoteKey.groupID) {
			return s.remoteAlias.instanceID;
		}
		return k.instanceID;
	}

	// Builds the mirror: an EMPTY quiet herd plus an animal created into it (the way SporeCoop does it). A herd of size 1
	// made by CreateHerd only got its animal when the game's own herd logic decided to, which happened for 23 of 99
	// animals in the first guest test. Returns false when it has to wait (model on its way) or failed.
	bool MakeMirror(State& s, const EntityState& st, Mirror& m)
	{
		Simulator::cSpeciesProfile* profile = nullptr;
		auto key = s.species.find(st.species);
		uint32_t built = MirrorInstance(s, st.species);
		if (key != s.species.end()) {
			ResourceKey use = key->second;
			if (built) {
				use.instanceID = built;  // the host's own species may be an alias (another body under the same key)
			}
			profile = SpeciesManager.GetSpeciesProfile(use);
		}
		if (!profile) {
			// The species is not buildable here (yet). Its model is on its way from the host (see OnMessage): wait for it
			// and only after kModelWaitMs show the guest's own species, loudly, as a counted stand-in.
			State::ModelWait& wait = s.models[st.species];
			DWORD now = GetTickCount();
			if (!wait.since) {
				wait.since = now;
			}
			if (st.species && !wait.failed && !wait.done && now - wait.since < kModelWaitMs) {
				return false;
			}
			if (auto avatar = GameNounManager.GetAvatar()) {
				profile = SpeciesManager.GetSpeciesProfile(avatar->mSpeciesKey);
			}
			++s.standIns;
			if (s.warnedSpeciesIds.insert(st.species).second) {
				Logf("world: species %08x has no model here (%s; definition %s), using the avatar's species as a stand-in",
					st.species, wait.failed ? "the host has none" : (wait.done ? "installed but not found" : "timeout"),
					s.species.count(st.species) ? "known" : "NEVER received");
			}
		}
		if (!profile) {
			return false;
		}
		Vector3 pos(st.pos.x, st.pos.y, st.pos.z);
		cHerdPtr herd = GameNounManager.CreateHerd(pos, profile, 0, false, 0, false);
		if (!herd) {
			return false;
		}
		herd->mTargetHerdSize = 0;  // no breeding or respawn in a mirror's herd
		herd->SetEnabled(false);
		// the host's age step decides baby or adult body; 0 (old packet or unknown) = the default
		Simulator::cCreatureAnimal* animal = Simulator::cCreatureAnimal::Create(pos, profile, st.age ? st.age : 1, herd.get(), false, false);
		m.age = st.age;
		m.herd = herd;
		if (animal) {
			m.animal = animal;
			EnsureTuning(animal);
		}
		m.species = st.species;
		m.built = built;
		m.viaAlias = key != s.species.end() && built && built != key->second.instanceID;
		return true;
	}

	void DestroyMirror(Mirror& m)
	{
		if (m.herd) {
			GameNounManager.DestroyInstance(m.herd.get());
		}
		m.herd = nullptr;
		m.animal = nullptr;
	}

	bool IsMirrorAnimal(State& s, void* animal)
	{
		for (auto& m : s.mirrors) {
			if (m.second.animal.get() == animal) {
				return true;
			}
		}
		return false;
	}

	void GuestMirrors(State& s, DWORD now)
	{
		const auto& table = s.guest->entities();
		// removed from the table -> remove the mirror
		for (auto it = s.mirrors.begin(); it != s.mirrors.end();) {
			if (!table.count(it->first)) {
				DestroyMirror(it->second);
				s.consumed.erase(it->first);
				it = s.mirrors.erase(it);
			}
			else {
				++it;
			}
		}
		for (auto it = s.corpses.begin(); it != s.corpses.end();) {
			it = table.count(it->first) ? std::next(it) : s.corpses.erase(it);
		}
		int created = 0;
		for (auto& e : table) {
			const EntityState& st = e.second;
			if (s.consumed.count(e.first)) {
				continue;  // eaten here: the host removes the real animal, until then no new mirror
			}
			Mirror& m = s.mirrors[e.first];
			// The actor decides his own action (PLAN rule 3): the guest ate this animal's corpse here, natively. Tell the host.
			if (m.animal && (m.animal->mbHasBeenEaten || m.animal->mbMarkedForDeletion)) {
				Writer w;
				w.U8(kMsgEaten); w.U32(e.first);
				QueueReliable(s, w.buf);
				s.consumed.insert(e.first);
				DestroyMirror(m);
				s.mirrors.erase(e.first);
				continue;
			}
			if (!m.herd) {
				if (created >= 3) {
					continue;  // a few new herds per frame, the game stutters otherwise
				}
				m = Mirror();
				if (MakeMirror(s, st, m)) {
					++created;
				}
				continue;
			}
			if (!m.animal) {
				++m.waitFrames;
				if (!m.herd->mHerd.empty()) {
					m.animal = m.herd->mHerd[0];
					EnsureTuning(m.animal.get());
				}
				continue;
			}
			// grew up (or was made with another age): build the body again, the model of a baby and an adult differ
			// or the host's own species is now (or no longer) built from an alias: another body under the same key
			if ((st.age && m.age != st.age) || (st.species && m.species != st.species) ||
				(st.species && (s.aliasReady || m.viaAlias) && m.built != MirrorInstance(s, st.species))) {
				DestroyMirror(m);
				m = Mirror();
				continue;
			}
			// size, health and the alpha flag come from the host (the guest's own game would roll its own values)
			if (fabsf(m.animal->GetScale() - st.scale) > 0.001f && st.scale > 0.0f) {
				m.animal->SetScale(st.scale);
			}
			bool dead = (st.flags & kFlagDead) || (st.maxHealth > 0.0f && st.health <= 0.0f);
			if (m.animal->mbDead) {
				m.dead = true;  // killed here by our own bite (native death, native DNA), or by the replay below
			}
			if (dead && !m.dead && !m.killAt) {
				m.killAt = now;
				KillLocally(m.animal.get());  // the host's animal died: 0 health now, the game's tick (ShouldSkipAI lets it run) makes the corpse
				if (m.animal->mbDead) {
					m.dead = true;
				}
			}
			else if (dead && !m.dead && now - m.killAt >= kDeathWaitMs) {
				// the game's tick made no corpse in time: do it ourselves, else the guest can never eat it
				m.animal->mbDead = true;
				m.dead = true;
				m.deathLogged = true;
				Logf("kill: mirror %u marked dead by us (the game's tick did not, after %u ms, health %.1f)", e.first, (unsigned)(now - m.killAt),
					m.animal->mHealthPoints);
			}
			if (m.dead && m.killAt && !m.deathLogged) {
				m.deathLogged = true;
				Logf("kill: mirror %u died by the game's tick after %u ms (health %.1f, eaten %d, flags %x)", e.first, (unsigned)(now - m.killAt),
					m.animal->mHealthPoints, m.animal->mbHasBeenEaten ? 1 : 0, (unsigned)m.animal->mGeneralFlags);
			}
			if (!dead && !m.dead && st.maxHealth > 0.0f) {
				m.animal->mMaxHealthPoints = st.maxHealth;
				// Our own bites already lowered the health here (they are reported to the host, which echoes the new value
				// a moment later): take the host's number only when it changed, so our hit is not undone in between.
				if (st.health != m.lastHostHealth) {
					m.animal->mHealthPoints = st.health;
					m.lastHostHealth = st.health;
				}
			}
			// Hypothesis 1 (the guest cannot eat a corpse): a mirror's food value is not the host's (the game fills it from a tuning the mirror lacks).
			if (m.dead && !m.foodApplied) {
				auto info = s.corpses.find(e.first);
				if (info != s.corpses.end()) {
					m.foodApplied = true;
					float before = m.animal->mFoodValue;
					if (before != info->second.food) {
						m.animal->mFoodValue = info->second.food;
					}
					static int foodLogged = 0;
					if (foodLogged++ < 20) {
						Logf("corpse: mirror %u food %.1f -> %.1f (host %.1f), dead %d, eaten %d, flags %x (host %x), field_E84 %s", e.first, before,
							m.animal->mFoodValue, info->second.food, m.animal->mbDead ? 1 : 0, m.animal->mbHasBeenEaten ? 1 : 0,
							(unsigned)m.animal->mGeneralFlags, (unsigned)info->second.flags,
							m.animal->field_E84 == sBlankTuning ? "is the blank tuning" : "is a real tuning");
					}
				}
			}
			if (st.flags & kFlagAlpha) {
				m.animal->mGeneralFlags |= Simulator::kCreatureFlagIsAlpha;
			}
			else {
				m.animal->mGeneralFlags &= ~Simulator::kCreatureFlagIsAlpha;
			}
			// move it towards the table state (exponential smoothing, snap at first and on jumps)
			Vector3 target(st.pos.x, st.pos.y, st.pos.z);
			float dt = (float)(now - m.last) / 1000.0f;
			m.last = now;
			if (dt < 0.0f || dt > 0.25f) {
				dt = 0.016f;
			}
			const float tr[4] = { st.rot.x, st.rot.y, st.rot.z, st.rot.w };
			if (!m.placed || (target - m.pos).Length() > 8.0f) {
				m.placed = true;
				m.pos = target;
				memcpy(m.rot, tr, sizeof(m.rot));
			}
			else {
				float a = 1.0f - expf(-dt * 10.0f);
				m.pos = m.pos + (target - m.pos) * a;
				float dot = m.rot[0] * tr[0] + m.rot[1] * tr[1] + m.rot[2] * tr[2] + m.rot[3] * tr[3];
				float sign = dot < 0.0f ? -1.0f : 1.0f;
				float q[4], len = 0.0f;
				for (int i = 0; i < 4; ++i) {
					q[i] = m.rot[i] * (1.0f - a) + tr[i] * sign * a;
					len += q[i] * q[i];
				}
				len = sqrtf(len);
				if (len > 0.0001f) {
					for (int i = 0; i < 4; ++i) {
						m.rot[i] = q[i] / len;
					}
				}
			}
			const Math::Quaternion rotation(m.rot[0], m.rot[1], m.rot[2], m.rot[3]);
			auto avatar = GameNounManager.GetAvatar();
			bool closeBy = avatar && (m.pos - avatar->GetPosition()).Length() < kFollowRadius;
			if (m.dead || m.killAt) {  // (a mirror in its last moments, waiting for the game's tick, is held as a corpse too)
				// A corpse must not fly away (SporeCoop found it does), but a Teleport every frame (it sets mbTeleport and resets the
				// placement) may disturb the eat interaction: place it once after the death and again only when it drifted.
				float drift = (m.animal->GetPosition() - m.pos).Length();
				if (!m.pinned || drift > 0.3f) {
					static int driftLogged = 0;
					if (m.pinned && driftLogged++ < 20) {
						Logf("corpse: mirror %u drifted %.2f m, placed again", e.first, drift);
					}
					m.pinned = true;
					m.animal->Teleport(m.pos, rotation);
				}
				m.follower = PuppetMotion::Follower();
			}
			else if (!closeBy) {
				// far animals are not worth the locomotion calls
				m.animal->Teleport(m.pos, rotation);
				m.follower = PuppetMotion::Follower();
			}
			else {
				// the game's locomotion walks the animal to the host's position (legs move), see game/PuppetMotion.cpp
				PuppetMotion::Drive(m.follower, m.animal.get(), m.pos, rotation);
				// Research (owner's report: in a fight the animal stood in front of the guest but at the host's side, and both could
				// hit it): how far is the mirror from the host's position when the player is next to it? Rate limited.
				static int offsetLogged = 0;
				float toPlayer = (m.animal->GetPosition() - avatar->GetPosition()).Length();
				float offset = (Vector3(st.pos.x, st.pos.y, st.pos.z) - m.animal->GetPosition()).Length();
				if (toPlayer < 8.0f && offset > 1.0f && offsetLogged < 60 && now - m.lastOffsetLog > 1000) {
					m.lastOffsetLog = now;
					++offsetLogged;
					Logf("mirror: %u is %.1f m from the host's position, %.1f m from our avatar (walking %d, remote speed %.1f)", e.first, offset,
						toPlayer, m.follower.moving ? 1 : 0, m.follower.remoteSpeed);
				}
			}
		}
	}

	// The guest's own game keeps making animals (herds, hatching, respawn). Once the host's table exists they are
	// strays: remove them, every half second, so the guest only sees the host's animals (R2 in docs/PLAN.md section 10).
	void GuestNeuter(State& s, DWORD now, void* puppet)
	{
		if (now - s.lastNeuter < kNeuterEveryMs) {
			return;
		}
		s.lastNeuter = now;
		std::vector<Simulator::cCreatureAnimal*> strays;
		int animalsTotal = 0, herdsTotal = 0, pooled = 0;
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			uint32_t noun = object->GetNounID();
			if (noun == Simulator::kHerd) {
				++herdsTotal;
			}
			if (noun != Simulator::kCreatureAnimal) {
				continue;
			}
			++animalsTotal;
			if (IsAvatar(object) || (void*)object == puppet) {
				continue;
			}
			if (IsMirrorAnimal(s, object)) {
				continue;
			}
			// The game's pool (animals at the planet's centre, same rule as HostTick) is left alone: the game keeps references to them,
			// and a destroyed one came back as a "picture" clone around the nest after a mating call (two-PC test 2026-10-31).
			if (auto candidate = object_cast<Simulator::cCreatureAnimal>(object)) {
				if (candidate->GetPosition().Length() < kPoolRadius) {
					++pooled;
					continue;
				}
			}
			bool pending = false;  // a mirror herd whose animal we have not picked up yet
			for (auto& m : s.mirrors) {
				if (m.second.herd && !m.second.animal) {
					auto animal = object_cast<Simulator::cCreatureAnimal>(object);
					if (animal && animal->mHerd == m.second.herd) {
						pending = true;
					}
				}
			}
			if (!pending) {
				if (auto animal = object_cast<Simulator::cCreatureAnimal>(object)) {
					strays.push_back(animal);
				}
			}
		}
		s.strays = (int)strays.size();
		if (pooled && !s.poolLogged) {
			s.poolLogged = true;
			Logf("world: %d pool animals within %.0f m of the planet's centre are left alone (not removed as strays)", pooled, kPoolRadius);
		}
		// Research for "hundreds of puppets inside each other after a mating call" (the guest's test of 2026-10-25): a census of what the
		// game holds when something runs away, at most every 5 s.
		static DWORD lastCensus = 0;
		if (now - lastCensus > 5000 && (animalsTotal - pooled > (int)s.mirrors.size() + 15 || strays.size() > 5)) {
			lastCensus = now;
			Logf("world: census: %d animals (%d mirrors, %d pool, %d strays about to go), %d herds, %d nest herds of ours", animalsTotal,
				(int)s.mirrors.size(), pooled, (int)strays.size(), herdsTotal, (int)s.nestMirrors.size());
		}
		if (!s.strayTuningLogged && !strays.empty()) {
			// Research: do all animals of a species share one tuning object (could be lent to mirrors later)? Nothing is changed.
			s.strayTuningLogged = true;
			for (size_t i = 0; i < strays.size() && i < 10; ++i) {
				Simulator::cCreatureAnimal* stray = strays[i];
				Logf("stray: animal %p species %08x profile %p field_E84 %p food %.1f", (void*)stray, SpeciesKeyOf(s, stray).instanceID,
					(void*)stray->mpSpeciesProfile, stray->field_E84, stray->mFoodValue);
			}
		}
		for (auto animal : strays) {
			auto herd = animal->mHerd;
			bool shared = false;  // a herd that also holds the avatar or the puppet is not destroyed whole
			if (herd) {
				for (auto& member : herd->mHerd) {
					if (IsAvatar(member.get()) || (void*)member.get() == puppet) {
						shared = true;
					}
				}
				for (auto& n : s.nestMirrors) {
					if (n.herd == herd) {
						shared = true;  // the herd of a nest we made: only the animal goes, not the nest
					}
				}
			}
			if (herd && !shared) {
				GameNounManager.DestroyInstance(herd.get());
			}
			else {
				GameNounManager.DestroyInstance(animal);
			}
			++s.removedStrays;
		}
	}

	void GuestTelemetry(State& s, DWORD now)
	{
		if (now - s.lastTelemetry < kTelemetryEveryMs) {
			return;
		}
		s.lastTelemetry = now;
		int placed = 0, bodiless = 0;
		for (auto& m : s.mirrors) {
			if (m.second.animal) {
				++placed;
				if (!m.second.animal->mpAnimatedCreature) {
					++bodiless;  // the animal exists but has no body model (owner: "half of them are bodiless dummies")
				}
			}
		}
		Writer w;
		w.U8(kMsgTelemetry); w.U32((uint32_t)s.guest->entities().size()); w.U32((uint32_t)placed); w.U32((uint32_t)s.strays);
		QueueReliable(s, w.buf);
		if (now - s.lastLog >= 10000) {
			s.lastLog = now;
			Logf("world: guest table %d, mirrors %d (%d without a body model), strays now %d, strays removed so far %d",
				(int)s.guest->entities().size(), placed, bodiless, s.strays, s.removedStrays);
		}
	}

	// ------------------------------------------------------------ nests (G2)
	// The nests of a planet are made by the game when the stage starts, so two machines that loaded the same save have different
	// ones (the friend had 96 and 108, the host 101; none at the same places). The host's list is sent every 20 s; the guest
	// removes its own nests once (except its avatar's) and makes the host's: an empty quiet herd with a nest at the same place.

	// Reads a nest's position and the species profile of its herd. The host crashed (CoopMod.dll+0x16ad8, "reading 00000a27") when
	// the guest connected and the first nest list was built, so every raw read of a game object here is guarded: a nest or herd the
	// game has half-destroyed gives false instead of a crash. (No C++ objects with destructors in a function with __try.)
	// It also reads the nest's look: orientation (x y z w), scale, model key (instance, type, group) and flags. The guest cannot
	// know them (the planet generator chose them).
	bool ReadNest(Simulator::cNest* nest, Simulator::cSpeciesProfile** profile, float* pos, Simulator::cHerd** herd,
		float* rot, float* scale, uint32_t* model, int* flags)
	{
		__try {
			Simulator::cHerd* h = nest->mpHerd.get();
			if (!h) {
				return false;
			}
			*herd = h;
			*profile = h->mpSpeciesProfile;
			const Vector3& p = nest->GetPosition();
			pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;
			const Math::Quaternion& q = nest->GetOrientation();
			rot[0] = q.x; rot[1] = q.y; rot[2] = q.z; rot[3] = q.w;
			*scale = nest->GetScale();
			const ResourceKey& mk = nest->GetModelKey();
			model[0] = mk.instanceID; model[1] = mk.typeID; model[2] = mk.groupID;
			*flags = nest->mFlags;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	void HostNests(State& s, DWORD now)
	{
		if (!s.nestsNow && now - s.lastNests < kNestsEveryMs) {
			return;
		}
		s.nestsNow = false;
		s.lastNests = now;
		std::vector<State::NestInfo> list;
		int unreadable = 0;
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != Simulator::kNest) {
				continue;
			}
			auto nest = object_cast<Simulator::cNest>(object);
			if (!nest) {
				continue;
			}
			Simulator::cSpeciesProfile* profile = nullptr;
			Simulator::cHerd* herd = nullptr;
			float pos[3] = { 0, 0, 0 };
			float rot[4] = { 0, 0, 0, 1 };
			float scale = 1.0f;
			uint32_t model[3] = { 0, 0, 0 };
			int flags = 0;
			if (!ReadNest(nest, &profile, pos, &herd, rot, &scale, model, &flags)) {
				++unreadable;
				continue;
			}
			ResourceKey key = KeyOfProfile(s, profile);
			if (!key.instanceID) {
				continue;  // a nest of no known species cannot be made on the other side
			}
			State::NestInfo info;
			info.pos = Vector3(pos[0], pos[1], pos[2]);
			info.key = key;
			for (int i = 0; i < 4; ++i) {
				info.rot[i] = rot[i];
			}
			info.model.instanceID = model[0]; info.model.typeID = model[1]; info.model.groupID = model[2];
			info.scale = scale;
			if (list.size() < 5) {
				Logf("nests: host nest %d model %08x!%08x!%08x scale %.2f flags %08x", (int)list.size(), model[2], model[1], model[0], scale, (unsigned)flags);
			}
			list.push_back(info);
		}
		Logf("nests: host list of %d nests built (%d unreadable)", (int)list.size(), unreadable);
		const size_t perMessage = 100;
		uint32_t chunks = (uint32_t)((list.size() + perMessage - 1) / perMessage);
		if (chunks == 0) {
			return;
		}
		for (uint32_t c = 0; c < chunks; ++c) {
			size_t from = c * perMessage, to = std::min(list.size(), from + perMessage);
			Writer w;
			w.U8(kMsgNests); w.U32(c); w.U32(chunks); w.U32((uint32_t)(to - from));
			for (size_t i = from; i < to; ++i) {
				w.F32(list[i].pos.x); w.F32(list[i].pos.y); w.F32(list[i].pos.z);
				w.U32(list[i].key.instanceID); w.U32(list[i].key.typeID); w.U32(list[i].key.groupID);
				for (int k = 0; k < 4; ++k) {
					w.F32(list[i].rot[k]);
				}
				w.U32(list[i].model.instanceID); w.U32(list[i].model.typeID); w.U32(list[i].model.groupID);
				w.F32(list[i].scale);
			}
			QueueReliable(s, w.buf);
		}
	}

	void DestroyNestMirror(State::NestMirror& m)
	{
		if (m.herd) {
			GameNounManager.DestroyInstance(m.herd.get());
		}
		if (m.nest) {
			GameNounManager.DestroyInstance(m.nest.get());
		}
		m.herd = nullptr;
		m.nest = nullptr;
	}

	// A nest made at run time gets no look from the planet generator and the game puts it away from the asked place: put it
	// exactly where the host's is, with its orientation, scale and model. The model key is set only if it differs; then the nest
	// gets a model world (SDK: the graphical model is loaded in SetModelWorld), taken from the nest itself, else from the avatar.
	void ShapeNestMirror(Simulator::cNest* nest, const State::NestInfo& info, bool log)
	{
		const Math::Quaternion rotation(info.rot[0], info.rot[1], info.rot[2], info.rot[3]);
		nest->Teleport(info.pos, rotation);
		if (info.scale > 0.0f) {
			nest->SetScale(info.scale);
		}
		ResourceKey given = nest->GetModelKey();
		bool changed = false;
		if (info.model.instanceID && (given.instanceID != info.model.instanceID || given.typeID != info.model.typeID ||
			given.groupID != info.model.groupID)) {
			nest->SetModelKey(info.model);
			changed = true;
			if (!nest->HasModel()) {
				Graphics::IModelWorld* world = nest->GetModelWorld();
				if (!world) {
					auto avatar = GameNounManager.GetAvatar();
					world = avatar ? avatar->GetModelWorld() : nullptr;
				}
				if (world) {
					nest->SetModelWorld(world);
				}
			}
		}
		if (log) {
			ResourceKey after = nest->GetModelKey();
			const Vector3& at = nest->GetPosition();
			Logf("nests: made one: game gave model %08x!%08x!%08x, host has %08x!%08x!%08x%s, now %08x!%08x!%08x hasModel %d, scale %.2f (host %.2f), flags %08x, off by %.1f m",
				given.groupID, given.typeID, given.instanceID, info.model.groupID, info.model.typeID, info.model.instanceID,
				changed ? " (set)" : " (same)", after.groupID, after.typeID, after.instanceID, nest->HasModel() ? 1 : 0,
				nest->GetScale(), info.scale, (unsigned)nest->mFlags, (at - info.pos).Length());
		}
	}

	void GuestNests(State& s, DWORD now)
	{
		static DWORD lastRun = 0;
		if (s.nestList.empty() || now - lastRun < 500) {
			return;
		}
		lastRun = now;
		Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			return;
		}
		Simulator::cHerd* avatarHerd = avatar->mHerd.get();

		if (!s.nestWiped) {
			s.nestWiped = true;
			// the guest's own nests (made by its game at stage start) differ from the host's: remove them, with their herds,
			// except the avatar's own nest
			std::vector<Simulator::cHerd*> herds;
			for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
				Simulator::cGameData* object = &*it;
				if (object->GetNounID() != Simulator::kNest) {
					continue;
				}
				auto nest = object_cast<Simulator::cNest>(object);
				if (nest && nest->mpHerd && nest->mpHerd.get() != avatarHerd) {
					herds.push_back(nest->mpHerd.get());
				}
			}
			for (auto herd : herds) {
				GameNounManager.DestroyInstance(herd);
				++s.nestsRemoved;
			}
			// nests that survived their herd (they had none)
			std::vector<Simulator::cGameData*> rest;
			for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
				Simulator::cGameData* object = &*it;
				if (object->GetNounID() == Simulator::kNest) {
					auto nest = object_cast<Simulator::cNest>(object);
					if (nest && (!nest->mpHerd || nest->mpHerd.get() != avatarHerd)) {
						rest.push_back(object);
					}
				}
			}
			for (auto object : rest) {
				GameNounManager.DestroyInstance(object);
				++s.nestsRemoved;
			}
			Logf("nests: removed the guest's own %d herds and %d leftover nests", (int)herds.size(), (int)rest.size());
		}

		// Where are the nests? Only our own record counts (the nests we made) plus the kept nest of the avatar. The first version
		// looked for Nest objects at the host's positions in the game's object list and, finding none (the game places a nest
		// on the ground, away from the asked position, or lists it later), made 5 more every half second: 295 nests in the
		// friend's log.
		std::vector<Vector3> existing;
		for (auto& n : s.nestMirrors) {
			existing.push_back(n.pos);
		}
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() == Simulator::kNest) {
				auto nest = object_cast<Simulator::cNest>(object);
				if (nest && nest->mpHerd && nest->mpHerd.get() == avatarHerd) {
					existing.push_back(nest->GetPosition());
				}
			}
		}
		int made = 0;
		for (auto& info : s.nestList) {
			if (made >= 5) {
				break;
			}
			bool there = false;
			for (auto& p : existing) {
				if ((p - info.pos).Length() < 2.0f) {
					there = true;
					break;
				}
			}
			if (there) {
				continue;
			}
			Simulator::cSpeciesProfile* profile = SpeciesManager.GetSpeciesProfile(info.key);
			if (!profile) {
				++s.nestsFailed;
				continue;
			}
			State::NestMirror m;
			m.pos = info.pos;
			m.herd = GameNounManager.CreateHerd(info.pos, profile, 0, false, 0, true);
			if (!m.herd) {
				++s.nestsFailed;
				continue;
			}
			m.herd->mTargetHerdSize = 0;
			// The herd of a nest stays ENABLED: the 102 Nest objects the guest's dump shows were there at the host's places but not
			// visible, probably because a disabled herd draws no nest. Animals such a herd may spawn are removed as strays.
			m.nest = m.herd->mpNest;
			if (!m.nest) {
				m.nest = GameNounManager.CreateNest(info.pos, m.herd.get());
			}
			if (m.nest) {
				ShapeNestMirror(m.nest.get(), info, s.nestsCreated < 10);
			}
			existing.push_back(info.pos);
			s.nestMirrors.push_back(m);
			++s.nestsCreated;
			++made;
		}
		if (made) {
			int nestObjects = 0, withHerd = 0;
			for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
				Simulator::cGameData* object = &*it;
				if (object->GetNounID() == Simulator::kNest) {
					++nestObjects;
					auto nest = object_cast<Simulator::cNest>(object);
					if (nest && nest->mpHerd) {
						++withHerd;
					}
				}
			}
			Logf("nests: made %d from the host's list (%d in the list, %d made so far, %d failed); the game now holds %d Nest objects, %d with a herd",
				made, (int)s.nestList.size(), s.nestsCreated, s.nestsFailed, nestObjects, withHerd);
		}
		// nests we made that the host no longer has
		for (auto it = s.nestMirrors.begin(); it != s.nestMirrors.end();) {
			bool listed = false;
			for (auto& info : s.nestList) {
				if ((it->pos - info.pos).Length() < 2.0f) {
					listed = true;
					break;
				}
			}
			if (listed) {
				++it;
			}
			else {
				DestroyNestMirror(*it);
				it = s.nestMirrors.erase(it);
			}
		}
	}

	void DestroyAllMirrors(State& s)
	{
		for (auto& m : s.mirrors) {
			DestroyMirror(m.second);
		}
		s.mirrors.clear();
		for (auto& n : s.nestMirrors) {
			DestroyNestMirror(n);
		}
		s.nestMirrors.clear();
	}
}

namespace World
{
	void SetTransport(SendFn unreliable, SendFn reliable)
	{
		sSendUnreliable = unreliable;
		sSendReliable = reliable;
	}

	void HostPeerConnected()
	{
		State& s = S();
		s.fullNow = true;
		s.nestsNow = true;
		s.reliableQueue.clear();
		// species definitions have to reach the new guest again
		s.speciesSent.clear();
		// and the corpse info of the dead animals
		s.corpseSent.clear();
	}

	void Update(bool host, bool inGame, void* puppet)
	{
		State& s = S();
		DWORD now = GetTickCount();
		FlushReliable(s);
		if (!inGame) {
			return;
		}
		s.isHost = host;
		s.puppet = puppet;
		if (host || s.haveWorld) {
			Objects::Update(host, inGame);  // eaten / hatched / picked-up fruit, plants and eggs
		}

		// Each side tells the other the health and size of its own avatar, and shows them on its puppet of that player
		// (the puppet is a stand-in animal with the default values: 13 HP instead of the 10 the player really has).
		if (now - s.lastAvatarSend >= kAvatarEveryMs && sSendUnreliable) {
			s.lastAvatarSend = now;
			if (auto avatar = GameNounManager.GetAvatar()) {
				Writer w;
				w.U8(kMsgAvatar); w.F32(avatar->mHealthPoints); w.F32(avatar->mMaxHealthPoints); w.F32(avatar->GetScale());
				sSendUnreliable(w.buf.data(), (uint32_t)w.buf.size());
			}
		}
		// The species of our own avatar goes to the other side now and when it changes (the player came back from the editor with
		// another body) and every 10 s; the other side builds our puppet from it.
		if (now - s.lastSpeciesSend >= 1000) {
			if (auto avatar = GameNounManager.GetAvatar()) {
				const ResourceKey& key = avatar->mSpeciesKey;
				bool changed = key.instanceID != s.sentKey.instanceID || key.groupID != s.sentKey.groupID;
				bool seqChanged = avatar->mProfileSeq != s.sentSeq;
				if (key.instanceID && (changed || seqChanged || now - s.lastSpeciesSend >= 10000)) {
					if (changed || seqChanged) {
						Logf("world: our avatar is species %08x/%08x/%08x, profile %p, profile sequence %u (was %u)", key.instanceID, key.typeID, key.groupID,
							(void*)avatar->mpSpeciesProfile, (unsigned)avatar->mProfileSeq, (unsigned)s.sentSeq);
					}
					// The key can stay while the body changes (an edit): the hash tells the other side which body this is. It is read again
					// with every send (every 10 s too): the editor may write the new body after the profile sequence changed.
					std::vector<Models::Record> records;
					uint32_t hash = Models::Read(key.instanceID, key.typeID, key.groupID, records, true) ? Models::Hash(records) : 0;
					if (changed || !s.sentHashValid || hash != s.sentHash) {
						s.sentHash = hash;
						s.sentHashValid = true;
						++s.ownStamp;
						Logf("world: our avatar's body hash is %08x (%d records)", s.sentHash, (int)records.size());
					}
					s.sentSeq = avatar->mProfileSeq;
					s.lastSpeciesSend = now;
					s.sentKey = key;
					Writer w;
					w.U8(kMsgAvatarSpecies); w.U32(key.instanceID); w.U32(key.typeID); w.U32(key.groupID); w.U32(s.sentHash);
					QueueReliable(s, w.buf);
				}
			}
		}
		if (s.remoteKnown) {
			if (s.remoteDirty || !s.remoteResolved || s.ownStamp != s.cmpStamp) {
				ResolveRemote(s);
			}
			RefreshAlias(s);
		}
		if (puppet && s.remoteAvatar.valid) {
			auto p = static_cast<Simulator::cCreatureAnimal*>(puppet);
			if (s.remoteAvatar.maxHealth > 0.0f) {
				p->mMaxHealthPoints = s.remoteAvatar.maxHealth;
				p->mHealthPoints = s.remoteAvatar.health;
			}
			if (s.remoteAvatar.scale > 0.0f && fabsf(p->GetScale() - s.remoteAvatar.scale) > 0.001f) {
				p->SetScale(s.remoteAvatar.scale);
			}
		}
		if (host) {
			HostTick(s, now, puppet);
			HostNests(s, now);
		}
		else if (s.haveWorld) {
			GuestMirrors(s, now);
			GuestNests(s, now);
			GuestNeuter(s, now, puppet);
			GuestTelemetry(s, now);
		}
	}

	void OnMessage(int channel, const uint8_t* data, uint32_t size, bool host)
	{
		if (!sState && size == 0) {
			return;
		}
		State& s = S();
		if (size < 1) {
			return;
		}
		if (!host && data[0] == kMsgNests && size >= 13) {
			Reader r(data, size);
			r.U8();
			uint32_t chunk = r.U32(), chunks = r.U32(), n = r.U32();
			if (!r.ok || chunks == 0 || chunks > 100 || n > 200 || size != 13 + n * 56) {
				return;
			}
			if (chunk == 0) {
				s.nestIncoming.clear();
				s.nestChunks = chunks;
				s.nestChunksGot = 0;
			}
			for (uint32_t i = 0; i < n; ++i) {
				State::NestInfo info;
				info.pos = Vector3(r.F32(), r.F32(), r.F32());
				info.key.instanceID = r.U32(); info.key.typeID = r.U32(); info.key.groupID = r.U32();
				for (int k = 0; k < 4; ++k) {
					info.rot[k] = r.F32();
				}
				info.model.instanceID = r.U32(); info.model.typeID = r.U32(); info.model.groupID = r.U32();
				info.scale = r.F32();
				s.nestIncoming.push_back(info);
			}
			if (r.ok && ++s.nestChunksGot >= s.nestChunks) {
				s.nestList.swap(s.nestIncoming);
				s.nestIncoming.clear();
				Logf("nests: the host's list arrived, %d nests", (int)s.nestList.size());
			}
			return;
		}
		if (data[0] == kMsgAnim && size == 9) {
			Reader r(data, size);
			r.U8();
			uint32_t who = r.U32(), id = r.U32();
			Simulator::cCreatureAnimal* target = nullptr;
			if (host) {
				if (who == 0xFFFFFFFFu) {
					target = static_cast<Simulator::cCreatureAnimal*>(s.puppet);
				}
			}
			else if (who == 0) {
				target = static_cast<Simulator::cCreatureAnimal*>(s.puppet);
			}
			else {
				auto m = s.mirrors.find(who);
				if (m != s.mirrors.end() && !m->second.dead) {
					target = m->second.animal.get();
				}
			}
			if (r.ok && target) {
				sApplyingAnim = true;
				target->PlayAnimation(id);
				sApplyingAnim = false;
				++s.animPlayed;
				// Reported by the friend: "looking for a partner" showed only its first frames. Our own walk/stand orders (every
				// 100 ms) cut the animation: leave the creature alone for a while if it stands.
				if (who != 0 && who != 0xFFFFFFFFu) {
					auto m = s.mirrors.find(who);
					if (m != s.mirrors.end()) {
						PuppetMotion::Hold(m->second.follower, 2500);
					}
				}
				else {
					PuppetMotion::HoldPuppet(2500);
				}
			}
			return;
		}
		if (Objects::OnMessage(data, size, host)) {
			return;
		}
		if (data[0] == kMsgAvatar && size == 13) {
			Reader r(data, size);
			r.U8();
			s.remoteAvatar.health = r.F32();
			s.remoteAvatar.maxHealth = r.F32();
			s.remoteAvatar.scale = r.F32();
			s.remoteAvatar.valid = r.ok;
			return;
		}
		if (host && data[0] == kMsgDamageReq && size == 10) {
			// the guest bit something: the host applies it as if its own avatar had (the game pays kills only to the avatar)
			Reader r(data, size);
			r.U8();
			uint32_t netId = r.U32();
			float damage = r.F32();
			int type = r.U8();
			auto hostAvatar = GameNounManager.GetAvatar();
			if (!r.ok || !hostAvatar || !(damage > 0.0f) || damage > 1000.0f) {
				return;
			}
			Simulator::cCreatureAnimal* victim = nullptr;
			// the guest's hit is applied with the guest's puppet as the attacker: the game pays no reward to a puppet (the guest got
			// his DNA natively when he made the hit on his machine)
			Simulator::cCombatant* attacker = s.puppet ? static_cast<Simulator::cCreatureAnimal*>(s.puppet) : nullptr;
			if (netId == 0) {
				victim = hostAvatar;  // the guest bit the host's player
				attacker = static_cast<Simulator::cCreatureAnimal*>(s.puppet);
			}
			else {
				for (auto& id : s.ids) {
					if (id.second == netId) {
						victim = static_cast<Simulator::cCreatureAnimal*>(static_cast<Simulator::cGameData*>(id.first));
						break;
					}
				}
			}
			if (victim) {
				++s.damageApplied;
				DealDamage(victim, damage, type, attacker, static_cast<Simulator::cGameData*>(hostAvatar)->GetPoliticalID());
				if (s.damageLogged < 40) {
					++s.damageLogged;
					Logf("damage: applied the guest's hit on %s %u: %.1f (type %d)", netId ? "animal" : "the host's avatar", netId, damage, type);
				}
			}
			return;
		}
		if (host && data[0] == kMsgEaten && size == 5) {
			// the guest ate the corpse of an animal: remove the real one here (its state then disappears for everybody)
			Reader r(data, size);
			r.U8();
			uint32_t netId = r.U32();
			for (auto& id : s.ids) {
				if (id.second == netId) {
					Simulator::cGameData* object = static_cast<Simulator::cGameData*>(id.first);
					GameNounManager.DestroyInstance(object);
					Logf("eaten: the guest ate animal %u, removed here", netId);
					break;
				}
			}
			return;
		}
		if (!host && data[0] == kMsgCorpse && size == 13) {
			Reader r(data, size);
			r.U8();
			uint32_t netId = r.U32();
			State::CorpseInfo info;
			info.food = r.F32();
			info.flags = r.U32();
			if (r.ok) {
				s.corpses[netId] = info;
				auto m = s.mirrors.find(netId);
				if (m != s.mirrors.end()) {
					m->second.foodApplied = false;  // a mirror that already died here takes the (late) value too
				}
			}
			return;
		}
		if (!host && data[0] == kMsgDamage && size == 6) {
			Reader r(data, size);
			r.U8();
			float damage = r.F32();
			int type = r.U8();
			auto avatar = GameNounManager.GetAvatar();
			if (r.ok && avatar && damage > 0.0f && damage <= 1000.0f) {
				++s.damageApplied;
				DealDamage(avatar, damage, type, nullptr, 0);
				if (s.damageLogged < 40) {
					++s.damageLogged;
					Logf("damage: the host sent a hit on our avatar: %.1f (type %d)", damage, type);
				}
			}
			return;
		}
		// ---- creature models, in both directions (the guest's own creature travels to the host as well) and the species of the
		// other player's avatar
		if (data[0] == kMsgModelReq && size == 13) {
			Reader r(data, size);
			r.U8();
			uint32_t instance = r.U32(), type = r.U32(), group = r.U32();
			std::vector<Models::Record> records;
			if (!Models::Read(instance, type, group, records)) {
				Logf("world: the other side asked for species %08x, we have no record for it", instance);
				Writer w; w.U8(kMsgModelNone); w.U32(instance);
				QueueReliable(s, w.buf);
				return;
			}
			size_t total = 0;
			for (size_t i = 0; i < records.size(); ++i) {
				const Models::Record& rec = records[i];
				total += rec.data.size();
				for (uint32_t offset = 0; offset < rec.data.size(); offset += kModelChunk) {
					uint32_t len = (uint32_t)std::min<size_t>(kModelChunk, rec.data.size() - offset);
					Writer w;
					w.U8(kMsgModel); w.U32(instance); w.U32(type); w.U32(group);
					w.U8((uint8_t)i); w.U8((uint8_t)records.size()); w.U32(rec.type);
					w.U32((uint32_t)rec.data.size()); w.U32(offset); w.U32(len);
					w.buf.insert(w.buf.end(), rec.data.begin() + offset, rec.data.begin() + offset + len);
					QueueReliable(s, w.buf);
				}
			}
			Logf("world: sending the model of species %08x (%d records, %d bytes)", instance, (int)records.size(), (int)total);
			return;
		}
		if (data[0] == kMsgModelNone && size == 5) {
			Reader r(data, size);
			r.U8();
			uint32_t instance = r.U32();
			s.models[instance].failed = true;
			if (s.pendingAlias.count(instance)) {
				FailAlias(s, "the other side has no record for the key");
			}
			return;
		}
		if (data[0] == kMsgModel && size >= 38) {
			Reader r(data, size);
			r.U8();
			uint32_t instance = r.U32(), type = r.U32(), group = r.U32();
			uint8_t index = r.U8(), count = r.U8();
			uint32_t recType = r.U32(), total = r.U32(), offset = r.U32(), len = r.U32();
			if (!r.ok || len > size - 38 || total > 8u * 1024 * 1024 || offset + len > total || index >= count) {
				return;
			}
			State::ModelWait& wait = s.models[instance];
			if (wait.done) {
				return;
			}
			if (wait.records.size() != count) {
				wait.records.assign(count, Models::Record());
				wait.got.assign(count, 0);
			}
			Models::Record& rec = wait.records[index];
			if (rec.data.size() != total) {
				rec.data.assign(total, 0);
				rec.type = recType; rec.group = group; rec.instance = instance;
			}
			memcpy(rec.data.data() + offset, data + 38, len);
			wait.got[index] += len;
			bool complete = true;
			for (uint8_t i = 0; i < count; ++i) {
				complete = complete && !wait.records[i].data.empty() && wait.got[i] == wait.records[i].data.size();
			}
			if (complete) {
				wait.done = true;
				auto pending = s.pendingAlias.find(instance);
				if (pending != s.pendingAlias.end()) {
					// the body of a key we already hold: file it under the alias (never over our own body)
					ResourceKey alias = pending->second;
					uint32_t got = Models::Hash(wait.records);
					if (got != s.remoteHash) {
						Logf("world: the model of %08x for alias %08x has hash %08x, announced %08x", instance, alias.instanceID, got, s.remoteHash);
						FailAlias(s, "the received body is not the announced one");
					}
					else if (!Models::InstallAlias(alias.instanceID, type, group, wait.records)) {
						wait.failed = true;
						FailAlias(s, "the package could not be written or registered");
					}
					else if (!Models::Has(alias.instanceID, type, group)) {
						FailAlias(s, "registered, but the game finds no profile under the alias");
					}
					else {
						RefreshAlias(s);
					}
				}
				else if (!Models::Install(instance, type, group, wait.records)) {
					wait.failed = true;
				}
				std::vector<Models::Record>().swap(wait.records);
			}
			return;
		}
		if (data[0] == kMsgAvatarSpecies && (size == 17 || size == 13)) {
			Reader r(data, size);
			r.U8();
			ResourceKey key;
			key.instanceID = r.U32();
			key.typeID = r.U32();
			key.groupID = r.U32();
			uint32_t hash = size == 17 ? r.U32() : 0;
			if (r.ok && key.instanceID) {
				bool keyChanged = !s.remoteKnown || s.remoteKey.instanceID != key.instanceID || s.remoteKey.groupID != key.groupID;
				if (keyChanged) {
					Logf("world: the other player's avatar is species %08x/%08x/%08x (body hash %08x)", key.instanceID, key.typeID, key.groupID, hash);
					s.remoteSince = GetTickCount();
					s.aliasWanted = s.aliasFailed = s.aliasReady = false;
					s.pendingAlias.clear();
					s.remoteResolved = false;
				}
				else if (hash != s.remoteHash) {
					Logf("world: the other player's creature %08x has a new body (hash %08x, was %08x)", key.instanceID, hash, s.remoteHash);
				}
				if (keyChanged || hash != s.remoteHash) {
					s.remoteDirty = true;
				}
				s.remoteKey = key;
				s.remoteHash = hash;
				s.remoteKnown = true;
				if (s.remoteDirty) {
					ResolveRemote(s);
				}
			}
			return;
		}
		if (host) {
			if (data[0] == kMsgTelemetry && size == 13) {
				Reader r(data, size);
				r.U8();
				s.guestTable = (int)r.U32();
				s.guestMirrors = (int)r.U32();
				s.guestStrays = (int)r.U32();
				static DWORD lastLog = 0;
				DWORD now = GetTickCount();
				if (now - lastLog >= 10000) {
					lastLog = now;
					Logf("world: host animals %d | guest table %d, mirrors %d, strays %d", s.hostAnimals, s.guestTable,
						s.guestMirrors, s.guestStrays);
				}
			}
			return;
		}
		if (data[0] == kMsgSpecies && size == 13) {
			Reader r(data, size);
			r.U8();
			ResourceKey key;
			key.instanceID = r.U32();
			key.typeID = r.U32();
			key.groupID = r.U32();
			s.species[key.instanceID] = key;
			Models::RegisterCached();
			if (!Models::Has(key.instanceID, key.typeID, key.groupID)) {
				State::ModelWait& wait = s.models[key.instanceID];
				if (!wait.since) {
					wait.since = GetTickCount();
				}
				if (!wait.done && !wait.failed && !wait.requested) {
					wait.requested = true;
					Writer w; w.U8(kMsgModelReq); w.U32(key.instanceID); w.U32(key.typeID); w.U32(key.groupID);
					QueueReliable(s, w.buf);
					Logf("world: species %08x/%08x/%08x is not buildable here, asking the host for its model", key.instanceID,
						key.typeID, key.groupID);
				}
			}
			return;
		}
		if (s.guest->Receive(data, size)) {
			if (data[0] == (uint8_t)MsgType::Full && !s.haveWorld) {
				s.haveWorld = true;
				Logf("world: first snapshot from the host, %d animals", (int)s.guest->entities().size());
				App::ConsolePrintF("coop: the host's world arrived (%d animals).", (int)s.guest->entities().size());
			}
		}
	}

	// Research for gate G1 (eating): the game's creature-mode actions (EatFruit, EatMeat, Pickup, Mate, Interact, ...) carry the
	// acting creature and the target object. Reads the first two fields of the action data; guarded, nothing is changed.
	bool ReadAction(void* data, void** creature, void** target, uint32_t* noun, float* pos)
	{
		__try {
			void** fields = (void**)data;
			*creature = fields[0];
			*target = fields[1];
			*noun = 0;
			pos[0] = pos[1] = pos[2] = 0.0f;
			if (*target) {
				Simulator::cGameData* object = (Simulator::cGameData*)*target;
				*noun = object->GetNounID();
				if (auto spatial = object_cast<Simulator::cSpatialObject>(object)) {
					const Vector3& p = spatial->GetPosition();
					pos[0] = p.x; pos[1] = p.y; pos[2] = p.z;
				}
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	void OnModeAction(uint32_t actionId, void* data)
	{
		static std::map<uint32_t, int>* seen = nullptr;  // freed in Shutdown
		if (!seen) {
			seen = new std::map<uint32_t, int>();
		}
		int count = ++(*seen)[actionId];
		if (count > 5 && count % 100 != 0) {
			return;
		}
		const char* name = "?";
		bool known = true;
		switch (actionId) {
		case 0xD3353638: name = "Interact"; break;
		case 0xD335362B: name = "EatFruit"; break;
		case 0xD335362D: name = "EatMeat"; break;
		case 0xD335362C: name = "Mate/LayEgg/Pickup1"; break;
		case 0xD3353636: name = "Pickup"; break;
		default: known = false; break;
		}
		if (!known) {
			Logf("action: %08x (unknown), seen %d times", actionId, count);
			return;
		}
		void* creature = nullptr;
		void* target = nullptr;
		uint32_t noun = 0;
		float pos[3] = { 0, 0, 0 };
		if (!ReadAction(data, &creature, &target, &noun, pos)) {
			Logf("action: %08x %s, data unreadable", actionId, name);
			return;
		}
		auto avatar = GameNounManager.GetAvatar();
		const bool ours = creature == (void*)static_cast<Simulator::cCreatureBase*>(avatar);
		Logf("action: %08x %s by %s, target noun %08x at %.1f %.1f %.1f, seen %d times", actionId, name,
			ours ? "our avatar" : "someone else", noun, pos[0], pos[1], pos[2], count);
		// Research (the guest cannot eat a mirror's corpse): is the target of EatMeat a mirror, and what state is it in?
		if (actionId == 0xD335362D && ours && sState && !sState->isHost && sState->haveWorld) {
			State& s = *sState;
			uint32_t mirrorId = 0;
			for (auto& m : s.mirrors) {
				Simulator::cCreatureAnimal* a = m.second.animal.get();
				if (a && target && (target == (void*)static_cast<Simulator::cGameData*>(a) || target == (void*)a)) {
					mirrorId = m.first;
					break;
				}
			}
			if (mirrorId) {
				Simulator::cCreatureAnimal* a = s.mirrors[mirrorId].animal.get();
				Logf("eat: our avatar eats mirror %u (dead %d, food %.1f, eaten %d, flags %x)", mirrorId, a->mbDead ? 1 : 0, a->mFoodValue,
					a->mbHasBeenEaten ? 1 : 0, (unsigned)a->mGeneralFlags);
			}
			else {
				Logf("eat: EatMeat target is not a mirror (noun %08x)", noun);
			}
		}
	}

	void OnAnimation(void* creaturePtr, uint32_t animationId)
	{
		if (!sState || sApplyingAnim) {
			return;
		}
		State& s = *sState;
		auto creature = static_cast<Simulator::cCreatureBase*>(creaturePtr);
		auto avatar = GameNounManager.GetAvatar();
		uint32_t who = 0;
		if (avatar && creature == static_cast<Simulator::cCreatureBase*>(avatar)) {
			who = s.isHost ? 0u : 0xFFFFFFFFu;
		}
		else if (s.isHost) {
			bool found = false;
			for (auto& id : s.ids) {
				auto animal = static_cast<Simulator::cCreatureAnimal*>(static_cast<Simulator::cGameData*>(id.first));
				if (static_cast<Simulator::cCreatureBase*>(animal) == creature) {
					who = id.second;
					found = true;
					break;
				}
			}
			if (!found) {
				return;  // the guest's puppet and strangers
			}
		}
		else {
			return;  // the guest does not announce the animations of mirrors
		}
		if (++s.animSeen[animationId] <= 3 || s.animSeen[animationId] % 200 == 0) {
			Logf("anim: %08x played by %s %u (seen %d times)", animationId, who == 0 ? "the host avatar" : (who == 0xFFFFFFFFu ? "the guest avatar" : "animal"), who, s.animSeen[animationId]);
		}
		DWORD now = GetTickCount();
		DWORD& last = s.lastAnim[((uint64_t)who << 32) | animationId];
		if (now - last < kAnimRepeatMs) {
			return;  // looping animations (walking) would flood the link
		}
		last = now;
		Writer w;
		w.U8(kMsgAnim); w.U32(who); w.U32(animationId);
		QueueReliable(s, w.buf);
		++s.animSent;
	}

	bool FilterDamage(void* victimPtr, float damage, int type, void* attackerPtr)
	{
		if (!sState || sApplyingRemote) {
			return false;
		}
		State& s = *sState;
		auto victim = static_cast<Simulator::cCreatureAnimal*>(victimPtr);
		auto attacker = static_cast<Simulator::cCombatant*>(attackerPtr);
		Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
		bool byAvatar = avatar && attacker == static_cast<Simulator::cCombatant*>(avatar);

		if (!s.isHost && s.haveWorld) {
			if (victim == avatar) {
				return false;  // our own player: the game handles it, the host's NPCs reach us through kMsgDamage
			}
			uint32_t netId = 0;
			for (auto& m : s.mirrors) {
				if (m.second.animal.get() == victim) {
					netId = m.first;
				}
			}
			bool hostPuppet = s.puppet && (void*)victim == s.puppet;
			if (netId || hostPuppet) {
				if (s.damageLogged < 40) {
					++s.damageLogged;
					Logf("damage: guest, hit on %s %u: %.1f (type %d) by %s", netId ? "mirror" : "the host's puppet", netId, damage, type,
						byAvatar ? "our avatar -> request to the host" : "someone else -> ignored");
				}
				if (byAvatar && damage > 0.0f) {
					Writer w;
					w.U8(kMsgDamageReq); w.U32(netId); w.F32(damage); w.U8((uint8_t)type);
					QueueReliable(s, w.buf);
					++s.damageSent;
				}
				// PLAN rule 3: the actor decides the result of his own action. Our own bite on a mirror is applied here at once, by
				// the game (health, death and the DNA reward are native); the host gets the same hit as a report and applies it
				// to the real animal. Hits from anybody else are the host's business.
				if (netId && byAvatar) {
					// The LETHAL bite is not applied here: the game's native death of a mirror crashed the friend's game
					// (SporeApp.exe+0x797779, reading 0x14, right after the third bite on a mirror; the mirror has no
					// nest herd, no archetype tuning, nothing the death code may want). The host kills the real animal
					// and the mirror dies by the replay (KillLocally) a moment later.
					bool lethal = victim->mHealthPoints - damage <= 0.01f;
					return lethal;
				}
				return true;
			}
			return false;
		}
		if (s.isHost && s.puppet && (void*)victim == s.puppet) {
			// a hit (an NPC's, or the host player's) on the guest's puppet: the guest's own game applies it to its avatar
			if (s.damageLogged < 40) {
				++s.damageLogged;
				Logf("damage: host, hit on the guest's puppet: %.1f (type %d) -> sent to the guest", damage, type);
			}
			if (damage > 0.0f) {
				Writer w;
				w.U8(kMsgDamage); w.F32(damage); w.U8((uint8_t)type);
				QueueReliable(s, w.buf);
				++s.damageSent;
			}
			return true;
		}
		return false;
	}

	void TestHit(bool kill)
	{
		Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			App::ConsolePrintF("coop: needs the creature stage");
			return;
		}
		Simulator::cCreatureAnimal* best = nullptr;
		float bestDistance = 1e9f;
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != Simulator::kCreatureAnimal || object == avatar) {
				continue;
			}
			auto animal = object_cast<Simulator::cCreatureAnimal>(object);
			if (!animal || animal->mbDead) {
				continue;
			}
			float d = (animal->GetPosition() - avatar->GetPosition()).Length();
			if (d < bestDistance) {
				bestDistance = d;
				best = animal;
			}
		}
		if (!best) {
			App::ConsolePrintF("coop: no animal around");
			return;
		}
		App::ConsolePrintF("coop: %s the nearest animal (%.1f m away, health %.1f/%.1f)", kill ? "killing" : "biting", bestDistance,
			best->mHealthPoints, best->mMaxHealthPoints);
		if (kill) {
			KillLocally(best);
		}
		else {
			// the normal path: through the TakeDamage detour (FilterDamage), as a real bite from our avatar would go
			best->TakeDamage(1.0f, static_cast<Simulator::cGameData*>(avatar)->GetPoliticalID(), Simulator::kMelee, Vector3(0.0f, 0.0f, 0.0f), avatar);
		}
	}

	bool ShouldSkipAI(void* animal)
	{
		if (!sState || !sState->haveWorld || animal == (void*)GameNounManager.GetAvatar()) {
			return false;
		}
		// A mirror that was just killed (0 health, no corpse yet) gets the game's own tick for kDeathWaitMs: that is what turns a
		// 0-health creature into a corpse (GuestMirrors marks it dead by itself if the tick does not).
		for (auto& m : sState->mirrors) {
			if (m.second.animal.get() == animal) {
				if (m.second.killAt && !m.second.dead && !m.second.animal->mbDead && m.second.animal->mHealthPoints <= 0.0f &&
					GetTickCount() - m.second.killAt < kDeathWaitMs) {
					return false;
				}
				break;
			}
		}
		return true;
	}

	void Reset()
	{
		if (!sState) {
			return;
		}
		Objects::Reset();
		DestroyAllMirrors(*sState);
		delete sState->guest;
		delete sState;
		sState = nullptr;
	}

	int RemoteSpecies(uint32_t* key)
	{
		if (!sState || !sState->remoteKnown) {
			return 0;
		}
		State& s = *sState;
		const ResourceKey& k = s.remoteKey;
		if (s.aliasWanted && !s.aliasFailed) {
			// his body differs from ours under the same key: build from the alias once it is here
			if (s.aliasReady) {
				key[0] = s.remoteAlias.instanceID; key[1] = s.remoteAlias.typeID; key[2] = s.remoteAlias.groupID;
				return 2;
			}
			if (GetTickCount() - s.aliasSince > 15000) {
				FailAlias(s, "timeout, the body did not arrive");
			}
			else {
				return 1;
			}
		}
		if (Models::Has(k.instanceID, k.typeID, k.groupID)) {
			key[0] = k.instanceID; key[1] = k.typeID; key[2] = k.groupID;
			return 2;
		}
		auto wait = s.models.find(k.instanceID);
		bool failed = wait != s.models.end() && wait->second.failed;
		if (failed || GetTickCount() - s.remoteSince > 15000) {
			return 0;  // the model does not come: the caller shows the local species instead
		}
		return 1;
	}

	void SendReliable(const uint8_t* data, size_t size)
	{
		if (size) {
			S().reliableQueue.push_back(Message(data, data + size));
		}
	}

	void EnsureTuning(void* creature)
	{
		::EnsureTuning(static_cast<Simulator::cCreatureBase*>(static_cast<Simulator::cCreatureAnimal*>(creature)));
	}

	void Shutdown()
	{
		// sBlankTuning is NOT freed: creatures of the game (the puppet) may still point at it after the DLL is gone.
		Reset();
		Models::Shutdown();
	}

	void PrintStatus()
	{
		if (!sState) {
			App::ConsolePrintF("coop world: idle");
			return;
		}
		State& s = *sState;
		Objects::PrintStatus();
		App::ConsolePrintF("coop world: animations sent %d, played %d", s.animSent, s.animPlayed);
		App::ConsolePrintF("coop world: nests in the host's list %d, made here %d, own removed %d, failed %d", (int)s.nestList.size(), s.nestsCreated, s.nestsRemoved, s.nestsFailed);
		App::ConsolePrintF("coop world: damage requests sent %d, hits applied %d", s.damageSent, s.damageApplied);
		App::ConsolePrintF("coop world: host animals %d | guest table %d, mirrors %d, strays %d (removed %d), stand-in models %d | reported by guest: table %d, mirrors %d, strays %d",
			s.hostAnimals, (int)s.guest->entities().size(), (int)s.mirrors.size(), s.strays, s.removedStrays, s.standIns,
			s.guestTable, s.guestMirrors, s.guestStrays);
	}
}
