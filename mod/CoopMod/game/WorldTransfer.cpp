// WorldTransfer.cpp : see WorldTransfer.h.
#include "stdafx.h"
#include "WorldTransfer.h"
#include "../Log.h"
#include "../../../core/coop_launch.h"
#include "../../../core/coop_manifest.h"

#include <cstddef>
#include <string>
#include <vector>

namespace
{
	namespace mf = coop::manifest;

	const uint32_t kFileMagic = 0x4C465053;      // 'SPFL' file chunk
	const uint32_t kManifestMagic = 0x464D5053;  // 'SPMF'
	const uint32_t kNeedMagic = 0x444E5053;      // 'SPND'
	const uint32_t kRequestMagic = 0x51525053;   // 'SPRQ'
	const uint32_t kChunk = 60 * 1024;
	const DWORD kStallMs = 30000;                // the guest gives up when nothing arrives for this long

#pragma pack(push, 1)
	struct FilePacket
	{
		uint32_t magic;
		char name[64];
		uint32_t offset;
		uint32_t totalSize;
		uint32_t dataLen;
		unsigned char data[kChunk];
	};
#pragma pack(pop)
	const size_t kFileHeader = offsetof(FilePacket, data);

	WorldTransfer::SendFn sSend = nullptr;

	uint32_t MagicOf(const unsigned char* data, int size)
	{
		return size >= 4 ? (uint32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24)) : 0;
	}

	// a message: the magic and a payload
	std::vector<unsigned char> Message(uint32_t magic, const std::vector<unsigned char>& payload)
	{
		std::vector<unsigned char> out;
		mf::PutU32(out, magic);
		out.insert(out.end(), payload.begin(), payload.end());
		return out;
	}

	// ------------------------------------------------------------------ host
	struct HostData
	{
		std::string dir;                              // the folder that was offered
		mf::Manifest manifest;
		std::vector<std::vector<unsigned char>> data; // the bytes of the snapshot, same order as the manifest
		std::vector<unsigned char> pending;           // the manifest message Steam has not taken yet
		std::vector<size_t> queue;                    // the files the guest asked for
		size_t next = 0;
		uint32_t offset = 0;
		bool sending = false;
	};
	HostData* sHost = nullptr;

	// ------------------------------------------------------------------ guest
	enum class Phase { Idle, Downloading, Staged, Failed };

	struct GuestData
	{
		mf::Download download;
		Phase phase = Phase::Idle;
		bool waitingManifest = false;                 // the retry asked for a fresh manifest
		int attempt = 0;                              // 0 = first try, 1 = the retry
		bool upToDate = false;
		std::string error, games, cache, incoming;
		uint64_t host = 0;
		DWORD lastActivity = 0;
		std::vector<unsigned char> pending;           // a message for the host that Steam has not taken yet
	};
	GuestData* sGuest = nullptr;

	GuestData& G()
	{
		if (!sGuest) {
			sGuest = new GuestData();
		}
		return *sGuest;
	}

	// the download is over for good: the message is for the player; the half-built folder goes
	void Fail(GuestData& g, const std::string& text)
	{
		Logf("world: download failed: %s", text.c_str());
		App::ConsolePrintF("coop: %s", text.c_str());
		if (!g.incoming.empty() && !mf::RemoveIncoming(g.incoming)) {
			Logf("world: could not delete %s", g.incoming.c_str());
		}
		g.phase = Phase::Failed;
		g.error = text;
		g.pending.clear();
	}

	// the world is in Coop\<id>: the next start opens it
	bool Stage(GuestData& g, bool upToDate)
	{
		if (!coop::launch::WriteLaunch(g.games, g.host)) {
			Fail(g, "Could not write CoopLaunch.txt in the Games folder. Check that the folder is writable, then join again.");
			return false;
		}
		g.phase = Phase::Staged;
		g.upToDate = upToDate;
		g.pending.clear();
		Logf("world: staged (%s)", upToDate ? "cache was up to date" : "downloaded and checked");
		return true;
	}

	// This try did not produce a world that matches the manifest: ask for a fresh manifest and download everything once more.
	// The second failure ends the join.
	void TryFailed(GuestData& g, const std::string& why)
	{
		Logf("world: try %d failed: %s", g.attempt + 1, why.c_str());
		if (!g.incoming.empty()) {
			mf::RemoveIncoming(g.incoming);
		}
		if (g.attempt == 0) {
			g.attempt = 1;
			g.waitingManifest = true;
			g.lastActivity = GetTickCount();
			g.pending = Message(kRequestMagic, std::vector<unsigned char>());
			App::ConsolePrintF("coop: the downloaded world did not check out (%s), trying once more...", why.c_str());
			return;
		}
		Fail(g, "The downloaded world is damaged (" + why + "). Join again; if it happens again, ask the host to save and try later.");
	}

	// everything needed has arrived (or nothing was needed): build the folder, check it, make it the cache. True when staged.
	bool Finish(GuestData& g)
	{
		std::string why;
		if (!g.download.Finish(&why)) {
			TryFailed(g, why);
			return false;
		}
		if (!mf::Commit(g.download.Incoming(), g.cache, &why)) {
			Fail(g, "Could not store the world: " + why);
			return false;
		}
		return Stage(g, false);
	}

	bool OnManifest(GuestData& g, const unsigned char* payload, size_t size, const std::string& gamesDir, uint64_t hostId)
	{
		if (g.phase != Phase::Idle && g.phase != Phase::Downloading) {
			return false;  // staged already, or failed (the player joins again)
		}
		mf::Manifest manifest;
		if (!mf::Decode(payload, size, manifest)) {
			Fail(g, "The host sent a file list this version cannot read. Both run update.bat, then try again.");
			return false;
		}
		g.games = gamesDir;
		g.host = hostId;
		g.cache = gamesDir + "\\Coop\\" + std::to_string((unsigned long long)hostId);
		g.incoming = g.cache + ".incoming";
		g.lastActivity = GetTickCount();
		g.waitingManifest = false;
		g.pending.clear();
		std::string why;
		if (!g.download.Begin(manifest, g.cache, g.incoming, hostId, g.attempt > 0, &why)) {
			Fail(g, "Could not prepare the download: " + why);
			return false;
		}
		g.phase = Phase::Downloading;
		uint64_t total = 0;
		for (const mf::Entry& e : manifest) {
			total += e.size;
		}
		Logf("world: manifest %d files %u bytes, need %d files %u bytes%s", (int)manifest.size(), (unsigned)total,
			(int)g.download.Need().size(), (unsigned)g.download.NeedBytes(), g.attempt ? " (second try, everything)" : "");
		if (g.download.UpToDate()) {
			App::ConsolePrintF("coop: your copy of the host's world is up to date, nothing to download.");
			return Stage(g, true);
		}
		if (g.download.Need().empty()) {
			return Finish(g);  // only extra files in the cache: build the folder without them
		}
		App::ConsolePrintF("coop: downloading %d of %d files (%u KB)...", (int)g.download.Need().size(), (int)manifest.size(),
			(unsigned)(g.download.NeedBytes() / 1024));
		g.pending = Message(kNeedMagic, mf::EncodeNames(g.download.Need()));
		return false;
	}

	bool OnChunk(GuestData& g, const unsigned char* data, int size)
	{
		if (g.phase != Phase::Downloading || g.waitingManifest || g.download.Need().empty()) {
			return false;
		}
		if (size < (int)kFileHeader) {
			return false;
		}
		const FilePacket& p = *(const FilePacket*)data;
		if (p.dataLen > kChunk || (int)(kFileHeader + p.dataLen) > size) {
			return false;
		}
		char name[65];
		memcpy(name, p.name, 64);
		name[64] = 0;
		g.lastActivity = GetTickCount();
		std::string why;
		if (!g.download.OnChunk(name, p.offset, p.totalSize, p.data, p.dataLen, &why)) {
			TryFailed(g, why);
			return false;
		}
		return g.download.Complete() && Finish(g);
	}
}

namespace WorldTransfer
{
	void SetSender(SendFn send)
	{
		sSend = send;
	}

	// ------------------------------------------------------------------ host
	void HostStart(const std::string& gameDir)
	{
		HostStop();
		sHost = new HostData();
		HostData& h = *sHost;
		h.dir = gameDir;
		std::vector<std::string> skipped;
		if (!mf::Snapshot(gameDir, h.manifest, h.data, &skipped)) {
			Logf("world: no save folder %s", gameDir.c_str());
			HostStop();
			return;
		}
		for (const std::string& s : skipped) {
			Logf("world: not offered: %s", s.c_str());
		}
		if (h.manifest.empty()) {
			Logf("world: %s has no files to offer", gameDir.c_str());
			HostStop();
			return;
		}
		uint64_t total = 0;
		for (const mf::Entry& e : h.manifest) {
			total += e.size;
		}
		h.pending = Message(kManifestMagic, mf::Encode(h.manifest));
		Logf("world: offering %d files, %u bytes, from %s", (int)h.manifest.size(), (unsigned)total, gameDir.c_str());
		App::ConsolePrintF("coop: offering the world (%d files)...", (int)h.manifest.size());
	}

	void HostMessage(const unsigned char* data, int size)
	{
		uint32_t magic = MagicOf(data, size);
		if (magic == kRequestMagic) {
			if (sHost && !sHost->dir.empty()) {
				std::string dir = sHost->dir;
				Logf("world: the guest asked for a fresh manifest");
				HostStart(dir);
			}
			return;
		}
		if (magic != kNeedMagic || !sHost || sHost->sending || !sHost->pending.empty()) {
			return;
		}
		HostData& h = *sHost;
		std::vector<std::string> names;
		if (!mf::DecodeNames(data + 4, size - 4, names)) {
			Logf("world: unreadable list of wanted files");
			return;
		}
		uint64_t bytes = 0;
		h.queue.clear();
		for (const std::string& name : names) {
			for (size_t i = 0; i < h.manifest.size(); ++i) {
				if (h.manifest[i].name == name && std::find(h.queue.begin(), h.queue.end(), i) == h.queue.end()) {
					h.queue.push_back(i);
					bytes += h.manifest[i].size;
				}
			}
		}
		h.next = 0;
		h.offset = 0;
		h.sending = !h.queue.empty();
		Logf("world: the guest needs %d of %d files, %u bytes", (int)h.queue.size(), (int)h.manifest.size(), (unsigned)bytes);
		if (h.sending) {
			App::ConsolePrintF("coop: sending %d files to the guest...", (int)h.queue.size());
		}
	}

	void HostPump()
	{
		if (!sHost || !sSend) {
			return;
		}
		HostData& h = *sHost;
		if (!h.pending.empty()) {
			if (!sSend(h.pending.data(), (uint32_t)h.pending.size())) {
				return;
			}
			h.pending.clear();
			Logf("world: manifest sent");
		}
		static FilePacket packet;
		for (int budget = 0; budget < 3 && h.sending; ++budget) {
			size_t index = h.queue[h.next];
			const std::vector<unsigned char>& bytes = h.data[index];
			uint32_t total = (uint32_t)bytes.size();
			uint32_t len = total - h.offset;
			if (len > kChunk) {
				len = kChunk;
			}
			packet.magic = kFileMagic;
			memset(packet.name, 0, sizeof(packet.name));
			strcpy(packet.name, h.manifest[index].name.c_str());
			packet.offset = h.offset;
			packet.totalSize = total;
			packet.dataLen = len;
			if (len) {
				memcpy(packet.data, bytes.data() + h.offset, len);
			}
			// not taken (Steam's send buffer is full): the same chunk again next frame
			if (!sSend(&packet, (uint32_t)(kFileHeader + len))) {
				return;
			}
			h.offset += len;
			if (h.offset >= total) {
				h.offset = 0;
				if (++h.next >= h.queue.size()) {
					h.sending = false;
					Logf("world: all files sent");
					App::ConsolePrintF("coop: world sent.");
					// the bytes are not needed any more; a new connection or a retry takes a new snapshot
					std::vector<std::vector<unsigned char>>().swap(h.data);
					h.manifest.clear();
					// h.dir stays: a retry (SPRQ) takes a new snapshot
				}
			}
		}
	}

	void HostStop()
	{
		delete sHost;
		sHost = nullptr;
	}

	// ------------------------------------------------------------------ guest
	GuestState Guest()
	{
		GuestState s;
		if (!sGuest) {
			return s;
		}
		const GuestData& g = *sGuest;
		s.receiving = g.phase == Phase::Downloading;
		s.progress = s.receiving && !g.waitingManifest ? g.download.Progress() : 0;
		s.staged = g.phase == Phase::Staged;
		s.upToDate = s.staged && g.upToDate;
		if (g.phase == Phase::Failed) {
			s.error = g.error;
		}
		return s;
	}

	bool GuestMessage(const unsigned char* data, int size, const std::string& gamesDir, uint64_t hostId)
	{
		uint32_t magic = MagicOf(data, size);
		if (magic == kManifestMagic) {
			return OnManifest(G(), data + 4, size - 4, gamesDir, hostId);
		}
		if (magic == kFileMagic) {
			return OnChunk(G(), data, size);
		}
		return false;
	}

	void GuestUpdate(bool hostAlive)
	{
		if (!sGuest || sGuest->phase != Phase::Downloading) {
			return;
		}
		GuestData& g = *sGuest;
		if (!hostAlive) {
			GuestAbort("The host went away during the download. Nothing was changed on your PC; join again when the host is back.");
		}
		else if (GetTickCount() - g.lastActivity > kStallMs) {
			GuestAbort("The host stopped sending the world. Nothing was changed on your PC; join again.");
		}
		else if (!g.pending.empty() && sSend && sSend(g.pending.data(), (uint32_t)g.pending.size())) {
			g.pending.clear();
		}
	}

	void GuestAbort(const char* error)
	{
		if (!sGuest || sGuest->phase != Phase::Downloading) {
			return;
		}
		GuestData& g = *sGuest;
		if (error) {
			Fail(g, error);
			return;
		}
		Logf("world: download cancelled");
		if (!g.incoming.empty() && !mf::RemoveIncoming(g.incoming)) {
			Logf("world: could not delete %s", g.incoming.c_str());
		}
		g.phase = Phase::Idle;
		g.pending.clear();
	}

	void GuestReset()
	{
		GuestAbort(nullptr);
		delete sGuest;
		sGuest = nullptr;
	}

	void Shutdown()
	{
		HostStop();
		delete sGuest;
		sGuest = nullptr;
		sSend = nullptr;
	}
}
