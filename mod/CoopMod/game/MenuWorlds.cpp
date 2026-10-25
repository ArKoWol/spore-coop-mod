// MenuWorlds.cpp : see MenuWorlds.h.
#include "stdafx.h"
#include "MenuWorlds.h"
#include "../Log.h"

#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\cPlanetRecord.h>
#include <Spore\Simulator\SubSystem\GamePersistenceManager.h>
#include <Spore\App\cGameModeManager.h>
#include <Spore\GalaxyGameEntry\GlobalGGEUI.h>

#include <string>
#include <vector>

using namespace Simulator;

namespace
{
	struct Added
	{
		cStarRecordPtr star;
		int oldFlags = 0;
		uint32_t oldStarter = 0;
		std::string spo;  // the save file we copied for it
		TechLevel oldTech = TechLevel::None;
		eastl::string16 oldName;
	};
	std::vector<Added> sAdded;

	std::string Game0Dir()
	{
		const char* appdata = getenv("APPDATA");
		return std::string(appdata ? appdata : "") + "\\Spore\\Games\\Game0\\";
	}
	int sNextTest = 1;

	std::string Narrow(const eastl::string16& s)
	{
		std::string out;
		for (char16_t c : s) {
			out += c < 128 ? (char)c : '?';
		}
		return out;
	}

	void Print(const char* text)
	{
		Logf("%s", text);
		App::ConsolePrintF("%s", text);
	}

	void DescribeStar(cStarRecord* star, const char* prefix)
	{
		char line[400];
		snprintf(line, sizeof(line), "%s star %08x '%s' type %d flags 0x%x tech %d version %d.%d starter %08x planets %d (loaded %d) time %d %d %d pos %.1f %.1f %.1f",
			prefix, star->mKey.internalValue, Narrow(star->mName).c_str(), (int)star->mType, star->mFlags, (int)star->mTechLevel,
			star->mSavedGameVersionMajor, star->mSavedGameVersionMinor, star->mStarterWorldID, (int)star->mPlanetCount,
			(int)star->mPlanets.size(), star->mSavedGameTimeStamp[0], star->mSavedGameTimeStamp[1], star->mSavedGameTimeStamp[2],
			star->mPosition.x, star->mPosition.y, star->mPosition.z);
		Print(line);
		for (const cPlanetRecordPtr& planet : star->mPlanets) {
			if (!planet) {
				continue;
			}
			const ResourceKey& t = planet->mGeneratedTerrainKey;
			snprintf(line, sizeof(line), "%s   planet %08x '%s' type %d tech %d flags 0x%x terrain %08x!%08x.%08x",
				prefix, planet->mKey.internalValue, Narrow(planet->mName).c_str(), (int)planet->mType, (int)planet->mTechLevel,
				planet->mFlags, t.groupID, t.instanceID, t.typeID);
			Print(line);
		}
	}

	bool IsAdded(cStarRecord* star)
	{
		for (const Added& a : sAdded) {
			if (a.star.get() == star) {
				return true;
			}
		}
		return false;
	}

	bool IsAddedId(uint32_t starId)
	{
		for (const Added& a : sAdded) {
			if (a.star && a.star->mKey.internalValue == starId) {
				return true;
			}
		}
		return false;
	}

	// Play on a test world is held here until coop playnow (spike J9: can the mod stop a load and start it later itself?)
	bool sHoldPlay = false;
	bool sReleasing = false;
	bool sHookAttached = false;
	GameLoadParameters* sHeld = nullptr;
	uint32_t sHeldMode = 0;

	// what the game manager knows at the moment of a mode change (looking for the chosen save)
	void LogPersistence(const char* prefix)
	{
		cGamePersistenceManager* m = cGamePersistenceManager::Get();
		if (!m) {
			return;
		}
		char line[300];
		snprintf(line, sizeof(line), "%s persistence: %d %d %d %d %d '%s' %d %d %d %d %d", prefix, m->field_20, m->field_24,
			m->field_28, m->field_2C, m->field_30, Narrow(m->field_34).c_str(), m->field_44, m->field_45, m->field_46, m->field_47,
			m->field_48);
		Print(line);
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

	// looks for the test star (its id or a pointer to its record or its planets) in the menu object and one pointer deeper
	void FindInMenu(uint32_t starId, const void* starPtr, const std::vector<const void*>& planetPtrs)
	{
		const uint8_t* gge = (const uint8_t*)GalaxyGameEntry::GlobalGGEUI::Get();
		char line[200];
		if (!gge) {
			Print("ggefind: no GlobalGGEUI");
			return;
		}
		int hits = 0;
		auto check = [&](const uint8_t* base, int depthOffset, int offset, uint32_t w) {
			const char* what = nullptr;
			if (w == starId) what = "star id";
			else if ((const void*)(uintptr_t)w == starPtr) what = "star record";
			else {
				for (const void* p : planetPtrs) {
					if ((const void*)(uintptr_t)w == p) what = "planet record";
				}
			}
			if (what && hits < 40) {
				if (depthOffset < 0) snprintf(line, sizeof(line), "ggefind: GGE+%03x = %s", offset, what);
				else snprintf(line, sizeof(line), "ggefind: [GGE+%03x]+%03x = %s", depthOffset, offset, what);
				Print(line);
				++hits;
			}
		};
		for (int off = 0; off < 0x300; off += 4) {
			uint32_t w;
			if (!ReadWord(gge + off, w)) break;
			check(gge, -1, off, w);
			if (w > 0x10000 && w < 0x7FFF0000) {
				for (int o2 = 0; o2 < 0x200; o2 += 4) {
					uint32_t w2;
					if (!ReadWord((const uint8_t*)(uintptr_t)w + o2, w2)) break;
					check((const uint8_t*)(uintptr_t)w, off, o2, w2);
				}
			}
		}
		snprintf(line, sizeof(line), "ggefind: %d hit(s) for star %08x", hits, starId);
		Print(line);
	}

	// return addresses on the stack inside SporeApp.exe: who sent the message / changed the mode
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

	// logs the load/save messages of the simulator (spike: is Play a kMessageLoadGame, what does it carry?)
	class LoadListener : public App::DefaultMessageListener
	{
	public:
		bool HandleMessage(uint32_t messageID, void* message) override
		{
			char line[200];
			const uint32_t* words = (const uint32_t*)message;
			snprintf(line, sizeof(line), "message: %08x data %p [%08x %08x %08x %08x]", messageID, message,
				words ? words[0] : 0, words ? words[1] : 0, words ? words[2] : 0, words ? words[3] : 0);
			Print(line);
			LogPersistence("message:");
			// the message object: 16 words, and every word that looks like a pointer to a UTF-16 string
			for (int i = 0; words && i < 16; i += 4) {
				snprintf(line, sizeof(line), "message:   +%02x %08x %08x %08x %08x", i * 4, words[i], words[i + 1], words[i + 2], words[i + 3]);
				Print(line);
			}
			LogStack("message:");
			return false;
		}
	};
	App::IMessageListener* sLoadListener = nullptr;
	const uint32_t kWatchedMessages[] = { (uint32_t)SimulatorMessages::kMessageLoadGame, (uint32_t)SimulatorMessages::kMsgSaveGame };

	void LogLoad(const char* what, const GameLoadParameters& p)
	{
		char line[300];
		snprintf(line, sizeof(line), "load: %s game '%s' star '%s' starId %08x mode %d difficulty %d creature %08x!%08x",
			what, Narrow(p.mGameName).c_str(), Narrow(p.mStarName).c_str(), p.mStarID.internalValue, (int)p.mGameMode,
			(int)p.mDifficulty, p.mCreatureKey.groupID, p.mCreatureKey.instanceID);
		Print(line);
	}
}

member_detour(SetActiveMode_detour, App::cGameModeManager, bool(uint32_t))
{
	bool detoured(uint32_t modeID)
	{
		uint32_t from = this->GetActiveModeID();
		char line[120];
		snprintf(line, sizeof(line), "mode: 0x%x -> 0x%x%s", from, modeID, sReleasing ? " (released)" : "");
		Print(line);
		LogPersistence("mode:");
		LogStack("mode:");
		if (sHoldPlay && !sReleasing && from == (uint32_t)GameModeIDs::kGGEMode && modeID != from && !sAdded.empty()) {
			sHeldMode = modeID;
			Print("mode: HELD (leaving the main menu while a test world exists); coop playnow continues");
			return false;
		}
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
		return original_function(this, name);
	}
};

member_detour(LoadGame_detour, Simulator::cGamePersistenceManager, bool(const Simulator::GameLoadParameters&))
{
	bool detoured(const Simulator::GameLoadParameters& params)
	{
		LogLoad(sReleasing ? "released" : "requested", params);
		if (sHoldPlay && !sReleasing && IsAddedId(params.mStarID.internalValue)) {
			delete sHeld;
			sHeld = new Simulator::GameLoadParameters(params);
			Print("load: HELD (a test world); coop playnow starts it");
			return false;
		}
		bool result = original_function(this, params);
		char line[60];
		snprintf(line, sizeof(line), "load: original LoadGame -> %d", result ? 1 : 0);
		Print(line);
		return result;
	}
};

namespace MenuWorlds
{
	void Dump()
	{
		char line[200];
		snprintf(line, sizeof(line), "slots: mode 0x%x (main menu is 0x%x)", GameModeManager.GetActiveModeID(), (uint32_t)GameModeIDs::kGGEMode);
		Print(line);
		cStarManager* stars = cStarManager::Get();
		if (!stars) {
			Print("slots: no StarManager");
			return;
		}
		snprintf(line, sizeof(line), "slots: %d saved-game stars, %d grid rows", (int)stars->mSavedGameStarRecords.size(),
			(int)stars->mStarRecordGrid.size());
		Print(line);
		for (const cStarRecordPtr& star : stars->mSavedGameStarRecords) {
			if (star) {
				DescribeStar(star.get(), "slots:");
			}
		}
		// stars flagged as saves that are not in the list (or the other way round) tell how the menu finds them
		int flagged = 0, total = 0;
		for (auto& row : stars->mStarRecordGrid) {
			for (const cStarRecordPtr& star : row) {
				if (!star) {
					continue;
				}
				++total;
				if (star->mFlags & kStarFlagSaveGame) {
					++flagged;
					bool listed = false;
					for (const cStarRecordPtr& s : stars->mSavedGameStarRecords) {
						listed = listed || s.get() == star.get();
					}
					if (!listed) {
						DescribeStar(star.get(), "slots: flagged, NOT in the list:");
					}
				}
			}
		}
		snprintf(line, sizeof(line), "slots: %d stars in the grid, %d flagged kStarFlagSaveGame", total, flagged);
		Print(line);
	}

	void AddTestWorld(bool nearCenter)
	{
		cStarManager* stars = cStarManager::Get();
		if (!stars || stars->mSavedGameStarRecords.empty() || !stars->mSavedGameStarRecords[0]) {
			Print("addworld: needs at least one saved game in this universe (the template)");
			return;
		}
		// template: a creature-stage save if there is one
		cStarRecord* tmpl = stars->mSavedGameStarRecords[0].get();
		for (const cStarRecordPtr& s : stars->mSavedGameStarRecords) {
			if (s && s->mTechLevel == TechLevel::Creature) {
				tmpl = s.get();
				break;
			}
		}
		// a free star of the same kind: not a save or a possible save, not owned, nobody lives there
		// the closest one to the template (next to the existing saves) or to the galaxy's centre
		Vector3 aim = nearCenter ? Vector3(0, 0, 0) : tmpl->mPosition;
		float minDist = nearCenter ? 40.0f * 40.0f : 4.0f;  // keep out of the core / off the template's marker
		cStarRecord* target = nullptr;
		float best = 1e30f;
		for (auto& row : stars->mStarRecordGrid) {
			for (const cStarRecordPtr& star : row) {
				if (star && star.get() != tmpl && star->mType == tmpl->mType
					&& !(star->mFlags & (kStarFlagSaveGame | kStarFlagPotentialSaveGame | kStarFlagIsOwned))
					&& !IsAdded(star.get())) {
					float dx = star->mPosition.x - aim.x, dy = star->mPosition.y - aim.y;
					float d = dx * dx + dy * dy;
					if (d > minDist && d < best) {
						best = d;
						target = star.get();
					}
				}
			}
		}
		if (!target) {
			Print("addworld: no free star found; the first stars of the grid:");
			int shown = 0;
			for (auto& row : stars->mStarRecordGrid) {
				for (const cStarRecordPtr& star : row) {
					if (star && shown < 6) {
						DescribeStar(star.get(), "addworld:  ");
						++shown;
					}
				}
			}
			return;
		}
		Added a;
		a.star = target;
		a.oldFlags = target->mFlags;
		a.oldTech = target->mTechLevel;
		a.oldName = target->mName;
		a.oldStarter = target->mStarterWorldID;

		char16_t name[16];
		char narrow[16];
		snprintf(narrow, sizeof(narrow), "test%d", sNextTest++);
		size_t i = 0;
		for (; narrow[i]; ++i) {
			name[i] = (char16_t)narrow[i];
		}
		name[i] = 0;

		target->mName = name;
		target->mFlags |= tmpl->mFlags & (kStarFlagSaveGame | kStarFlagPotentialSaveGame);
		target->mTechLevel = tmpl->mTechLevel;
		target->mSavedGameVersionMajor = tmpl->mSavedGameVersionMajor;
		target->mSavedGameVersionMinor = tmpl->mSavedGameVersionMinor;
		for (int k = 0; k < 9; ++k) {
			target->mSavedGameTimeStamp[k] = tmpl->mSavedGameTimeStamp[k];
		}
		target->mStarterWorldID = tmpl->mStarterWorldID;

		// a planet like the template's played planet, same terrain, named like the world, and a copy of its save file
		cPlanetRecord* played = nullptr;
		for (const cPlanetRecordPtr& p : tmpl->GetPlanetRecords()) {
			if (p && p->mTechLevel == tmpl->mTechLevel && p->mGeneratedTerrainKey.instanceID) {
				played = p.get();
				break;
			}
		}
		StarManager.RequirePlanetsForStar(target);
		auto& planets = target->GetPlanetRecords();
		if (played && !planets.empty() && planets[0]) {
			cPlanetRecord* p0 = planets[0].get();
			p0->mName = name;
			p0->mType = played->mType;
			p0->mTechLevel = played->mTechLevel;
			p0->mFlags = played->mFlags;
			p0->mGeneratedTerrainKey = played->mGeneratedTerrainKey;
			std::string from = Game0Dir() + Narrow(played->mName) + ".spo";
			std::string to = Game0Dir() + narrow + ".spo";
			bool copied = CopyFileA(from.c_str(), to.c_str(), TRUE) != 0;
			if (copied) {
				a.spo = to;
			}
			char line[300];
			snprintf(line, sizeof(line), "addworld: planet 0 of %d renamed, save %s -> %s: %s", (int)planets.size(), from.c_str(),
				to.c_str(), copied ? "copied" : "copy FAILED");
			Print(line);
		}
		else {
			Print("addworld: no planets for the new star (or no played planet in the template)");
		}
		sAdded.push_back(a);
		stars->mSavedGameStarRecords.push_back(cStarRecordPtr(target));
		DescribeStar(tmpl, "addworld: template");
		DescribeStar(target, "addworld: added");
		Print("addworld: done; look at the galaxy (coop menureload rebuilds the menu, coop delworld undoes it)");
	}

	void RemoveTestWorlds()
	{
		cStarManager* stars = cStarManager::Get();
		int removed = 0;
		for (Added& a : sAdded) {
			if (stars) {
				auto& list = stars->mSavedGameStarRecords;
				for (auto it = list.begin(); it != list.end(); ++it) {
					if (it->get() == a.star.get()) {
						list.erase(it);
						break;
					}
				}
			}
			a.star->mFlags = a.oldFlags;
			a.star->mTechLevel = a.oldTech;
			a.star->mName = a.oldName;
			a.star->mStarterWorldID = a.oldStarter;
			if (!a.spo.empty()) {
				DeleteFileA(a.spo.c_str());
			}
			++removed;
		}
		sAdded.clear();
		char line[80];
		snprintf(line, sizeof(line), "delworld: %d test world(s) removed", removed);
		Print(line);
	}

	void ReloadMenu()
	{
		bool ok = GameModeManager.SetActiveMode(GameModeIDs::kGGEMode);
		char line[80];
		snprintf(line, sizeof(line), "menureload: SetActiveMode(main menu) = %d", ok ? 1 : 0);
		Print(line);
	}

	void SetHoldPlay(HMODULE module)
	{
		if (!sHookAttached) {
			cGamePersistenceManager* manager = cGamePersistenceManager::Get();
			if (!manager) {
				Print("holdplay: no GamePersistenceManager");
				return;
			}
			// LoadGame is the first own virtual (vtable slot 0x10 / 4) of the manager
			void** vtable = *(void***)manager;
			uintptr_t address = (uintptr_t)vtable[0x10 / 4];
			PrepareDetours(module);
			LoadGame_detour::attach(address);
			LONG error = CommitDetours();
			sHookAttached = error == NO_ERROR;
			char line[120];
			snprintf(line, sizeof(line), "holdplay: LoadGame at %08x hooked: %s (%ld)", (unsigned)address, sHookAttached ? "yes" : "NO", error);
			Print(line);
			if (!sHookAttached) {
				return;
			}
		}
		if (!sLoadListener) {
			sLoadListener = new LoadListener();
			sLoadListener->AddRef();
			for (uint32_t id : kWatchedMessages) {
				MessageManager.AddListener(sLoadListener, id);
			}
		}
		sHoldPlay = !sHoldPlay;
		Print(sHoldPlay ? "holdplay: on (Play on a test world is held; coop playnow starts it)" : "holdplay: off");
	}

	void PlayNow()
	{
		if (sHeldMode) {
			uint32_t mode = sHeldMode;
			sHeldMode = 0;
			sReleasing = true;
			bool ok = GameModeManager.SetActiveMode(mode);
			sReleasing = false;
			char line[80];
			snprintf(line, sizeof(line), "playnow: SetActiveMode(0x%x) -> %d", mode, ok ? 1 : 0);
			Print(line);
			return;
		}
		if (!sHeld) {
			Print("playnow: nothing is held");
			return;
		}
		GameLoadParameters* params = sHeld;
		sHeld = nullptr;
		sReleasing = true;
		bool ok = GamePersistenceManager.LoadGame(*params);
		sReleasing = false;
		delete params;
		char line[60];
		snprintf(line, sizeof(line), "playnow: LoadGame -> %d", ok ? 1 : 0);
		Print(line);
	}

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
		if (sLoadListener) {
			for (uint32_t id : kWatchedMessages) {
				MessageManager.RemoveListener(sLoadListener, id);
			}
			sLoadListener->Release();
			sLoadListener = nullptr;
		}
		if (sHookAttached) {
			LoadGame_detour::detach();
			sHookAttached = false;
		}
	}

	// Watches the menu's selection candidates ([GGE+0xFC]+0x7C and +0x138, found with coop ggefind) and logs every change
	// with the star's name, so a few clicks in the galaxy tell which one is "the selected star".
	void Update()
	{
		if (GameModeManager.GetActiveModeID() != (uint32_t)GameModeIDs::kGGEMode) {
			return;
		}
		static uint32_t last[2] = { 0, 0 };
		const uint8_t* gge = (const uint8_t*)GalaxyGameEntry::GlobalGGEUI::Get();
		uint32_t object = 0;
		if (!gge || !ReadWord(gge + 0xFC, object) || object < 0x10000) {
			return;
		}
		const int offsets[2] = { 0x7C, 0x138 };
		for (int i = 0; i < 2; ++i) {
			uint32_t id = 0;
			if (!ReadWord((const uint8_t*)(uintptr_t)object + offsets[i], id) || id == last[i]) {
				continue;
			}
			last[i] = id;
			cStarRecord* star = (id && id != 0xFFFFFFFF && cStarManager::Get()) ? StarManager.GetStarRecord(StarID(id)) : nullptr;
			char line[160];
			snprintf(line, sizeof(line), "select: +%03x = %08x '%s'", offsets[i], id, star ? Narrow(star->mName).c_str() : "-");
			Print(line);
		}
	}

	void FindSelected()
	{
		if (sAdded.empty()) {
			Print("ggefind: add a test world first (coop addcenter), select it in the galaxy, then run this");
			return;
		}
		cStarRecord* star = sAdded.back().star.get();
		std::vector<const void*> planets;
		for (const cPlanetRecordPtr& p : star->mPlanets) {
			planets.push_back(p.get());
		}
		FindInMenu(star->mKey.internalValue, star, planets);
	}

	void Shutdown()
	{
		delete sHeld;
		sHeld = nullptr;
		std::vector<Added>().swap(sAdded);
	}
}
