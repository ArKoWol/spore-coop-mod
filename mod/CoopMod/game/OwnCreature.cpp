// OwnCreature.cpp : see OwnCreature.h.
#include "stdafx.h"
#include "OwnCreature.h"
#include "CoopLaunch.h"
#include "MenuWorlds.h"
#include "../Log.h"

#include <Spore\Editors\Editor.h>
#include <Spore\Editors\cEditorResource.h>
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>

#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	typedef std::array<uint32_t, 3> Key3;  // instance, type, group

	const uint32_t kCrtType = 0x2B978C46;   // creature creations (FACTS section 8)
	const uint32_t kBemType = 0x1A99B06B;   // Editors::cEditorResource::RESOURCE_TYPE
	const DWORD kSettleMs = 5000;           // the avatar must exist this long before we touch it (SetSpecies during the load crashes)
	const DWORD kCheckEveryMs = 1000;
	const DWORD kExitChecks[] = { 500, 3000, 8000 };  // EditorSaves is compared again this long after the editor closed

	// Everything lives in one heap object that Shutdown() frees: no static destructor may run at process exit (FACTS section 3).
	struct State
	{
		bool inEditor = false;
		DWORD editorSince = 0, editorLeft = 0;
		int exitCheck = -1;                // index into kExitChecks of the next comparison, -1 = none due
		std::set<Key3> keysAtEnter;        // creature records of EditorSaves when the editor opened
		bool haveKeysAtEnter = false;
		uint32_t enterType = kCrtType;     // the type of the avatar's key when the editor opened
		bool editorRequestLogged = false;
		Key3 edited = { 0, 0, 0 };         // the newest creature key the editor wrote
		bool change = false;               // TakeChange
		std::vector<uint32_t> creatureTypes;

		// the avatar's profile as it is in memory
		void* fpProfile = nullptr;
		uint32_t fpHash = 0, fpSeq = 0, fpLoaded = 0;
		bool fpValid = false;
		DWORD lastCheck = 0;

		// the copies of a body found by ReadFresh, by instance: a signature of (location, size, hash) of every copy
		std::map<uint32_t, uint32_t> copiesSig;

		// own creature file / the guest's own creature
		void* avatar = nullptr;
		DWORD avatarSince = 0;
		Key3 written = { 0, 0, 0 };
		bool menuTried = false;
		void* swapped = nullptr;           // the avatar we switched (or decided not to switch)
		void* verify = nullptr;
		DWORD verifyAt = 0;
	};
	State* sS = nullptr;

	State& S()
	{
		if (!sS) {
			sS = new State();
		}
		return *sS;
	}

	uint32_t Fnv(uint32_t h, const uint8_t* p, size_t n)
	{
		for (size_t i = 0; i < n; ++i) {
			h = (h ^ p[i]) * 16777619u;
		}
		return h;
	}

	std::string Narrow(const char16_t* s)
	{
		std::string out;
		for (const char16_t* c = s; c && *c; ++c) {
			out += *c < 128 ? (char)*c : '?';
		}
		return out;
	}

	bool IsEditorSaves(Resource::Database* db)
	{
		std::string path = Narrow(db ? db->GetLocation() : nullptr);
		for (char& c : path) {
			c = (char)tolower((unsigned char)c);
		}
		return path.find("editorsaves") != std::string::npos;
	}

	std::string GamesDir()
	{
		const char* appdata = getenv("APPDATA");
		return std::string(appdata ? appdata : "") + "\\Spore\\Games";
	}

	std::string OwnFile()
	{
		return GamesDir() + "\\CoopOwnCreature.txt";
	}

	bool ReadOwnFile(Key3& key, char* source, size_t sourceSize)
	{
		FILE* f = fopen(OwnFile().c_str(), "r");
		if (!f) {
			return false;
		}
		char src[32] = {};
		int n = fscanf(f, "%x %x %x %31s", &key[0], &key[1], &key[2], src);
		fclose(f);
		if (source && sourceSize) {
			strncpy_s(source, sourceSize, src, _TRUNCATE);
		}
		return n >= 3 && key[0];
	}

	void WriteOwnFile(State& s, const Key3& key, const char* source)
	{
		FILE* f = fopen(OwnFile().c_str(), "w");
		if (!f) {
			Logf("own: cannot write %s", OwnFile().c_str());
			return;
		}
		fprintf(f, "%08x %08x %08x %s\n", key[0], key[1], key[2], source);
		fclose(f);
		s.written = key;
		Logf("own: our own creature is %08x/%08x/%08x (from %s), written for co-op launches", key[0], key[1], key[2], source);
	}

	const std::vector<uint32_t>& CreatureTypes(State& s)
	{
		if (s.creatureTypes.empty()) {
			eastl::vector<uint32_t> types;
			ResourceManager.GetRecordTypesFromResourceType(types, kCrtType);
			s.creatureTypes.push_back(kCrtType);
			s.creatureTypes.push_back(kBemType);
			for (uint32_t t : types) {
				if (t != kCrtType && t != kBemType) {
					s.creatureTypes.push_back(t);
				}
			}
			std::string list;
			for (uint32_t t : s.creatureTypes) {
				char part[16];
				snprintf(part, sizeof(part), " %08x", t);
				list += part;
			}
			Logf("own: creature record types:%s", list.c_str());
		}
		return s.creatureTypes;
	}

	Resource::Database* EditorSavesDb()
	{
		Resource::IResourceManager::DatabaseList dbs;
		ResourceManager.GetDatabaseList(dbs, nullptr);
		for (Resource::Database* db : dbs) {
			if (IsEditorSaves(db)) {
				return db;
			}
		}
		return nullptr;
	}

	// the creature records in EditorSaves.package
	bool EditorSavesKeys(State& s, std::set<Key3>& out)
	{
		out.clear();
		Resource::Database* db = EditorSavesDb();
		if (!db) {
			return false;
		}
		const std::vector<uint32_t>& types = CreatureTypes(s);
		eastl::vector<ResourceKey> keys;
		db->GetKeyList(keys, nullptr);
		for (const ResourceKey& k : keys) {
			for (uint32_t t : types) {
				if (k.typeID == t) {
					out.insert(Key3{ k.instanceID, k.typeID, k.groupID });
					break;
				}
			}
		}
		return true;
	}

	bool ReadRecord(Resource::Database* db, const ResourceKey& key, std::vector<uint8_t>& data)
	{
		data.clear();
		Resource::IRecord* record = nullptr;
		if (!db || !db->OpenRecord(key, &record) || !record) {
			return false;
		}
		if (IO::IStream* stream = record->GetStream()) {
			int size = (int)stream->GetSize();
			if (size > 0 && size < 8 * 1024 * 1024) {
				data.resize(size);
				stream->SetPosition(0);
				if (stream->Read(data.data(), size) != size) {
					data.clear();
				}
			}
		}
		db->CloseRecord(record);
		return !data.empty();
	}

	// guarded: a hash of the plain fields of a species profile (colours, then creatureSeq .. mDominantAbilityType), and creatureSeq
	bool HashProfile(Simulator::cSpeciesProfile* p, uint32_t* hash, uint32_t* seq)
	{
		__try {
			const uint8_t* b = (const uint8_t*)p;
			uint32_t h = Fnv(2166136261u, b + 0x4E0, 0x504 - 0x4E0);
			h = Fnv(h, b + 0x53C, 0x6B8 - 0x53C);
			*hash = h;
			*seq = p->creatureSeq;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// guarded read of the creature key stored in a profile
	bool ProfileKey(Simulator::cSpeciesProfile* p, Key3& out)
	{
		__try {
			out[0] = p->mCreatureKey.instanceID;
			out[1] = p->mCreatureKey.typeID;
			out[2] = p->mCreatureKey.groupID;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// a hash of the creation as the resource cache holds it (the editor's resource object of the key), 0 if none is loaded
	uint32_t LoadedFingerprint(const Key3& key)
	{
		const uint32_t types[2] = { key[1], kBemType };
		for (uint32_t type : types) {
			ResourceObjectPtr obj;
			if (!ResourceManager.GetLoadedResource(ResourceKey(key[0], type, key[2]), &obj) || !obj) {
				continue;
			}
			auto res = object_cast<Editors::cEditorResource>(obj.get());
			if (!res) {
				continue;
			}
			uint32_t h = Fnv(2166136261u, (const uint8_t*)&type, 4);
			size_t n = res->mBlocks.size();
			h = Fnv(h, (const uint8_t*)&n, sizeof(n));
			if (n) {
				h = Fnv(h, (const uint8_t*)res->mBlocks.data(), n * sizeof(Editors::cEditorResourceBlock));
			}
			return h ? h : 1;
		}
		return 0;
	}

	// guarded: the key the editor was opened with (EditorRequest::mModelKey) and the parent asset key
	bool EditorKeys(Key3& model, Key3& parent)
	{
		__try {
			Editors::cEditor* editor = Editors::GetEditor();
			if (!editor) {
				return false;
			}
			model = Key3{ 0, 0, 0 };
			if (Editors::EditorRequest* request = editor->mEditorRequest.get()) {
				model = Key3{ request->mModelKey.instanceID, request->mModelKey.typeID, request->mModelKey.groupID };
			}
			parent = Key3{ editor->mParentAssetKey.instanceID, editor->mParentAssetKey.typeID, editor->mParentAssetKey.groupID };
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool IsEditorMode(uint32_t mode)
	{
		return mode == (uint32_t)GameModeIDs::kEditorMode || mode == (uint32_t)GameModeIDs::kGameEditMode;
	}

	Key3 AvatarKey(Simulator::cCreatureAnimal* avatar)
	{
		Key3 k = { avatar->mSpeciesKey.instanceID, avatar->mSpeciesKey.typeID, avatar->mSpeciesKey.groupID };
		Key3 p = { 0, 0, 0 };
		if (avatar->mpSpeciesProfile && ProfileKey(avatar->mpSpeciesProfile, p) && p[0]) {
			k = p;
		}
		return k;
	}

	// The editor closed: which creature records did it add to EditorSaves?
	void CompareEditorSaves(State& s, const char* when)
	{
		std::set<Key3> now;
		if (!s.haveKeysAtEnter || !EditorSavesKeys(s, now)) {
			Logf("own: %s: EditorSaves.package not found, no new key can be seen", when);
			return;
		}
		std::vector<Key3> added;
		for (const Key3& k : now) {
			if (!s.keysAtEnter.count(k)) {
				added.push_back(k);
			}
		}
		std::string list;
		for (size_t i = 0; i < added.size() && i < 8; ++i) {
			char part[40];
			snprintf(part, sizeof(part), " %08x/%08x/%08x", added[i][0], added[i][1], added[i][2]);
			list += part;
		}
		Logf("own: %s: %d creature records in EditorSaves, %d new since the editor opened%s", when, (int)now.size(), (int)added.size(),
			list.c_str());
		for (const Key3& k : added) {
			s.keysAtEnter.insert(k);  // reported once
			// the key in the type of the avatar's key (crt): Models::Read finds the record types of that type
			Key3 key = { k[0], s.enterType, k[2] };
			if (key[0] != s.edited[0] || key[2] != s.edited[2]) {
				s.edited = key;
				s.change = true;
				Logf("own: the editor wrote creature %08x/%08x/%08x: announced as our avatar's body from now on (game maps it to %s)", key[0],
					key[1], key[2], SpeciesManager.GetSpeciesProfile(ResourceKey(key[0], key[1], key[2])) ? "a profile" : "no profile");
			}
		}
	}

	void WatchEditor(State& s, DWORD now)
	{
		const bool editor = IsEditorMode(GameModeManager.GetActiveModeID());
		if (editor && !s.inEditor) {
			s.inEditor = true;
			s.editorSince = now;
			s.editorRequestLogged = false;
			s.exitCheck = -1;
			if (auto avatar = GameNounManager.GetAvatar()) {
				Key3 k = AvatarKey(avatar);
				if (k[1]) {
					s.enterType = k[1];
				}
			}
			s.haveKeysAtEnter = EditorSavesKeys(s, s.keysAtEnter);
			Logf("own: editor opened (mode %08x), %d creature records in EditorSaves%s", GameModeManager.GetActiveModeID(),
				(int)s.keysAtEnter.size(), s.haveKeysAtEnter ? "" : " (EditorSaves.package NOT found)");
		}
		if (editor && !s.editorRequestLogged && now - s.editorSince > 1000) {
			s.editorRequestLogged = true;
			Key3 model, parent;
			if (EditorKeys(model, parent)) {
				Logf("own: the editor edits %08x/%08x/%08x (parent asset %08x/%08x/%08x)", model[0], model[1], model[2], parent[0], parent[1],
					parent[2]);
			}
		}
		if (!editor && s.inEditor) {
			s.inEditor = false;
			s.editorLeft = now;
			s.exitCheck = 0;
			s.change = true;
			Key3 model, parent;
			if (EditorKeys(model, parent)) {
				Logf("own: editor closed after %u s (it edited %08x/%08x/%08x, parent asset %08x/%08x/%08x)", (unsigned)((now - s.editorSince) / 1000),
					model[0], model[1], model[2], parent[0], parent[1], parent[2]);
			}
			else {
				Logf("own: editor closed after %u s", (unsigned)((now - s.editorSince) / 1000));
			}
		}
		if (!s.inEditor && s.exitCheck >= 0 && now - s.editorLeft >= kExitChecks[s.exitCheck]) {
			char when[48];
			snprintf(when, sizeof(when), "%u ms after the editor", (unsigned)kExitChecks[s.exitCheck]);
			CompareEditorSaves(s, when);
			s.change = true;
			++s.exitCheck;
			if (s.exitCheck >= (int)(sizeof(kExitChecks) / sizeof(kExitChecks[0]))) {
				s.exitCheck = -1;
			}
		}
	}

	// The avatar's species profile in memory: an edit that changes the look of every creature of the species should change it.
	void WatchProfile(State& s, Simulator::cCreatureAnimal* avatar)
	{
		void* profile = avatar->mpSpeciesProfile;
		uint32_t hash = 0, seq = 0;
		if (!profile || !HashProfile(avatar->mpSpeciesProfile, &hash, &seq)) {
			return;
		}
		const uint32_t loaded = LoadedFingerprint(AvatarKey(avatar));
		if (s.fpValid && profile == s.fpProfile && hash == s.fpHash && seq == s.fpSeq && loaded == s.fpLoaded) {
			return;
		}
		if (s.fpValid) {
			Logf("own: our avatar's species profile changed in memory: profile %p -> %p, fields %08x -> %08x, creatureSeq %u -> %u, "
				"cached creation %08x -> %08x (avatar profile sequence %u)", s.fpProfile, profile, s.fpHash, hash, (unsigned)s.fpSeq,
				(unsigned)seq, s.fpLoaded, loaded, (unsigned)avatar->mProfileSeq);
			s.change = true;
		}
		s.fpValid = true;
		s.fpProfile = profile;
		s.fpHash = hash;
		s.fpSeq = seq;
		s.fpLoaded = loaded;
	}

	// Outside a co-op launch the avatar is the player's own creature: remember its key for the next co-op launch.
	void RememberOwn(State& s, Simulator::cCreatureAnimal* avatar)
	{
		Key3 key = AvatarKey(avatar);
		if (s.edited[0]) {
			key = s.edited;
		}
		if (!key[0] || key == s.written) {
			return;
		}
		Key3 old = { 0, 0, 0 };
		if (ReadOwnFile(old, nullptr, 0) && old == key) {
			s.written = key;
			return;
		}
		WriteOwnFile(s, key, "avatar");
	}

	// In the main menu of the own world, when no file exists yet: the species of the newest creature-stage save (a guess that the
	// star's citizen species is the player's species).
	void RememberFromMenu(State& s)
	{
		if (s.menuTried) {
			return;
		}
		Key3 old;
		if (ReadOwnFile(old, nullptr, 0)) {
			s.menuTried = true;
			return;
		}
		uint32_t starId = 0;
		std::string planet;
		if (!MenuWorlds::HostPlanet(starId, planet)) {
			return;  // the list may still be empty: try again
		}
		s.menuTried = true;
		Simulator::cStarManager* stars = Simulator::cStarManager::Get();
		if (!stars) {
			return;
		}
		for (const cStarRecordPtr& star : stars->mSavedGameStarRecords) {
			if (star && star->mKey.internalValue == starId) {
				const ResourceKey& k = star->mCitizenSpeciesKey;
				Logf("own: newest creature save (%s): citizen species %08x/%08x/%08x", planet.c_str(), k.instanceID, k.typeID, k.groupID);
				if (k.instanceID) {
					WriteOwnFile(s, Key3{ k.instanceID, k.typeID, k.groupID }, "save-star");
				}
				return;
			}
		}
	}

	// In a co-op launch: the guest plays his own creature. ModAPI has no SetSpecies (SporeCoop calls the game's own); we set the
	// species fields of the avatar and reload its model (guess, never tried before 2026-10-31).
	void PlayOwn(State& s, Simulator::cCreatureAnimal* avatar, DWORD now)
	{
		if (s.swapped == avatar) {
			return;
		}
		s.swapped = avatar;
		if (getenv("COOP_NO_OWN_CREATURE")) {
			Logf("own: COOP_NO_OWN_CREATURE is set, the guest plays the host's creature");
			return;
		}
		Key3 key;
		char source[32] = {};
		if (!ReadOwnFile(key, source, sizeof(source))) {
			Logf("own: no own creature known (%s missing): play your own creature stage once with this build", OwnFile().c_str());
			return;
		}
		const Key3 before = AvatarKey(avatar);
		const Key3 species = { avatar->mSpeciesKey.instanceID, avatar->mSpeciesKey.typeID, avatar->mSpeciesKey.groupID };
		if ((key[0] == before[0] && key[2] == before[2]) || (key[0] == species[0] && key[2] == species[2])) {
			Logf("own: our own creature %08x is already the avatar's species, nothing to switch", key[0]);
			return;
		}
		const ResourceKey own(key[0], key[1], key[2]);
		Simulator::cSpeciesProfile* profile = SpeciesManager.GetSpeciesProfile(own);
		if (!profile) {
			Logf("own: our own creature %08x/%08x/%08x (from %s) is not buildable here, the guest plays the host's creature", key[0], key[1], key[2],
				source);
			return;
		}
		const ResourceKey model = avatar->GetModelKey();
		Logf("own: switching our avatar to our own creature %08x/%08x/%08x (from %s): profile %p -> %p, species %08x, model key %08x/%08x/%08x, "
			"profile sequence %u -> %u", key[0], key[1], key[2], source, (void*)avatar->mpSpeciesProfile, (void*)profile, species[0], model.instanceID,
			model.typeID, model.groupID, (unsigned)avatar->mProfileSeq, (unsigned)profile->creatureSeq);
		avatar->mpSpeciesProfile = profile;
		avatar->mSpeciesKey = own;
		avatar->mProfileSeq = profile->creatureSeq;
		if ((model.instanceID == before[0] && model.groupID == before[2]) || (model.instanceID == species[0] && model.groupID == species[2])) {
			avatar->SetModelKey(own);
			Logf("own: model key was the species key: SetModelKey(%08x)", key[0]);
		}
		else {
			avatar->GrowUp();
			Logf("own: model key is not the species key: GrowUp() to reload the model");
		}
		s.verify = avatar;
		s.verifyAt = now + 2000;
		s.change = true;
	}

	void Verify(State& s, Simulator::cCreatureAnimal* avatar, DWORD now)
	{
		if (!s.verify || now < s.verifyAt) {
			return;
		}
		if (s.verify == avatar) {
			const ResourceKey model = avatar->GetModelKey();
			Logf("own: 2 s after the switch: species %08x, profile %p, model key %08x/%08x/%08x, has model %d, health %.1f/%.1f",
				avatar->mSpeciesKey.instanceID, (void*)avatar->mpSpeciesProfile, model.instanceID, model.typeID, model.groupID,
				avatar->GetModel() ? 1 : 0, avatar->mHealthPoints, avatar->mMaxHealthPoints);
		}
		s.verify = nullptr;
	}
}

namespace OwnCreature
{
	void Update(bool inGame)
	{
		State& s = S();
		const DWORD now = GetTickCount();
		WatchEditor(s, now);
		if (!inGame) {
			if (!CoopLaunch::Active() && GameModeManager.GetActiveModeID() == (uint32_t)GameModeIDs::kGGEMode) {
				RememberFromMenu(s);
			}
			return;
		}
		auto avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			return;
		}
		if (avatar != s.avatar) {
			s.avatar = avatar;
			s.avatarSince = now;
			s.fpValid = false;
		}
		if (now - s.lastCheck < kCheckEveryMs) {
			return;
		}
		s.lastCheck = now;
		WatchProfile(s, avatar);
		Verify(s, avatar, now);
		if (now - s.avatarSince < kSettleMs) {
			return;
		}
		if (CoopLaunch::Active()) {
			PlayOwn(s, avatar, now);
		}
		else {
			RememberOwn(s, avatar);
		}
	}

	bool TakeChange()
	{
		if (!sS || !sS->change) {
			return false;
		}
		sS->change = false;
		return true;
	}

	bool EditedKey(uint32_t* key)
	{
		if (!sS || !sS->edited[0]) {
			return false;
		}
		key[0] = sS->edited[0]; key[1] = sS->edited[1]; key[2] = sS->edited[2];
		return true;
	}

	bool ReadFresh(uint32_t instance, uint32_t type, uint32_t group, std::vector<Models::Record>& out)
	{
		State& s = S();
		out.clear();
		eastl::vector<uint32_t> types;
		ResourceManager.GetRecordTypesFromResourceType(types, type);
		if (types.empty()) {
			types.push_back(type);
		}
		uint32_t sig = 2166136261u;
		std::string copies;
		int copyCount = 0;
		for (uint32_t recordType : types) {
			const ResourceKey key(instance, recordType, group);
			Resource::Database* top = ResourceManager.FindRecord(key);
			Resource::IResourceManager::DatabaseList dbs;
			ResourceManager.GetDatabaseList(dbs, &key);
			std::vector<uint8_t> chosen, fromTop;
			bool chosenFromEditorSaves = false;
			for (Resource::Database* db : dbs) {
				std::vector<uint8_t> data;
				if (!ReadRecord(db, key, data)) {
					continue;
				}
				const uint32_t h = Fnv(2166136261u, data.data(), data.size());
				const bool editorSaves = IsEditorSaves(db);
				char part[300];
				snprintf(part, sizeof(part), " [%08x %s: %d bytes, hash %08x%s]", recordType, Narrow(db->GetLocation()).c_str(), (int)data.size(), h,
					db == top ? ", FindRecord's" : "");
				copies += part;
				++copyCount;
				sig = Fnv(sig, (const uint8_t*)part, strlen(part));
				if (db == top) {
					fromTop = data;
				}
				if (editorSaves && !chosenFromEditorSaves) {
					chosen.swap(data);
					chosenFromEditorSaves = true;
				}
			}
			if (!chosenFromEditorSaves) {
				chosen.swap(fromTop);
			}
			if (chosen.empty() && top) {
				ReadRecord(top, key, chosen);  // GetDatabaseList found nothing: as Models::Read does it
			}
			if (!chosen.empty()) {
				Models::Record r;
				r.type = recordType; r.group = group; r.instance = instance;
				r.data.swap(chosen);
				out.push_back(std::move(r));
			}
		}
		auto& last = s.copiesSig[instance];
		if (last != sig) {
			last = sig;
			Logf("own: body %08x/%08x/%08x: %d copies%s", instance, type, group, copyCount, copies.c_str());
		}
		return !out.empty();
	}

	void Shutdown()
	{
		delete sS;
		sS = nullptr;
	}
}
