// MenuWorlds.cpp : see MenuWorlds.h.
#include "stdafx.h"
#include "MenuWorlds.h"
#include "../Log.h"
#include "LoadTrace.h"

#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\cPlanetRecord.h>
#include <Spore\App\cGameModeManager.h>
#include <Spore\GalaxyGameEntry\GlobalGGEUI.h>

#include <string>

using namespace Simulator;

namespace
{
	// UTF-16 to a narrow string: UTF-8 for the windows, the ANSI code page for file names
	std::string Narrow(const eastl::string16& s, UINT codePage)
	{
		if (s.empty()) {
			return std::string();
		}
		int size = WideCharToMultiByte(codePage, 0, (LPCWSTR)s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
		std::string out(size > 0 ? size : 0, '\0');
		if (size > 0) {
			WideCharToMultiByte(codePage, 0, (LPCWSTR)s.c_str(), (int)s.size(), &out[0], size, nullptr, nullptr);
		}
		return out;
	}

	std::string Game0Dir()
	{
		const char* appdata = getenv("APPDATA");
		return std::string(appdata ? appdata : "") + "\\Spore\\Games\\Game0\\";
	}

	void Print(const char* text)
	{
		Logf("%s", text);
		App::ConsolePrintF("%s", text);
	}

	// reads a word, false if the memory is not readable
	bool ReadWord(const void* at, uint32_t& out)
	{
		__try {
			out = *(const uint32_t*)at;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool WriteWord(void* at, uint32_t value)
	{
		__try {
			*(uint32_t*)at = value;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// the menu object that holds the selected star ([GlobalGGEUI + 0xFC], docs/FACTS.md section 6), 0 if unknown
	uint32_t MenuObject()
	{
		const uint8_t* gge = (const uint8_t*)GalaxyGameEntry::GlobalGGEUI::Get();
		uint32_t object = 0;
		if (!gge || !ReadWord(gge + 0xFC, object) || object < 0x10000) {
			return 0;
		}
		return object;
	}

	const int kSelectedStarOffset = 0x7C;

	// return addresses on the stack inside SporeApp.exe: who changed the mode
	void LogStack(const char* prefix)
	{
		void* frames[24];
		USHORT count = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
		uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
		std::string text = prefix;
		text += " stack";
		for (USHORT i = 0; i < count; ++i) {
			uintptr_t a = (uintptr_t)frames[i];
			char item[32];
			if (a >= base && a < base + 0x2000000) {
				snprintf(item, sizeof(item), " exe+%x", (unsigned)(a - base));
			}
			else {
				snprintf(item, sizeof(item), " %08x", (unsigned)a);
			}
			text += item;
		}
		Print(text.c_str());
	}
}

member_detour(SetActiveMode_detour, App::cGameModeManager, bool(uint32_t))
{
	bool detoured(uint32_t modeID)
	{
		char line[120];
		snprintf(line, sizeof(line), "mode: 0x%x -> 0x%x", this->GetActiveModeID(), modeID);
		Print(line);
		LogStack("mode:");
		return original_function(this, modeID);
	}
};

member_detour(SetActiveModeAt_detour, App::cGameModeManager, bool(size_t))
{
	bool detoured(size_t index)
	{
		char line[80];
		snprintf(line, sizeof(line), "mode: SetActiveModeAt(%u) from 0x%x", (unsigned)index, this->GetActiveModeID());
		Print(line);
		LogStack("mode:");
		return original_function(this, index);
	}
};

member_detour(SetActiveModeByName_detour, App::cGameModeManager, bool(const char*))
{
	bool detoured(const char* name)
	{
		char line[120];
		snprintf(line, sizeof(line), "mode: SetActiveModeByName(%s) from 0x%x", name ? name : "null", this->GetActiveModeID());
		Print(line);
		LogStack("mode:");
		if (name && strcmp(name, "LoadGame") == 0) {
			char what[64];
			snprintf(what, sizeof(what), "Play, selected star %08x", MenuWorlds::SelectedStar());
			LoadTrace::Begin(what);
		}
		LoadTrace::OnMode(name);
		return original_function(this, name);
	}
};

namespace MenuWorlds
{
	void AttachStartupHooks()
	{
		SetActiveMode_detour::attach(GetAddress(App::cGameModeManager, SetActiveMode));
		SetActiveModeAt_detour::attach(GetAddress(App::cGameModeManager, SetActiveModeAt));
		SetActiveModeByName_detour::attach(GetAddress(App::cGameModeManager, SetActiveModeByName));
	}

	void DetachHook()
	{
		SetActiveMode_detour::detach();
		SetActiveModeAt_detour::detach();
		SetActiveModeByName_detour::detach();
	}

	// Watches the menu's selection candidates ([GGE+0xFC]+0x7C and +0x138) and logs every change with the star's name, so a
	// few clicks in the galaxy (or our own selection, spike J9) show up in the log.
	void Update()
	{
		if (GameModeManager.GetActiveModeID() != (uint32_t)GameModeIDs::kGGEMode) {
			return;
		}
		static uint32_t last[2] = { 0, 0 };
		uint32_t object = MenuObject();
		if (!object) {
			return;
		}
		const int offsets[2] = { kSelectedStarOffset, 0x138 };
		for (int i = 0; i < 2; ++i) {
			uint32_t id = 0;
			if (!ReadWord((const uint8_t*)(uintptr_t)object + offsets[i], id) || id == last[i]) {
				continue;
			}
			last[i] = id;
			cStarRecord* star = (id && id != 0xFFFFFFFF && cStarManager::Get()) ? StarManager.GetStarRecord(StarID(id)) : nullptr;
			char line[160];
			snprintf(line, sizeof(line), "select: +%03x = %08x '%s'", offsets[i], id, star ? Narrow(star->mName, CP_UTF8).c_str() : "-");
			Print(line);
		}
	}

	uint32_t SelectedStar()
	{
		uint32_t object = MenuObject(), id = 0;
		if (!object || !ReadWord((const uint8_t*)(uintptr_t)object + kSelectedStarOffset, id)) {
			return 0;
		}
		return id;
	}

	bool SelectStar(uint32_t starId)
	{
		uint32_t object = MenuObject();
		if (!object) {
			return false;
		}
		uint32_t before = 0;
		ReadWord((const uint8_t*)(uintptr_t)object + kSelectedStarOffset, before);
		bool written = WriteWord((uint8_t*)(uintptr_t)object + kSelectedStarOffset, starId);
		uint32_t after = 0;
		ReadWord((const uint8_t*)(uintptr_t)object + kSelectedStarOffset, after);
		Logf("menu: J9: selected star %08x -> wrote %08x (%s), reads %08x", before, starId, written ? "ok" : "FAILED", after);
		return written;
	}

	bool HostPlanet(uint32_t& starId, std::string& planetName)
	{
		cStarManager* stars = cStarManager::Get();
		if (!stars) {
			return false;
		}
		// the creature-stage save played last (newest .spo; the file is named after the star's played planet): in a co-op
		// launch Game0 is the host's world, so this is the planet the host plays now
		cStarRecord* best = nullptr;
		std::string bestName;
		FILETIME newest = {};
		bool haveTime = false;
		for (const cStarRecordPtr& s : stars->mSavedGameStarRecords) {
			if (!s || s->mTechLevel != TechLevel::Creature) {
				continue;
			}
			for (const cPlanetRecordPtr& p : s->GetPlanetRecords()) {
				if (!p || p->mTechLevel != s->mTechLevel || !p->mGeneratedTerrainKey.instanceID) {
					continue;
				}
				WIN32_FILE_ATTRIBUTE_DATA info;
				std::string spo = Game0Dir() + Narrow(p->mName, CP_ACP) + ".spo";
				if (GetFileAttributesExA(spo.c_str(), GetFileExInfoStandard, &info)) {
					if (!haveTime || CompareFileTime(&info.ftLastWriteTime, &newest) > 0) {
						newest = info.ftLastWriteTime;
						haveTime = true;
						best = s.get();
						bestName = Narrow(p->mName, CP_UTF8);
					}
				}
				else if (!best) {
					best = s.get();  // no readable file time: the first creature star, replaced by any star with a file
					bestName = Narrow(p->mName, CP_UTF8);
				}
				break;
			}
		}
		if (!best) {
			return false;
		}
		starId = best->mKey.internalValue;
		planetName = bestName;
		return true;
	}
}
