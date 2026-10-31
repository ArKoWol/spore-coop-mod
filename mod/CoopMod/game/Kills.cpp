// Kills.cpp : see Kills.h. Addresses are of the March 2017 exe (the GOG build, FACTS.md section 1), found by disassembly.
#include "stdafx.h"
#include "Kills.h"
#include "../World.h"
#include "../Log.h"

#include <cstdio>
#include <cstring>

namespace
{
	// cSpeciesRelationshipManager::ApplyEvent(event, key*, creature), thiscall, ret 0xC (FACTS.md section 9). Events 0 and 5
	// raise the species' kill count; at the extinction threshold it calls the extinction 0xBA4750 with the dead creature.
	const uintptr_t kApplyEvent = 0xBA4A00;
	const uint8_t kApplyEventStart[] = { 0x51, 0x53, 0x8B, 0x5C, 0x24, 0x10 };
	// cSpeciesManager: the herd type entry for (type, index) from its table at +0xB0, null when the type is unknown. The
	// extinction calls it with the herd's mArchetype and mGeneration and reads +0x14 of the result without a null check.
	const uintptr_t kHerdTypeLookup = 0x4E00C0;
	const uint8_t kHerdTypeLookupStart[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB4, 0x00, 0x00, 0x00 };
	typedef void* (__fastcall* HerdTypeLookup_t)(void* self, void* unusedEdx, uint32_t type, uint32_t index);

	bool sAttached = false;
	int sUsable = -1;  // -1 not checked, 0 the code at the addresses is not what we expect (another exe), 1 fine
	int sLogged = 0;
	const int kMaxLogs = 30;

	// the herd types last copied, per species profile (plain arrays: no static destructors, FACTS.md section 3)
	struct Donor { void* profile; uint32_t type, group; };
	Donor sDonors[32];
	int sDonorCount = 0;

	bool CodeMatches(uintptr_t address, const uint8_t* bytes, size_t size)
	{
		__try {
			return memcmp((const void*)address, bytes, size) == 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool Usable()
	{
		if (sUsable < 0) {
			sUsable = CodeMatches(kHerdTypeLookup, kHerdTypeLookupStart, sizeof(kHerdTypeLookupStart)) ? 1 : 0;
			if (!sUsable) {
				Logf("kills: the herd type lookup is not at %08x in this exe: native kills of mirrors stay off", (unsigned)kHerdTypeLookup);
			}
		}
		return sUsable == 1;
	}

	bool TypeKnown(uint32_t type, uint32_t index)
	{
		void* manager = Editors::cSpeciesManager::Get();
		if (!manager) {
			return false;
		}
		__try {
			return ((HerdTypeLookup_t)kHerdTypeLookup)(manager, nullptr, type, index) != nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	// A herd the game made whose type is known: one of `profile` if there is one, else any.
	bool FindDonor(Simulator::cHerd* target, void* profile, uint32_t* type, uint32_t* group, bool* sameSpecies)
	{
		for (int i = 0; i < sDonorCount; ++i) {
			if (sDonors[i].profile == profile && TypeKnown(sDonors[i].type, 0)) {
				*type = sDonors[i].type;
				*group = sDonors[i].group;
				*sameSpecies = true;
				return true;
			}
		}
		bool found = false;
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() != Simulator::kHerd) {
				continue;
			}
			auto herd = object_cast<Simulator::cHerd>(object);
			if (!herd || herd == target || !TypeKnown(herd->mArchetype, 0)) {
				continue;
			}
			bool same = profile && (void*)herd->mpSpeciesProfile == profile;
			if (!found || same) {
				found = true;
				*type = herd->mArchetype;
				*group = herd->mArchetypeGroup;
				*sameSpecies = same;
			}
			if (same) {
				break;
			}
		}
		if (found && *sameSpecies && profile) {
			Donor& d = sDonors[sDonorCount < 32 ? sDonorCount++ : 31];
			d.profile = profile;
			d.type = *type;
			d.group = *group;
		}
		return found;
	}
}

// The species relation events (attacked, killed, impressed, ...): World decides which may reach the game's table.
struct RelationManagerObject {};
member_detour(ApplyEvent_detour, RelationManagerObject, void(int, void*, void*))
{
	void detoured(int event, void* key, void* creature)
	{
		if (!World::AllowRelationEvent(creature, event)) {
			return;
		}
		original_function(this, event, key, creature);
	}
};

namespace Kills
{
	void Attach()
	{
		if (!CodeMatches(kApplyEvent, kApplyEventStart, sizeof(kApplyEventStart))) {
			Logf("kills: ApplyEvent is not at %08x in this exe: relation events are not filtered", (unsigned)kApplyEvent);
			return;
		}
		sAttached = true;
		ApplyEvent_detour::attach(Address(kApplyEvent));
	}

	void Detach()
	{
		if (sAttached) {
			sAttached = false;
			ApplyEvent_detour::detach();
		}
	}

	bool EnsureHerdType(void* herdPtr, const char* why)
	{
		auto herd = static_cast<Simulator::cHerd*>(herdPtr);
		if (!herd || !Usable()) {
			return false;
		}
		if (TypeKnown(herd->mArchetype, (uint32_t)herd->mGeneration)) {
			return true;
		}
		uint32_t type = 0, group = 0;
		bool same = false;
		void* profile = herd->mpSpeciesProfile;
		if (!FindDonor(herd, profile, &type, &group, &same)) {
			if (sLogged++ < kMaxLogs) {
				Logf("kills: herd %p has type %08x/%08x (generation %d) the game does not know, and no herd of the game to copy one from (%s)",
					herdPtr, herd->mArchetype, herd->mArchetypeGroup, herd->mGeneration, why);
			}
			return false;
		}
		if (sLogged++ < kMaxLogs) {
			Logf("kills: herd %p had type %08x/%08x (generation %d) the game does not know: took %08x/%08x from a herd of %s (%s)", herdPtr,
				herd->mArchetype, herd->mArchetypeGroup, herd->mGeneration, type, group, same ? "the same species" : "another species", why);
		}
		herd->mArchetype = type;
		herd->mArchetypeGroup = group;
		return TypeKnown(herd->mArchetype, (uint32_t)herd->mGeneration);
	}

	bool SelfCheck(char* detail, size_t size)
	{
		auto avatar = GameNounManager.GetAvatar();
		if (!avatar) {
			snprintf(detail, size, "needs the creature stage");
			return false;
		}
		if (!Usable()) {
			snprintf(detail, size, "the herd type lookup is not at %08x in this exe", (unsigned)kHerdTypeLookup);
			return false;
		}
		int known = 0, unknown = 0;
		for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
			Simulator::cGameData* object = &*it;
			if (object->GetNounID() == Simulator::kHerd) {
				auto herd = object_cast<Simulator::cHerd>(object);
				if (herd) {
					TypeKnown(herd->mArchetype, 0) ? ++known : ++unknown;
				}
			}
		}
		Simulator::cSpeciesProfile* profile = avatar->mpSpeciesProfile;
		if (!profile) {
			profile = SpeciesManager.GetSpeciesProfile(avatar->mSpeciesKey);
		}
		if (!profile) {
			snprintf(detail, size, "our avatar has no species profile");
			return false;
		}
		// made exactly like a mirror's herd (World.cpp MakeMirror), then removed again
		cHerdPtr herd = GameNounManager.CreateHerd(avatar->GetPosition(), profile, 0, false, 0, false);
		if (!herd) {
			snprintf(detail, size, "CreateHerd failed");
			return false;
		}
		herd->mTargetHerdSize = 0;
		herd->SetEnabled(false);
		uint32_t typeBefore = herd->mArchetype;
		int generation = herd->mGeneration;
		bool knownBefore = TypeKnown(herd->mArchetype, (uint32_t)herd->mGeneration);
		bool ok = EnsureHerdType(herd.get(), "self check");
		uint32_t typeAfter = herd->mArchetype;
		GameNounManager.DestroyInstance(herd.get());
		snprintf(detail, size, "herds of the game: %d of known type, %d unknown; a mirror-like herd had type %08x (generation %d), %s; after "
			"the repair %08x, %s", known, unknown, typeBefore, generation, knownBefore ? "known" : "UNKNOWN (the crash cause)", typeAfter,
			ok ? "known" : "still unknown");
		Logf("kills: self check: %s", detail);
		return ok;
	}
}
