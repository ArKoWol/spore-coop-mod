// Objects.cpp : see Objects.h.
#include "stdafx.h"
#include "Objects.h"
#include "World.h"
#include "Log.h"
#include "../../core/coop_core.h"

#include <map>
#include <set>
#include <vector>

namespace
{
	const DWORD kScanEveryMs = 500;
	const float kMatchRadius = 0.75f;      // same kind within this distance = the same object on the other machine
	const uint32_t kManifestChunk = 200;   // objects per manifest message

	// Switched off after the third test (fruit and bushes flickered on the guest, fruit lay at other places): fruit also rots, falls
	// and regrows by itself on each machine, so "it is gone here" is not "it was eaten", and the two sides kept destroying each
	// other's objects. Scanning and the join manifest only LOG now (research); eating will be replicated from the game's own
	// EatFruit / EatMeat actions (see World::OnModeAction).
	const bool kBroadcast = false;
	const bool kApplyManifest = false;

	struct Obj
	{
		uint32_t noun = 0;
		Vector3 pos;
	};

	bool Tracked(uint32_t noun)
	{
		return noun == Simulator::kFruit || noun == Simulator::kFruitGroup || noun == Simulator::kGamePlant || noun == Simulator::kEgg;
	}

	const char* Name(uint32_t noun)
	{
		switch (noun) {
		case Simulator::kFruit: return "Fruit";
		case Simulator::kFruitGroup: return "FruitGroup";
		case Simulator::kGamePlant: return "GamePlant";
		case Simulator::kEgg: return "Egg";
		}
		return "?";
	}

	// one heap object, freed by Reset(): no static destructor may hold memory at process exit
	struct State
	{
		std::map<void*, Obj> seen;
		std::set<void*> silent;       // objects we destroyed because the other side said so: not reported back
		DWORD lastScan = 0;
		bool primed = false;
		int removesSent = 0, removesApplied = 0, removesNotFound = 0, adds = 0;
		std::map<uint32_t, int> gone;  // per kind: how many disappeared here (research)
		int goneLogged = 0;
		// host: the guest's manifest while it arrives
		std::vector<Obj> manifest;
		uint32_t manifestChunks = 0, manifestGot = 0;
		bool manifestSent = false;    // guest: sent once per connection
	};
	State* sState = nullptr;
	State& S()
	{
		if (!sState) {
			sState = new State();
		}
		return *sState;
	}

	void Scan(std::map<void*, Obj>& out)
	{
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			uint32_t noun = object->GetNounID();
			if (!Tracked(noun)) {
				continue;
			}
			auto spatial = object_cast<Simulator::cSpatialObject>(object);
			if (!spatial) {
				continue;
			}
			Obj o;
			o.noun = noun;
			o.pos = spatial->GetPosition();
			out[object] = o;
		}
	}

	void SendRemove(const Obj& o)
	{
		coop::Writer w;
		w.U8(Objects::kMsgRemove); w.U32(o.noun); w.F32(o.pos.x); w.F32(o.pos.y); w.F32(o.pos.z);
		World::SendReliable(w.buf.data(), w.buf.size());
	}

	// destroys the nearest object of this kind within kMatchRadius
	bool DestroyMatching(State& s, uint32_t noun, const Vector3& pos)
	{
		Simulator::cGameData* best = nullptr;
		float bestDistance = kMatchRadius;
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != noun) {
				continue;
			}
			auto spatial = object_cast<Simulator::cSpatialObject>(object);
			if (!spatial) {
				continue;
			}
			float d = (spatial->GetPosition() - pos).Length();
			if (d <= bestDistance) {
				bestDistance = d;
				best = object;
			}
		}
		if (!best) {
			return false;
		}
		s.silent.insert(best);
		GameNounManager.DestroyInstance(best);
		return true;
	}

	void SendManifest(State& s, const std::map<void*, Obj>& current)
	{
		std::vector<Obj> all;
		for (auto& c : current) {
			all.push_back(c.second);
		}
		uint32_t chunks = (uint32_t)((all.size() + kManifestChunk - 1) / kManifestChunk);
		if (chunks == 0) {
			chunks = 1;
		}
		for (uint32_t c = 0; c < chunks; ++c) {
			size_t from = (size_t)c * kManifestChunk;
			size_t to = std::min(all.size(), from + kManifestChunk);
			coop::Writer w;
			w.U8(Objects::kMsgManifest); w.U32(c); w.U32(chunks); w.U32((uint32_t)(to - from));
			for (size_t i = from; i < to; ++i) {
				w.U32(all[i].noun); w.F32(all[i].pos.x); w.F32(all[i].pos.y); w.F32(all[i].pos.z);
			}
			World::SendReliable(w.buf.data(), w.buf.size());
		}
		s.manifestSent = true;
		Logf("eco: manifest of %d objects sent to the host", (int)all.size());
	}

	// host: what does the guest have that we do not (it was eaten here since the save), and the other way round
	void CompareManifest(State& s)
	{
		std::map<void*, Obj> current;
		Scan(current);
		std::map<uint32_t, int> guestOnly, hostOnly, matched;
		std::set<void*> used;
		std::vector<Obj> toRemove;
		for (const Obj& g : s.manifest) {
			void* match = nullptr;
			float best = kMatchRadius;
			for (auto& h : current) {
				if (h.second.noun != g.noun || used.count(h.first)) {
					continue;
				}
				float d = (h.second.pos - g.pos).Length();
				if (d <= best) {
					best = d;
					match = h.first;
				}
			}
			if (match) {
				used.insert(match);
				++matched[g.noun];
			}
			else {
				++guestOnly[g.noun];
				toRemove.push_back(g);
			}
		}
		// Safety: a few guest-only objects are what was eaten since the save. If a whole kind disagrees (positions that do not
		// line up between the machines) removing them would wipe the guest's world, so such a kind is only logged.
		std::set<uint32_t> refused;
		for (auto& entry : guestOnly) {
			if (entry.second > std::max(20, matched[entry.first] / 4)) {
				refused.insert(entry.first);
				Logf("eco: manifest %s: %d guest-only against %d matched, NOT removing (the worlds do not line up)", Name(entry.first),
					entry.second, matched[entry.first]);
			}
		}
		for (const Obj& g : toRemove) {
			if (kApplyManifest && !refused.count(g.noun)) {
				SendRemove(g);  // the guest destroys its copy
				++s.removesSent;
			}
		}
		for (auto& h : current) {
			if (!used.count(h.first)) {
				++hostOnly[h.second.noun];
			}
		}
		for (uint32_t noun : { (uint32_t)Simulator::kFruit, (uint32_t)Simulator::kFruitGroup, (uint32_t)Simulator::kGamePlant, (uint32_t)Simulator::kEgg }) {
			Logf("eco: manifest %-10s matched %d, guest-only %d (removed at the guest), host-only %d", Name(noun), matched[noun], guestOnly[noun],
				hostOnly[noun]);
		}
		App::ConsolePrintF("coop: objects compared with the guest (see the eco: lines in the log), %d removed at the guest", s.removesSent);
		std::vector<Obj>().swap(s.manifest);
	}
}

namespace Objects
{
	void Update(bool host, bool inGame)
	{
		if (!inGame) {
			return;
		}
		State& s = S();
		DWORD now = GetTickCount();
		if (now - s.lastScan < kScanEveryMs) {
			return;
		}
		s.lastScan = now;

		std::map<void*, Obj> current;
		Scan(current);
		if (!s.primed) {
			s.primed = true;
			s.seen = current;
			if (!host && !s.manifestSent) {
				SendManifest(s, current);
			}
			return;
		}
		// gone since the last scan: eaten, hatched, picked up -> tell the other side (not the ones we destroyed for it)
		for (auto& old : s.seen) {
			if (current.count(old.first)) {
				continue;
			}
			if (s.silent.erase(old.first)) {
				continue;
			}
			++s.gone[old.second.noun];
			if (kBroadcast) {
				SendRemove(old.second);
				++s.removesSent;
			}
			if (s.goneLogged++ < 60) {
				Logf("eco: %s at %.1f %.1f %.1f is gone here%s", Name(old.second.noun), old.second.pos.x, old.second.pos.y, old.second.pos.z,
					kBroadcast ? " -> told the other side" : " (research: not reported)");
			}
		}
		for (auto& now2 : current) {
			if (!s.seen.count(now2.first)) {
				++s.adds;  // new fruit or an egg laid: not replicated yet, counted so the log shows how much differs
			}
		}
		s.seen.swap(current);
	}

	bool OnMessage(const uint8_t* data, uint32_t size, bool host)
	{
		if (size < 1 || (data[0] != kMsgManifest && data[0] != kMsgRemove)) {
			return false;
		}
		State& s = S();
		coop::Reader r(data, size);
		r.U8();
		if (data[0] == kMsgRemove) {
			uint32_t noun = r.U32();
			Vector3 pos(r.F32(), r.F32(), r.F32());
			if (!r.ok || !Tracked(noun)) {
				return true;
			}
			if (DestroyMatching(s, noun, pos)) {
				++s.removesApplied;
			}
			else {
				++s.removesNotFound;
			}
			return true;
		}
		// manifest chunk (host only)
		if (!host) {
			return true;
		}
		uint32_t chunk = r.U32(), chunks = r.U32(), n = r.U32();
		if (!r.ok || chunks == 0 || chunks > 1000 || n > kManifestChunk * 2) {
			return true;
		}
		if (chunk == 0) {
			s.manifest.clear();
			s.manifestChunks = chunks;
			s.manifestGot = 0;
		}
		for (uint32_t i = 0; i < n; ++i) {
			Obj o;
			o.noun = r.U32();
			o.pos = Vector3(r.F32(), r.F32(), r.F32());
			if (!r.ok) {
				return true;
			}
			if (Tracked(o.noun)) {
				s.manifest.push_back(o);
			}
		}
		if (++s.manifestGot >= s.manifestChunks) {
			CompareManifest(s);
		}
		return true;
	}

	void Reset()
	{
		delete sState;
		sState = nullptr;
	}

	void PrintStatus()
	{
		if (!sState) {
			App::ConsolePrintF("coop objects: idle");
			return;
		}
		for (auto& g : sState->gone) {
			App::ConsolePrintF("coop objects: %s gone here %d times", Name(g.first), g.second);
		}
		App::ConsolePrintF("coop objects: tracking %d (fruit, fruit groups, plants, eggs), removes sent %d, applied %d, not found %d, new here %d",
			(int)sState->seen.size(), sState->removesSent, sState->removesApplied, sState->removesNotFound, sState->adds);
	}
}
