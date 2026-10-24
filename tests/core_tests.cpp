// core_tests.cpp : tests of the replication core (core/coop_core.h) without the game.
//
// A fake host world changes every tick; the packets travel through a network that loses, duplicates, delays and
// reorders the unreliable ones (reliable events are ordered and never lost, except in the blackout test). Oracles:
//   I1 the guest never has an id the host never created
//   I2 once a Destroy event was delivered, that id never comes back
//   I3 the tick applied to an entity never goes backwards
//   final: after the traffic quiets down the guest equals the host
// Mutation twins: each protection of the core is switched off once, and the matching test must fail.
//
// Build and run: tests\run_core_tests.bat   (prints ALL OK)
#include "../core/coop_core.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>

using namespace coop;

namespace
{
	struct Sink : IWorldSink
	{
		int creates = 0, updates = 0, destroys = 0;
		void OnCreate(const EntityState&) override { ++creates; }
		void OnUpdate(const EntityState&) override { ++updates; }
		void OnDestroy(uint32_t) override { ++destroys; }
	};

	struct Packet { int at; int order; Message msg; bool reliable; };

	class Net
	{
	public:
		Net(unsigned seed, double loss, double dup, int maxDelay) : rng(seed), loss(loss), dup(dup), maxDelay(maxDelay) {}
		bool blackout = false;

		void Unreliable(const Message& m, int now)
		{
			if (m.empty() || blackout || Chance(loss)) return;
			int copies = 1 + (Chance(dup) ? 1 : 0);
			for (int i = 0; i < copies; ++i) inflight.push_back({ now + Delay(), order++, m, false });
		}
		void Reliable(const Message& m, int now)
		{
			if (blackout) return;  // a reconnect gap loses even reliable messages
			lastReliable = std::max(lastReliable, now + Delay());
			inflight.push_back({ lastReliable, order++, m, true });
		}
		// Delivers what is due, in (time, send order).
		void Deliver(int now, const std::function<void(const Packet&)>& to)
		{
			std::vector<Packet> due, rest;
			for (auto& p : inflight) (p.at <= now ? due : rest).push_back(p);
			inflight.swap(rest);
			std::sort(due.begin(), due.end(), [](const Packet& a, const Packet& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
			for (auto& p : due) to(p);
		}
	private:
		bool Chance(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }
		int Delay() { return maxDelay ? (int)(rng() % (maxDelay + 1)) : 0; }
		std::mt19937 rng;
		double loss, dup;
		int maxDelay;
		std::vector<Packet> inflight;
		int order = 0, lastReliable = 0;
	};

	struct Scenario
	{
		unsigned seed = 1;
		int ticks = 600;
		double loss = 0.3, dup = 0.1;
		int maxDelay = 8;
		int blackoutFrom = -1, blackoutTo = -1;
		int fullEvery = 100;
	};

	EntityState RandomState(std::mt19937& rng)
	{
		auto f = [&](float range) { return (float)((int)(rng() % 20001) - 10000) / 10000.0f * range; };
		EntityState s;
		s.kind = (Kind)(1 + rng() % 7);
		s.species = rng() % 5;
		s.pos = { f(1000), f(1000), f(1000) };
		s.rot = { f(1), f(1), f(1), 1 };
		s.maxHealth = 10 + (float)(rng() % 90);
		s.health = s.maxHealth;
		return s;
	}

	// Runs one scenario and returns "" if every oracle held, otherwise the first violation.
	std::string Run(const Scenario& sc, GuestOptions opt)
	{
		std::mt19937 rng(sc.seed);
		HostWorld host;
		Sink sink;
		GuestWorld guest(&sink, opt);
		Net net(sc.seed * 7919u, sc.loss, sc.dup, sc.maxDelay);
		std::set<uint32_t> ever, destroyDelivered;
		std::map<uint32_t, uint32_t> lastApplied;

		for (int i = 0; i < 20; ++i) ever.insert(host.Spawn(RandomState(rng)));
		auto checkTick = [&](int t) -> std::string {
			for (auto& e : guest.entities()) {
				if (!ever.count(e.first)) return "I1 guest has unknown id " + std::to_string(e.first) + " at tick " + std::to_string(t);
				if (destroyDelivered.count(e.first)) return "I2 destroyed id " + std::to_string(e.first) + " came back at tick " + std::to_string(t);
				uint32_t a = guest.AppliedTick(e.first);
				if (a < lastApplied[e.first]) return "I3 applied tick of " + std::to_string(e.first) + " went back at tick " + std::to_string(t);
				lastApplied[e.first] = a;
			}
			return "";
		};
		auto deliver = [&](const Packet& p) {
			guest.Receive(p.msg);
			if (p.reliable && p.msg[0] == (uint8_t)MsgType::Destroy) {
				Reader r(p.msg.data(), p.msg.size()); r.U8(); r.U32();
				destroyDelivered.insert(r.U32());
			}
		};

		const int total = sc.ticks + 300;  // the last 300 ticks are quiet: no changes, no loss
		for (int t = 1; t <= total; ++t) {
			bool quiet = t > sc.ticks;
			net.blackout = !quiet && sc.blackoutFrom >= 0 && t >= sc.blackoutFrom && t < sc.blackoutTo;
			if (!quiet) {
				std::vector<uint32_t> ids;
				for (auto& e : host.entities()) ids.push_back(e.first);
				for (uint32_t id : ids) {
					if (rng() % 100 < 30) {
						EntityState s = host.entities().at(id);
						s.pos.x += 0.5f; s.pos.z -= 0.25f; s.health = std::max(0.0f, s.health - (rng() % 5 == 0 ? 1.0f : 0.0f));
						host.Update(id, s);
					}
				}
				if (rng() % 100 < 5 && host.entities().size() < 80) ever.insert(host.Spawn(RandomState(rng)));
				if (rng() % 100 < 4 && !ids.empty()) host.Destroy(ids[rng() % ids.size()]);
			}
			host.AdvanceTick();
			for (auto& m : host.TakeEvents()) net.Reliable(m, t);
			net.Unreliable(host.MakeDelta(), t);
			if (t % sc.fullEvery == 0) net.Unreliable(host.MakeFull(), t);
			if (quiet) { net.blackout = false; }
			net.Deliver(t, deliver);
			std::string v = checkTick(t);
			if (!v.empty()) return v;
		}
		// the loss stays on in the quiet phase, so a full snapshot may still be lost: give the last ones time
		net.Deliver(1 << 30, deliver);
		std::string v = checkTick(total);
		if (!v.empty()) return v;
		// final: after one more clean full snapshot the guest equals the host
		guest.Receive(host.MakeFull());
		if (guest.entities().size() != host.entities().size()) return "final: sizes differ " + std::to_string(guest.entities().size()) + " vs " + std::to_string(host.entities().size());
		for (auto& e : host.entities()) {
			auto it = guest.entities().find(e.first);
			if (it == guest.entities().end() || it->second != e.second) return "final: entity " + std::to_string(e.first) + " differs";
		}
		return "";
	}

	int failures = 0;
	void Check(bool ok, const std::string& name, const std::string& detail = "")
	{
		std::printf("%s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : "  -- ", detail.c_str());
		if (!ok) ++failures;
	}

	// Number of seeds (out of n) whose run reported a violation, and the first message.
	int Violations(Scenario sc, GuestOptions opt, int seeds, std::string* first)
	{
		int bad = 0;
		for (int s = 1; s <= seeds; ++s) {
			sc.seed = s;
			std::string v = Run(sc, opt);
			if (!v.empty()) { if (!bad && first) *first = "seed " + std::to_string(s) + ": " + v; ++bad; }
		}
		return bad;
	}
}

int main(int argc, char** argv)
{
	const int seeds = argc > 1 ? std::atoi(argv[1]) : 20;  // more seeds: tests\out\core_tests.exe 500
	std::printf("core tests\n");

	// wire format
	{
		std::mt19937 rng(5);
		bool same = true;
		for (int i = 0; i < 1000; ++i) {
			EntityState s = RandomState(rng); s.netId = rng(); s.anim = (uint16_t)rng(); s.flags = (uint8_t)rng(); s.owner = (uint8_t)rng();
			Writer w; w.State(s);
			Reader r(w.buf.data(), w.buf.size());
			if (r.State() != s || !r.ok || !r.Done()) same = false;
		}
		Check(same, "wire: 1000 random states round-trip");
		Sink sink; GuestWorld guest(&sink);
		HostWorld host; host.Spawn(RandomState(rng));
		Message full = host.MakeFull();
		bool refused = true;
		for (size_t cut = 0; cut + 1 < full.size(); ++cut) {
			Message part(full.begin(), full.begin() + cut);
			if (guest.Receive(part)) refused = false;
		}
		Check(refused && guest.entities().empty(), "wire: every truncated message is refused and changes nothing");
	}

	// a stale delta must not override a newer state
	{
		HostWorld host; Sink sink; GuestWorld guest(&sink);
		std::mt19937 rng(9);
		uint32_t id = host.Spawn(RandomState(rng));
		for (auto& m : host.TakeEvents()) guest.Receive(m);
		host.AdvanceTick(); EntityState a = host.entities().at(id); a.pos.x = 1; host.Update(id, a); Message older = host.MakeDelta();
		host.AdvanceTick(); a.pos.x = 2; host.Update(id, a); Message newer = host.MakeDelta();
		guest.Receive(newer); guest.Receive(older);
		Check(guest.entities().at(id).pos.x == 2, "stale: an older delta arriving late is ignored");
	}

	// late join: a Full snapshot alone builds the whole world
	{
		HostWorld host; std::mt19937 rng(3);
		for (int i = 0; i < 30; ++i) host.Spawn(RandomState(rng));
		host.TakeEvents();
		Sink sink; GuestWorld guest(&sink);
		guest.Receive(host.MakeFull());
		bool equal = guest.entities().size() == host.entities().size();
		for (auto& e : host.entities()) equal = equal && guest.entities().count(e.first) && guest.entities().at(e.first) == e.second;
		Check(equal && sink.creates == 30, "late join: one Full snapshot builds the world");
	}

	// hash
	{
		HostWorld host; std::mt19937 rng(4); uint32_t id = host.Spawn(RandomState(rng));
		uint64_t h1 = HashWorld(host.entities());
		EntityState s = host.entities().at(id); s.pos.x += 1; host.Update(id, s);
		Check(h1 == HashWorld(host.entities()) ? false : true, "hash: a changed entity changes the hash");
	}

	// convergence under loss, duplicates and reordering
	Scenario chaos;
	std::string first;
	int bad = Violations(chaos, GuestOptions(), seeds, &first);
	Check(bad == 0, "chaos: " + std::to_string(seeds) + " seeds, 30% loss, 10% duplicates, up to 8 ticks of reordering", first);

	// a reconnect gap: everything lost for 100 ticks, then a Full snapshot must repair the guest
	Scenario gap; gap.blackoutFrom = 150; gap.blackoutTo = 250;
	first.clear();
	bad = Violations(gap, GuestOptions(), seeds, &first);
	Check(bad == 0, "gap: " + std::to_string(seeds) + " seeds, 100 ticks of total loss, healed by the next Full snapshot", first);

	// mutation twins: switch each protection off once, the matching test must notice
	{
		GuestOptions o; o.rejectStale = false;
		Check(Violations(chaos, o, 20, nullptr) > 0, "mutation: without rejectStale the chaos test fails");
		o = GuestOptions(); o.tombstones = false;
		Check(Violations(chaos, o, 20, nullptr) > 0, "mutation: without tombstones the chaos test fails");
		o = GuestOptions(); o.reconcileOnFull = false;
		Check(Violations(gap, o, 20, nullptr) > 0, "mutation: without reconcileOnFull the gap test fails");
	}

	if (failures) { std::printf("%d FAILED\n", failures); return 1; }
	std::printf("ALL OK\n");
	return 0;
}
