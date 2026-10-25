// SaveIds.cpp : see SaveIds.h.
#include "stdafx.h"
#include "SaveIds.h"

#include <cstdio>
#include <cstring>

namespace
{
	struct Entry
	{
		uint32_t type = 0, group = 0, instance = 0;
		std::vector<uint8_t> data;  // uncompressed
	};

	uint32_t U32(const uint8_t* p)
	{
		return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
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

	// EA RefPack (QFS)
	bool RefPack(const uint8_t* src, size_t n, std::vector<uint8_t>& out)
	{
		if (n < 5 || src[1] != 0xFB) {
			return false;
		}
		size_t pos;
		size_t size;
		if (src[0] & 0x80) {
			size = (size_t)src[2] << 24 | (size_t)src[3] << 16 | (size_t)src[4] << 8 | src[5];
			pos = 6;
		}
		else {
			size = (size_t)src[2] << 16 | (size_t)src[3] << 8 | src[4];
			pos = 5;
		}
		if (src[0] & 0x01) {
			pos += (src[0] & 0x80) ? 4 : 3;
		}
		out.clear();
		out.reserve(size);
		while (pos < n) {
			uint8_t b0 = src[pos];
			size_t plain, copy = 0, offset = 0;
			if (b0 < 0x80) {
				if (pos + 1 >= n) return false;
				uint8_t b1 = src[pos + 1];
				pos += 2;
				plain = b0 & 3;
				copy = ((b0 & 0x1C) >> 2) + 3;
				offset = ((b0 & 0x60) << 3) + b1 + 1;
			}
			else if (b0 < 0xC0) {
				if (pos + 2 >= n) return false;
				uint8_t b1 = src[pos + 1], b2 = src[pos + 2];
				pos += 3;
				plain = (b1 & 0xC0) >> 6;
				copy = (b0 & 0x3F) + 4;
				offset = ((b1 & 0x3F) << 8) + b2 + 1;
			}
			else if (b0 < 0xE0) {
				if (pos + 3 >= n) return false;
				uint8_t b1 = src[pos + 1], b2 = src[pos + 2], b3 = src[pos + 3];
				pos += 4;
				plain = b0 & 3;
				copy = ((b0 & 0x0C) << 6) + b3 + 5;
				offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
			}
			else if (b0 < 0xFC) {
				plain = ((b0 & 0x1F) << 2) + 4;
				pos += 1;
				if (pos + plain > n) return false;
				out.insert(out.end(), src + pos, src + pos + plain);
				pos += plain;
				continue;
			}
			else {
				plain = b0 & 3;
				pos += 1;
				if (pos + plain > n) return false;
				out.insert(out.end(), src + pos, src + pos + plain);
				break;
			}
			if (pos + plain > n) return false;
			out.insert(out.end(), src + pos, src + pos + plain);
			pos += plain;
			if (offset > out.size()) return false;
			for (size_t i = 0; i < copy; ++i) {
				out.push_back(out[out.size() - offset]);
			}
		}
		return out.size() == size;
	}

	// a Spore DBPF 3.0 (index flags: bit 0 type, bit 1 group, bit 2 instance-high constant)
	bool ReadDbpf(const uint8_t* raw, size_t n, std::vector<Entry>& out, uint32_t& flags, uint32_t consts[3])
	{
		if (n < 96 || memcmp(raw, "DBPF", 4) != 0) {
			return false;
		}
		uint32_t count = U32(raw + 0x24), indexOffset = U32(raw + 0x40);
		if (indexOffset + 4 > n) {
			return false;
		}
		size_t p = indexOffset;
		flags = U32(raw + p);
		p += 4;
		for (int i = 0; i < 3; ++i) {
			consts[i] = 0;
			if (flags & (1u << i)) {
				consts[i] = U32(raw + p);
				p += 4;
			}
		}
		out.clear();
		for (uint32_t i = 0; i < count; ++i) {
			Entry e;
			if (flags & 1) e.type = consts[0]; else { e.type = U32(raw + p); p += 4; }
			if (flags & 2) e.group = consts[1]; else { e.group = U32(raw + p); p += 4; }
			e.instance = U32(raw + p);
			p += 4;
			if (p + 16 > n) return false;
			uint32_t offset = U32(raw + p), csize = U32(raw + p + 4) & 0x7FFFFFFF;
			uint16_t compression = (uint16_t)(raw[p + 12] | raw[p + 13] << 8);
			p += 16;
			if ((size_t)offset + csize > n) return false;
			if (compression == 0xFFFF) {
				if (!RefPack(raw + offset, csize, e.data)) return false;
			}
			else {
				e.data.assign(raw + offset, raw + offset + csize);
			}
			out.push_back(std::move(e));
		}
		return true;
	}

	std::vector<uint8_t> WriteDbpf(const std::vector<Entry>& entries, uint32_t flags, const uint32_t consts[3])
	{
		std::vector<uint8_t> out(96, 0);
		std::vector<uint32_t> offsets;
		for (const Entry& e : entries) {
			offsets.push_back((uint32_t)out.size());
			out.insert(out.end(), e.data.begin(), e.data.end());
		}
		uint32_t indexOffset = (uint32_t)out.size();
		Push32(out, flags);
		for (int i = 0; i < 3; ++i) {
			if (flags & (1u << i)) Push32(out, consts[i]);
		}
		for (size_t i = 0; i < entries.size(); ++i) {
			const Entry& e = entries[i];
			if (!(flags & 1)) Push32(out, e.type);
			if (!(flags & 2)) Push32(out, e.group);
			Push32(out, e.instance);
			uint32_t size = (uint32_t)e.data.size();
			Push32(out, offsets[i]);
			Push32(out, size | 0x80000000u);
			Push32(out, size);
			out.push_back(0); out.push_back(0);  // not compressed
			out.push_back(1); out.push_back(0);
		}
		uint32_t indexSize = (uint32_t)out.size() - indexOffset;
		out[0] = 'D'; out[1] = 'B'; out[2] = 'P'; out[3] = 'F';
		Put32(out, 4, 3);
		Put32(out, 0x24, (uint32_t)entries.size());
		Put32(out, 0x2C, indexSize);
		Put32(out, 0x3C, 3);
		Put32(out, 0x40, indexOffset);
		return out;
	}

	// the low 3 bytes of a StarID (planet index is the 4th byte), big- and little-endian
	int ReplaceStar(std::vector<uint8_t>& data, uint32_t fromStar, uint32_t toStar)
	{
		const uint8_t be[2][3] = { { (uint8_t)(fromStar >> 16), (uint8_t)(fromStar >> 8), (uint8_t)fromStar },
			{ (uint8_t)(toStar >> 16), (uint8_t)(toStar >> 8), (uint8_t)toStar } };
		const uint8_t le[2][3] = { { (uint8_t)fromStar, (uint8_t)(fromStar >> 8), (uint8_t)(fromStar >> 16) },
			{ (uint8_t)toStar, (uint8_t)(toStar >> 8), (uint8_t)(toStar >> 16) } };
		int count = 0;
		for (size_t i = 0; i + 3 <= data.size(); ++i) {
			if (memcmp(&data[i], be[0], 3) == 0) {
				memcpy(&data[i], be[1], 3);
				++count;
				i += 2;
			}
			else if (memcmp(&data[i], le[0], 3) == 0) {
				memcpy(&data[i], le[1], 3);
				++count;
				i += 2;
			}
		}
		return count;
	}

	bool RewriteDbpf(const uint8_t* raw, size_t n, uint32_t fromStar, uint32_t toStar, std::vector<uint8_t>& out, int& replaced)
	{
		std::vector<Entry> entries;
		uint32_t flags = 0, consts[3];
		if (!ReadDbpf(raw, n, entries, flags, consts)) {
			return false;
		}
		replaced = 0;
		for (Entry& e : entries) {
			replaced += ReplaceStar(e.data, fromStar, toStar);
		}
		out = WriteDbpf(entries, flags, consts);
		return true;
	}
}

namespace SaveIds
{
	bool RewriteSave(const std::string& fromPath, const std::string& toPath, uint32_t fromStar, uint32_t toStar)
	{
		std::vector<uint8_t> raw;
		if (FILE* f = fopen(fromPath.c_str(), "rb")) {
			fseek(f, 0, SEEK_END);
			long size = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (size > 0) {
				raw.resize((size_t)size);
				if (fread(raw.data(), 1, raw.size(), f) != raw.size()) raw.clear();
			}
			fclose(f);
		}
		std::vector<uint8_t> out;
		int replaced = 0;
		if (raw.empty() || !RewriteDbpf(raw.data(), raw.size(), fromStar, toStar, out, replaced) || replaced == 0) {
			return false;
		}
		FILE* f = fopen(toPath.c_str(), "wb");
		if (!f) {
			return false;
		}
		bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
		fclose(f);
		return ok;
	}

	bool RewritePlanetRecord(const std::vector<uint8_t>& record, uint32_t fromStar, uint32_t toStar, std::vector<uint8_t>& out)
	{
		if (record.size() < 4 + 96) {
			return false;
		}
		std::vector<uint8_t> nested;
		int replaced = 0;
		if (!RewriteDbpf(record.data() + 4, record.size() - 4, fromStar, toStar, nested, replaced)) {
			return false;
		}
		out.assign(record.begin(), record.begin() + 4);
		out.insert(out.end(), nested.begin(), nested.end());
		return true;
	}
}
