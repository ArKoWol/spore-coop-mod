// manifest_tests.cpp : tests of the world-transfer manifest logic (core/coop_manifest.h) on temporary folders, no game.
//
// Oracles: a download that does not check out never reaches the cache (the cache folder keeps exactly its files); only a folder
// named *.incoming is ever deleted on purpose; a rejoin with an identical cache needs no file; a changed, missing or removed file
// is the only thing that is downloaded or dropped; the folder that replaces the cache holds exactly the manifest's files.
//
// Build and run: tests\run_core_tests.bat   (prints ALL OK)
#include "../core/coop_manifest.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace mf = coop::manifest;

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

	// every file of a folder as name -> content
	std::map<std::string, std::string> Files(const fs::path& dir)
	{
		std::map<std::string, std::string> all;
		if (fs::exists(dir)) {
			for (const auto& e : fs::directory_iterator(dir)) {
				if (e.is_regular_file()) {
					std::string name = e.path().filename().string(), content = Slurp(e.path());
					if (name == mf::kHostFileName) {
						content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());  // written in text mode
					}
					all[name] = content;
				}
			}
		}
		return all;
	}

	// a throw-away folder tree: host\ (the host's Game0), root\Coop\<id> (the guest's cache)
	struct Tree
	{
		fs::path root;
		static constexpr unsigned long long kId = 76561198000000001ull;
		Tree()
		{
			static int counter = 0;
			root = fs::temp_directory_path() / ("coop_manifest_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++counter));
			fs::remove_all(root);
			fs::create_directories(root);
		}
		~Tree() { std::error_code ec; fs::remove_all(root, ec); }
		fs::path Host() const { return root / "host"; }
		fs::path Cache() const { return root / "Coop" / std::to_string(kId); }
		fs::path Incoming() const { return root / "Coop" / (std::to_string(kId) + ".incoming"); }
		std::string S(const fs::path& p) const { return p.string(); }

		// a host world with files of different sizes (one bigger than a chunk, one empty)
		void HostWorld()
		{
			Put(Host() / "planetRecords.pkp", std::string(150000, 'p'));
			Put(Host() / "creature.spo", "creature of the host");
			Put(Host() / "PlanetScripts.pld", std::string(70000, 'q'));
			Put(Host() / "empty.bin", "");
			Put(Host() / "options.txt", "a=1\n");
		}
		// the guest's cache = what an earlier download of the host's world produced
		void CacheFromHost()
		{
			for (const auto& kv : Files(Host())) {
				Put(Cache() / kv.first, kv.second);
			}
			Put(Cache() / mf::kHostFileName, std::to_string(kId) + "\n");
		}
	};

	struct Host
	{
		mf::Manifest manifest;
		std::vector<std::vector<unsigned char>> data;
		explicit Host(const fs::path& dir) { mf::Snapshot(dir.string(), manifest, data); }
		const std::vector<unsigned char>& Bytes(const std::string& name) const
		{
			for (size_t i = 0; i < manifest.size(); ++i) {
				if (manifest[i].name == name) {
					return data[i];
				}
			}
			static const std::vector<unsigned char> none;
			return none;
		}
	};

	// what the wire does with a manifest: encode on the host, decode on the guest
	mf::Manifest OverTheWire(const mf::Manifest& m)
	{
		std::vector<unsigned char> blob = mf::Encode(m);
		mf::Manifest back;
		mf::Decode(blob.data(), blob.size(), back);
		return back;
	}

	// the host sends every file the guest asked for, in chunks of `chunk` bytes; `tamper` changes the bytes of that file on the way
	bool SendNeeded(mf::Download& d, const Host& host, uint32_t chunk, const std::string& tamper = std::string(), std::string* error = nullptr)
	{
		for (const std::string& name : d.Need()) {
			std::vector<unsigned char> bytes = host.Bytes(name);
			if (name == tamper && !bytes.empty()) {
				bytes[bytes.size() / 2] ^= 0x55;
			}
			uint32_t total = (uint32_t)bytes.size(), offset = 0;
			do {
				uint32_t len = std::min<uint32_t>(chunk, total - offset);
				if (!d.OnChunk(name, offset, total, bytes.data() + offset, len, error)) {
					return false;
				}
				offset += len;
			} while (offset < total);
		}
		return true;
	}

	void TestCrcAndWire()
	{
		Check(mf::Crc32("123456789", 9) == 0xCBF43926u, "crc32 of the check string");
		Check(mf::Crc32("", 0) == 0, "crc32 of nothing");
		uint32_t part = mf::Crc32("1234", 4);
		Check(mf::Crc32("56789", 5, part) == 0xCBF43926u, "crc32 can continue a running value");

		mf::Manifest m = { { "a.bin", 10, 0x11223344u }, { "empty", 0, 0 }, { "Name with space.pkp", 70000, 0xFFFFFFFFu } };
		std::vector<unsigned char> blob = mf::Encode(m);
		mf::Manifest back;
		Check(mf::Decode(blob.data(), blob.size(), back) && back.size() == 3, "manifest round trip");
		Check(back.size() == 3 && back[0].name == "a.bin" && back[0].size == 10 && back[0].crc == 0x11223344u &&
			back[2].name == "Name with space.pkp" && back[2].size == 70000 && back[2].crc == 0xFFFFFFFFu, "manifest round trip keeps the values");

		Check(!mf::Decode(blob.data(), blob.size() - 1, back), "a truncated manifest is refused");
		Check(back.empty(), "a refused manifest leaves nothing behind");
		std::vector<unsigned char> longer = blob;
		longer.push_back(0);
		Check(!mf::Decode(longer.data(), longer.size(), back), "a manifest with rest bytes is refused");
		mf::Manifest dup = { { "a", 1, 1 }, { "a", 2, 2 } };
		blob = mf::Encode(dup);
		Check(!mf::Decode(blob.data(), blob.size(), back), "a duplicate name is refused");
		for (const char* bad : { "..\\x", "a/b", "..", "x..y", "C:evil", "coop_host.txt", "" }) {
			mf::Manifest one = { { bad, 1, 1 } };
			blob = mf::Encode(one);
			Check(!mf::Decode(blob.data(), blob.size(), back), std::string("a bad name is refused: '") + bad + "'");
		}
		mf::Manifest big = { { "big", mf::kMaxFileSize + 1, 1 } };
		blob = mf::Encode(big);
		Check(!mf::Decode(blob.data(), blob.size(), back), "a file above the size limit is refused");
		mf::Manifest many;
		for (size_t i = 0; i <= mf::kMaxEntries; ++i) {
			many.push_back({ "f" + std::to_string(i), 1, 1 });
		}
		blob = mf::Encode(many);
		Check(!mf::Decode(blob.data(), blob.size(), back), "too many files are refused");

		std::vector<std::string> names = { "a.bin", "Name with space.pkp" }, namesBack;
		blob = mf::EncodeNames(names);
		Check(mf::DecodeNames(blob.data(), blob.size(), namesBack) && namesBack == names, "name list round trip");
		Check(!mf::DecodeNames(blob.data(), blob.size() - 2, namesBack), "a truncated name list is refused");
	}

	void TestSnapshot()
	{
		Tree t;
		t.HostWorld();
		Put(t.Host() / "coop_host.txt", "1\n");  // a marker file is not part of the world
		fs::create_directories(t.Host() / "subfolder");
		Put(t.Host() / "subfolder" / "inner.txt", "x");
		Put(t.Host() / (std::string(70, 'n') + ".txt"), "name too long");
		Host host(t.Host());
		Check(host.manifest.size() == 5, "snapshot lists the five world files");
		bool sorted = true, noMarker = true;
		for (size_t i = 1; i < host.manifest.size(); ++i) {
			sorted = sorted && host.manifest[i - 1].name < host.manifest[i].name;
		}
		for (const mf::Entry& e : host.manifest) {
			noMarker = noMarker && e.name != mf::kHostFileName && e.name != "inner.txt";
		}
		Check(sorted, "snapshot is sorted by name");
		Check(noMarker, "snapshot skips coop_host.txt and subfolders");
		bool sizesOk = true;
		for (size_t i = 0; i < host.manifest.size(); ++i) {
			sizesOk = sizesOk && host.manifest[i].size == host.data[i].size() &&
				host.manifest[i].crc == mf::Crc32(host.data[i].data(), host.data[i].size());
		}
		Check(sizesOk, "snapshot sizes and CRCs belong to the bytes it holds");
		mf::Manifest m;
		std::vector<std::vector<unsigned char>> d;
		Check(!mf::Snapshot((t.root / "nothing").string(), m, d), "a missing folder is reported");
	}

	void TestPlans()
	{
		Tree t;
		t.HostWorld();
		Host host(t.Host());

		mf::Plan plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need.size() == 5 && plan.rebuild && !plan.UpToDate(), "new folder: everything is needed");
		Check(plan.needBytes == 150000 + 20 + 70000 + 0 + 4, "new folder: byte count of the download");

		t.CacheFromHost();
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.UpToDate() && plan.need.empty(), "identical cache: nothing to download");
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, true);
		Check(plan.need.size() == 5, "'everything' asks for all files although the cache is identical");

		Put(t.Cache() / "creature.spo", "creature of the host, played on by the guest");
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need == std::vector<std::string>{ "creature.spo" } && !plan.UpToDate(), "one changed file: only that file is needed");

		Put(t.Cache() / "creature.spo", "creature of the host");
		Put(t.Cache() / "options.txt", "A=1\n");  // same size, other content: the size alone would not notice
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need == std::vector<std::string>{ "options.txt" }, "same size, other bytes: found by the CRC");

		Put(t.Cache() / "options.txt", "a=1\n");
		fs::remove(t.Cache() / "PlanetScripts.pld");
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need == std::vector<std::string>{ "PlanetScripts.pld" }, "a missing file is needed");

		t.CacheFromHost();
		Put(t.Cache() / "old_planet.pkp", "the host deleted this file");
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need.empty() && plan.rebuild && !plan.UpToDate(), "a file the host no longer has: nothing to download, but the folder is rebuilt");

		fs::remove(t.Cache() / "old_planet.pkp");
		Put(t.Cache() / mf::kHostFileName, "42\n");
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need.empty() && plan.rebuild, "a cache that names another host is rebuilt");
		fs::remove(t.Cache() / mf::kHostFileName);
		plan = mf::MakePlan(host.manifest, t.S(t.Cache()), Tree::kId, false);
		Check(plan.need.empty() && plan.rebuild, "a cache without coop_host.txt is rebuilt");
	}

	// the full download of a rejoin: Begin, send what is needed, Finish, Commit
	bool RunDownload(Tree& t, const Host& host, uint32_t chunk, const std::string& tamper, std::vector<std::string>* needed,
		std::string* error, bool everything = false)
	{
		mf::Download d;
		if (!d.Begin(OverTheWire(host.manifest), t.S(t.Cache()), t.S(t.Incoming()), Tree::kId, everything, error)) {
			return false;
		}
		if (needed) {
			*needed = d.Need();
		}
		if (d.UpToDate()) {
			return true;
		}
		if (!SendNeeded(d, host, chunk, tamper, error) || !d.Complete()) {
			return false;
		}
		return d.Finish(error) && mf::Commit(t.S(t.Incoming()), t.S(t.Cache()), error);
	}

	void TestDownloads()
	{
		std::string error;
		std::vector<std::string> needed;

		// a new folder (no cache): everything arrives, the cache is the host's world
		{
			Tree t;
			t.HostWorld();
			Host host(t.Host());
			Check(RunDownload(t, host, 20000, "", &needed, &error), "first join: download succeeds (" + error + ")");
			Check(needed.size() == 5, "first join: five files requested");
			auto got = Files(t.Cache());
			auto want = Files(t.Host());
			want[mf::kHostFileName] = std::to_string(Tree::kId) + "\n";
			Check(got == want, "first join: the cache holds the host's files and coop_host.txt");
			Check(!fs::exists(t.Incoming()), "first join: no .incoming left");
			Check(!fs::exists(t.Cache().string() + ".replaced"), "first join: no .replaced left");
		}

		// an identical cache: nothing to download and nothing is touched
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			Host host(t.Host());
			auto before = Files(t.Cache());
			Check(RunDownload(t, host, 20000, "", &needed, &error) && needed.empty(), "identical cache: up to date");
			Check(Files(t.Cache()) == before, "identical cache: the cache is unchanged");
			Check(!fs::exists(t.Incoming()), "identical cache: no .incoming is even made");
		}

		// one changed file: only that file is downloaded; the others are copied from the cache
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			Put(t.Host() / "creature.spo", "creature of the host, new planet");
			Host host(t.Host());
			Check(RunDownload(t, host, 16, "", &needed, &error), "one changed file: download succeeds (" + error + ")");
			Check(needed == std::vector<std::string>{ "creature.spo" }, "one changed file: only creature.spo requested");
			auto want = Files(t.Host());
			want[mf::kHostFileName] = std::to_string(Tree::kId) + "\n";
			Check(Files(t.Cache()) == want, "one changed file: the cache now equals the host's world");
		}

		// a missing file: only that file
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			fs::remove(t.Cache() / "empty.bin");
			Host host(t.Host());
			Check(RunDownload(t, host, 20000, "", &needed, &error) && needed == std::vector<std::string>{ "empty.bin" }, "missing (empty) file: downloaded again");
			Check(fs::exists(t.Cache() / "empty.bin") && fs::file_size(t.Cache() / "empty.bin") == 0, "missing (empty) file: exists with size 0");
		}

		// a file the manifest no longer lists is not carried over (and nothing is downloaded for it)
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			Put(t.Cache() / "old_planet.pkp", "the host deleted this file");
			Host host(t.Host());
			mf::Download d;
			Check(d.Begin(OverTheWire(host.manifest), t.S(t.Cache()), t.S(t.Incoming()), Tree::kId, false, &error), "removed file: begin");
			Check(!d.UpToDate() && d.Need().empty() && d.Complete(), "removed file: nothing to download but a rebuild");
			Check(d.Finish(&error) && mf::Commit(t.S(t.Incoming()), t.S(t.Cache()), &error), "removed file: finish and commit (" + error + ")");
			Check(!fs::exists(t.Cache() / "old_planet.pkp"), "removed file: not carried over");
			Check(Files(t.Cache()).size() == 6, "removed file: the five world files and coop_host.txt");
		}

		// a corrupted received file fails the check; the cache stays exactly as it was
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			Put(t.Host() / "creature.spo", "creature of the host, new planet");
			Host host(t.Host());
			auto cacheBefore = Files(t.Cache());
			Check(!RunDownload(t, host, 20000, "creature.spo", &needed, &error), "corrupted file: the download is refused");
			Check(error.find("creature.spo") != std::string::npos, "corrupted file: the error names the file (" + error + ")");
			Check(Files(t.Cache()) == cacheBefore, "corrupted file: the cache is untouched");
			Check(mf::RemoveIncoming(t.S(t.Incoming())) && !fs::exists(t.Incoming()), "corrupted file: .incoming can be cleaned up");
			Check(Files(t.Cache()) == cacheBefore, "corrupted file: the cache is still untouched after the cleanup");
			// the retry: everything again, this time without damage
			Check(RunDownload(t, host, 20000, "", &needed, &error, true) && needed.size() == 5, "corrupted file: the second try downloads everything (" + error + ")");
			auto want = Files(t.Host());
			want[mf::kHostFileName] = std::to_string(Tree::kId) + "\n";
			Check(Files(t.Cache()) == want, "corrupted file: after the second try the cache is the host's world");
		}

		// a file damaged on disk after it arrived (disk, virus scanner) fails the check as well
		{
			Tree t;
			t.HostWorld();
			Host host(t.Host());
			mf::Download d;
			d.Begin(OverTheWire(host.manifest), t.S(t.Cache()), t.S(t.Incoming()), Tree::kId, false, &error);
			SendNeeded(d, host, 20000, "", &error);
			std::string damaged = Slurp(t.Incoming() / "planetRecords.pkp");
			damaged.resize(damaged.size() - 1);  // a truncated file
			Put(t.Incoming() / "planetRecords.pkp", damaged);
			Check(d.Complete() && !d.Finish(&error), "truncated on disk: the check fails");
			Check(!fs::exists(t.Cache()), "truncated on disk: no cache was made");
		}

		// a cache file that changed between the plan and the build is found by the check, not copied blindly
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			Put(t.Host() / "creature.spo", "creature of the host, new planet");
			Host host(t.Host());
			mf::Download d;
			d.Begin(OverTheWire(host.manifest), t.S(t.Cache()), t.S(t.Incoming()), Tree::kId, false, &error);
			SendNeeded(d, host, 20000, "", &error);
			Put(t.Cache() / "options.txt", "A=1\n");
			Check(!d.Finish(&error) && error.find("options.txt") != std::string::npos, "cache changed under the download: the check fails (" + error + ")");
		}

		// the chunks are checked: wrong order, unknown file, wrong total, too much data
		{
			Tree t;
			t.HostWorld();
			Host host(t.Host());
			unsigned char bytes[8] = {};
			mf::Download d;
			d.Begin(OverTheWire(host.manifest), t.S(t.Cache()), t.S(t.Incoming()), Tree::kId, false, &error);
			Check(!d.OnChunk("creature.spo", 4, 20, bytes, 4, &error), "a chunk that skips ahead is refused");
			Check(!d.OnChunk("unknown.bin", 0, 8, bytes, 8, &error), "a file that is not needed is refused");
			Check(!d.OnChunk("creature.spo", 0, 19, bytes, 8, &error), "a wrong total size is refused");
			Check(d.OnChunk("creature.spo", 0, 20, bytes, 8, &error), "the right first chunk is taken");
			Check(!d.OnChunk("creature.spo", 0, 20, bytes, 8, &error), "the same chunk twice is refused");
			Check(!d.Complete(), "a download that stops half way is not complete");
			Check(d.Progress() > 0.0f && d.Progress() < 1.0f, "progress is between 0 and 1 during a download");
		}

		// Commit that cannot move the cache: the old cache stays, the new folder stays for the retry
		{
			Tree t;
			t.HostWorld();
			t.CacheFromHost();
			Put(t.Host() / "creature.spo", "creature of the host, new planet");
			Host host(t.Host());
			mf::Download d;
			d.Begin(OverTheWire(host.manifest), t.S(t.Cache()), t.S(t.Incoming()), Tree::kId, false, &error);
			SendNeeded(d, host, 20000, "", &error);
			Check(d.Finish(&error), "commit failure: the folder checks out");
			auto cacheBefore = Files(t.Cache());
			FILE* hold = fopen((t.Cache() / "options.txt").string().c_str(), "rb");  // the game holding a file open (FACTS section 2)
			std::string why;
			Check(!mf::Commit(t.S(t.Incoming()), t.S(t.Cache()), &why) && !why.empty(), "commit failure: reported");
			if (hold) {
				fclose(hold);
			}
			Check(Files(t.Cache()) == cacheBefore, "commit failure: the old cache is where it was, unchanged");
			Check(fs::exists(t.Incoming()), "commit failure: the new folder is still there");
			Check(mf::Commit(t.S(t.Incoming()), t.S(t.Cache()), &why), "commit works once the file is closed");
		}
	}

	void TestCleanup()
	{
		Tree t;
		t.HostWorld();
		t.CacheFromHost();
		auto cache = Files(t.Cache());

		Check(!mf::RemoveIncoming(t.S(t.Cache())), "RemoveIncoming refuses the cache folder");
		Check(!mf::RemoveIncoming(t.S(t.root / "Coop")), "RemoveIncoming refuses the Coop folder");
		Check(!mf::RemoveIncoming(t.S(t.Host())), "RemoveIncoming refuses any other folder");
		Check(!mf::RemoveIncoming(t.S(t.root / "Game0")), "RemoveIncoming refuses Game0");
		Check(!mf::RemoveIncoming(t.Incoming().string() + "\\"), "RemoveIncoming refuses a path with a trailing separator");
		Check(Files(t.Cache()) == cache && fs::exists(t.Host()), "the refused calls deleted nothing");

		Put(t.Incoming() / "a.bin", "partial");
		Put(t.Incoming() / "nested" / "b.bin", "deeper");
		Check(mf::RemoveIncoming(t.S(t.Incoming())) && !fs::exists(t.Incoming()), "RemoveIncoming deletes a download with its contents");
		Check(Files(t.Cache()) == cache, "the cache next to it is untouched");
		Check(mf::RemoveIncoming(t.S(t.Incoming())), "RemoveIncoming of a missing folder is fine");
		Check(fs::exists(t.Cache()) && fs::exists(t.Host()), "cache and host world are still there");
	}
}

int main()
{
	TestCrcAndWire();
	TestSnapshot();
	TestPlans();
	TestDownloads();
	TestCleanup();
	if (failures) {
		std::printf("%d of %d manifest checks FAILED\n", failures, checks);
		return 1;
	}
	std::printf("manifest tests: %d checks passed\n", checks);
	return 0;
}
