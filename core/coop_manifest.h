// coop_manifest.h : the manifest of a world transfer (docs/features/JOIN_FLOW.md section 6), without the game.
//
// The host describes its Game0 as a manifest (every file: name, size, CRC32). The guest compares it with its cache
// Games\Coop\<host id>, asks only for the files that differ, builds Coop\<host id>.incoming from the unchanged files of the cache
// plus the received ones, checks every file against the manifest and only then replaces the cache. A broken download never
// reaches the cache. Only the folder "<id>.incoming" is ever deleted by this code (RemoveIncoming), and the cache it replaces.
//
// The game side (messages on the Steam channel, restart) is game/WorldTransfer.cpp; this file is plain C++ with Win32 file calls.
//
// Wire formats (little endian, the blobs travel inside the messages of game/WorldTransfer.cpp):
//   manifest   u32 count, then per file: u8 nameLen, name bytes, u32 size, u32 crc32
//   name list  u32 count, then per file: u8 nameLen, name bytes
#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace coop
{
	namespace manifest
	{
		const size_t kMaxName = 63;                    // a name fits the 64-byte field of a file packet
		const uint32_t kMaxFileSize = 8u * 1024 * 1024; // bigger files are not part of a world
		const size_t kMaxEntries = 256;
		const uint64_t kMaxTotal = 64ull * 1024 * 1024;
		const char kHostFileName[] = "coop_host.txt";  // written by the guest into a received world (the host's Steam ID)

		struct Entry
		{
			std::string name;
			uint32_t size = 0;
			uint32_t crc = 0;
		};
		typedef std::vector<Entry> Manifest;

		// ---------------------------------------------------------------- CRC32 (the usual one, "123456789" -> CBF43926)
		inline uint32_t Crc32(const void* data, size_t size, uint32_t crc = 0)
		{
			static uint32_t table[256];
			static bool ready = false;
			if (!ready) {
				for (uint32_t i = 0; i < 256; ++i) {
					uint32_t c = i;
					for (int k = 0; k < 8; ++k) {
						c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					}
					table[i] = c;
				}
				ready = true;
			}
			const unsigned char* p = (const unsigned char*)data;
			crc = ~crc;
			for (size_t i = 0; i < size; ++i) {
				crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
			}
			return ~crc;
		}

		// ---------------------------------------------------------------- small file helpers
		inline std::string Path(const std::string& dir, const std::string& name)
		{
			return dir + "\\" + name;
		}

		inline bool Exists(const std::string& path)
		{
			return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		}

		inline bool IsDirectory(const std::string& path)
		{
			DWORD a = GetFileAttributesA(path.c_str());
			return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
		}

		// the whole file; false if it cannot be opened
		inline bool ReadAll(const std::string& path, std::vector<unsigned char>& out)
		{
			out.clear();
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) {
				return false;
			}
			unsigned char block[64 * 1024];
			size_t n;
			while ((n = fread(block, 1, sizeof(block), f)) > 0) {
				out.insert(out.end(), block, block + n);
			}
			fclose(f);
			return true;
		}

		// size and CRC32 of a file, read in blocks; false if it cannot be opened (or is bigger than 4 GB)
		inline bool FileInfo(const std::string& path, uint32_t& size, uint32_t& crc)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) {
				return false;
			}
			unsigned char block[64 * 1024];
			uint64_t total = 0;
			uint32_t c = 0;
			size_t n;
			while ((n = fread(block, 1, sizeof(block), f)) > 0) {
				c = Crc32(block, n, c);
				total += n;
			}
			fclose(f);
			if (total > 0xFFFFFFFFull) {
				return false;
			}
			size = (uint32_t)total;
			crc = c;
			return true;
		}

		// the names of the entries of a folder (files and folders), without . and ..; false if the folder cannot be listed
		inline bool List(const std::string& dir, std::vector<std::string>& names, std::vector<bool>* isDir = nullptr)
		{
			names.clear();
			if (isDir) {
				isDir->clear();
			}
			WIN32_FIND_DATAA fd;
			HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
			if (h == INVALID_HANDLE_VALUE) {
				return false;
			}
			do {
				std::string name = fd.cFileName;
				if (name == "." || name == "..") {
					continue;
				}
				names.push_back(name);
				if (isDir) {
					isDir->push_back((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
				}
			} while (FindNextFileA(h, &fd));
			FindClose(h);
			return true;
		}

		inline bool EndsWith(const std::string& s, const char* tail)
		{
			size_t n = strlen(tail);
			return s.size() > n && s.compare(s.size() - n, n, tail) == 0;
		}

		// deletes a folder with everything in it (only called through the guarded functions below)
		inline void RemoveTree(const std::string& dir)
		{
			std::vector<std::string> names;
			std::vector<bool> isDir;
			if (List(dir, names, &isDir)) {
				for (size_t i = 0; i < names.size(); ++i) {
					std::string path = Path(dir, names[i]);
					if (isDir[i]) {
						RemoveTree(path);
					}
					else {
						SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
						DeleteFileA(path.c_str());
					}
				}
			}
			RemoveDirectoryA(dir.c_str());
		}

		// Deletes a download in progress. Refuses (returns false) any folder whose name does not end in ".incoming": this is the
		// only place where the guest side deletes a folder on purpose, and it must never reach a cache or a save. A missing
		// folder is fine. True when the folder is gone.
		inline bool RemoveIncoming(const std::string& dir)
		{
			if (!EndsWith(dir, ".incoming") || dir.find_last_of("\\/") == dir.size() - 1) {
				return false;
			}
			if (Exists(dir)) {
				RemoveTree(dir);
			}
			return !Exists(dir);
		}

		// ---------------------------------------------------------------- names and the wire formats
		inline bool ValidName(const std::string& name)
		{
			return !name.empty() && name.size() <= kMaxName && name.find_first_of("\\/:*?\"<>|") == std::string::npos &&
				name.find("..") == std::string::npos && name != kHostFileName;
		}

		inline void PutU32(std::vector<unsigned char>& out, uint32_t v)
		{
			for (int i = 0; i < 4; ++i) {
				out.push_back((unsigned char)(v >> (8 * i)));
			}
		}

		inline void PutName(std::vector<unsigned char>& out, const std::string& name)
		{
			out.push_back((unsigned char)name.size());
			out.insert(out.end(), name.begin(), name.end());
		}

		struct Reader
		{
			const unsigned char* p;
			size_t left;
			bool ok = true;
			Reader(const unsigned char* data, size_t size) : p(data), left(size) {}
			uint32_t U32()
			{
				if (left < 4) {
					ok = false;
					return 0;
				}
				uint32_t v = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
				p += 4;
				left -= 4;
				return v;
			}
			std::string Name()
			{
				if (left < 1) {
					ok = false;
					return std::string();
				}
				size_t n = p[0];
				if (left < 1 + n) {
					ok = false;
					return std::string();
				}
				std::string s((const char*)p + 1, n);
				p += 1 + n;
				left -= 1 + n;
				return s;
			}
		};

		inline std::vector<unsigned char> Encode(const Manifest& m)
		{
			std::vector<unsigned char> out;
			PutU32(out, (uint32_t)m.size());
			for (const Entry& e : m) {
				PutName(out, e.name);
				PutU32(out, e.size);
				PutU32(out, e.crc);
			}
			return out;
		}

		// false for anything that is not a manifest we would have produced: bad names, duplicates, too many or too big files, rest bytes
		inline bool Decode(const unsigned char* data, size_t size, Manifest& m)
		{
			m.clear();
			Reader r(data, size);
			uint32_t count = r.U32();
			if (!r.ok || count > kMaxEntries) {
				return false;
			}
			uint64_t total = 0;
			for (uint32_t i = 0; i < count; ++i) {
				Entry e;
				e.name = r.Name();
				e.size = r.U32();
				e.crc = r.U32();
				total += e.size;
				if (!r.ok || !ValidName(e.name) || e.size > kMaxFileSize || total > kMaxTotal) {
					m.clear();
					return false;
				}
				for (const Entry& other : m) {
					if (other.name == e.name) {
						m.clear();
						return false;
					}
				}
				m.push_back(e);
			}
			if (r.left != 0) {
				m.clear();
				return false;
			}
			return true;
		}

		inline std::vector<unsigned char> EncodeNames(const std::vector<std::string>& names)
		{
			std::vector<unsigned char> out;
			PutU32(out, (uint32_t)names.size());
			for (const std::string& n : names) {
				PutName(out, n);
			}
			return out;
		}

		inline bool DecodeNames(const unsigned char* data, size_t size, std::vector<std::string>& names)
		{
			names.clear();
			Reader r(data, size);
			uint32_t count = r.U32();
			if (!r.ok || count > kMaxEntries) {
				return false;
			}
			for (uint32_t i = 0; i < count; ++i) {
				std::string n = r.Name();
				if (!r.ok || !ValidName(n)) {
					names.clear();
					return false;
				}
				names.push_back(n);
			}
			if (r.left != 0) {
				names.clear();
				return false;
			}
			return true;
		}

		inline const Entry* Find(const Manifest& m, const std::string& name)
		{
			for (const Entry& e : m) {
				if (e.name == name) {
					return &e;
				}
			}
			return nullptr;
		}

		// ---------------------------------------------------------------- host: the manifest of a folder
		// Reads the files of `dir` (not its subfolders; the host's Game0 has none) into memory, sorted by name, so the manifest and
		// the bytes that are sent later come from the same moment. Skips what a world does not have (too long a name, too big,
		// coop_host.txt) or what does not fit into the limits; `skipped` gets one line per skipped file.
		inline bool Snapshot(const std::string& dir, Manifest& m, std::vector<std::vector<unsigned char>>& data,
			std::vector<std::string>* skipped = nullptr)
		{
			m.clear();
			data.clear();
			std::vector<std::string> names;
			std::vector<bool> isDir;
			if (!List(dir, names, &isDir)) {
				return false;
			}
			std::vector<std::string> files;
			for (size_t i = 0; i < names.size(); ++i) {
				if (!isDir[i]) {
					files.push_back(names[i]);
				}
			}
			std::sort(files.begin(), files.end());
			uint64_t total = 0;
			for (const std::string& name : files) {
				std::vector<unsigned char> bytes;
				const char* why = nullptr;
				if (name == kHostFileName) {
					continue;
				}
				if (!ValidName(name)) {
					why = "name";
				}
				else if (m.size() >= kMaxEntries) {
					why = "too many files";
				}
				else if (!ReadAll(Path(dir, name), bytes)) {
					why = "cannot be read";
				}
				else if (bytes.size() > kMaxFileSize || total + bytes.size() > kMaxTotal) {
					why = "too big";
				}
				if (why) {
					if (skipped) {
						skipped->push_back(name + " (" + why + ")");
					}
					continue;
				}
				Entry e;
				e.name = name;
				e.size = (uint32_t)bytes.size();
				e.crc = Crc32(bytes.data(), bytes.size());
				total += bytes.size();
				m.push_back(e);
				data.push_back(std::move(bytes));
			}
			return true;
		}

		// ---------------------------------------------------------------- guest: what to download
		struct Plan
		{
			std::vector<std::string> need;  // files to download (missing or different in the cache)
			uint64_t needBytes = 0;
			bool rebuild = false;           // the cache differs from the manifest in a way that needs a new folder (extra files, no marker)
			bool UpToDate() const { return need.empty() && !rebuild; }
		};

		inline unsigned long long ReadHostId(const std::string& path)
		{
			unsigned long long id = 0;
			if (FILE* f = fopen(path.c_str(), "r")) {
				if (fscanf(f, "%llu", &id) != 1) {
					id = 0;
				}
				fclose(f);
			}
			return id;
		}

		// Compares the manifest with the cache folder (which may not exist). `everything`: ask for all files (the second try
		// after a failed download). The cache is up to date when every file matches by size and CRC32, it holds no other file or
		// folder, and its coop_host.txt names this host.
		inline Plan MakePlan(const Manifest& m, const std::string& cacheDir, unsigned long long hostId, bool everything)
		{
			Plan plan;
			bool haveCache = IsDirectory(cacheDir);
			for (const Entry& e : m) {
				uint32_t size = 0, crc = 0;
				bool same = !everything && haveCache && FileInfo(Path(cacheDir, e.name), size, crc) && size == e.size && crc == e.crc;
				if (!same) {
					plan.need.push_back(e.name);
					plan.needBytes += e.size;
				}
			}
			plan.rebuild = everything || !haveCache;
			if (haveCache && !plan.rebuild) {
				std::vector<std::string> names;
				List(cacheDir, names);
				for (const std::string& n : names) {
					if (n != kHostFileName && !Find(m, n)) {
						plan.rebuild = true;  // a file the host no longer has: not carried over
					}
				}
				if (ReadHostId(Path(cacheDir, kHostFileName)) != hostId) {
					plan.rebuild = true;
				}
			}
			return plan;
		}

		// Every file of the manifest is in `dir` with the right size and CRC32, nothing else is (except coop_host.txt).
		// `bad` gets the first file that fails.
		inline bool Verify(const Manifest& m, const std::string& dir, std::string* bad = nullptr)
		{
			for (const Entry& e : m) {
				uint32_t size = 0, crc = 0;
				if (!FileInfo(Path(dir, e.name), size, crc) || size != e.size || crc != e.crc) {
					if (bad) {
						*bad = e.name;
					}
					return false;
				}
			}
			std::vector<std::string> names;
			if (!List(dir, names)) {
				if (bad) {
					*bad = dir;
				}
				return false;
			}
			for (const std::string& n : names) {
				if (n != kHostFileName && !Find(m, n)) {
					if (bad) {
						*bad = n;
					}
					return false;
				}
			}
			return true;
		}

		// A download: manifest in, the files that are needed come in as chunks, Finish() builds and checks the new folder.
		// Begin() makes the (empty) incoming folder when something has to be built; nothing is made for an up-to-date cache.
		class Download
		{
		public:
			bool Begin(const Manifest& m, const std::string& cacheDir, const std::string& incomingDir, unsigned long long hostId,
				bool everything, std::string* error)
			{
				Reset();
				manifest_ = m;
				cache_ = cacheDir;
				incoming_ = incomingDir;
				host_ = hostId;
				plan_ = MakePlan(m, cacheDir, hostId, everything);
				got_.assign(plan_.need.size(), 0);
				done_.assign(plan_.need.size(), false);
				if (plan_.UpToDate()) {
					return true;
				}
				if (!RemoveIncoming(incoming_)) {
					return Fail(error, "cannot clear " + incoming_);
				}
				size_t cut = incoming_.find_last_of('\\');
				if (cut != std::string::npos) {
					CreateDirectoryA(incoming_.substr(0, cut).c_str(), nullptr);
				}
				if (!CreateDirectoryA(incoming_.c_str(), nullptr)) {
					return Fail(error, "cannot create " + incoming_);
				}
				return true;
			}

			const Manifest& Files() const { return manifest_; }
			const std::vector<std::string>& Need() const { return plan_.need; }
			uint64_t NeedBytes() const { return plan_.needBytes; }
			bool UpToDate() const { return plan_.UpToDate(); }
			const std::string& Incoming() const { return incoming_; }

			// one chunk of a needed file, in order; false (and an error) for anything unexpected
			bool OnChunk(const std::string& name, uint32_t offset, uint32_t totalSize, const unsigned char* data, uint32_t len,
				std::string* error)
			{
				size_t i = 0;
				while (i < plan_.need.size() && plan_.need[i] != name) {
					++i;
				}
				const Entry* e = Find(manifest_, name);
				if (i == plan_.need.size() || !e) {
					return Fail(error, "unexpected file " + name);
				}
				if (done_[i] || totalSize != e->size || offset != got_[i] || (uint64_t)offset + len > e->size ||
					(len == 0 && e->size != 0)) {
					return Fail(error, "unexpected chunk of " + name);
				}
				FILE* f = fopen(Path(incoming_, name).c_str(), offset == 0 ? "wb" : "ab");
				if (!f) {
					return Fail(error, "cannot write " + name);
				}
				bool ok = len == 0 || fwrite(data, 1, len, f) == len;
				ok = fclose(f) == 0 && ok;
				if (!ok) {
					return Fail(error, "cannot write " + name);
				}
				got_[i] += len;
				received_ += len;
				if (got_[i] == e->size) {
					done_[i] = true;
					++doneCount_;
				}
				return true;
			}

			bool Complete() const { return doneCount_ == plan_.need.size(); }
			// bytes received of the bytes needed, 0..1 (1 when nothing is needed)
			float Progress() const { return plan_.needBytes ? (float)((double)received_ / (double)plan_.needBytes) : 1.0f; }

			// All needed files arrived: copy the unchanged files from the cache, add coop_host.txt, check every file.
			// On failure `error` names the file; the incoming folder is left for the caller to remove.
			bool Finish(std::string* error)
			{
				for (const Entry& e : manifest_) {
					if (std::find(plan_.need.begin(), plan_.need.end(), e.name) != plan_.need.end()) {
						continue;
					}
					if (!CopyFileA(Path(cache_, e.name).c_str(), Path(incoming_, e.name).c_str(), FALSE)) {
						return Fail(error, "cannot copy " + e.name + " from the cache");
					}
				}
				if (FILE* f = fopen(Path(incoming_, kHostFileName).c_str(), "w")) {
					fprintf(f, "%llu\n", host_);
					fclose(f);
				}
				else {
					return Fail(error, "cannot write " + std::string(kHostFileName));
				}
				std::string bad;
				if (!Verify(manifest_, incoming_, &bad)) {
					return Fail(error, "check failed for " + bad);
				}
				return true;
			}

		private:
			void Reset()
			{
				manifest_.clear();
				plan_ = Plan();
				got_.clear();
				done_.clear();
				doneCount_ = 0;
				received_ = 0;
			}
			static bool Fail(std::string* error, const std::string& text)
			{
				if (error) {
					*error = text;
				}
				return false;
			}

			Manifest manifest_;
			Plan plan_;
			std::string cache_, incoming_;
			unsigned long long host_ = 0;
			std::vector<uint32_t> got_;
			std::vector<bool> done_;
			size_t doneCount_ = 0;
			uint64_t received_ = 0;
		};

		// The checked folder becomes the cache: the older cache is moved aside, the new folder takes its place, the old one is deleted.
		// If the new folder cannot be moved the older cache is put back. Only the old cache (".replaced") is deleted here.
		inline bool Commit(const std::string& incomingDir, const std::string& cacheDir, std::string* error = nullptr)
		{
			std::string aside = cacheDir + ".replaced";
			if (Exists(aside)) {
				RemoveTree(aside);  // left by a crash of an earlier commit: only ever an older cache
			}
			bool hadCache = Exists(cacheDir);
			DWORD code = 0;
			for (int attempt = 0; attempt < 3; ++attempt) {
				if (hadCache && !MoveFileA(cacheDir.c_str(), aside.c_str())) {
					code = GetLastError();
					Sleep(100);
					continue;
				}
				if (MoveFileA(incomingDir.c_str(), cacheDir.c_str())) {
					if (hadCache) {
						RemoveTree(aside);
					}
					return true;
				}
				code = GetLastError();
				if (hadCache) {
					MoveFileA(aside.c_str(), cacheDir.c_str());  // put the older cache back
				}
				Sleep(100);
			}
			if (error) {
				*error = "cannot move " + incomingDir + " to " + cacheDir + " (error " + std::to_string(code) + ")";
			}
			return false;
		}
	}
}
