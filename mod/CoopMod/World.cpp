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
	const uint8_t kMsgNests = 113;      // host -> guest: [113][chunk][chunks][n] then n x (x y z f32, species instance, type, group)
	const DWORD kNestsEveryMs = 20000;
	const uint8_t kMsgAnim = 112;       // both ways: [112][who][animation id]; who: 0 = the host's avatar, 0xFFFFFFFF = the guest's avatar, else a netId
	const DWORD kAnimRepeatMs = 400;     // the same animation of the same creature is sent at most this often
	const DWORD kAvatarEveryMs = 200;
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
		uint32_t species = 0;  // the species the body was made for (a hatchling's species becomes known a moment after it appears)
		PuppetMotion::Follower follower;  // the game's locomotion walks the mirror (legs move) instead of sliding it
		bool dead = false;
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
		ResourceKey sentKey;                 // species of our own avatar as last sent
		DWORD lastSpeciesSend = 0;
		void* puppet = nullptr;              // the other player's puppet (a cCreatureAnimal*)
		DWORD lastAvatarSend = 0;
		struct RemoteAvatar { float health = 0, maxHealth = 0, scale = 0; bool valid = false; } remoteAvatar;
		int damageLogged = 0, damageSent = 0, damageApplied = 0;
		std::map<uint64_t, DWORD> lastAnim;   // (who, animation) -> last time sent
		std::map<uint32_t, int> animSeen;     // animation id -> how often the game played it (log for spike S15)
		int animSent = 0, animPlayed = 0;

		// nests (G2): the host's list, and the nests the guest made from it
		struct NestInfo { Vector3 pos; ResourceKey key; };
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
		DealDamage(animal, animal->mMaxHealthPoints + 100.0f, Simulator::kMelee, nullptr, 0);
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
			Logf("world: host sees %d animals, announces %d, %d wait for their species", (int)present.size(), (int)current.size(),
				(int)(present.size() - current.size()));
		}

		s.host.AdvanceTick();
		// gone
		for (auto it = s.ids.begin(); it != s.ids.end();) {
			if (!current.count(it->first)) {
				s.host.Destroy(it->second);
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
		if (sSendUnreliable) {
			for (auto& m : s.host.MakeDeltas(kDeltaPerMessage)) {
				sSendUnreliable(m.data(), (uint32_t)m.size());
			}
		}
	}

	// ------------------------------------------------------------ guest

	// Builds the mirror: an EMPTY quiet herd plus an animal created into it (the way SporeCoop does it). A herd of size 1
	// made by CreateHerd only got its animal when the game's own herd logic decided to, which happened for 23 of 99
	// animals in the first guest test. Returns false when it has to wait (model on its way) or failed.
	bool MakeMirror(State& s, const EntityState& st, Mirror& m)
	{
		Simulator::cSpeciesProfile* profile = nullptr;
		auto key = s.species.find(st.species);
		if (key != s.species.end()) {
			profile = SpeciesManager.GetSpeciesProfile(key->second);
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
			if ((st.age && m.age != st.age) || (st.species && m.species != st.species)) {
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
			if (dead && !m.dead) {
				m.dead = true;
				KillLocally(m.animal.get());  // the host's animal died: the game plays the death, a corpse is pinned below
			}
			else if (!dead && !m.dead && st.maxHealth > 0.0f) {
				m.animal->mMaxHealthPoints = st.maxHealth;
				// Our own bites already lowered the health here (they are reported to the host, which echoes the new value
				// a moment later): take the host's number only when it changed, so our hit is not undone in between.
				if (st.health != m.lastHostHealth) {
					m.animal->mHealthPoints = st.health;
					m.lastHostHealth = st.health;
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
			if (m.dead || !closeBy) {
				// a corpse must not fly away (SporeCoop found it does), far animals are not worth the locomotion calls
				m.animal->Teleport(m.pos, rotation);
				m.follower = PuppetMotion::Follower();
			}
			else {
				// the game's locomotion walks the animal to the host's position (legs move), see game/PuppetMotion.cpp
				PuppetMotion::Drive(m.follower, m.animal.get(), m.pos, rotation);
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
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != Simulator::kCreatureAnimal || IsAvatar(object) || (void*)object == puppet) {
				continue;
			}
			if (IsMirrorAnimal(s, object)) {
				continue;
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
		int placed = 0;
		for (auto& m : s.mirrors) {
			if (m.second.animal) {
				++placed;
			}
		}
		Writer w;
		w.U8(kMsgTelemetry); w.U32((uint32_t)s.guest->entities().size()); w.U32((uint32_t)placed); w.U32((uint32_t)s.strays);
		QueueReliable(s, w.buf);
		if (now - s.lastLog >= 10000) {
			s.lastLog = now;
			Logf("world: guest table %d, mirrors %d, strays now %d, strays removed so far %d", (int)s.guest->entities().size(),
				placed, s.strays, s.removedStrays);
		}
	}

	// ------------------------------------------------------------ nests (G2)
	// The nests of a planet are made by the game when the stage starts, so two machines that loaded the same save have different
	// ones (the friend had 96 and 108, the host 101; none at the same places). The host's list is sent every 20 s; the guest
	// removes its own nests once (except its avatar's) and makes the host's: an empty quiet herd with a nest at the same place.

	// Reads a nest's position and the species profile of its herd. The host crashed (CoopMod.dll+0x16ad8, "reading 00000a27") when
	// the guest connected and the first nest list was built, so every raw read of a game object here is guarded: a nest or herd the
	// game has half-destroyed gives false instead of a crash. (No C++ objects with destructors in a function with __try.)
	bool ReadNest(Simulator::cNest* nest, Simulator::cSpeciesProfile** profile, float* pos, Simulator::cHerd** herd)
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
			if (!ReadNest(nest, &profile, pos, &herd)) {
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
			m.herd->SetEnabled(false);
			m.nest = m.herd->mpNest;
			if (!m.nest) {
				m.nest = GameNounManager.CreateNest(info.pos, m.herd.get());
			}
			existing.push_back(info.pos);
			s.nestMirrors.push_back(m);
			++s.nestsCreated;
			++made;
		}
		if (made) {
			Logf("nests: made %d from the host's list (%d in the list, %d made so far, %d failed)", made, (int)s.nestList.size(), s.nestsCreated,
				s.nestsFailed);
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
				if (key.instanceID && (changed || now - s.lastSpeciesSend >= 10000)) {
					s.lastSpeciesSend = now;
					s.sentKey = key;
					Writer w;
					w.U8(kMsgAvatarSpecies); w.U32(key.instanceID); w.U32(key.typeID); w.U32(key.groupID);
					QueueReliable(s, w.buf);
				}
			}
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
			if (!r.ok || chunks == 0 || chunks > 100 || n > 200 || size != 13 + n * 24) {
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
			s.models[r.U32()].failed = true;
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
				if (!Models::Install(instance, type, group, wait.records)) {
					wait.failed = true;
				}
				std::vector<Models::Record>().swap(wait.records);
			}
			return;
		}
		if (data[0] == kMsgAvatarSpecies && size == 13) {
			Reader r(data, size);
			r.U8();
			ResourceKey key;
			key.instanceID = r.U32();
			key.typeID = r.U32();
			key.groupID = r.U32();
			if (r.ok && key.instanceID) {
				if (!s.remoteKnown || s.remoteKey.instanceID != key.instanceID || s.remoteKey.groupID != key.groupID) {
					Logf("world: the other player's avatar is species %08x/%08x/%08x", key.instanceID, key.typeID, key.groupID);
					s.remoteSince = GetTickCount();
				}
				s.remoteKey = key;
				s.remoteKnown = true;
				Models::RegisterCached();
				if (!Models::Has(key.instanceID, key.typeID, key.groupID)) {
					State::ModelWait& wait = s.models[key.instanceID];
					if (!wait.requested && !wait.done && !wait.failed) {
						wait.requested = true;
						wait.since = GetTickCount();
						Writer w; w.U8(kMsgModelReq); w.U32(key.instanceID); w.U32(key.typeID); w.U32(key.groupID);
						QueueReliable(s, w.buf);
						Logf("world: the other player's creature %08x is not buildable here, asking for its model", key.instanceID);
					}
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
		Logf("action: %08x %s by %s, target noun %08x at %.1f %.1f %.1f, seen %d times", actionId, name,
			creature == (void*)static_cast<Simulator::cCreatureBase*>(avatar) ? "our avatar" : "someone else", noun, pos[0], pos[1], pos[2], count);
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
		return sState && sState->haveWorld && animal != (void*)GameNounManager.GetAvatar();
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
