// models_tests.cpp : tests of the creature model chunks (core/coop_models.h), no game.
//
// Oracles: every chunk Encode makes is read back by Decode; the chunks rebuild the record byte for byte; the header is 31
// bytes (a receiver that read the bytes from 38 dropped every chunk, 2026-11-01); a short, cut or inconsistent chunk is refused.
//
// Build and run: tests\run_core_tests.bat   (prints ALL OK)
#include "../core/coop_models.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace coop::models;

namespace
{
	int failures = 0;
	int checks = 0;

	void Check(bool ok, const std::string& what)
	{
		++checks;
		if (!ok) {
			++failures;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	std::vector<uint8_t> Bytes(uint32_t size, uint32_t seed)
	{
		std::vector<uint8_t> b(size);
		for (uint32_t i = 0; i < size; ++i) {
			seed = seed * 1664525u + 1013904223u;
			b[i] = (uint8_t)(seed >> 24);
		}
		return b;
	}

	// Encodes a record, decodes every chunk and puts the bytes back together.
	bool RoundTrip(uint32_t size, uint32_t chunk, std::string& why)
	{
		std::vector<uint8_t> record = Bytes(size, size + 7);
		auto chunks = Encode(0x2911834d, 0x2b978c46, 0x40626200, 1, 2, 0x1a99b06b, record.data(), size, chunk);
		std::vector<uint8_t> back(size, 0);
		uint32_t got = 0;
		for (const auto& m : chunks) {
			Chunk c;
			if (!Decode(m.data(), m.size(), c)) {
				why = "a chunk was refused";
				return false;
			}
			if (c.instance != 0x2911834d || c.type != 0x2b978c46 || c.group != 0x40626200 || c.index != 1 || c.count != 2
				|| c.recType != 0x1a99b06b || c.total != size) {
				why = "a header field changed";
				return false;
			}
			std::memcpy(back.data() + c.offset, c.bytes, c.len);
			got += c.len;
		}
		if (got != size || back != record) {
			why = "the bytes differ";
			return false;
		}
		return true;
	}
}

int main()
{
	// the size of a real creature (2334, 3117 bytes), one byte, exactly one chunk, a chunk and one byte, several chunks
	const uint32_t sizes[] = { 2334, 3117, 1, kChunk, kChunk + 1, 3 * kChunk + 17 };
	for (uint32_t size : sizes) {
		std::string why;
		Check(RoundTrip(size, kChunk, why), "round trip of " + std::to_string(size) + " bytes: " + why);
	}
	{
		std::string why;
		Check(RoundTrip(1000, 64, why), "round trip with small chunks: " + why);
	}

	std::vector<uint8_t> record = Bytes(3117, 1);
	auto chunks = Encode(1, 2, 3, 0, 1, 4, record.data(), 3117);
	Check(chunks.size() == 1, "3117 bytes travel in one chunk");
	Check(chunks[0].size() == kHeader + 3117, "the header is 31 bytes");
	Chunk c;
	Check(Decode(chunks[0].data(), chunks[0].size(), c) && c.bytes == chunks[0].data() + 31, "the bytes start right after 31 bytes");

	// refused: too short, another message, cut, too long, record bigger than allowed, index not below count
	Check(!Decode(chunks[0].data(), kHeader - 1, c), "a message shorter than the header is refused");
	std::vector<uint8_t> other = chunks[0];
	other[0] = 102;
	Check(!Decode(other.data(), other.size(), c), "another message type is refused");
	Check(!Decode(chunks[0].data(), chunks[0].size() - 1, c), "a cut chunk is refused");
	std::vector<uint8_t> longer = chunks[0];
	longer.push_back(0);
	Check(!Decode(longer.data(), longer.size(), c), "a chunk with extra bytes is refused");
	std::vector<uint8_t> huge = chunks[0];
	uint32_t big = kMaxRecord + 1;
	std::memcpy(huge.data() + 19, &big, 4);
	Check(!Decode(huge.data(), huge.size(), c), "a record over 8 MB is refused");
	std::vector<uint8_t> badIndex = chunks[0];
	badIndex[13] = 1;  // index 1 of count 1
	Check(!Decode(badIndex.data(), badIndex.size(), c), "an index not below the count is refused");
	std::vector<uint8_t> past = chunks[0];
	uint32_t offset = 10;  // 10 + 3117 bytes do not fit a record of 3117
	std::memcpy(past.data() + 23, &offset, 4);
	Check(!Decode(past.data(), past.size(), c), "bytes past the end of the record are refused");

	if (failures) {
		std::printf("models tests: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}
	std::printf("models tests: %d checks passed\n", checks);
	return 0;
}
