// World.cpp : the shared world, see World.h.
#include "stdafx.h"
#include "World.h"
#include "Log.h"
#include "Models.h"
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
		bool haveWorld = false;  // a snapshot arrived: the guest's own animals are strays now
		DWORD lastNeuter = 0, lastTelemetry = 0, lastLog = 0;
		int strays = 0, removedStrays = 0;
		std::set<uint32_t> warnedSpeciesIds;
		// telemetry received by the host
		int guestTable = -1, guestMirrors = -1, guestStrays = -1;

		// both roles
		bool isHost = false;
		void* puppet = nullptr;              // the other player's puppet (a cCreatureAnimal*)
		DWORD lastAvatarSend = 0;
		struct RemoteAvatar { float health = 0, maxHealth = 0, scale = 0; bool valid = false; } remoteAvatar;
		int damageLogged = 0, damageSent = 0, damageApplied = 0;

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

	void DealDamage(Simulator::cCreatureAnimal* victim, float damage, int type, Simulator::cCombatant* attacker, uint32_t attackerId)
	{
		sApplyingRemote = true;
		victim->TakeDamage(damage, attackerId, type, Vector3(0.0f, 0.0f, 0.0f), attacker);
		sApplyingRemote = false;
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
	ResourceKey SpeciesKeyOf(State& s, Simulator::cCreatureAnimal* animal)
	{
		if (animal->mSpeciesKey.instanceID) {
			return animal->mSpeciesKey;
		}
		Simulator::cSpeciesProfile* profile = animal->mpSpeciesProfile;
		if (!profile && animal->mHerd) {
			profile = animal->mHerd->mpSpeciesProfile;
		}
		if (!profile) {
			return ResourceKey();
		}
		auto cached = s.profileKeys.find(profile);
		if (cached != s.profileKeys.end()) {
			return cached->second;
		}
		ResourceKey found;
		for (auto it = SpeciesManager.mSpeciesProfiles.begin(); it != SpeciesManager.mSpeciesProfiles.end(); ++it) {
			if (&it->second == profile) {
				found = it->first;
				break;
			}
		}
		s.profileKeys[profile] = found;
		Logf("world: species profile %p -> key %08x/%08x/%08x", (void*)profile, found.instanceID, found.typeID, found.groupID);
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
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != Simulator::kCreatureAnimal || IsAvatar(object) || (void*)object == puppet) {
				continue;
			}
			auto animal = object_cast<Simulator::cCreatureAnimal>(object);
			if (!animal) {
				continue;
			}
			EntityState st;
			st.kind = Kind::Creature;
			const ResourceKey speciesKey = SpeciesKeyOf(s, animal);
			st.species = speciesKey.instanceID;
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
		}
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
				it = s.mirrors.erase(it);
			}
			else {
				++it;
			}
		}
		int created = 0;
		for (auto& e : table) {
			const EntityState& st = e.second;
			Mirror& m = s.mirrors[e.first];
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
				}
				continue;
			}
			// grew up (or was made with another age): build the body again, the model of a baby and an adult differ
			if (st.age && m.age != st.age) {
				DestroyMirror(m);
				m = Mirror();
				continue;
			}
			// size, health and the alpha flag come from the host (the guest's own game would roll its own values)
			if (fabsf(m.animal->GetScale() - st.scale) > 0.001f && st.scale > 0.0f) {
				m.animal->SetScale(st.scale);
			}
			bool dead = (st.flags & kFlagDead) || (st.maxHealth > 0.0f && st.health <= 0.0f);
			if (dead && !m.dead) {
				m.dead = true;
				KillLocally(m.animal.get());  // the game plays the death; a corpse is then pinned to the host's corpse below
			}
			else if (!dead && st.maxHealth > 0.0f) {
				m.animal->mMaxHealthPoints = st.maxHealth;
				m.animal->mHealthPoints = st.health;
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
	// strays: remove them, every half second, so the guest only sees the host's animals (risk R2 of DESIGN.md).
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

	void DestroyAllMirrors(State& s)
	{
		for (auto& m : s.mirrors) {
			DestroyMirror(m.second);
		}
		s.mirrors.clear();
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
		}
		else if (s.haveWorld) {
			GuestMirrors(s, now);
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
			Simulator::cCombatant* attacker = hostAvatar;
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
		if (host) {
			if (data[0] == kMsgModelReq && size == 13) {
				Reader r(data, size);
				r.U8();
				uint32_t instance = r.U32(), type = r.U32(), group = r.U32();
				std::vector<Models::Record> records;
				if (!Models::Read(instance, type, group, records)) {
					Logf("world: the guest asked for species %08x, the host has no record for it", instance);
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
				Logf("world: sending the model of species %08x to the guest (%d records, %d bytes)", instance,
					(int)records.size(), (int)total);
				return;
			}
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
		if (s.guest->Receive(data, size)) {
			if (data[0] == (uint8_t)MsgType::Full && !s.haveWorld) {
				s.haveWorld = true;
				Logf("world: first snapshot from the host, %d animals", (int)s.guest->entities().size());
				App::ConsolePrintF("coop: the host's world arrived (%d animals).", (int)s.guest->entities().size());
			}
		}
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
				return true;  // the host decides; the new health comes back with the next state
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
		DestroyAllMirrors(*sState);
		delete sState->guest;
		delete sState;
		sState = nullptr;
	}

	void Shutdown()
	{
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
		App::ConsolePrintF("coop world: damage requests sent %d, hits applied %d", s.damageSent, s.damageApplied);
		App::ConsolePrintF("coop world: host animals %d | guest table %d, mirrors %d, strays %d (removed %d), stand-in models %d | reported by guest: table %d, mirrors %d, strays %d",
			s.hostAnimals, (int)s.guest->entities().size(), (int)s.mirrors.size(), s.strays, s.removedStrays, s.standIns,
			s.guestTable, s.guestMirrors, s.guestStrays);
	}
}
