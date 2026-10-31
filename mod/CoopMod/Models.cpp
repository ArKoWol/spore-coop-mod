// Models.cpp : see Models.h.
#include "stdafx.h"
#include "Models.h"
#include "Log.h"

#include <Spore/Resource/DatabasePackedFile.h>

#include <string>
#include <vector>

namespace
{
	std::vector<Resource::Database*>* sRegistered = nullptr;  // heap object, freed in Shutdown (no static destructor)
	bool sCachedDone = false;

	std::wstring ModelsDir()
	{
		wchar_t exe[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exe, MAX_PATH);
		std::wstring dir = exe;
		dir.erase(dir.find_last_of(L'\\'));
		return dir + L"\\CoopModels";
	}

	bool Register(const std::wstring& path)
	{
		auto db = new Resource::DatabasePackedFile((const char16_t*)path.c_str());
		db->AddRef();
		if (!db->Initialize() || !db->Open(IO::AccessFlags::Read, IO::CD::Default, false)) {
			Logf("models: cannot open %ls", path.c_str());
			db->Release();
			return false;
		}
		// priority 1000 is where the game puts My Creations; ours only adds keys the game does not know
		ResourceManager.RegisterDatabase(true, db, 1000);
		if (!sRegistered) {
			sRegistered = new std::vector<Resource::Database*>();
		}
		sRegistered->push_back(db);
		return true;
	}

	void Put32(std::vector<uint8_t>& out, size_t at, uint32_t v)
	{
		for (int i = 0; i < 4; ++i) {
			out[at + i] = (uint8_t)(v >> (8 * i));
		}
	}

	void Push32(std::vector<uint8_t>& out, uint32_t v)
	{
		for (int i = 0; i < 4; ++i) {
			out.push_back((uint8_t)(v >> (8 * i)));
		}
	}

	// Same layout as make_assets.ps1: DBPF 3.0, uncompressed entries, index flags 4.
	std::vector<uint8_t> BuildPackage(const std::vector<Models::Record>& records)
	{
		std::vector<uint8_t> out(96, 0);
		std::vector<uint32_t> offsets;
		for (auto& r : records) {
			offsets.push_back((uint32_t)out.size());
			out.insert(out.end(), r.data.begin(), r.data.end());
		}
		uint32_t indexOffset = (uint32_t)out.size();
		Push32(out, 4);
		Push32(out, 0);
		for (size_t i = 0; i < records.size(); ++i) {
			uint32_t size = (uint32_t)records[i].data.size();
			Push32(out, records[i].type);
			Push32(out, records[i].group);
			Push32(out, records[i].instance);
			Push32(out, offsets[i]);
			Push32(out, size | 0x80000000u);
			Push32(out, size);
			out.push_back(0); out.push_back(0);  // compression: none
			out.push_back(1); out.push_back(0);  // flags
		}
		uint32_t indexSize = (uint32_t)out.size() - indexOffset;
		out[0] = 'D'; out[1] = 'B'; out[2] = 'P'; out[3] = 'F';
		Put32(out, 4, 3);
		Put32(out, 0x24, (uint32_t)records.size());
		Put32(out, 0x2C, indexSize);
		Put32(out, 0x3C, 3);
		Put32(out, 0x40, indexOffset);
		return out;
	}
}

namespace Models
{
	bool Has(uint32_t instance, uint32_t type, uint32_t group)
	{
		return SpeciesManager.GetSpeciesProfile(ResourceKey(instance, type, group)) != nullptr;
	}

	bool Read(uint32_t instance, uint32_t type, uint32_t group, std::vector<Record>& out, bool quiet)
	{
		out.clear();
		// a creature key (type crt) is stored as a `bem` record; ask the game which record types belong to it
		eastl::vector<uint32_t> types;
		ResourceManager.GetRecordTypesFromResourceType(types, type);
		if (types.empty()) {
			types.push_back(type);
		}
		for (uint32_t recordType : types) {
			ResourceKey key(instance, recordType, group);
			Resource::Database* db = ResourceManager.FindRecord(key);
			if (!db) {
				continue;
			}
			Resource::IRecord* record = nullptr;
			if (!db->OpenRecord(key, &record) || !record) {
				continue;
			}
			IO::IStream* stream = record->GetStream();
			Record r;
			r.type = recordType; r.group = group; r.instance = instance;
			if (stream) {
				int size = (int)stream->GetSize();
				if (size > 0 && size < 8 * 1024 * 1024) {
					r.data.resize(size);
					stream->SetPosition(0);
					int got = stream->Read(r.data.data(), size);
					if (got != size) {
						r.data.clear();
					}
				}
			}
			db->CloseRecord(record);
			if (!quiet) {
				Logf("models: species %08x/%08x/%08x record type %08x: %d bytes from %ls", instance, type, group, recordType,
					(int)r.data.size(), (const wchar_t*)db->GetLocation());
			}
			if (!r.data.empty()) {
				out.push_back(std::move(r));
			}
		}
		return !out.empty();
	}

	bool Install(uint32_t instance, uint32_t type, uint32_t group, const std::vector<Record>& records)
	{
		if (records.empty()) {
			return false;
		}
		std::wstring dir = ModelsDir();
		CreateDirectoryW(dir.c_str(), nullptr);
		wchar_t name[64];
		swprintf(name, 64, L"\\m_%08x_%08x.package", group, instance);
		std::wstring path = dir + name;
		std::vector<uint8_t> bytes = BuildPackage(records);
		FILE* f = _wfopen(path.c_str(), L"wb");
		if (!f) {
			Logf("models: cannot write %ls", path.c_str());
			return false;
		}
		fwrite(bytes.data(), 1, bytes.size(), f);
		fclose(f);
		bool ok = Register(path);
		Logf("models: installed species %08x/%08x/%08x (%d records, %d bytes): %s, profile now %s", instance, type, group,
			(int)records.size(), (int)bytes.size(), ok ? "registered" : "FAILED", Has(instance, type, group) ? "found" : "NOT found");
		return ok;
	}

	uint32_t Hash(const std::vector<Record>& records)
	{
		if (records.empty()) {
			return 0;
		}
		uint32_t h = 2166136261u;
		for (const Record& r : records) {
			for (int i = 0; i < 4; ++i) {
				h = (h ^ (uint8_t)(r.type >> (8 * i))) * 16777619u;
			}
			for (uint8_t b : r.data) {
				h = (h ^ b) * 16777619u;
			}
		}
		return h ? h : 1;
	}

	uint32_t AliasInstance(uint32_t instance, uint32_t hash)
	{
		uint32_t a = (instance * 0x9E3779B1u) ^ (hash * 0x85EBCA6Bu) ^ 0xC0DE5EEDu;
		while (a == 0 || a == instance) {
			a = a * 1664525u + 1013904223u;
		}
		return a;
	}

	bool InstallAlias(uint32_t aliasInstance, uint32_t type, uint32_t group, const std::vector<Record>& records)
	{
		std::vector<Record> copy = records;
		for (Record& r : copy) {
			r.instance = aliasInstance;
		}
		return Install(aliasInstance, type, group, copy);
	}

	bool WritePackage(const std::wstring& path, const std::vector<Record>& records)
	{
		std::vector<uint8_t> bytes = BuildPackage(records);
		FILE* f = _wfopen(path.c_str(), L"wb");
		if (!f) {
			Logf("models: cannot write %ls", path.c_str());
			return false;
		}
		fwrite(bytes.data(), 1, bytes.size(), f);
		fclose(f);
		return Register(path);
	}

	void RegisterCached()
	{
		if (sCachedDone) {
			return;
		}
		sCachedDone = true;
		std::wstring dir = ModelsDir();
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((dir + L"\\m_*.package").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) {
			return;
		}
		int n = 0;
		do {
			if (Register(dir + L"\\" + fd.cFileName)) {
				++n;
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
		Logf("models: %d cached package(s) registered", n);
	}

	void Shutdown()
	{
		if (sRegistered) {
			for (auto db : *sRegistered) {
				ResourceManager.RegisterDatabase(false, db, 1000);
				db->Close();
				db->Release();
			}
			delete sRegistered;
			sRegistered = nullptr;
		}
		sCachedDone = false;
	}
}
