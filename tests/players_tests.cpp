// players_tests.cpp : tests of the world sidecar Game0\coop_players.json (core/coop_players.h), no game.
//
// Oracles: Format then Parse gives back the same entries; unknown fields are skipped, not fatal; a text that is not a sidecar
// is refused; SetCreature changes only the entry of its id; Save then Load on a temporary folder gives back the entries.
//
// Build and run: tests\run_core_tests.bat   (prints ALL OK)
#include "../core/coop_players.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace coop::players;
namespace fs = std::filesystem;

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

	Entry Make(unsigned long long id, uint32_t a, uint32_t b, uint32_t c)
	{
		Entry e;
		e.id = id;
		e.creature[0] = a; e.creature[1] = b; e.creature[2] = c;
		return e;
	}

	bool Same(const Entry& x, const Entry& y)
	{
		return x.id == y.id && x.creature[0] == y.creature[0] && x.creature[1] == y.creature[1] && x.creature[2] == y.creature[2];
	}
}

int main()
{
	// round trip
	{
		std::vector<Entry> in = { Make(76561190000000001ull, 0x1a2b3c4d, 0x2b978c46, 0x40626200), Make(27790, 0xffffffff, 0, 1) };
		std::vector<Entry> out;
		Check(Parse(Format(in), out), "round trip parses");
		Check(out.size() == 2 && Same(out[0], in[0]) && Same(out[1], in[1]), "round trip gives the same entries");
		Check(Parse(Format({}), out) && out.empty(), "an empty list round-trips");
	}
	// unknown fields and other layouts
	{
		const std::string t = "{\"players\":[{\"dna\": 120, \"id\":\"5000\", \"later\": {\"a\": [1, 2, \"]\"]},"
			"\"creature\":\"00000010/2b978c46/00000000\", \"name\": \"x\"}], \"version\": 2}";
		std::vector<Entry> out;
		Check(Parse(t, out), "unknown fields are skipped");
		Check(out.size() == 1 && out[0].id == 5000 && out[0].creature[0] == 0x10 && out[0].creature[1] == 0x2b978c46, "fields read around unknown ones");
	}
	// damaged or foreign texts
	{
		std::vector<Entry> out = { Make(1, 1, 1, 1) };
		Check(!Parse("", out) && out.empty(), "an empty text is no sidecar");
		Check(!Parse("{\"players\": {}}", out), "players must be a list");
		Check(!Parse("{\"players\": [{\"id\": \"12x\"}]}", out), "an id that is not a number is refused");
		Check(!Parse("{\"players\": [{\"id\": \"12\"", out), "a cut text is refused");
		Check(Parse("{\"players\": [{\"id\": \"12\", \"creature\": \"zz\"}]}", out) && out.size() == 1 && out[0].creature[0] == 0,
			"a damaged creature key keeps the entry, without a creature");
		Check(Parse("{\"players\": [{\"creature\": \"00000001/00000002/00000003\"}]}", out) && out.empty(), "an entry without id is left out");
	}
	// SetCreature
	{
		std::vector<Entry> list = { Make(1, 1, 2, 3), Make(2, 4, 5, 6) };
		const uint32_t same[3] = { 1, 2, 3 }, other[3] = { 7, 8, 9 };
		Check(!SetCreature(list, 1, same), "the same key changes nothing");
		Check(SetCreature(list, 2, other) && Same(list[1], Make(2, 7, 8, 9)) && Same(list[0], Make(1, 1, 2, 3)), "only the entry of the id changes");
		Check(SetCreature(list, 3, other) && list.size() == 3 && Same(list[2], Make(3, 7, 8, 9)), "a new id is added");
		Check(Find(list, 4) == nullptr && Find(list, 3) != nullptr, "Find");
	}
	// files
	{
		const fs::path dir = fs::temp_directory_path() / ("coop_players_test_" + std::to_string(GetCurrentProcessId()));
		fs::remove_all(dir);
		fs::create_directories(dir);
		std::vector<Entry> out = { Make(9, 9, 9, 9) };
		Check(Load(dir.string(), out) && out.empty(), "a missing file is an empty list");
		std::vector<Entry> in = { Make(42, 0xabc, 0x2b978c46, 0) };
		Check(Save(dir.string(), in), "save");
		Check(Load(dir.string(), out) && out.size() == 1 && Same(out[0], in[0]), "load gives back what was saved");
		in.push_back(Make(43, 1, 1, 1));
		Check(Save(dir.string(), in) && Load(dir.string(), out) && out.size() == 2, "save replaces the old file");
		Check(!fs::exists(dir / (std::string(kFileName) + ".tmp")), "no temporary file is left");
		{
			FILE* f = std::fopen((dir / kFileName).string().c_str(), "wb");
			std::fputs("not json", f);
			std::fclose(f);
		}
		Check(!Load(dir.string(), out), "a damaged file is reported");
		fs::remove_all(dir);
	}
	std::printf("%s: %d checks, %d failed\n", failures ? "FAILED" : "ALL OK", checks, failures);
	return failures ? 1 : 0;
}
