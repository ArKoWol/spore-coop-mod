// LocalNet.h : the local transport, for two game copies on one PC (the one-PC test). It carries the same messages on the same
// channels as Steam's ISteamNetworkingMessages (dllmain.cpp: state packets on 0, world files on 1, the shared world on 2 and 3)
// over one TCP connection on 127.0.0.1. The host listens on its port, the guest connects to it (and again after a drop).
// Frame on the wire: uint32 size, uint8 channel, then the message. No game headers.
#pragma once

#include <cstdint>
#include <vector>

namespace LocalNet
{
	// A local session uses ports as player ids (the role of the Steam ID): ids from 1025 to 65535 are local, real Steam IDs
	// are 17-digit numbers.
	inline bool IsLocalId(uint64_t id) { return id > 1024 && id < 65536; }

	bool Host(uint16_t port);   // listen on 127.0.0.1:port
	bool Join(uint16_t port);   // connect to 127.0.0.1:port (retried every second until the host answers)
	void Stop();
	void Pump();                // accept / connect, send what is queued, read what arrived; every frame
	bool Connected();

	// Queues one message. An unreliable one is dropped when much is still waiting to go out (like Steam's buffer).
	bool Send(int channel, const void* data, uint32_t size, bool reliable);
	// Takes the oldest message of the channel; false if there is none.
	bool Receive(int channel, std::vector<uint8_t>& out);

	void Shutdown();            // Dispose: frees the buffers (allocator note in dllmain.cpp)
}
