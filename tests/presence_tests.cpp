// presence_tests.cpp : tests of the Rich Presence string and the list rules (core/coop_presence.h), no game.
//
// Oracles: Format then Parse gives back the same Info; a foreign or damaged string is refused, never half read; the key
// never contains a second '|' from a planet name; Join is refused for the reasons of the plan and only for them.
//
// Build and run: tests\run_core_tests.bat   (prints ALL OK)
#include "../core/coop_presence.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace coop::presence;

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

	Info Make(unsigned protocol, State state, const char* planet, int players, int slots)
	{
		Info i;
		i.protocol = protocol;
		i.state = state;
		i.planet = planet;
		i.players = players;
		i.slots = slots;
		return i;
	}

	void TestFormatAndParse()
	{
		Info host = Make(16, State::Hosting, "Leian", 1, 2);
		Check(Format(host) == "16|hosting|Leian|1/2", "format of a host: " + Format(host));
		Info back;
		Check(Parse(Format(host), back), "a host's string parses");
		Check(back.protocol == 16 && back.state == State::Hosting && back.planet == "Leian" && back.players == 1 && back.slots == 2,
			"a host's string parses back to the same values");

		for (State s : { State::Menu, State::Hosting, State::Guest, State::Editing, State::Busy, State::Playing }) {
			Info i = Make(16, s, s == State::Menu ? "" : "Planet Nine", s == State::Hosting ? 2 : 0, s == State::Hosting ? 2 : 0);
			Info j;
			Check(Parse(Format(i), j) && j.state == s && j.planet == i.planet && j.players == i.players, std::string("round trip of ") + StateWord(s));
		}

		Info menu = Make(16, State::Menu, "", 0, 0);
		Check(Format(menu) == "16|menu||0/0", "an empty planet keeps its field: " + Format(menu));
		Check(Parse("16|menu||0/0", back) && back.planet.empty(), "an empty planet parses");

		// a later version's state word is shown as busy and never joined
		Check(Parse("17|spaceship|X|1/2", back) && back.state == State::Busy, "an unknown state word counts as busy");

		for (const char* bad : { "", "x", "16|hosting|Leian", "16|hosting|Leian|1/2|extra", "a|hosting|Leian|1/2", "16|hosting|Leian|12",
				"16|hosting|Leian|1/", "16|hosting|Leian|/2", "16|hosting|Leian|-1/2", "|hosting|Leian|1/2" }) {
			Info untouched = Make(7, State::Menu, "keep", 5, 5);
			Check(!Parse(bad, untouched) && untouched.protocol == 7 && untouched.planet == "keep", std::string("refused: '") + bad + "'");
		}
	}

	void TestPlanetCleaning()
	{
		Info i = Make(16, State::Hosting, "Le|ian\nX", 1, 2);
		std::string text = Format(i);
		Check(std::count(text.begin(), text.end(), '|') == 3, "a planet name cannot add a field: " + text);
		Info back;
		Check(Parse(text, back) && back.planet == "Le ian X", "'|' and control characters become spaces");

		std::string longName(200, 'a');
		Check(CleanPlanet(longName).size() == kMaxPlanet, "a long planet name is cut");

		// 2-byte letters: the cut must not split one
		std::string cyr;
		for (int k = 0; k < 40; ++k) {
			cyr += "\xD0\x9B";  // a Cyrillic letter, two bytes
		}
		std::string cut = CleanPlanet(cyr);
		bool whole = cut.size() % 2 == 0 && cut.size() <= kMaxPlanet && !cut.empty();
		for (size_t k = 0; whole && k < cut.size(); k += 2) {
			whole = (unsigned char)cut[k] == 0xD0 && (unsigned char)cut[k + 1] == 0x9B;
		}
		Check(whole, "a UTF-8 planet name is cut between letters");

		Check(CleanPlanet("  Leian  ") == "  Leian", "only trailing blanks are trimmed, as written");
		Check(Format(Make(16, State::Hosting, "", 1, 2)) == "16|hosting||1/2", "a host without a known planet");
	}

	void TestJoinRules()
	{
		const unsigned me = 16;
		Check(JoinBlock(Make(16, State::Hosting, "Leian", 1, 2), me) == Block::None, "a host with a free place can be joined");
		Check(JoinBlock(Make(15, State::Hosting, "Leian", 1, 2), me) == Block::Protocol, "another protocol is refused (E3)");
		Check(JoinBlock(Make(17, State::Hosting, "Leian", 1, 2), me) == Block::Protocol, "a newer protocol is refused too");
		Check(JoinBlock(Make(16, State::Hosting, "Leian", 2, 2), me) == Block::Full, "a full world is refused (E7)");
		Check(JoinBlock(Make(16, State::Editing, "Leian", 1, 2), me) == Block::NotInCreatureStage, "a host in the editor is refused (E5)");
		Check(JoinBlock(Make(16, State::Menu, "Leian", 1, 2), me) == Block::NotInCreatureStage, "a host in the menu is refused (E5)");
		Check(JoinBlock(Make(16, State::Busy, "Leian", 1, 2), me) == Block::NotInCreatureStage, "a host that is loading is refused (E5)");
		Check(JoinBlock(Make(15, State::Editing, "Leian", 1, 2), me) == Block::Protocol, "the version is named before the stage");
		Check(JoinBlock(Make(16, State::Hosting, "Leian", 3, 2), me) == Block::Full, "more players than slots is full");

		Check(BlockReason(Block::Protocol, Make(15, State::Hosting, "", 1, 2), "Artyom") == "Artyom runs another mod version: both run update.bat",
			"the text of the protocol refusal");
		Check(BlockReason(Block::Full, Make(16, State::Hosting, "", 2, 2), "Artyom").find("2/2") != std::string::npos, "the full world says 2/2");
		Check(BlockReason(Block::NotInCreatureStage, Make(16, State::Editing, "", 1, 2), "Artyom").find("editor") != std::string::npos,
			"the stage refusal names the editor");
		Check(BlockReason(Block::None, Make(16, State::Hosting, "", 1, 2), "Artyom").empty(), "no refusal, no text");
	}

	void TestInviteAndText()
	{
		const unsigned me = 16;
		Check(CanInvite(Make(16, State::Menu, "", 0, 0), me), "a friend in the menu can be invited");
		Check(!CanInvite(Make(15, State::Menu, "", 0, 0), me), "another version cannot be invited");
		Check(!CanInvite(Make(16, State::Hosting, "Leian", 1, 2), me), "a host is not invited");
		Check(!CanInvite(Make(16, State::Guest, "Leian", 0, 0), me), "a guest is not invited");
		Check(!CanInvite(Make(16, State::Editing, "", 0, 0), me), "a friend in the editor is not invited");
		Check(!CanInvite(Make(16, State::Playing, "", 0, 0), me), "a friend in his own game is not invited");
		Check(!CanInvite(Make(16, State::Menu, "Leian", 1, 2), me), "a friend whose world still runs is not invited");

		Check(StateText(Make(16, State::Hosting, "Leian", 1, 2)) == "hosting Leian 1/2", "state text of a host");
		Check(StateText(Make(16, State::Hosting, "", 1, 2)) == "hosting 1/2", "state text of a host without a planet");
		Check(StateText(Make(16, State::Menu, "", 0, 0)) == "in the main menu", "state text of the menu");
		Check(StateText(Make(16, State::Guest, "Leian", 0, 0)) == "playing in Leian", "state text of a guest");
		Check(StateText(Make(16, State::Editing, "Leian", 1, 2)).find("Leian 1/2") != std::string::npos, "an editor host keeps its world in the text");
		Check(HasSession(Make(16, State::Editing, "Leian", 1, 2)) && !HasSession(Make(16, State::Guest, "Leian", 2, 2)) &&
			!HasSession(Make(16, State::Menu, "", 0, 0)), "who owns a world");
	}

	void TestOrder()
	{
		struct Row { std::string name; Info info; unsigned long long id; };
		std::vector<Row> rows = {
			{ "zed", Make(16, State::Menu, "", 0, 0), 5 },
			{ "Bob", Make(16, State::Hosting, "B", 1, 2), 3 },
			{ "anna", Make(16, State::Playing, "", 0, 0), 4 },
			{ "Carl", Make(16, State::Editing, "C", 1, 2), 2 },
			{ "anna", Make(16, State::Menu, "", 0, 0), 1 },
			{ "Aaron", Make(16, State::Hosting, "A", 2, 2), 6 },
		};
		std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return Before(a.info, a.name, a.id, b.info, b.name, b.id); });
		std::string order;
		for (const Row& r : rows) {
			order += r.name + std::to_string(r.id) + " ";
		}
		Check(order == "Aaron6 Bob3 Carl2 anna1 anna4 zed5 ", "worlds first, then by name (case-insensitive), then id: " + order);
	}
}

int main()
{
	TestFormatAndParse();
	TestPlanetCleaning();
	TestJoinRules();
	TestInviteAndText();
	TestOrder();
	if (failures) {
		std::printf("%d of %d presence checks FAILED\n", failures, checks);
		return 1;
	}
	std::printf("presence tests: %d checks passed\n", checks);
	return 0;
}
