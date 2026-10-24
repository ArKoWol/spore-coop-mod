// coop_core.h : the replication core of the co-op mod (milestone M0 of DESIGN.md).
//
// Plain C++17, no Spore and no Windows headers, so it is tested without the game (tests/core_tests.cpp, with a
// network that loses, duplicates and reorders packets). The game adapter (game/) feeds it the host's entities and turns
// what the guest receives into mirrors.
//
//   host:  Spawn / Update / Destroy  ->  MakeDelta() every tick (unreliable), TakeEvents() (reliable, ordered),
//          MakeFull() every ~10 s (unreliable, heals everything)
//   guest: GuestWorld::Receive(bytes) for every message, in any order, with losses and duplicates on the
//          unreliable ones; the sink gets OnCreate / OnUpdate / OnDestroy.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace coop
{
	struct Vec3 { float x = 0, y = 0, z = 0; };
	struct Quat { float x = 0, y = 0, z = 0, w = 1; };

	enum class Kind : uint8_t { Creature = 1, Nest = 2, Egg = 3, Plant = 4, Fruit = 5, Rock = 6, Other = 7 };

	// Flags
	enum : uint8_t { kFlagDead = 1 };
	// owner: the player index for avatars (0 = host, 1 = guest), 255 for everything the host's world owns
	constexpr uint8_t kWorldOwner = 255;

	struct EntityState
	{
		uint32_t netId = 0;
		Kind kind = Kind::Other;
		uint32_t species = 0;  // hash of the species key, 0 if not a creature
		Vec3 pos;
		Quat rot;
		float health = 0, maxHealth = 0;
		uint16_t anim = 0;
		uint8_t flags = 0;
		uint8_t owner = kWorldOwner;

		bool operator==(const EntityState& o) const
		{
			return netId == o.netId && kind == o.kind && species == o.species && pos.x == o.pos.x && pos.y == o.pos.y &&
				pos.z == o.pos.z && rot.x == o.rot.x && rot.y == o.rot.y && rot.z == o.rot.z && rot.w == o.rot.w &&
				health == o.health && maxHealth == o.maxHealth && anim == o.anim && flags == o.flags && owner == o.owner;
		}
		bool operator!=(const EntityState& o) const { return !(*this == o); }
	};

	using Message = std::vector<uint8_t>;

	enum class MsgType : uint8_t { Delta = 1, Full = 2, Spawn = 3, Destroy = 4 };

	// ---------------------------------------------------------------- wire format

	class Writer
	{
	public:
		void U8(uint8_t v) { buf.push_back(v); }
		void U16(uint16_t v) { Raw(&v, 2); }
		void U32(uint32_t v) { Raw(&v, 4); }
		void F32(float v) { Raw(&v, 4); }
		void State(const EntityState& s)
		{
			U32(s.netId); U8((uint8_t)s.kind); U32(s.species);
			F32(s.pos.x); F32(s.pos.y); F32(s.pos.z);
			F32(s.rot.x); F32(s.rot.y); F32(s.rot.z); F32(s.rot.w);
			F32(s.health); F32(s.maxHealth); U16(s.anim); U8(s.flags); U8(s.owner);
		}
		Message buf;
	private:
		void Raw(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; buf.insert(buf.end(), b, b + n); }
	};

	class Reader
	{
	public:
		Reader(const uint8_t* data, size_t size) : p(data), end(data + size) {}
		bool ok = true;
		uint8_t U8() { uint8_t v = 0; Raw(&v, 1); return v; }
		uint16_t U16() { uint16_t v = 0; Raw(&v, 2); return v; }
		uint32_t U32() { uint32_t v = 0; Raw(&v, 4); return v; }
		float F32() { float v = 0; Raw(&v, 4); return v; }
		EntityState State()
		{
			EntityState s;
			s.netId = U32(); s.kind = (Kind)U8(); s.species = U32();
			s.pos.x = F32(); s.pos.y = F32(); s.pos.z = F32();
			s.rot.x = F32(); s.rot.y = F32(); s.rot.z = F32(); s.rot.w = F32();
			s.health = F32(); s.maxHealth = F32(); s.anim = U16(); s.flags = U8(); s.owner = U8();
			return s;
		}
		bool Done() const { return p == end; }
	private:
		void Raw(void* out, size_t n)
		{
			if ((size_t)(end - p) < n) { ok = false; p = end; return; }
			std::memcpy(out, p, n); p += n;
		}
		const uint8_t* p;
		const uint8_t* end;
	};

	// ---------------------------------------------------------------- host side

	class HostWorld
	{
	public:
		uint32_t tick() const { return tick_; }
		void AdvanceTick() { ++tick_; }
		const std::map<uint32_t, EntityState>& entities() const { return entities_; }

		// Creates an entity, assigns its netId (never reused) and queues a reliable Spawn event.
		uint32_t Spawn(EntityState s)
		{
			s.netId = nextId_++;
			entities_[s.netId] = s;
			dirty_.insert(s.netId);
			Writer w; w.U8((uint8_t)MsgType::Spawn); w.U32(tick_); w.State(s);
			events_.push_back(std::move(w.buf));
			return s.netId;
		}

		void Update(uint32_t netId, EntityState s)
		{
			auto it = entities_.find(netId);
			if (it == entities_.end()) return;
			s.netId = netId;
			if (it->second != s) { it->second = s; dirty_.insert(netId); }
		}

		void Destroy(uint32_t netId)
		{
			if (!entities_.erase(netId)) return;
			dirty_.erase(netId);
			Writer w; w.U8((uint8_t)MsgType::Destroy); w.U32(tick_); w.U32(netId);
			events_.push_back(std::move(w.buf));
		}

		// Reliable, ordered messages produced since the last call (Spawn, Destroy).
		std::vector<Message> TakeEvents() { std::vector<Message> out; out.swap(events_); return out; }

		// Unreliable: the entities that changed since the last delta. Empty message if nothing changed.
		Message MakeDelta()
		{
			Message out;
			if (dirty_.empty()) return out;
			Writer w; w.U8((uint8_t)MsgType::Delta); w.U32(tick_); w.U16((uint16_t)dirty_.size());
			for (uint32_t id : dirty_) w.State(entities_[id]);
			dirty_.clear();
			return w.buf;
		}

		// Unreliable, sent every ~10 s and to a joining guest: every entity.
		Message MakeFull() const
		{
			Writer w; w.U8((uint8_t)MsgType::Full); w.U32(tick_); w.U16((uint16_t)entities_.size());
			for (auto& e : entities_) w.State(e.second);
			return w.buf;
		}

	private:
		uint32_t tick_ = 1;
		uint32_t nextId_ = 1;
		std::map<uint32_t, EntityState> entities_;
		std::set<uint32_t> dirty_;
		std::vector<Message> events_;
	};

	// ---------------------------------------------------------------- guest side

	struct IWorldSink
	{
		virtual ~IWorldSink() {}
		virtual void OnCreate(const EntityState& s) = 0;
		virtual void OnUpdate(const EntityState& s) = 0;
		virtual void OnDestroy(uint32_t netId) = 0;
	};

	// The three switches exist so the tests can turn each protection off once and check that a test notices
	// (mutation twins). A real guest keeps them all on.
	struct GuestOptions
	{
		bool rejectStale = true;      // ignore an older state than the one already applied
		bool tombstones = true;       // remember destroyed ids so a late unreliable delta cannot bring them back
		bool reconcileOnFull = true;  // a full snapshot also removes entities it does not contain
	};

	class GuestWorld
	{
	public:
		explicit GuestWorld(IWorldSink* sink, GuestOptions options = GuestOptions()) : sink_(sink), opt_(options) {}

		const std::map<uint32_t, EntityState>& entities() const { return entities_; }
		// The host tick of the newest state applied to this entity, 0 if unknown.
		uint32_t AppliedTick(uint32_t netId) const { auto it = tick_.find(netId); return it == tick_.end() ? 0 : it->second; }

		// Returns false if the message is malformed.
		bool Receive(const Message& m) { return Receive(m.data(), m.size()); }
		bool Receive(const uint8_t* data, size_t size)
		{
			Reader r(data, size);
			MsgType type = (MsgType)r.U8();
			uint32_t tick = r.U32();
			if (!r.ok) return false;
			switch (type) {
			case MsgType::Delta: {
				uint16_t n = r.U16();
				for (uint16_t i = 0; i < n && r.ok; ++i) { EntityState s = r.State(); if (r.ok) ApplyState(s, tick, Source::Delta); }
				return r.ok;
			}
			case MsgType::Spawn: {
				EntityState s = r.State();
				if (!r.ok) return false;
				ApplyState(s, tick, Source::Spawn);
				return true;
			}
			case MsgType::Destroy: {
				uint32_t id = r.U32();
				if (!r.ok) return false;
				Remove(id);
				if (opt_.tombstones) tombstones_[id] = tick;
				return true;
			}
			case MsgType::Full: {
				uint16_t n = r.U16();
				std::vector<EntityState> states;
				for (uint16_t i = 0; i < n && r.ok; ++i) states.push_back(r.State());
				if (!r.ok) return false;
				ApplyFull(states, tick);
				return true;
			}
			}
			return false;
		}

	private:
		enum class Source { Delta, Spawn, Full };

		void ApplyState(const EntityState& s, uint32_t tick, Source source)
		{
			if (opt_.tombstones && tombstones_.count(s.netId)) return;  // destroyed already
			auto it = entities_.find(s.netId);
			if (it == entities_.end()) {
				// A delta older than the newest full snapshot cannot create an entity: that snapshot (or a Destroy)
				// already says it does not exist. Found by the chaos test: a late duplicate delta brought a destroyed
				// entity back after its tombstone had been cleared by a full snapshot.
				if (source == Source::Delta && opt_.rejectStale && tick < lastFull_) return;
				entities_[s.netId] = s;
				tick_[s.netId] = tick;
				sink_->OnCreate(s);
				return;
			}
			if (opt_.rejectStale && tick <= tick_[s.netId]) return;
			if (it->second != s) { it->second = s; sink_->OnUpdate(s); }
			tick_[s.netId] = tick;
		}

		void ApplyFull(const std::vector<EntityState>& states, uint32_t tick)
		{
			if (opt_.rejectStale && tick <= lastFull_) return;
			lastFull_ = tick;
			std::set<uint32_t> present;
			for (auto& s : states) {
				present.insert(s.netId);
				ApplyState(s, tick, Source::Full);
			}
			if (opt_.reconcileOnFull) {
				std::vector<uint32_t> gone;
				for (auto& e : entities_)
					if (!present.count(e.first) && tick_[e.first] <= tick) gone.push_back(e.first);
				for (uint32_t id : gone) Remove(id);
			}
			// ids the host no longer has are confirmed absent by a full snapshot: forget their tombstones
			for (auto it = tombstones_.begin(); it != tombstones_.end();) it = (it->second < tick) ? tombstones_.erase(it) : std::next(it);
		}

		void Remove(uint32_t id)
		{
			if (entities_.erase(id)) { tick_.erase(id); sink_->OnDestroy(id); }
		}

		IWorldSink* sink_;
		GuestOptions opt_;
		std::map<uint32_t, EntityState> entities_;
		std::map<uint32_t, uint32_t> tick_;
		std::map<uint32_t, uint32_t> tombstones_;
		uint32_t lastFull_ = 0;
	};

	// A hash of a world state for telemetry (positions rounded to 1 cm, so tiny float noise does not matter).
	inline uint64_t HashWorld(const std::map<uint32_t, EntityState>& entities)
	{
		uint64_t h = 1469598103934665603ull;
		auto mix = [&h](int64_t v) { for (int i = 0; i < 8; ++i) { h ^= (uint64_t)((v >> (i * 8)) & 0xff); h *= 1099511628211ull; } };
		for (auto& e : entities) {
			const EntityState& s = e.second;
			mix(s.netId); mix((int)s.kind); mix(s.species);
			mix((int64_t)(s.pos.x * 100)); mix((int64_t)(s.pos.y * 100)); mix((int64_t)(s.pos.z * 100));
			mix((int64_t)(s.health * 10)); mix(s.flags); mix(s.owner);
		}
		return h;
	}
}
