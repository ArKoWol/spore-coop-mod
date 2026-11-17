// OwnCreature.cpp : see OwnCreature.h.
#include "stdafx.h"
#include "OwnCreature.h"
#include "CoopLaunch.h"
#include "CoopNet.h"
#include "../Log.h"
#include "../../../core/coop_players.h"

#include <Spore\Editors\Editor.h>
#include <Spore\Editors\cEditorResource.h>
#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\cCreatureGameData.h>

#include <algorithm>
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
		std::vector<coop::players::Entry> sidecar;  // host: Game0\coop_players.json as last read or written
		bool sidecarLoaded = false;
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

	// the world this game plays: on the host his own Game0, in a co-op launch the host's world (with its sidecar)
	std::string WorldDir()
	{
		return GamesDir() + "\\Game0";
	}

	// In a co-op launch: the creature the host keeps for us in his world (D26). False = none (first join) or a damaged file.
	bool SidecarCreature(unsigned long long myId, Key3& key)
	{
		std::vector<coop::players::Entry> entries;
		if (!coop::players::Load(WorldDir(), entries)) {
			Logf("own: %s\\%s is damaged: no own creature from it", WorldDir().c_str(), coop::players::kFileName);
			return false;
		}
		const coop::players::Entry* e = coop::players::Find(entries, myId);
		if (!e || !e->creature[0]) {
			return false;
		}
		key = { e->creature[0], e->creature[1], e->creature[2] };
		return true;
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

	// The game's own rebuild of the ability buttons (exe 0xD49400, March 2017; FACTS.md section 7): clears the button map
	// (Simulator::cAbilityUIMap, its pointer at 0x169E3EC), puts every ability of the player's creature on the button of its type
	// and redraws the icons (0xD48CE0). The game calls it after the creature editor. No arguments.
	const uintptr_t kRebuildAbilityButtons = 0xD49400;
	const uintptr_t kAbilityUIMapPtr = 0x169E3EC;

	bool RebuildAbilityButtons()
	{
		const uint8_t* code = (const uint8_t*)Address(kRebuildAbilityButtons);
		__try {
			// mov ecx, [map pointer] ; xor eax, eax - the pointer is relocated like the code (ASLR)
			uint32_t operand;
			memcpy(&operand, code + 2, 4);
			if (code[0] != 0x8B || code[1] != 0x0D || operand != (uint32_t)Address(kAbilityUIMapPtr) || code[6] != 0x33 || code[7] != 0xC0) {
				Logf("own: the ability button rebuild is not at %08x in this exe: buttons stay the old creature's", (unsigned)kRebuildAbilityButtons);
				return false;
			}
			if (!*(void* const*)Address(kAbilityUIMapPtr)) {
				Logf("own: no ability button map yet: buttons not rebuilt");
				return false;
			}
			((void(*)())Address(kRebuildAbilityButtons))();
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			Logf("own: the ability button rebuild failed (exception)");
			return false;
		}
		return true;
	}

	// cSpeciesProfile: loads a creature into this profile, as the game does for the player's species when the editor closes: the
	// creature key, the abilities and the stats change, the species key stays (exe 0x4D4F70, thiscall, the key by pointer, returns
	// false if there is no such creature; FACTS.md section 7).
	const uintptr_t kProfileLoad = 0x4D4F70;
	const uint8_t kProfileLoadStart[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x89, 0x4D, 0xE8 };

	bool LoadIntoProfile(Simulator::cSpeciesProfile* profile, const ResourceKey* key)
	{
		__try {
			if (memcmp((const void*)Address(kProfileLoad), kProfileLoadStart, sizeof(kProfileLoadStart)) != 0) {
				Logf("own: the profile load is not at %08x in this exe", (unsigned)kProfileLoad);
				return false;
			}
			return ((bool(__thiscall*)(void*, const ResourceKey*))Address(kProfileLoad))(profile, key);
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			Logf("own: the profile load failed (exception)");
			return false;
		}
	}

	// the ability of this type in the creature's list, else the first of this category; -1 if none
	int AbilityLike(Simulator::cCreatureAnimal* creature, int type, int category)
	{
		int byCategory = -1;
		for (int i = 0; i < creature->GetAbilitiesCount(); ++i) {
			Simulator::cCreatureAbility* a = creature->GetAbility(i);
			if (a && (int)a->mType == type) {
				return i;
			}
			if (a && byCategory < 0 && a->mCategory == category) {
				byCategory = i;
			}
		}
		return byCategory;
	}

	// In a co-op launch: the guest plays his own creature. Done the way the game does it when the editor closes: our creature is loaded
	// into the profile of the player's species, so the game still sees its own player, with his family, nest and mates (a switch to
	// another species left the guest without a mate and with the wrong buttons, 2-PC test 2026-11-08). The models are reloaded and the
	// ability buttons rebuilt; a respawn then builds our creature by itself.
	void PlayOwn(State& s, Simulator::cCreatureAnimal* avatar, DWORD now)
	{
		if (s.swapped == avatar) {
			return;
		}
		if (getenv("COOP_NO_OWN_CREATURE")) {
			s.swapped = avatar;
			Logf("own: COOP_NO_OWN_CREATURE is set, the guest plays the host's creature");
			return;
		}
		const unsigned long long myId = CoopNet::MyId();
		if (!myId) {
			return;  // the transport has no id for us yet: try again at the next check
		}
		s.swapped = avatar;
		Key3 key;
		const char* source = "the host's world";
		if (!SidecarCreature(myId, key)) {
			Logf("own: the host's world keeps no creature for us (first join): we play the host's creature until our first editor "
				"visit (D26)");
			return;
		}
		const Key3 before = AvatarKey(avatar);
		if (key[0] == before[0] && key[2] == before[2]) {
			Logf("own: our own creature %08x is already the avatar's creature, nothing to load", key[0]);
			return;
		}
		Simulator::cSpeciesProfile* profile = avatar->mpSpeciesProfile;
		const ResourceKey own(key[0], key[1], key[2]);
		if (!profile || !SpeciesManager.GetSpeciesProfile(own)) {
			Logf("own: our own creature %08x/%08x/%08x (from %s) is not buildable here, the guest plays the host's creature", key[0], key[1], key[2], source);
			return;
		}
		// the default abilities are indexes into the list that is about to change: keep what they are
		Simulator::cCreatureAbility* attack = avatar->mDefaultAttackAbilityIndex >= 0 ? avatar->GetAbility(avatar->mDefaultAttackAbilityIndex) : nullptr;
		Simulator::cCreatureAbility* social = avatar->mDefaultSocializeAbilityIndex >= 0 ? avatar->GetAbility(avatar->mDefaultSocializeAbilityIndex) : nullptr;
		const int attackType = attack ? (int)attack->mType : -1, attackCategory = attack ? attack->mCategory : -1;
		const int socialType = social ? (int)social->mType : -1, socialCategory = social ? social->mCategory : -1;
		const uint32_t seqBefore = profile->creatureSeq;
		if (!LoadIntoProfile(profile, &own)) {
			Logf("own: our own creature %08x could not be loaded into the player's profile, the guest plays the host's creature", key[0]);
			return;
		}
		const int newAttack = AbilityLike(avatar, attackType, attackCategory), newSocial = AbilityLike(avatar, socialType, socialCategory);
		Logf("own: our own creature %08x/%08x/%08x (from %s) loaded into the player's profile %p: creature %08x -> %08x, species %08x stays, sequence %u -> %u, "
			"%d abilities, default attack %d -> %d, socialize %d -> %d", key[0], key[1], key[2], source, (void*)profile, before[0], profile->mCreatureKey.instanceID,
			avatar->mSpeciesKey.instanceID, (unsigned)seqBefore, (unsigned)profile->creatureSeq, avatar->GetAbilitiesCount(), avatar->mDefaultAttackAbilityIndex, newAttack,
			avatar->mDefaultSocializeAbilityIndex, newSocial);
		// everybody of the player's species that is here now: the avatar and its family
		int family = 0;
		auto reload = [&](Simulator::cCreatureAnimal* creature) {
			if (newAttack >= 0) {
				creature->mDefaultAttackAbilityIndex = newAttack;
			}
			if (newSocial >= 0) {
				creature->mDefaultSocializeAbilityIndex = newSocial;
			}
			creature->mCurrentAttackIdx = (uint32_t)-1;
			creature->mProfileSeq = profile->creatureSeq;
			creature->GrowUp();
		};
		reload(avatar);
		if (Simulator::cHerd* herd = avatar->mHerd.get()) {
			for (auto& member : herd->mHerd) {
				if (member && member.get() != avatar) {
					reload(member.get());
					++family;
				}
			}
		}
		const bool buttons = RebuildAbilityButtons();
		Logf("own: %d of our family reloaded, ability buttons %s", family, buttons ? "rebuilt" : "NOT rebuilt");
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
	void DumpAbilities()
	{
		auto avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			Logf("own: abilities: no avatar (creature stage only)");
			return;
		}
		int count = avatar->GetAbilitiesCount();
		Logf("own: abilities of our avatar (species %08x, profile %p): %d, default attack %d, default socialize %d, current attack %d, "
			"stance %s", avatar->mSpeciesKey.instanceID, (void*)avatar->mpSpeciesProfile, count, avatar->mDefaultAttackAbilityIndex,
			avatar->mDefaultSocializeAbilityIndex, (int)avatar->mCurrentAttackIdx,
			Simulator::cCreatureGameData::GetAbilityMode() == Simulator::cCreatureGameData::AbilityMode::Attack ? "attack" : "social");
		for (int i = 0; i < count && i < 32; ++i) {
			Simulator::cCreatureAbility* a = avatar->GetAbility(i);
			if (a) {
				Logf("own: ab %2d: type %d, category %d, hot key %d", i, (int)a->mType, a->mCategory, a->mVerbIconTriggerKey);
			}
		}
	}

	void Update(bool inGame)
	{
		State& s = S();
		const DWORD now = GetTickCount();
		WatchEditor(s, now);
		if (!inGame) {
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
	}

	void OnFriendCreature(unsigned long long guestId, const uint32_t key[3], const uint32_t hostOwn[3])
	{
		if (!guestId || !key[0] || !hostOwn[0]) {
			return;  // without our own key we cannot tell his creature from ours
		}
		if (key[0] == hostOwn[0] && key[2] == hostOwn[2]) {
			return;  // the guest plays our creature (first join, or his own is not loaded yet): nothing of his to keep
		}
		State& s = S();
		const std::string dir = WorldDir();
		if (!s.sidecarLoaded) {
			if (!coop::players::Load(dir, s.sidecar)) {
				Logf("own: %s\\%s is damaged: it is written again, with the guest's creature only", dir.c_str(), coop::players::kFileName);
				s.sidecar.clear();
			}
			s.sidecarLoaded = true;
		}
		const uint32_t k[3] = { key[0], key[1], key[2] };
		if (!coop::players::SetCreature(s.sidecar, guestId, k)) {
			return;
		}
		if (coop::players::Save(dir, s.sidecar)) {
			Logf("own: the guest's creature %08x/%08x/%08x is kept in our world (%s): he gets it back at his next join", k[0], k[1], k[2],
				coop::players::kFileName);
		}
		else {
			Logf("own: cannot write %s\\%s: the guest's creature is not kept", dir.c_str(), coop::players::kFileName);
			s.sidecarLoaded = false;
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
