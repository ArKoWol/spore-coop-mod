// RecentHosts.cpp : the list of hosts this PC has joined (see RecentHosts.h).
#include "stdafx.h"
#include "RecentHosts.h"
#include "Log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace
{
	const size_t kMaxHosts = 10;

	struct Host
	{
		uint64_t id = 0;
		long long when = 0;  // unix time of the last successful join
		std::string name;
	};

	std::string ListPath()
	{
		const char* appdata = getenv("APPDATA");
		return std::string(appdata ? appdata : "") + "\\Spore\\Games\\CoopHosts.txt";
	}

	// one host per line: <steam id> <tab> <unix time> <tab> <name>
	std::vector<Host> Load()
	{
		std::vector<Host> hosts;
		if (FILE* f = fopen(ListPath().c_str(), "r")) {
			char line[512];
			while (fgets(line, sizeof(line), f) && hosts.size() < kMaxHosts) {
				Host h;
				char* p = line;
				h.id = _strtoui64(p, &p, 10);
				if (!h.id) {
					continue;
				}
				if (*p == '\t') {
					h.when = _strtoi64(p + 1, &p, 10);
				}
				if (*p == '\t') {
					h.name = p + 1;
					while (!h.name.empty() && (h.name.back() == '\n' || h.name.back() == '\r')) {
						h.name.pop_back();
					}
				}
				hosts.push_back(h);
			}
			fclose(f);
		}
		return hosts;
	}

	void Save(const std::vector<Host>& hosts)
	{
		if (FILE* f = fopen(ListPath().c_str(), "w")) {
			for (const Host& h : hosts) {
				fprintf(f, "%llu\t%lld\t%s\n", (unsigned long long)h.id, h.when, h.name.c_str());
			}
			fclose(f);
		}
	}

	std::string Lower(std::string s)
	{
		for (char& c : s) {
			c = (char)tolower((unsigned char)c);
		}
		return s;
	}

	std::string Describe(const Host& h)
	{
		char text[160];
		snprintf(text, sizeof(text), "%s (%llu)", h.name.empty() ? "?" : h.name.c_str(), (unsigned long long)h.id);
		return text;
	}
}

namespace RecentHosts
{
	void Remember(uint64_t steamId, const char* name)
	{
		if (!steamId) {
			return;
		}
		std::vector<Host> hosts = Load();
		Host h;
		h.id = steamId;
		h.when = (long long)time(nullptr);
		for (size_t i = 0; i < hosts.size(); ++i) {
			if (hosts[i].id == steamId) {
				h.name = hosts[i].name;
				hosts.erase(hosts.begin() + i);
				break;
			}
		}
		if (name && *name) {
			h.name = name;
		}
		hosts.insert(hosts.begin(), h);
		if (hosts.size() > kMaxHosts) {
			hosts.resize(kMaxHosts);
		}
		Save(hosts);
		Logf("hosts: remembered %s", Describe(h).c_str());
	}

	uint64_t Resolve(const char* arg, char* error, size_t errorSize)
	{
		error[0] = 0;
		std::string text = arg ? arg : "";
		std::vector<Host> hosts = Load();

		bool digits = !text.empty();
		for (char c : text) {
			digits = digits && isdigit((unsigned char)c);
		}
		// a Steam ID is a 17-digit number; anything short is a list number
		if (digits && text.size() >= 15) {
			return _strtoui64(text.c_str(), nullptr, 10);
		}
		if (hosts.empty()) {
			snprintf(error, errorSize, "no remembered hosts yet: join once with the host's Steam ID (coopjoin 7656...)");
			return 0;
		}
		if (Lower(text) == "last") {
			return hosts[0].id;
		}
		if (digits) {
			size_t index = (size_t)atoi(text.c_str());
			if (index >= 1 && index <= hosts.size()) {
				return hosts[index - 1].id;
			}
			snprintf(error, errorSize, "there is no host number %s; coop friends lists them", text.c_str());
			return 0;
		}
		// part of a name
		std::string wanted = Lower(text);
		const Host* found = nullptr;
		int matches = 0;
		for (const Host& h : hosts) {
			if (!h.name.empty() && Lower(h.name).find(wanted) != std::string::npos) {
				found = &h;
				++matches;
			}
		}
		if (matches == 1) {
			return found->id;
		}
		snprintf(error, errorSize, matches ? "'%s' matches several hosts; use the number from coop friends"
			: "no remembered host is called '%s'; coop friends lists them", text.c_str());
		return 0;
	}

	bool Get(size_t index, uint64_t& steamId, char* name, size_t nameSize, long long& when)
	{
		std::vector<Host> hosts = Load();
		if (index >= hosts.size()) {
			return false;
		}
		steamId = hosts[index].id;
		when = hosts[index].when;
		snprintf(name, nameSize, "%s", hosts[index].name.c_str());
		return true;
	}

	void Print()
	{
		std::vector<Host> hosts = Load();
		if (hosts.empty()) {
			App::ConsolePrintF("coop: no remembered hosts yet. Join once with the host's Steam ID: coopjoin 7656...");
			return;
		}
		App::ConsolePrintF("coop: hosts you joined (coopjoin last | coopjoin <number> | coopjoin <name>):");
		for (size_t i = 0; i < hosts.size(); ++i) {
			char when[32] = "?";
			time_t t = (time_t)hosts[i].when;
			if (struct tm* local = localtime(&t)) {
				strftime(when, sizeof(when), "%d.%m %H:%M", local);
			}
			App::ConsolePrintF("  %u. %s, last joined %s", (unsigned)(i + 1), Describe(hosts[i]).c_str(), when);
		}
	}
}
