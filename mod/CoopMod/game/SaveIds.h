// SaveIds.h : moving a save and its planet records from one star to another (plain C++, no game headers).
//
// A save names the planets of its star system by PlanetID (planet index << 24 | StarID): in the save itself (several
// entries, some RefPack-compressed, big- and little-endian) and in the planet records of planetRecords.pkp (one record
// per planet, each a nested DBPF). To give a copied save its own star, every one of those ids is rewritten. The results
// are written uncompressed (the game reads uncompressed DBPF entries). Facts: docs/FACTS.md section 6.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SaveIds
{
	// a whole .spo file: every id of `fromStar`'s planets becomes the same planet of `toStar`; false on a read/format error
	bool RewriteSave(const std::string& fromPath, const std::string& toPath, uint32_t fromStar, uint32_t toStar);

	// one planet record as stored in planetRecords.pkp ([4-byte head][nested DBPF])
	bool RewritePlanetRecord(const std::vector<uint8_t>& record, uint32_t fromStar, uint32_t toStar, std::vector<uint8_t>& out);
}
