// MenuWorlds.cpp : see MenuWorlds.h.
#include "stdafx.h"
#include "MenuWorlds.h"
#include "CoopMenu.h"
#include "../Log.h"
#include "../Models.h"
#include "SaveIds.h"
#include "LoadTrace.h"

#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\cPlanetRecord.h>
#include <Spore\Simulator\SubSystem\GamePersistenceManager.h>
#include <Spore\App\cGameModeManager.h>
#include <Spore\GalaxyGameEntry\GlobalGGEUI.h>

#include <map>
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
		std::vector<uint32_t> planetRecords;  // planet records we wrote into the game's planet database
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

	// The game finds the star of a save from the save itself: entry 0x0355C93A of the .spo (uncompressed, a few dozen
	// bytes) holds the played planet's PlanetID (field 0x0355FA98, the first value, at +72). A copied save still points at
	// the template's planet, so after the galaxy is read again (leaving a game without saving) it shows on the template's
	// star and ours is gone. Pointing it at our planet makes the game itself put the save on our star.
	bool PatchSavePlanet(const std::string& path, uint32_t fromPlanet, uint32_t toPlanet)
	{
		FILE* f = fopen(path.c_str(), "r+b");
		if (!f) {
			return false;
		}
		bool done = false;
		uint8_t head[96];
		if (fread(head, 1, 96, f) == 96 && memcmp(head, "DBPF", 4) == 0) {
			auto u32 = [](const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; };
			uint32_t count = u32(head + 0x24), indexOffset = u32(head + 0x40), indexSize = u32(head + 0x2C);
			std::vector<uint8_t> index(indexSize);
			fseek(f, (long)indexOffset, SEEK_SET);
			if (indexSize > 8 && fread(index.data(), 1, indexSize, f) == indexSize) {
				uint32_t flags = u32(index.data());
				size_t p = 4 + ((flags & 1) ? 4 : 0) + ((flags & 2) ? 4 : 0) + ((flags & 4) ? 4 : 0);
				for (uint32_t i = 0; i < count && !done; ++i) {
					uint32_t type = (flags & 1) ? 0 : u32(&index[p]);
					p += (flags & 1) ? 0 : 4;
					p += (flags & 2) ? 0 : 4;  // group
					p += 4;                    // instance
					uint32_t offset = u32(&index[p]);
					uint16_t compression = (uint16_t)(index[p + 12] | index[p + 13] << 8);
					p += 16;
					if (type == 0x0355C93A && compression == 0) {
						uint8_t value[4];
						fseek(f, (long)offset + 72, SEEK_SET);
						if (fread(value, 1, 4, f) == 4) {
							uint32_t current = (uint32_t)value[0] << 24 | value[1] << 16 | value[2] << 8 | value[3];
							if (current == fromPlanet) {
								uint8_t to[4] = { (uint8_t)(toPlanet >> 24), (uint8_t)(toPlanet >> 16), (uint8_t)(toPlanet >> 8), (uint8_t)toPlanet };
								fseek(f, (long)offset + 72, SEEK_SET);
								done = fwrite(to, 1, 4, f) == 4;
							}
						}
					}
				}
			}
		}
		fclose(f);
		return done;
	}

	const uint32_t kPlanetRecordType = 0x05220CB8, kPlanetRecordGroup = 1;  // planetRecords.pkp entries

	Resource::Database* PlanetDatabase()
	{
		cStarManager* stars = cStarManager::Get();
		return stars ? stars->mpPlanetRecordsDatabase.get() : nullptr;
	}

	bool ReadRecord(Resource::Database* db, const ResourceKey& key, std::vector<uint8_t>& out)
	{
		Resource::IRecord* record = nullptr;
		if (!db || !db->OpenRecord(key, &record) || !record) {
			return false;
		}
		bool ok = false;
		if (IO::IStream* stream = record->GetStream()) {
			int size = (int)stream->GetSize();
			if (size > 0 && size < 4 * 1024 * 1024) {
				out.resize(size);
				stream->SetPosition(0);
				ok = stream->Read(out.data(), size) == size;
			}
		}
		db->CloseRecord(record);
		return ok;
	}

	bool WriteRecord(Resource::Database* db, const ResourceKey& key, const std::vector<uint8_t>& data)
	{
		Resource::IRecord* record = nullptr;
		if (!db || !db->OpenRecord(key, &record, IO::AccessFlags::ReadWrite, IO::CD::CreateAlways) || !record) {
			return false;
		}
		bool ok = false;
		if (IO::IStream* stream = record->GetStream()) {
			ok = stream->Write(data.data(), data.size()) != 0;  // a record stream returns true, not the byte count
		}
		db->CloseRecord(record);
		return ok;
	}

	// The game reads planet records only from its own database (planetRecords.pkp), also while loading a save, not from
	// our in-memory planets or other packages: copy the template's records there under our star's planet ids. Written
	// through the game's own open database object (the file itself is locked while the game runs).
	Resource::Database* PlanetTempDatabase()
	{
		cStarManager* stars = cStarManager::Get();
		return stars ? stars->mpPlanetRecordsTempDatabase.get() : nullptr;
	}

	void CopyPlanetRecords(cStarRecord* tmpl, cStarRecord* target, std::vector<uint32_t>& written)
	{
		Resource::Database* db = PlanetDatabase();
		Resource::Database* temp = PlanetTempDatabase();
		char access[120];
		snprintf(access, sizeof(access), "addworld: planet databases: main %p access %d, temp %p access %d", (void*)db,
			db ? (int)db->GetAccessFlags() : -1, (void*)temp, temp ? (int)temp->GetAccessFlags() : -1);
		Print(access);
		int copied = 0;
		for (size_t k = 0; k < tmpl->mPlanetCount && k < 32; ++k) {
			uint32_t from = (uint32_t)(k << 24) | tmpl->mKey.internalValue;
			uint32_t to = (uint32_t)(k << 24) | target->mKey.internalValue;
			std::vector<uint8_t> data, moved;
			bool read = ReadRecord(db, ResourceKey(from, kPlanetRecordType, kPlanetRecordGroup), data);
			bool rewritten = read && SaveIds::RewritePlanetRecord(data, tmpl->mKey.internalValue, target->mKey.internalValue, moved);
			// the main database is open read-only; the temporary one (planetRecords.pkt) takes the game's unsaved changes
			bool wrote = rewritten && WriteRecord(temp, ResourceKey(to, kPlanetRecordType, kPlanetRecordGroup), moved);
			if (!wrote) {
				char line[200];
				snprintf(line, sizeof(line), "addworld: planet record %08x: db %p, read %d (%d bytes, head %02x%02x%02x%02x %c%c%c%c), rewritten %d, written %d",
					from, (void*)db, read ? 1 : 0, (int)data.size(), data.size() > 3 ? data[0] : 0, data.size() > 3 ? data[1] : 0,
					data.size() > 3 ? data[2] : 0, data.size() > 3 ? data[3] : 0, data.size() > 7 ? data[4] : '-', data.size() > 7 ? data[5] : '-',
					data.size() > 7 ? data[6] : '-', data.size() > 7 ? data[7] : '-', rewritten ? 1 : 0, wrote ? 1 : 0);
				Print(line);
				continue;
			}
			written.push_back(to);
			++copied;
		}
		char line[120];
		snprintf(line, sizeof(line), "addworld: %d planet record(s) copied into the game's planet database (template has %d)", copied,
			(int)tmpl->mPlanetCount);
		Print(line);
	}

	void DeletePlanetRecords(std::vector<uint32_t>& written)
	{
		Resource::Database* db = PlanetTempDatabase();
		for (uint32_t id : written) {
			if (db) {
				db->DeleteRecord(ResourceKey(id, kPlanetRecordType, kPlanetRecordGroup));
			}
		}
		written.clear();
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
		if (name && strcmp(name, "GalaxyGameEntry") == 0) {
			CoopMenu::EnsureStar();
		}
		// Play in the main menu: the CO-OP star without a friend's world opens our window instead of loading
		if (name && strcmp(name, "LoadGame") == 0 && CoopMenu::HoldPlay()) {
			Print("mode: LoadGame held for the CO-OP window");
			return false;
		}
		if (name && strcmp(name, "LoadGame") == 0) {
			char what[64];
			snprintf(what, sizeof(what), "Play, selected star %08x", MenuWorlds::SelectedStar());
			LoadTrace::Begin(what);
		}
		LoadTrace::OnMode(name);
		return original_function(this, name);
	}
};

// The main menu mode's OnEnter: when the player leaves a game without saving, the galaxy is read again from disk before
// this (our CO-OP star, which lives only in memory, is gone) and the menu builds its star markers inside it. Putting the
// star back right before it makes the menu draw it.
member_detour(GGEOnEnter_detour, App::IGameMode, bool())
{
	bool detoured()
	{
		Print("mode: main menu OnEnter");
		CoopMenu::EnsureStar();
		return original_function(this);
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

	cStarRecord* AddWorld(const char* narrow, bool nearCenter)
	{
		cStarManager* stars = cStarManager::Get();
		if (!stars || stars->mSavedGameStarRecords.empty() || !stars->mSavedGameStarRecords[0]) {
			Print("addworld: needs at least one saved game in this universe (the template)");
			return nullptr;
		}
		// template: the creature-stage save played last (newest .spo), so in a host's installed universe it is the host's
		// current world; any save if there is no creature-stage one
		cStarRecord* tmpl = stars->mSavedGameStarRecords[0].get();
		FILETIME newest = {};
		for (const cStarRecordPtr& s : stars->mSavedGameStarRecords) {
			if (!s || s->mTechLevel != TechLevel::Creature || IsAdded(s.get())) {
				continue;
			}
			for (const cPlanetRecordPtr& p : s->GetPlanetRecords()) {
				if (!p || p->mTechLevel != s->mTechLevel || !p->mGeneratedTerrainKey.instanceID) {
					continue;
				}
				WIN32_FILE_ATTRIBUTE_DATA info;
				std::string spo = Game0Dir() + Narrow(p->mName) + ".spo";
				if (GetFileAttributesExA(spo.c_str(), GetFileExInfoStandard, &info) && CompareFileTime(&info.ftLastWriteTime, &newest) > 0) {
					newest = info.ftLastWriteTime;
					tmpl = s.get();
				}
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
			return nullptr;
		}
		Added a;
		a.star = target;
		a.oldFlags = target->mFlags;
		a.oldTech = target->mTechLevel;
		a.oldName = target->mName;
		a.oldStarter = target->mStarterWorldID;

		char16_t name[64];
		size_t i = 0;
		for (; narrow[i] && i < 63; ++i) {
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
		// the copied save refers to every planet of the template's system (planet ids 0..n-1 of the star): give our star
		// the same number of planets, each like the template's (the generated count is random, 1 to 8)
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
		if (played && !planets.empty() && planets[0]) {
			cPlanetRecord* p0 = planets[0].get();
			p0->mName = name;
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
			std::string from = Game0Dir() + Narrow(played->mName) + ".spo";
			std::string to = Game0Dir() + narrow + ".spo";
			// an exact copy: it still points at the template's planet, whose records are on disk, so Play loads it. Moving it to
			// our star (COOP_MOVE_SAVE=1: SaveIds + the planet records copied into the game's planet database) keeps the star
			// after a galaxy reload but the load still crashes: research, docs/FACTS.md section 6
			bool moved = false;
			if (getenv("COOP_MOVE_SAVE")) {
				CopyPlanetRecords(tmpl, target, a.planetRecords);
				moved = !a.planetRecords.empty() && SaveIds::RewriteSave(from, to, tmpl->mKey.internalValue, target->mKey.internalValue);
				Print(moved ? "addworld: COOP_MOVE_SAVE: save moved to our star" : "addworld: COOP_MOVE_SAVE: moving FAILED, exact copy");
			}
			bool copied = moved || CopyFileA(from.c_str(), to.c_str(), FALSE) != 0;
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
		return target;
	}

	void AddTestWorld(bool nearCenter)
	{
		char narrow[16];
		snprintf(narrow, sizeof(narrow), "test%d", sNextTest++);
		if (AddWorld(narrow, nearCenter)) {
			Print("addworld: done; look at the galaxy (coop delworld undoes it)");
		}
	}

	std::string SavePath(cStarRecord* star)
	{
		for (const Added& a : sAdded) {
			if (a.star.get() == star) {
				return a.spo;
			}
		}
		return "";
	}

	void RenameWorld(cStarRecord* star, const char* narrow)
	{
		eastl::string16 name;
		for (const char* c = narrow; *c; ++c) {
			name += (char16_t)(unsigned char)*c;
		}
		star->mName = name;
	}

	void Forget(cStarRecord* star)
	{
		for (auto it = sAdded.begin(); it != sAdded.end(); ++it) {
			if (it->star.get() == star) {
				sAdded.erase(it);
				return;
			}
		}
	}

	void RemoveWorld(cStarRecord* star)
	{
		cStarManager* stars = cStarManager::Get();
		for (auto it = sAdded.begin(); it != sAdded.end(); ++it) {
			if (it->star.get() != star) {
				continue;
			}
			if (stars) {
				auto& list = stars->mSavedGameStarRecords;
				for (auto s = list.begin(); s != list.end(); ++s) {
					if (s->get() == star) {
						list.erase(s);
						break;
					}
				}
			}
			star->mFlags = it->oldFlags;
			star->mTechLevel = it->oldTech;
			star->mName = it->oldName;
			star->mStarterWorldID = it->oldStarter;
			if (!it->spo.empty()) {
				DeleteFileA(it->spo.c_str());
			}
			DeletePlanetRecords(it->planetRecords);
			sAdded.erase(it);
			return;
		}
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

	bool sGGEHooked = false;

	void HookMenuEnter(HMODULE module)
	{
		if (sGGEHooked) {
			return;
		}
		App::IGameMode* gge = GameModeManager.GetGameMode(GameModeIDs::kGGEMode);
		if (!gge) {
			return;
		}
		void** vtable = *(void***)gge;
		uintptr_t address = (uintptr_t)vtable[0x18 / 4];  // OnEnter
		PrepareDetours(module);
		GGEOnEnter_detour::attach(address);
		sGGEHooked = CommitDetours() == NO_ERROR;
		char line[100];
		snprintf(line, sizeof(line), "mode: main menu OnEnter at %08x hooked: %s", (unsigned)address, sGGEHooked ? "yes" : "NO");
		Print(line);
	}

	void DetachHook()
	{
		if (sGGEHooked) {
			GGEOnEnter_detour::detach();
			sGGEHooked = false;
		}
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

	uint32_t SelectedStar()
	{
		const uint8_t* gge = (const uint8_t*)GalaxyGameEntry::GlobalGGEUI::Get();
		uint32_t object = 0, id = 0;
		if (!gge || !ReadWord(gge + 0xFC, object) || object < 0x10000 || !ReadWord((const uint8_t*)(uintptr_t)object + 0x7C, id)) {
			return 0;
		}
		return id;
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
