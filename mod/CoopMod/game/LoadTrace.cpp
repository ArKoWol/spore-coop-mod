// LoadTrace.cpp : see LoadTrace.h.
#include "stdafx.h"
#include "LoadTrace.h"
#include "../Log.h"

#include <Spore\Simulator\SubSystem\StarManager.h>
#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\cPlanetRecord.h>
#include <Spore\Resource\cResourceManager.h>

using namespace Simulator;

namespace
{
	bool sOn = false;
	DWORD sStopAt = 0;      // GetTickCount() when the trace ends (0 = not scheduled)
	int sLines = 0;         // a cap, the load touches thousands of records
	const int kMaxLines = 600;
	bool sDbHooked = false;
	Resource::Database* sPlanetDb = nullptr;
	Resource::Database* sPlanetTempDb = nullptr;

	bool Active()
	{
		if (sOn && sStopAt && (int)(GetTickCount() - sStopAt) > 0) {
			sOn = false;
			Logf("load: trace ended (%d lines)", sLines);
		}
		return sOn && sLines < kMaxLines;
	}

	const char* DbName(const Resource::Database* db)
	{
		return db == sPlanetDb ? "planets.pkp" : db == sPlanetTempDb ? "planets.pkt" : nullptr;
	}
}

// every record the game opens in the two planet databases
member_detour(OpenRecord_detour, Resource::Database,
	bool(const ResourceKey&, Resource::IRecord**, IO::AccessFlags, IO::CD, bool, Resource::RecordInfo*))
{
	bool detoured(const ResourceKey& key, Resource::IRecord** dst, IO::AccessFlags access, IO::CD cd, bool arg, Resource::RecordInfo* info)
	{
		bool ok = original_function(this, key, dst, access, cd, arg, info);
		const char* name = DbName(this);
		// the temporary planet database is asked for every resource (it sits in front of the others): only planet records
		if (name && key.typeID == 0x05220CB8 && Active()) {
			++sLines;
			Logf("load: %s OpenRecord %08x!%08x.%08x access %d -> %d", name, key.groupID, key.instanceID, key.typeID, (int)access, ok ? 1 : 0);
		}
		return ok;
	}
};

// resources of the planet record type (cPlanetRecord::TYPE) and the star record type
member_detour(GetResource_detour, Resource::cResourceManager,
	bool(const ResourceKey&, ResourceObjectPtr*, void*, Resource::Database*, Resource::IResourceFactory*, const ResourceKey*))
{
	bool detoured(const ResourceKey& key, ResourceObjectPtr* dst, void* data, Resource::Database* db, Resource::IResourceFactory* factory,
		const ResourceKey* cacheName)
	{
		bool ok = original_function(this, key, dst, data, db, factory, cacheName);
		if (Active() && (key.typeID == cPlanetRecord::TYPE || key.typeID == cStarRecord::TYPE)) {
			++sLines;
			Logf("load: GetResource %08x!%08x.%08x db %s -> %d (%p)", key.groupID, key.instanceID, key.typeID,
				db ? (DbName(db) ? DbName(db) : "other") : "any", ok ? 1 : 0, dst ? (void*)dst->get() : nullptr);
		}
		return ok;
	}
};

member_detour(StarGetPlanetRecord_detour, cStarManager, cPlanetRecord*(PlanetID))
{
	cPlanetRecord* detoured(PlanetID id)
	{
		cPlanetRecord* found = original_function(this, id);
		if (Active()) {
			++sLines;
			Logf("load: StarManager.GetPlanetRecord(%08x) -> %p", id.internalValue, (void*)found);
		}
		return found;
	}
};

member_detour(RecordGetPlanets_detour, cStarRecord, eastl::vector<cPlanetRecordPtr>&())
{
	eastl::vector<cPlanetRecordPtr>& detoured()
	{
		eastl::vector<cPlanetRecordPtr>& planets = original_function(this);
		if (Active()) {
			++sLines;
			Logf("load: star %08x '%ls'.GetPlanetRecords -> %d (count %d, this %p)", this->mKey.internalValue,
				(const wchar_t*)this->mName.c_str(), (int)planets.size(), (int)this->mPlanetCount, (void*)this);
		}
		return planets;
	}
};

member_detour(RecordToPlanet_detour, cStarManager, void(cPlanetRecord*, cPlanetPtr&))
{
	void detoured(cPlanetRecord* record, cPlanetPtr& dst)
	{
		original_function(this, record, dst);
		if (Active()) {
			++sLines;
			Logf("load: RecordToPlanet(%08x, %p) -> planet %p", record ? record->mKey.internalValue : 0, (void*)record, (void*)dst.get());
		}
	}
};

// cPlanet::Read (0xC72D20 in the March 2017 exe, found by disassembly): reads a planet from the save, then takes its
// PlanetID (+0x140) and asks StarManager for the record (stored at +0x13C). A null record crashes the load later.
struct PlanetObject {};
member_detour(PlanetRead_detour, PlanetObject, bool(void*))
{
	bool detoured(void* stream)
	{
		bool ok = original_function(this, stream);
		if (Active()) {
			++sLines;
			const uint8_t* self = (const uint8_t*)this;
			Logf("load: cPlanet::Read(%p) -> %d, planet id %08x, record %p", (void*)this, ok ? 1 : 0, *(const uint32_t*)(self + 0x140),
				*(void* const*)(self + 0x13C));
		}
		return ok;
	}
};

// the planet behind a visible planet noun (cVisiblePlanet::mpPlanet at +0x108): its id and record
void DescribeVisible(const char* what, const void* visible)
{
	__try {
		const uint8_t* planet = visible ? *(const uint8_t* const*)((const uint8_t*)visible + 0x108) : nullptr;
		if (planet) {
			Logf("load: %s visible %p planet %p id %08x record %p", what, visible, (const void*)planet, *(const uint32_t*)(planet + 0x140),
				*(void* const*)(planet + 0x13C));
		}
		else {
			Logf("load: %s visible %p planet null", what, visible);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		Logf("load: %s visible %p (unreadable)", what, visible);
	}
}

// 0xE984E0: makes the view (cPlanetGfx) of a visible planet; the crash is inside it when the planet has no record
member_detour(PlanetGfx_detour, PlanetObject, void*(void*))
{
	void* detoured(void* visible)
	{
		if (Active()) {
			++sLines;
			DescribeVisible("planet view for", visible);
		}
		return original_function(this, visible);
	}
};

namespace LoadTrace
{
	bool sAttached = false;

	void Attach()
	{
		if (!getenv("COOP_LOAD_TRACE")) {
			return;  // research tool, off by default: COOP_LOAD_TRACE=1
		}
		sAttached = true;
		PlanetGfx_detour::attach(Address(0xE984E0));
		PlanetRead_detour::attach(Address(0xC72D20));
		GetResource_detour::attach(GetAddress(Resource::cResourceManager, GetResource));
		StarGetPlanetRecord_detour::attach(GetAddress(cStarManager, GetPlanetRecord));
		RecordGetPlanets_detour::attach(GetAddress(cStarRecord, GetPlanetRecords));
		RecordToPlanet_detour::attach(GetAddress(cStarManager, RecordToPlanet));
	}

	void Detach()
	{
		if (!sAttached) {
			return;
		}
		sAttached = false;
		PlanetGfx_detour::detach();
		PlanetRead_detour::detach();
		GetResource_detour::detach();
		StarGetPlanetRecord_detour::detach();
		RecordGetPlanets_detour::detach();
		RecordToPlanet_detour::detach();
		if (sDbHooked) {
			OpenRecord_detour::detach();
			sDbHooked = false;
		}
	}

	void HookDatabases(HMODULE module)
	{
		cStarManager* stars = cStarManager::Get();
		if (!sAttached || sDbHooked || !stars || !stars->mpPlanetRecordsDatabase) {
			return;
		}
		sPlanetDb = stars->mpPlanetRecordsDatabase.get();
		sPlanetTempDb = stars->mpPlanetRecordsTempDatabase.get();
		void** vtable = *(void***)sPlanetDb;
		uintptr_t address = (uintptr_t)vtable[0x34 / 4];  // OpenRecord
		PrepareDetours(module);
		OpenRecord_detour::attach(address);
		sDbHooked = CommitDetours() == NO_ERROR;
		Logf("load: planet databases %p / %p, OpenRecord at %08x hooked: %s", (void*)sPlanetDb, (void*)sPlanetTempDb,
			(unsigned)address, sDbHooked ? "yes" : "NO");
	}

	void Begin(const char* what)
	{
		if (!sAttached) {
			return;
		}
		sOn = true;
		sStopAt = 0;
		sLines = 0;
		cStarManager* stars = cStarManager::Get();
		sPlanetDb = stars ? stars->mpPlanetRecordsDatabase.get() : nullptr;
		sPlanetTempDb = stars ? stars->mpPlanetRecordsTempDatabase.get() : nullptr;
		Logf("load: trace begins (%s)", what);
	}

	void OnMode(const char* name)
	{
		if (sOn && name && strncmp(name, "Game_", 5) == 0 && !sStopAt) {
			sStopAt = GetTickCount() + 4000;
			Logf("load: entered %s, the trace ends in 4 s", name);
		}
	}
}
