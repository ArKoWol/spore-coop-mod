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

	// the CO-OP star of a co-op launch (D24) and what it was before; heap: intrusive_ptr has a destructor
	struct CoopStar
	{
		cStarRecordPtr star;
		int oldFlags = 0;
		uint32_t oldStarter = 0;
		TechLevel oldTech = TechLevel::None;
		eastl::string16 oldName;
		std::string spo;  // our copy of the host's save ("" if the copy failed)
	};
	CoopStar* sCoop = nullptr;
	const char* kCoopSave = "CO-OP";  // the save file and the planet's name; the star shows "CO-OP: <host>"

	eastl::string16 Wide16(const std::string& utf8)
	{
		eastl::string16 out;
		int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
		if (size > 0) {
			std::wstring w(size, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &w[0], size);
			for (wchar_t c : w) {
				out += (char16_t)c;
			}
		}
		return out;
	}

	bool IsCoopName(const eastl::string16& n)
	{
		return n.size() >= 5 && n[0] == u'C' && n[1] == u'O' && n[2] == u'-' && n[3] == u'O' && n[4] == u'P';
	}

	void Unlist(cStarManager* stars, cStarRecord* star)
	{
		auto& list = stars->mSavedGameStarRecords;
		for (auto s = list.begin(); s != list.end(); ++s) {
			if (s->get() == star) {
				list.erase(s);
				return;
			}
		}
	}

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
			if (!s || s->mTechLevel != TechLevel::Creature || (sCoop && s.get() == sCoop->star.get())) {
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

	uint32_t AddCoopStar(uint32_t hostStar, const std::string& name)
	{
		cStarManager* stars = cStarManager::Get();
		cStarRecord* tmpl = (stars && hostStar) ? StarManager.GetStarRecord(StarID(hostStar)) : nullptr;
		if (!tmpl || sCoop) {
			return 0;
		}
		// stars named "CO-OP..." that an earlier co-op launch left in this galaxy (if the game saved it into the cache)
		auto& list = stars->mSavedGameStarRecords;
		for (size_t i = 0; i < list.size();) {
			cStarRecord* s = list[i].get();
			if (s && s != tmpl && IsCoopName(s->mName)) {
				Logf("menu: removing a CO-OP star left from an earlier run (%08x)", s->mKey.internalValue);
				s->mFlags &= ~(kStarFlagSaveGame | kStarFlagPotentialSaveGame);
				list.erase(list.begin() + i);
				continue;
			}
			++i;
		}
		cPlanetRecord* played = nullptr;
		for (const cPlanetRecordPtr& p : tmpl->GetPlanetRecords()) {
			if (p && p->mTechLevel == tmpl->mTechLevel && p->mGeneratedTerrainKey.instanceID) {
				played = p.get();
				break;
			}
		}
		if (!played) {
			Logf("menu: CO-OP star: the host's star %08x has no played planet", hostStar);
			return 0;
		}
		// a free star of the same kind near the centre (on screen when the menu opens): not a save or a possible save, not
		// owned, outside the core
		cStarRecord* target = nullptr;
		float best = 1e30f;
		for (auto& row : stars->mStarRecordGrid) {
			for (const cStarRecordPtr& star : row) {
				if (star && star.get() != tmpl && star->mType == tmpl->mType
					&& !(star->mFlags & (kStarFlagSaveGame | kStarFlagPotentialSaveGame | kStarFlagIsOwned))) {
					float d = star->mPosition.x * star->mPosition.x + star->mPosition.y * star->mPosition.y;
					if (d > 40.0f * 40.0f && d < best) {
						best = d;
						target = star.get();
					}
				}
			}
		}
		if (!target) {
			Logf("menu: CO-OP star: no free star near the centre");
			return 0;
		}
		std::string from = Game0Dir() + Narrow(played->mName, CP_ACP) + ".spo";
		std::string to = Game0Dir() + kCoopSave + ".spo";
		// an exact copy: it still points at the host's planet, whose records are on disk, so Play loads the host's world
		// (verified 2026-10-25); a save moved to another star does not load (FACTS section 6)
		if (!CopyFileA(from.c_str(), to.c_str(), FALSE)) {
			Logf("menu: CO-OP star: copying %s failed (%lu)", from.c_str(), GetLastError());
			return 0;
		}
		sCoop = new CoopStar();
		sCoop->star = target;
		sCoop->oldFlags = target->mFlags;
		sCoop->oldTech = target->mTechLevel;
		sCoop->oldName = target->mName;
		sCoop->oldStarter = target->mStarterWorldID;
		sCoop->spo = to;

		target->mName = Wide16(name);
		target->mFlags |= tmpl->mFlags & (kStarFlagSaveGame | kStarFlagPotentialSaveGame);
		target->mTechLevel = tmpl->mTechLevel;
		target->mSavedGameVersionMajor = tmpl->mSavedGameVersionMajor;
		target->mSavedGameVersionMinor = tmpl->mSavedGameVersionMinor;
		for (int k = 0; k < 9; ++k) {
			target->mSavedGameTimeStamp[k] = tmpl->mSavedGameTimeStamp[k];
		}
		target->mStarterWorldID = tmpl->mStarterWorldID;

		// the copied save refers to every planet of the host's system (planet ids 0..n-1 of the star): our star gets the same
		// number of planets, each like the host's (the generated count is random, 1 to 8); planet 0 is the played one
		StarManager.RequirePlanetsForStar(target);
		auto& planets = target->GetPlanetRecords();
		auto& tmplPlanets = tmpl->GetPlanetRecords();
		while (planets.size() < tmplPlanets.size()) {
			cPlanetRecordPtr extra;
			cPlanetRecord::Create(PlanetID(target->mKey, (unsigned)planets.size()), extra);
			if (!extra) {
				break;
			}
			planets.push_back(extra);
		}
		for (size_t k = 1; k < planets.size() && k < tmplPlanets.size(); ++k) {
			cPlanetRecord* p = planets[k].get();
			cPlanetRecord* s = tmplPlanets[k].get();
			if (!p || !s) {
				continue;
			}
			p->mName = s->mName;
			p->mType = s->mType;
			p->mTechLevel = s->mTechLevel;
			p->mFlags = s->mFlags;
			p->mGeneratedTerrainKey = s->mGeneratedTerrainKey;
			p->mAtmosphereScore = s->mAtmosphereScore;
			p->mTemperatureScore = s->mTemperatureScore;
			p->mWaterScore = s->mWaterScore;
			p->mPlantSpecies = s->mPlantSpecies;
			p->mAnimalSpecies = s->mAnimalSpecies;
			p->mOrbit = s->mOrbit;
		}
		target->mPlanetCount = (char)planets.size();
		if (!planets.empty() && planets[0]) {
			cPlanetRecord* p0 = planets[0].get();
			p0->mName = Wide16(kCoopSave);
			p0->mType = played->mType;
			p0->mTechLevel = played->mTechLevel;
			p0->mFlags = played->mFlags;
			p0->mGeneratedTerrainKey = played->mGeneratedTerrainKey;
			// what a creature-stage load reads from the planet record (a copied save that points at this planet crashed
			// without them): climate, the planet's species, orbit and rotation, objects, spice
			p0->mAtmosphereScore = played->mAtmosphereScore;
			p0->mTemperatureScore = played->mTemperatureScore;
			p0->mWaterScore = played->mWaterScore;
			p0->mPlantSpecies = played->mPlantSpecies;
			p0->mAnimalSpecies = played->mAnimalSpecies;
			p0->mOrbit = played->mOrbit;
			p0->mbRotationIsNull = played->mbRotationIsNull;
			p0->mRotationAxis = played->mRotationAxis;
			p0->mRotationPeriod = played->mRotationPeriod;
			p0->mPlanetRing = played->mPlanetRing;
			p0->field_AD = played->field_AD;
			p0->mPlanetObjects = played->mPlanetObjects;
			p0->mTerrainStampsToRemove = played->mTerrainStampsToRemove;
			p0->mbHomeWorld = played->mbHomeWorld;
			p0->field_188 = played->field_188;
			p0->mSpiceGen = played->mSpiceGen;
		}
		list.push_back(cStarRecordPtr(target));
		Logf("menu: CO-OP star %08x added near the centre (%d planets, save %s copied from %s's %s)", target->mKey.internalValue,
			(int)planets.size(), to.c_str(), Narrow(tmpl->mName, CP_UTF8).c_str(), Narrow(played->mName, CP_UTF8).c_str());
		return target->mKey.internalValue;
	}

	void RenameCoopStar(const std::string& name)
	{
		if (sCoop && sCoop->star) {
			sCoop->star->mName = Wide16(name);
		}
	}

	bool CoopStarListed()
	{
		cStarManager* stars = cStarManager::Get();
		if (!sCoop || !stars) {
			return false;
		}
		for (const cStarRecordPtr& s : stars->mSavedGameStarRecords) {
			if (s.get() == sCoop->star.get()) {
				return true;
			}
		}
		return false;
	}

	void ForgetCoopStar()
	{
		if (!sCoop) {
			return;
		}
		if (!sCoop->spo.empty()) {
			DeleteFileA(sCoop->spo.c_str());
		}
		delete sCoop;
		sCoop = nullptr;
	}

	void RemoveCoopStar()
	{
		if (!sCoop) {
			return;
		}
		cStarRecord* star = sCoop->star.get();
		if (cStarManager* stars = cStarManager::Get()) {
			Unlist(stars, star);
		}
		star->mFlags = sCoop->oldFlags;
		star->mTechLevel = sCoop->oldTech;
		star->mName = sCoop->oldName;
		star->mStarterWorldID = sCoop->oldStarter;
		ForgetCoopStar();
	}
}
