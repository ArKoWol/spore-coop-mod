// Models.h : creature models travel with the co-op session (spike S8, gate G2, docs/PLAN.md section 10).
//
// A save holds only creature KEYS, never their bodies. The bodies of the animals of a planet live in the host's
// EditorSaves.package / Pollination.package, so a guest without them cannot build those species. The host reads the
// records of a species from the resource manager, the guest writes them into a small DBPF package and registers it
// with the resource manager (idea and ids from WoodyWizard/SporeCoop, MIT).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Models
{
	struct Record
	{
		uint32_t type = 0, group = 0, instance = 0;
		std::vector<uint8_t> data;
	};

	// Host: the records that make up a species (its `bem` model). False if none was found.
	bool Read(uint32_t instance, uint32_t type, uint32_t group, std::vector<Record>& out);

	// True when the game can build this species here.
	bool Has(uint32_t instance, uint32_t type, uint32_t group);

	// Guest: writes CoopModels\m_<group>_<instance>.package and registers it. False on failure.
	bool Install(uint32_t instance, uint32_t type, uint32_t group, const std::vector<Record>& records);

	// Writes `records` as a DBPF package at `path` and registers it with the resource manager (unregistered in Shutdown).
	// Also used for the co-op window's pictures (game/CoopMenu.cpp).
	bool WritePackage(const std::wstring& path, const std::vector<Record>& records);

	// Guest, once per run: registers the packages saved in earlier sessions.
	void RegisterCached();

	// Dispose: unregisters everything we registered.
	void Shutdown();
}
