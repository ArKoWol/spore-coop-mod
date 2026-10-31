// coop_presence.h : what a running mod tells its Steam friends (docs/features/JOIN_FLOW.md section 5), without the game.
//
// The Steam Rich Presence key `sporecoop` holds `<protocol>|<state>|<planet>|<players>/<slots>`, for example
// `16|hosting|Leian|1/2`. This file formats and reads that string and holds the rules the CO-OP window and the invite list
// apply to it (who may be joined or invited, what a row says, in which order the rows come). Tests: tests/presence_tests.cpp.
#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace coop
{
	namespace presence
	{
		// What a player is doing. Hosting is the only state that can be joined. Playing = in a game of his own, not hosting
		// (an extension of the five states of the plan: the window should say it, not call it "busy").
		enum class State { Unknown, Menu, Hosting, Guest, Editing, Busy, Playing };

		struct Info
		{
			unsigned protocol = 0;
			State state = State::Unknown;
			std::string planet;  // UTF-8, may be empty
			int players = 0;     // in the world, the host included
			int slots = 0;       // 0 = no world to join (a guest and a player in the menu publish 0/0)
		};

		inline const char* StateWord(State s)
		{
			switch (s) {
			case State::Menu: return "menu";
			case State::Hosting: return "hosting";
			case State::Guest: return "guest";
			case State::Editing: return "editor";
			case State::Playing: return "playing";
			default: return "busy";
			}
		}

		// an unknown word (a later version) counts as busy: shown, never joined
		inline State ParseState(const std::string& word)
		{
			for (State s : { State::Menu, State::Hosting, State::Guest, State::Editing, State::Busy, State::Playing }) {
				if (word == StateWord(s)) {
					return s;
				}
			}
			return State::Busy;
		}

		// the planet name as it goes into the key: no '|' or control characters, at most `kMaxPlanet` bytes (never cut inside a
		// UTF-8 sequence)
		const size_t kMaxPlanet = 48;

		inline std::string CleanPlanet(const std::string& planet)
		{
			std::string out;
			for (char c : planet) {
				unsigned char u = (unsigned char)c;
				out += (c == '|' || u < 0x20 || u == 0x7F) ? ' ' : c;
			}
			if (out.size() > kMaxPlanet) {
				size_t cut = kMaxPlanet;
				while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) {
					--cut;  // out[cut] continues a character: step back to its first byte
				}
				out.resize(cut);
			}
			while (!out.empty() && out.back() == ' ') {
				out.pop_back();
			}
			return out;
		}

		inline std::string Format(const Info& i)
		{
			return std::to_string(i.protocol) + "|" + StateWord(i.state) + "|" + CleanPlanet(i.planet) + "|" + std::to_string(i.players)
				+ "/" + std::to_string(i.slots);
		}

		inline bool ParseNumber(const std::string& s, int& out)
		{
			if (s.empty() || s.size() > 6) {
				return false;
			}
			int v = 0;
			for (char c : s) {
				if (!isdigit((unsigned char)c)) {
					return false;
				}
				v = v * 10 + (c - '0');
			}
			out = v;
			return true;
		}

		// false: not our key (empty, other layout)
		inline bool Parse(const std::string& text, Info& out)
		{
			std::vector<std::string> part(1);
			for (char c : text) {
				if (c == '|') {
					part.emplace_back();
				}
				else {
					part.back() += c;
				}
			}
			if (part.size() != 4) {
				return false;
			}
			Info i;
			int protocol = 0;
			if (!ParseNumber(part[0], protocol)) {
				return false;
			}
			i.protocol = (unsigned)protocol;
			i.state = ParseState(part[1]);
			i.planet = part[2];
			size_t slash = part[3].find('/');
			if (slash == std::string::npos || !ParseNumber(part[3].substr(0, slash), i.players) || !ParseNumber(part[3].substr(slash + 1), i.slots)) {
				return false;
			}
			out = i;
			return true;
		}

		// the player runs a world that other players may be in (the row goes to the "hosting" section; Join may still be refused)
		inline bool HasSession(const Info& i)
		{
			return i.state != State::Guest && i.slots > 0;
		}

		enum class Block { None, Protocol, Full, NotInCreatureStage };

		// why Join is refused for this player, in the order the window names it (E3, E7, E5)
		inline Block JoinBlock(const Info& i, unsigned myProtocol)
		{
			if (i.protocol != myProtocol) {
				return Block::Protocol;
			}
			if (i.state != State::Hosting) {
				return Block::NotInCreatureStage;
			}
			if (i.players >= i.slots) {
				return Block::Full;
			}
			return Block::None;
		}

		inline std::string StateName(State s)
		{
			switch (s) {
			case State::Menu: return "in the main menu";
			case State::Hosting: return "hosting";
			case State::Guest: return "playing in a friend's world";
			case State::Editing: return "in the editor";
			case State::Playing: return "playing, not hosting";
			default: return "busy (loading)";
			}
		}

		inline std::string Count(const Info& i)
		{
			return std::to_string(i.players) + "/" + std::to_string(i.slots);
		}

		// the second line of a row for a player that has no refusal: `hosting Leian 1/2`, `in the main menu`
		inline std::string StateText(const Info& i)
		{
			if (i.state == State::Hosting) {
				return std::string("hosting ") + (i.planet.empty() ? "" : i.planet + " ") + Count(i);
			}
			if (i.state == State::Guest && !i.planet.empty()) {
				return "playing in " + i.planet;
			}
			std::string text = StateName(i.state);
			if (HasSession(i)) {
				text += std::string(", world ") + (i.planet.empty() ? "" : i.planet + " ") + Count(i);
			}
			return text;
		}

		// the text that replaces the state on a row whose Join is disabled
		inline std::string BlockReason(Block b, const Info& i, const std::string& name)
		{
			switch (b) {
			case Block::Protocol: return name + " runs another mod version: both run update.bat";
			case Block::Full: return name + "'s world is full (" + Count(i) + ")";
			case Block::NotInCreatureStage: return name + " is not in the creature stage: " + StateName(i.state) + " (" + Count(i) + ")";
			default: return "";
			}
		}

		// the host's invite list: players on the same version, in the main menu, with no world of their own
		inline bool CanInvite(const Info& i, unsigned myProtocol)
		{
			return i.protocol == myProtocol && i.state == State::Menu && !HasSession(i);
		}

		inline std::string Lower(std::string s)
		{
			for (char& c : s) {
				c = (char)tolower((unsigned char)c);
			}
			return s;
		}

		// order of the friends in the window: worlds first, then the others; inside a group by name (case-insensitive), then id
		inline bool Before(const Info& a, const std::string& nameA, unsigned long long idA, const Info& b, const std::string& nameB,
			unsigned long long idB)
		{
			bool sa = HasSession(a), sb = HasSession(b);
			if (sa != sb) {
				return sa;
			}
			std::string la = Lower(nameA), lb = Lower(nameB);
			if (la != lb) {
				return la < lb;
			}
			return idA < idB;
		}
	}
}
