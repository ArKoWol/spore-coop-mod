// launch_tests.cpp : tests of the co-op launch folder logic (core/coop_launch.h) on a temporary Games directory, no game.
//
// Oracles: after every step the files of the tree are the same files as before (same names, same contents, only the folders
// they sit in changed; marker files of the logic are the only ones that may appear or go): NOTHING IS EVER DELETED.
// Failed moves are made with a file held open inside the folder (the game does this to Game0, FACTS section 2).
//
// Build and run: tests\run_core_tests.bat   (prints ALL OK)
#include "../core/coop_launch.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace coop::launch;

namespace
{
	int failures = 0;
	int checks = 0;

	void Check(bool ok, const std::string& what)
	{
		++checks;
		if (!ok) {
			++failures;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	std::string Slurp(const fs::path& p)
	{
		std::ifstream in(p, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	void Put(const fs::path& p, const std::string& content)
	{
		fs::create_directories(p.parent_path());
		std::ofstream out(p, std::ios::binary);
		out << content;
	}

	bool IsMarker(const std::string& name)
	{
		return name == "CoopLaunch.txt" || name == "CoopNoOwn.txt" || name == "CoopMigrate.txt" || name == "CoopMigrateAsked.txt";
	}

	// every file of the tree as "name=content", sorted, markers of the logic left out
	std::vector<std::string> Inventory(const fs::path& root)
	{
		std::vector<std::string> all;
		for (const auto& e : fs::recursive_directory_iterator(root)) {
			if (e.is_regular_file() && !IsMarker(e.path().filename().string())) {
				all.push_back(e.path().filename().string() + "=" + Slurp(e.path()));
			}
		}
		std::sort(all.begin(), all.end());
		return all;
	}

	// a throw-away Games directory
	struct Games
	{
		fs::path root;
		Games()
		{
			static int counter = 0;
			root = fs::temp_directory_path() / ("coop_launch_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++counter));
			fs::remove_all(root);
			fs::create_directories(root);
		}
		~Games() { std::error_code ec; fs::remove_all(root, ec); }
		std::string Dir() const { return root.string(); }
		fs::path operator/(const char* name) const { return root / name; }

		void OwnGame0() { Put(root / "Game0" / "own.spo", "my creature"); Put(root / "Game0" / "stars.db", "my galaxy"); }
		void HostWorld(unsigned long long id, const char* tag = "host")
		{
			Put(root / "Coop" / std::to_string(id) / "coop_host.txt", std::to_string(id) + "\n");
			Put(root / "Coop" / std::to_string(id) / "host.spo", std::string(tag) + " creature");
		}
		void LaunchRequest(unsigned long long id) { Put(root / "CoopLaunch.txt", std::to_string(id) + "\n"); }
		bool Has(const char* rel) const { return fs::exists(root / rel); }
	};

	// holds a file open without delete sharing: renaming the folder that contains it fails, as in the running game
	struct Holder
	{
		HANDLE h = INVALID_HANDLE_VALUE;
		explicit Holder(const fs::path& file)
		{
			h = CreateFileW(file.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		}
		bool Ok() const { return h != INVALID_HANDLE_VALUE; }
		~Holder() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
	};

	void TestPlainStart()
	{
		Games g; g.OwnGame0();
		auto before = Inventory(g.root);
		Result r = Prepare(g.Dir());
		Check(!r.coopLaunch && !r.restored && !r.failed && r.message.empty(), "plain start: nothing happens");
		Check(Inventory(g.root) == before && g.Has("Game0\\own.spo"), "plain start: Game0 untouched");
		Games none;  // no Games content at all
		r = Prepare(none.Dir());
		Check(!r.coopLaunch && !r.failed && !none.Has("Game0"), "empty Games folder: nothing happens, no Game0 invented");
		r = Prepare((g.root / "does_not_exist").string());
		Check(!r.coopLaunch && !r.failed, "missing Games folder: nothing happens");
	}

	void TestLaunchAndRestore()
	{
		Games g; g.OwnGame0(); g.HostWorld(111); g.LaunchRequest(111);
		auto before = Inventory(g.root);
		Result r = Prepare(g.Dir());
		Check(r.coopLaunch && r.host == 111 && !r.failed, "launch: this run is a co-op launch of host 111");
		Check(g.Has("Game0\\host.spo") && !g.Has("Game0\\own.spo"), "launch: Game0 is the host's world");
		Check(g.Has("Game0.own\\own.spo") && !g.Has("Coop\\111"), "launch: own Game0 waits in Game0.own, the cache folder moved");
		Check(!g.Has("CoopLaunch.txt"), "launch: CoopLaunch.txt is read once and deleted");
		Check(Inventory(g.root) == before, "launch: no file lost");

		// the guest's game saves inside the co-op launch: it lands in the host's world copy only
		Put(g / "Game0" / "saved_in_coop.spo", "guest save in host world");
		auto during = Inventory(g.root);

		r = Prepare(g.Dir());  // the next start without CoopLaunch.txt (Leave, or the game was just closed)
		Check(r.restored && !r.coopLaunch && !r.failed, "restore: the next normal start restores");
		Check(g.Has("Game0\\own.spo") && g.Has("Game0\\stars.db") && !g.Has("Game0.own"), "restore: own Game0 is back, Game0.own gone");
		Check(g.Has("Coop\\111\\host.spo") && g.Has("Coop\\111\\coop_host.txt") && g.Has("Coop\\111\\saved_in_coop.spo"),
			"restore: the host's world (with the guest's save inside) is back in Coop\\111");
		Check(Inventory(g.root) == during, "restore: no file lost");

		r = Prepare(g.Dir());  // and once more: nothing to do
		Check(!r.restored && !r.coopLaunch && g.Has("Game0\\own.spo"), "restore twice: second start changes nothing");
	}

	void TestCrashRestoreAndRelaunch()
	{
		// the game was killed in the co-op launch (Game0.own present), then the player joins again (CoopLaunch.txt)
		Games g; g.OwnGame0(); g.HostWorld(111); g.LaunchRequest(111);
		Prepare(g.Dir());
		Put(g / "Game0" / "half_written.spo", "x");
		g.LaunchRequest(111);
		auto before = Inventory(g.root);
		Result r = Prepare(g.Dir());
		Check(r.restored && r.coopLaunch && r.host == 111 && !r.failed, "crash + join again: restore first, then the new co-op launch");
		Check(g.Has("Game0\\host.spo") && g.Has("Game0\\half_written.spo") && g.Has("Game0.own\\own.spo"),
			"crash + join again: Game0 is the host's world, the own save waits");
		Check(Inventory(g.root) == before, "crash + join again: no file lost");

		// killed, then a plain start: the own save is back
		r = Prepare(g.Dir());
		Check(r.restored && g.Has("Game0\\own.spo") && !g.Has("Game0.own"), "crash: the next start brings the own Game0 back");
	}

	void TestNoOwnGame0()
	{
		Games g; g.HostWorld(111); g.LaunchRequest(111);
		auto before = Inventory(g.root);
		Result r = Prepare(g.Dir());
		Check(r.coopLaunch && !r.failed, "no own Game0: the co-op launch works");
		Check(g.Has("Game0\\host.spo") && !g.Has("Game0.own") && g.Has("CoopNoOwn.txt"), "no own Game0: nothing to move aside, marker written");
		Check(Inventory(g.root) == before, "no own Game0: no file lost");
		r = Prepare(g.Dir());
		Check(r.restored && !r.coopLaunch, "no own Game0: the next start restores");
		Check(!g.Has("Game0") && g.Has("Coop\\111\\host.spo") && !g.Has("CoopNoOwn.txt"),
			"no own Game0: the host's world is back in Coop, no Game0 is invented, the marker is gone");

		// the marker with a Game0 that is not a host's world (e.g. the game made a new one) is never touched
		Games h; h.OwnGame0(); Put(h / "CoopNoOwn.txt", "1\n");
		auto beforeH = Inventory(h.root);
		r = Prepare(h.Dir());
		Check(!r.failed && Inventory(h.root) == beforeH && h.Has("Game0\\own.spo") && !h.Has("CoopNoOwn.txt"),
			"no own Game0: a marker with a foreign Game0 moves nothing");
	}

	void TestFailedMoves()
	{
		// (a) Game0 is held open (the zombie process): the co-op launch does not start, nothing changes
		{
			Games g; g.OwnGame0(); g.HostWorld(111); g.LaunchRequest(111);
			auto before = Inventory(g.root);
			Holder hold(g / "Game0" / "own.spo");
			Check(hold.Ok(), "failed move (a): setup holds a file open");
			Result r = Prepare(g.Dir());
			Check(r.failed && !r.coopLaunch && !r.message.empty(), "failed move (a): no co-op launch, a message for the player");
			Check(g.Has("Game0\\own.spo") && g.Has("Coop\\111\\host.spo") && !g.Has("Game0.own"), "failed move (a): both worlds stay where they were");
			Check(Inventory(g.root) == before, "failed move (a): no file lost");
			Check(!g.Has("CoopLaunch.txt"), "failed move (a): CoopLaunch.txt is consumed (no repeat at every start)");
		}
		// (b) the host's folder is held open: Game0 was moved aside already, the move is undone
		{
			Games g; g.OwnGame0(); g.HostWorld(111); g.LaunchRequest(111);
			auto before = Inventory(g.root);
			Holder hold(g / "Coop" / "111" / "host.spo");
			Result r = Prepare(g.Dir());
			Check(r.failed && !r.coopLaunch && !r.message.empty(), "failed move (b): no co-op launch, a message");
			Check(g.Has("Game0\\own.spo") && !g.Has("Game0.own") && g.Has("Coop\\111\\host.spo"), "failed move (b): undone, Game0 is the own one again");
			Check(Inventory(g.root) == before, "failed move (b): no file lost");
		}
		// (c) the restore fails: Game0.own stays, the co-op launch is not started, the next start retries
		{
			Games g; g.OwnGame0(); g.HostWorld(111); g.LaunchRequest(111);
			Prepare(g.Dir());
			g.LaunchRequest(111);
			auto before = Inventory(g.root);
			{
				Holder hold(g / "Game0" / "host.spo");
				Result r = Prepare(g.Dir());
				Check(r.failed && !r.coopLaunch && !r.restored && !r.message.empty(), "failed move (c): restore fails and says so");
				Check(g.Has("Game0.own\\own.spo") && g.Has("Game0\\host.spo"), "failed move (c): the own save is still safe in Game0.own");
				Check(Inventory(g.root) == before, "failed move (c): no file lost");
			}
			Result r = Prepare(g.Dir());
			Check(r.restored && g.Has("Game0\\own.spo") && !g.Has("Game0.own"), "failed move (c): the next start restores");
		}
		// (d) the own save is held open while it must come back: Game0 is put back, Game0.own stays
		{
			Games g; g.OwnGame0(); g.HostWorld(111); g.LaunchRequest(111);
			Prepare(g.Dir());
			auto before = Inventory(g.root);
			Holder hold(g / "Game0.own" / "own.spo");
			Result r = Prepare(g.Dir());
			Check(r.failed && !r.restored, "failed move (d): restore fails");
			Check(g.Has("Game0\\host.spo") && g.Has("Game0.own\\own.spo") && !g.Has("Coop\\111"), "failed move (d): undone, as it was");
			Check(Inventory(g.root) == before, "failed move (d): no file lost");
		}
	}

	void TestUnreadableAndMissing()
	{
		// Game0.own present, Game0 without coop_host.txt: it goes to Coop\unknown-<time>, never deleted
		Games g; Put(g / "Game0.own" / "own.spo", "mine"); Put(g / "Game0" / "mystery.spo", "?");
		auto before = Inventory(g.root);
		Result r = Prepare(g.Dir());
		Check(r.restored && g.Has("Game0\\own.spo") && !g.Has("Game0.own"), "unreadable id: the own save comes back");
		bool unknown = false;
		for (const auto& e : fs::directory_iterator(g / "Coop")) {
			unknown = unknown || e.path().filename().string().rfind("unknown-", 0) == 0;
		}
		Check(unknown && Inventory(g.root) == before, "unreadable id: the other Game0 is kept in Coop\\unknown-<time>");

		// Game0.own present, Game0 gone: it just comes back
		Games h; Put(h / "Game0.own" / "own.spo", "mine");
		r = Prepare(h.Dir());
		Check(r.restored && h.Has("Game0\\own.spo"), "Game0 missing, Game0.own present: restored");

		// CoopLaunch.txt names a host whose folder is not there
		Games m; m.OwnGame0(); m.LaunchRequest(222);
		auto beforeM = Inventory(m.root);
		r = Prepare(m.Dir());
		Check(!r.coopLaunch && !r.failed && !r.message.empty() && Inventory(m.root) == beforeM && !m.Has("CoopLaunch.txt"),
			"launch without a world folder: normal start, a message, file consumed");

		// a garbage CoopLaunch.txt
		Games t; t.OwnGame0(); Put(t / "CoopLaunch.txt", "banana");
		r = Prepare(t.Dir());
		Check(!r.coopLaunch && !t.Has("CoopLaunch.txt") && t.Has("Game0\\own.spo"), "garbage CoopLaunch.txt: normal start");

		// the host's folder name is not reused: a second cache of the same host keeps the first
		Games d; d.OwnGame0(); d.HostWorld(111, "first"); d.LaunchRequest(111);
		Prepare(d.Dir());
		d.HostWorld(111, "second");  // a new download arrived while the launch ran (it should not, but must not destroy)
		auto beforeD = Inventory(d.root);
		r = Prepare(d.Dir());
		Check(r.restored && !r.failed && Inventory(d.root) == beforeD && d.Has("Game0\\own.spo"), "restore into an occupied Coop\\<id>: both kept");
	}

	void TestMigration()
	{
		Games g;
		g.HostWorld(111);
		fs::rename(g / "Coop" / "111", g / "Game0");  // an old install: Game0 is the host's world
		Put(g / "Game0.coopbackup" / "own.spo", "my first save");
		Put(g / "Game0.coopbackup2" / "coop_host.txt", "222\n");  // a later join: another host's world
		Put(g / "Game0.coopbackup2" / "other_host.spo", "world of 222");
		Put(g / "Game0.coopbackup3" / "own3.spo", "a newer own save");
		Check(MigrationOffer(g.Dir()) == "Game0.coopbackup", "migration: the question offers the oldest backup");

		Games n;  // no backup at all
		Put(n / "Game0" / "coop_host.txt", "111\n");
		Check(MigrationOffer(n.Dir()).empty(), "migration: no backup, no question");
		Games own; own.OwnGame0(); Put(own / "Game0.coopbackup" / "own.spo", "x");
		Check(MigrationOffer(own.Dir()).empty(), "migration: Game0 is the player's own world, no question");
		Games hostOnly; Put(hostOnly / "Game0" / "coop_host.txt", "111\n"); Put(hostOnly / "Game0.coopbackup" / "coop_host.txt", "222\n");
		Check(MigrationOffer(hostOnly.Dir()).empty(), "migration: backups that are host worlds are not offered");
		Games pending; Put(pending / "Game0" / "coop_host.txt", "111\n"); Put(pending / "Game0.coopbackup" / "own.spo", "x");
		Put(pending / "Game0.own" / "own2.spo", "y");
		Check(MigrationOffer(pending.Dir()).empty(), "migration: not offered while a co-op launch is active");

		auto before = Inventory(g.root);
		Check(RequestMigration(g.Dir()), "migration: Restore writes CoopMigrate.txt");
		Result r = Prepare(g.Dir());
		Check(r.migrated && !r.failed, "migration: done at the next start");
		Check(g.Has("Game0\\own.spo") && !g.Has("Game0\\coop_host.txt"), "migration: Game0 is the oldest own backup");
		Check(g.Has("Coop\\111\\host.spo") && g.Has("Coop\\111\\coop_host.txt"), "migration: the friend's world went to Coop\\111");
		Check(!g.Has("Game0.coopbackup") && g.Has("Game0.coopbackup2\\other_host.spo") && g.Has("Game0.coopbackup3\\own3.spo"),
			"migration: the other backups stay");
		bool named = false;
		for (const std::string& line : r.log) {
			named = named || line.find("Game0.coopbackup3") != std::string::npos;
		}
		Check(named, "migration: the other backups are named in the log");
		Check(!g.Has("CoopMigrate.txt") && Inventory(g.root) == before, "migration: marker consumed, no file lost");
		Check(MigrationOffer(g.Dir()).empty(), "migration: not asked again");

		// the first backup is a host's world: the oldest one that is not, is taken
		Games s;
		Put(s / "Game0" / "coop_host.txt", "111\n");
		Put(s / "Game0.coopbackup" / "coop_host.txt", "333\n");
		Put(s / "Game0.coopbackup2" / "own.spo", "mine");
		Check(MigrationOffer(s.Dir()) == "Game0.coopbackup2", "migration: skips a backup that holds a host's world");

		// asked once, either answer
		MarkAsked(s.Dir());
		Check(MigrationOffer(s.Dir()).empty(), "migration: after either answer the question is not asked again");

		// a failed migration moves nothing
		Games f;
		Put(f / "Game0" / "coop_host.txt", "111\n"); Put(f / "Game0" / "w.spo", "w");
		Put(f / "Game0.coopbackup" / "own.spo", "mine");
		RequestMigration(f.Dir());
		auto beforeF = Inventory(f.root);
		Holder hold(f / "Game0.coopbackup" / "own.spo");
		r = Prepare(f.Dir());
		Check(!r.migrated && !r.message.empty() && f.Has("Game0\\w.spo") && f.Has("Game0.coopbackup\\own.spo") && Inventory(f.root) == beforeF,
			"migration: a failed move is undone, nothing lost");
	}

	void TestReceiveLaunchFile()
	{
		Games g;
		Check(WriteLaunch(g.Dir(), 76561198000000001ull), "launch file: written");
		Check(ReadId((g / "CoopLaunch.txt").string()) == 76561198000000001ull, "launch file: the 64-bit id survives");
	}
}

int main()
{
	TestPlainStart();
	TestLaunchAndRestore();
	TestCrashRestoreAndRelaunch();
	TestNoOwnGame0();
	TestFailedMoves();
	TestUnreadableAndMissing();
	TestMigration();
	TestReceiveLaunchFile();
	if (failures) {
		std::printf("%d of %d launch checks FAILED\n", failures, checks);
		return 1;
	}
	std::printf("launch tests: %d checks passed\n", checks);
	return 0;
}
