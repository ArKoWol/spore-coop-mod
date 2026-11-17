// coop_players.h : the sidecar of a host's world, Game0\coop_players.json (docs/PLAN.md section 8), without the game.
//
// The host keeps there what belongs to each guest in this world, by the guest's id (Steam ID, or the port of a local
// session). The file travels to the guest with the world (it is an ordinary file of Game0), so in a co-op launch the guest
// reads his own entry from his copy. Only the host writes it.
//
// Today an entry holds the guest's creature (instance/type/group of its creature resource, D26). Later fields (DNA, brain
// level, ...) go next to "creature"; the reader skips fields it does not know, the writer writes only the fields of Entry.
//
//   {
//     "version": 1,
//     "players": [
//       { "id": "76561190000000000", "creature": "1a2b3c4d/2b978c46/40626200" }
//     ]
//   }
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop
{
	namespace players
	{
		const char kFileName[] = "coop_players.json";

		struct Entry
		{
			unsigned long long id = 0;
			uint32_t creature[3] = { 0, 0, 0 };  // instance, type, group; instance 0 = none
		};

		namespace detail
		{
			inline void SkipSpace(const std::string& t, size_t& i)
			{
				while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) {
					++i;
				}
			}

			// a JSON string without escapes (ids and keys never have any); false on anything else
			inline bool ReadString(const std::string& t, size_t& i, std::string& out)
			{
				SkipSpace(t, i);
				if (i >= t.size() || t[i] != '"') {
					return false;
				}
				size_t end = t.find('"', i + 1);
				if (end == std::string::npos) {
					return false;
				}
				out = t.substr(i + 1, end - i - 1);
				if (out.find('\\') != std::string::npos) {
					return false;
				}
				i = end + 1;
				return true;
			}

			// skips one value of any kind (string, number, object, array, true/false/null)
			inline bool SkipValue(const std::string& t, size_t& i)
			{
				SkipSpace(t, i);
				if (i >= t.size()) {
					return false;
				}
				if (t[i] == '"') {
					std::string s;
					return ReadString(t, i, s);
				}
				if (t[i] == '{' || t[i] == '[') {
					int depth = 0;
					bool inString = false;
					for (; i < t.size(); ++i) {
						char c = t[i];
						if (inString) {
							if (c == '\\') {
								++i;
							}
							else if (c == '"') {
								inString = false;
							}
							continue;
						}
						if (c == '"') {
							inString = true;
						}
						else if (c == '{' || c == '[') {
							++depth;
						}
						else if (c == '}' || c == ']') {
							if (--depth == 0) {
								++i;
								return true;
							}
						}
					}
					return false;
				}
				while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ']') {
					++i;
				}
				return true;
			}

			inline bool ParseId(const std::string& s, unsigned long long& id)
			{
				if (s.empty() || s.size() > 20) {
					return false;
				}
				for (char c : s) {
					if (c < '0' || c > '9') {
						return false;
					}
				}
				id = std::strtoull(s.c_str(), nullptr, 10);
				return id != 0;
			}

			inline bool ParseKey(const std::string& s, uint32_t key[3])
			{
				unsigned a = 0, b = 0, c = 0;
				char tail = 0;
				if (std::sscanf(s.c_str(), "%8x/%8x/%8x%c", &a, &b, &c, &tail) != 3) {
					return false;
				}
				key[0] = a; key[1] = b; key[2] = c;
				return true;
			}

			// one { ... } of "players"
			inline bool ParseEntry(const std::string& t, size_t& i, Entry& e)
			{
				SkipSpace(t, i);
				if (i >= t.size() || t[i] != '{') {
					return false;
				}
				++i;
				for (;;) {
					SkipSpace(t, i);
					if (i < t.size() && t[i] == '}') {
						++i;
						return true;
					}
					std::string name, value;
					if (!ReadString(t, i, name)) {
						return false;
					}
					SkipSpace(t, i);
					if (i >= t.size() || t[i] != ':') {
						return false;
					}
					++i;
					if (name == "id" || name == "creature") {
						if (!ReadString(t, i, value)) {
							return false;
						}
						if (name == "id" && !ParseId(value, e.id)) {
							return false;
						}
						if (name == "creature" && !ParseKey(value, e.creature)) {
							e.creature[0] = e.creature[1] = e.creature[2] = 0;  // a damaged key: no creature, the entry stays
						}
					}
					else if (!SkipValue(t, i)) {
						return false;
					}
					SkipSpace(t, i);
					if (i < t.size() && t[i] == ',') {
						++i;
					}
				}
			}
		}

		// Reads the entries of a sidecar text. False (and nothing in `out`) when the text is not a sidecar; entries without an id
		// are left out.
		inline bool Parse(const std::string& text, std::vector<Entry>& out)
		{
			out.clear();
			size_t at = text.find("\"players\"");
			if (at == std::string::npos) {
				return false;
			}
			size_t i = at + 9;
			detail::SkipSpace(text, i);
			if (i >= text.size() || text[i] != ':') {
				return false;
			}
			++i;
			detail::SkipSpace(text, i);
			if (i >= text.size() || text[i] != '[') {
				return false;
			}
			++i;
			std::vector<Entry> entries;
			for (;;) {
				detail::SkipSpace(text, i);
				if (i < text.size() && text[i] == ']') {
					break;
				}
				Entry e;
				if (!detail::ParseEntry(text, i, e)) {
					return false;
				}
				if (e.id) {
					entries.push_back(e);
				}
				detail::SkipSpace(text, i);
				if (i < text.size() && text[i] == ',') {
					++i;
				}
			}
			out.swap(entries);
			return true;
		}

		inline std::string Format(const std::vector<Entry>& entries)
		{
			std::string t = "{\n  \"version\": 1,\n  \"players\": [";
			for (size_t n = 0; n < entries.size(); ++n) {
				const Entry& e = entries[n];
				char line[160];
				std::snprintf(line, sizeof(line), "%s\n    { \"id\": \"%llu\", \"creature\": \"%08x/%08x/%08x\" }", n ? "," : "", e.id,
					(unsigned)e.creature[0], (unsigned)e.creature[1], (unsigned)e.creature[2]);
				t += line;
			}
			t += entries.empty() ? "]\n}\n" : "\n  ]\n}\n";
			return t;
		}

		// The entry of `id`, or null.
		inline Entry* Find(std::vector<Entry>& entries, unsigned long long id)
		{
			for (Entry& e : entries) {
				if (e.id == id) {
					return &e;
				}
			}
			return nullptr;
		}

		// Sets the creature of `id` (adds the entry if needed). True if something changed.
		inline bool SetCreature(std::vector<Entry>& entries, unsigned long long id, const uint32_t key[3])
		{
			Entry* e = Find(entries, id);
			if (!e) {
				Entry n;
				n.id = id;
				entries.push_back(n);
				e = &entries.back();
			}
			if (e->creature[0] == key[0] && e->creature[1] == key[1] && e->creature[2] == key[2]) {
				return false;
			}
			e->creature[0] = key[0]; e->creature[1] = key[1]; e->creature[2] = key[2];
			return true;
		}

		// ---------------------------------------------------------------- files
		inline bool ReadText(const std::string& path, std::string& out)
		{
			out.clear();
			FILE* f = std::fopen(path.c_str(), "rb");
			if (!f) {
				return false;
			}
			char buf[4096];
			size_t n;
			while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < (1u << 20)) {
				out.append(buf, n);
			}
			std::fclose(f);
			return true;
		}

		// Loads the sidecar of a world folder. A missing file is an empty list (true); a damaged one is false.
		inline bool Load(const std::string& dir, std::vector<Entry>& out)
		{
			out.clear();
			std::string text;
			if (!ReadText(dir + "\\" + kFileName, text)) {
				return true;
			}
			return Parse(text, out);
		}

		// Writes the sidecar through a temporary file, so a crash never leaves half a file.
		inline bool Save(const std::string& dir, const std::vector<Entry>& entries)
		{
			const std::string path = dir + "\\" + kFileName, tmp = path + ".tmp";
			const std::string text = Format(entries);
			FILE* f = std::fopen(tmp.c_str(), "wb");
			if (!f) {
				return false;
			}
			bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
			ok = std::fclose(f) == 0 && ok;
			if (!ok) {
				std::remove(tmp.c_str());
				return false;
			}
			if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
				std::remove(tmp.c_str());
				return false;
			}
			return true;
		}
	}
}
