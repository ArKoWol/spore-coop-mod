// WorldTransfer.h : the world transfer of a join (docs/features/JOIN_FLOW.md section 6, docs/ARCHITECTURE.md section 5).
//
// The host offers its Game0 as a manifest (file names, sizes, CRC32); the guest compares it with its cache Games\Coop\<host id>,
// asks only for the files that differ and gets them as chunks (all on the reliable file channel, messages below). The guest builds
// Coop\<id>.incoming from the unchanged cache files and the received ones, checks every file, and only then replaces the cache and
// writes CoopLaunch.txt. The logic that needs no game (manifest, diff, build, verify) is core/coop_manifest.h, tested without the game.
// This file is the messages, the sending pump and the guest's state around it. Everything runs on the main thread.
//
// Messages (first u32 = magic, little endian; the payload formats are in core/coop_manifest.h):
//   'SPMF'  host -> guest   the manifest
//   'SPND'  guest -> host   the names of the files the guest needs (a name list); not sent when nothing is needed
//   'SPFL'  host -> guest   one chunk of a needed file: name[64], offset, total size, data length, data
//   'SPRQ'  guest -> host   "send a fresh manifest" (the retry after a download that did not check out)
#pragma once

#include <cstdint>
#include <string>

namespace WorldTransfer
{
	// sends one reliable message on the file channel; false = not now (Steam's buffer is full), try again next frame
	typedef bool (*SendFn)(const void* data, uint32_t size);
	void SetSender(SendFn send);

	// ---- host
	// takes a snapshot of the folder and offers it to the guest (a new connection, `coop sendworld`)
	void HostStart(const std::string& gameDir);
	// a message of the guest on the file channel
	void HostMessage(const unsigned char* data, int size);
	// every frame: sends the pending manifest and a few chunks of the files the guest asked for
	void HostPump();
	// the session ended: forget the snapshot
	void HostStop();

	// ---- guest
	struct GuestState
	{
		bool receiving = false;  // a manifest arrived and the world is not complete yet
		float progress = 0;      // 0..1 of the bytes that have to be downloaded
		bool staged = false;     // the world is in Coop\<id> and CoopLaunch.txt is written: the game restarts
		bool upToDate = false;   // staged without downloading anything: the cache already was the host's world
		std::string error;       // the download failed (text for the player), empty = no error
	};
	GuestState Guest();

	// A message of the host on the file channel. Returns true once, when the world became complete (staged).
	bool GuestMessage(const unsigned char* data, int size, const std::string& gamesDir, uint64_t hostId);
	// every frame while joining: resends a message Steam did not take, aborts when the host is gone or silent
	void GuestUpdate(bool hostAlive);
	// The download stops (the join was cancelled, the host went away): Coop\<id>.incoming is deleted, nothing else.
	// `error` is shown to the player; null = the player cancelled, no error.
	void GuestAbort(const char* error);
	// a new join starts: forget everything of the last one
	void GuestReset();

	// Dispose: frees the memory (ModAPI routes new/delete to the game's heap, FACTS section 3)
	void Shutdown();
}
