// World.cpp : the shared world, see World.h.
#include "stdafx.h"
#include "World.h"
#include "Log.h"
#include "../../core/coop_core.h"

#include <deque>
#include <map>
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

	struct Mirror
	{
		cHerdPtr herd;
		cCreatureAnimalPtr animal;
		int waitFrames = 0;
		bool placed = false;
		Vector3 pos;
		float rot[4] = { 0, 0, 0, 1 };
		DWORD last = 0;
	};

	// Everything lives in one heap object that Shutdown() frees: no static destructor may run at process exit.
	struct State : IWorldSink
	{
		std::deque<Message> reliableQueue;  // waits for a free send buffer, keeps the order

		// host
		HostWorld host;
		std::map<void*, uint32_t> ids;             // game object -> netId
		std::map<uint32_t, ResourceKey> speciesSent;
		DWORD lastTick = 0, lastFull = 0;
		bool fullNow = false;
		int hostAnimals = 0;

		// guest
		GuestWorld* guest = nullptr;
		std::map<uint32_t, ResourceKey> species;
		std::map<uint32_t, Mirror> mirrors;
		bool haveWorld = false;  // a snapshot arrived: the guest's own animals are strays now
		DWORD lastNeuter = 0, lastTelemetry = 0, lastLog = 0;
		int strays = 0, removedStrays = 0;
		bool warnedSpecies = false;
		// telemetry received by the host
		int guestTable = -1, guestMirrors = -1, guestStrays = -1;

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
			st.species = animal->mSpeciesKey.instanceID;
			const Vector3& p = animal->GetPosition();
			const Math::Quaternion& q = animal->GetOrientation();
			st.pos = { p.x, p.y, p.z };
			st.rot = { q.x, q.y, q.z, q.w };
			st.health = animal->mHealthPoints;
			st.maxHealth = animal->mMaxHealthPoints;
			current[object] = st;
			if (!s.speciesSent.count(st.species)) {
				s.speciesSent[st.species] = animal->mSpeciesKey;
				Writer w;
				w.U8(kMsgSpecies); w.U32(animal->mSpeciesKey.instanceID); w.U32(animal->mSpeciesKey.typeID);
				w.U32(animal->mSpeciesKey.groupID);
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
			QueueReliable(s, s.host.MakeFull());
		}
		if (sSendUnreliable) {
			for (auto& m : s.host.MakeDeltas(kDeltaPerMessage)) {
				sSendUnreliable(m.data(), (uint32_t)m.size());
			}
		}
	}

	// ------------------------------------------------------------ guest

	cHerdPtr MakeHerd(State& s, const EntityState& st)
	{
		Simulator::cSpeciesProfile* profile = nullptr;
		auto key = s.species.find(st.species);
		if (key != s.species.end()) {
			profile = SpeciesManager.GetSpeciesProfile(key->second);
		}
		if (!profile) {
			// the species is unknown here: show the guest's own species instead of nothing
			if (auto avatar = GameNounManager.GetAvatar()) {
				profile = SpeciesManager.GetSpeciesProfile(avatar->mSpeciesKey);
			}
			if (!s.warnedSpecies) {
				s.warnedSpecies = true;
				Logf("world: species %08x not found, using the avatar's species for mirrors", st.species);
			}
		}
		if (!profile) {
			return nullptr;
		}
		Vector3 pos(st.pos.x, st.pos.y, st.pos.z);
		return GameNounManager.CreateHerd(pos, profile, 1, false, 0, false);
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
				++created;
				m = Mirror();
				m.herd = MakeHerd(s, st);
				continue;
			}
			if (!m.animal) {
				++m.waitFrames;
				if (!m.herd->mHerd.empty()) {
					m.animal = m.herd->mHerd[0];
				}
				continue;
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
			m.animal->Teleport(m.pos, Math::Quaternion(m.rot[0], m.rot[1], m.rot[2], m.rot[3]));
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
	}

	void PrintStatus()
	{
		if (!sState) {
			App::ConsolePrintF("coop world: idle");
			return;
		}
		State& s = *sState;
		App::ConsolePrintF("coop world: host animals %d | guest table %d, mirrors %d, strays %d (removed %d) | reported by guest: table %d, mirrors %d, strays %d",
			s.hostAnimals, (int)s.guest->entities().size(), (int)s.mirrors.size(), s.strays, s.removedStrays, s.guestTable,
			s.guestMirrors, s.guestStrays);
	}
}
