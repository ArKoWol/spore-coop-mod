// coop_models.h : the chunks a creature model travels in (kMsgModel, mod/CoopMod/World.cpp), without the game.
//
// A model is a few records (the creature's files); each record is cut into chunks of at most kChunk bytes. A chunk is
// [103][instance][type][group][index u8][count u8][recType][total][offset][len][bytes]: kHeader = 31 bytes before the bytes.
// The receiver once read the bytes from offset 38 and dropped every chunk (2026-11-01). Tests: tests/models_tests.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace coop
{
	namespace models
	{
		const uint8_t kMsgModel = 103;
		const size_t kHeader = 31;
		const uint32_t kChunk = 40 * 1024;
		const uint32_t kMaxRecord = 8u * 1024 * 1024;

		struct Chunk
		{
			uint32_t instance = 0, type = 0, group = 0;
			uint8_t index = 0, count = 0;   // which record of how many
			uint32_t recType = 0;           // the record's resource type
			uint32_t total = 0;             // the record's size
			uint32_t offset = 0, len = 0;   // where these bytes go in it
			const uint8_t* bytes = nullptr; // points into the message
		};

		namespace detail
		{
			inline void Put(std::vector<uint8_t>& out, uint32_t v)
			{
				const uint8_t* b = (const uint8_t*)&v;
				out.insert(out.end(), b, b + 4);
			}
			inline uint32_t Get(const uint8_t* p)
			{
				uint32_t v;
				std::memcpy(&v, p, 4);
				return v;
			}
		}

		// The chunks of record `index` of `count` (its bytes `data`, `size` of them), in order.
		inline std::vector<std::vector<uint8_t>> Encode(uint32_t instance, uint32_t type, uint32_t group, uint8_t index,
			uint8_t count, uint32_t recType, const uint8_t* data, uint32_t size, uint32_t chunk = kChunk)
		{
			std::vector<std::vector<uint8_t>> out;
			for (uint32_t offset = 0; offset < size; offset += chunk) {
				uint32_t len = size - offset < chunk ? size - offset : chunk;
				std::vector<uint8_t> m;
				m.reserve(kHeader + len);
				m.push_back(kMsgModel);
				detail::Put(m, instance); detail::Put(m, type); detail::Put(m, group);
				m.push_back(index); m.push_back(count);
				detail::Put(m, recType); detail::Put(m, size); detail::Put(m, offset); detail::Put(m, len);
				m.insert(m.end(), data + offset, data + offset + len);
				out.push_back(m);
			}
			return out;
		}

		// Reads one chunk; false for anything that is not a whole, consistent chunk (nothing is half read).
		inline bool Decode(const uint8_t* data, size_t size, Chunk& c)
		{
			if (!data || size < kHeader || data[0] != kMsgModel) {
				return false;
			}
			c.instance = detail::Get(data + 1); c.type = detail::Get(data + 5); c.group = detail::Get(data + 9);
			c.index = data[13]; c.count = data[14];
			c.recType = detail::Get(data + 15); c.total = detail::Get(data + 19);
			c.offset = detail::Get(data + 23); c.len = detail::Get(data + 27);
			c.bytes = data + kHeader;
			return c.len == size - kHeader && c.total <= kMaxRecord && c.offset <= c.total && c.len <= c.total - c.offset
				&& c.index < c.count;
		}
	}
}
