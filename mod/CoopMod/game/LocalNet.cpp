// LocalNet.cpp : the local transport (see LocalNet.h). Non-blocking sockets, all on the game's main thread.
#include "stdafx.h"
#include "LocalNet.h"
#include "../Log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace
{
	const int kChannels = 4;
	const uint32_t kMaxMessage = 4 * 1024 * 1024;
	const size_t kUnreliableLimit = 256 * 1024;  // queued bytes above which an unreliable message is dropped
	const DWORD kRetryMs = 1000;

	struct Queue
	{
		std::vector<std::vector<uint8_t>> items;
		size_t head = 0;
	};

	bool sStarted = false;   // WSAStartup done
	bool sHost = false;
	uint16_t sPort = 0;
	SOCKET sListen = INVALID_SOCKET;
	SOCKET sConn = INVALID_SOCKET;
	bool sConnecting = false;  // guest: a connect() is in progress
	bool sConnected = false;
	DWORD sNextTry = 0;
	std::vector<uint8_t> sOut;  // framed bytes waiting to be sent
	size_t sOutSent = 0;        // bytes of sOut already sent
	std::vector<uint8_t> sIn;   // received bytes not yet split into messages
	Queue sQueues[kChannels];

	void CloseConn(const char* why)
	{
		if (sConn != INVALID_SOCKET) {
			closesocket(sConn);
			sConn = INVALID_SOCKET;
			if (sConnected) {
				Logf("local: connection closed (%s)", why);
			}
		}
		sConnected = false;
		sConnecting = false;
		sIn.clear();
		// a half-sent frame cannot be continued on a new connection; what was queued is lost, as with a dropped Steam session
		sOut.clear();
		sOutSent = 0;
	}

	void UseSocket(SOCKET s)
	{
		u_long nonBlocking = 1;
		ioctlsocket(s, FIONBIO, &nonBlocking);
		BOOL noDelay = TRUE;
		setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
	}

	sockaddr_in Loopback(uint16_t port)
	{
		sockaddr_in a = {};
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		a.sin_port = htons(port);
		return a;
	}

	bool StartWinsock()
	{
		if (!sStarted) {
			WSADATA wsa;
			if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
				Logf("local: WSAStartup failed");
				return false;
			}
			sStarted = true;
		}
		return true;
	}

	void TryConnect()
	{
		sConn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sConn == INVALID_SOCKET) {
			return;
		}
		UseSocket(sConn);
		sockaddr_in to = Loopback(sPort);
		if (connect(sConn, (sockaddr*)&to, sizeof(to)) == 0) {
			sConnected = true;
			Logf("local: connected to 127.0.0.1:%u", (unsigned)sPort);
		}
		else if (WSAGetLastError() == WSAEWOULDBLOCK) {
			sConnecting = true;
		}
		else {
			closesocket(sConn);
			sConn = INVALID_SOCKET;
		}
	}

	void CheckConnecting()
	{
		fd_set writable, failed;
		FD_ZERO(&writable);
		FD_ZERO(&failed);
		FD_SET(sConn, &writable);
		FD_SET(sConn, &failed);
		timeval none = { 0, 0 };
		if (select(0, nullptr, &writable, &failed, &none) <= 0) {
			return;  // still connecting
		}
		sConnecting = false;
		if (FD_ISSET(sConn, &writable)) {
			sConnected = true;
			Logf("local: connected to 127.0.0.1:%u", (unsigned)sPort);
		}
		else {
			closesocket(sConn);  // refused: the host is not up yet
			sConn = INVALID_SOCKET;
		}
	}

	void Flush()
	{
		while (sOutSent < sOut.size()) {
			int n = send(sConn, (const char*)sOut.data() + sOutSent, (int)(sOut.size() - sOutSent), 0);
			if (n <= 0) {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					CloseConn("send failed");
				}
				return;
			}
			sOutSent += n;
		}
		sOut.clear();
		sOutSent = 0;
	}

	void Read()
	{
		char buffer[64 * 1024];
		for (;;) {
			int n = recv(sConn, buffer, sizeof(buffer), 0);
			if (n == 0 || (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) {
				CloseConn(n == 0 ? "the other game closed it" : "receive failed");
				return;
			}
			if (n < 0) {
				break;
			}
			sIn.insert(sIn.end(), buffer, buffer + n);
		}
		size_t at = 0;
		while (sIn.size() - at >= 5) {
			uint32_t size;
			memcpy(&size, sIn.data() + at, 4);
			uint8_t channel = sIn[at + 4];
			if (size > kMaxMessage || channel >= kChannels) {
				CloseConn("bad frame");
				return;
			}
			if (sIn.size() - at - 5 < size) {
				break;
			}
			const uint8_t* body = sIn.data() + at + 5;
			sQueues[channel].items.emplace_back(body, body + size);
			at += 5 + size;
		}
		sIn.erase(sIn.begin(), sIn.begin() + at);
	}
}

namespace LocalNet
{
	bool Host(uint16_t port)
	{
		Stop();
		if (!StartWinsock()) {
			return false;
		}
		sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in local = Loopback(port);
		if (sListen == INVALID_SOCKET || bind(sListen, (sockaddr*)&local, sizeof(local)) != 0 || listen(sListen, 1) != 0) {
			Logf("local: cannot listen on 127.0.0.1:%u (%d)", (unsigned)port, WSAGetLastError());
			Stop();
			return false;
		}
		UseSocket(sListen);
		sHost = true;
		sPort = port;
		Logf("local: hosting on 127.0.0.1:%u", (unsigned)port);
		return true;
	}

	bool Join(uint16_t port)
	{
		Stop();
		if (!StartWinsock()) {
			return false;
		}
		sHost = false;
		sPort = port;
		sNextTry = GetTickCount();
		Logf("local: joining 127.0.0.1:%u", (unsigned)port);
		return true;
	}

	void Stop()
	{
		CloseConn("stopped");
		if (sListen != INVALID_SOCKET) {
			closesocket(sListen);
			sListen = INVALID_SOCKET;
		}
		for (Queue& q : sQueues) {
			q.items.clear();
			q.head = 0;
		}
		sPort = 0;
		if (sStarted) {
			WSACleanup();
			sStarted = false;
		}
	}

	void Pump()
	{
		if (!sPort) {
			return;
		}
		if (sHost && sListen != INVALID_SOCKET) {
			SOCKET incoming = accept(sListen, nullptr, nullptr);
			if (incoming != INVALID_SOCKET) {
				CloseConn("a new guest connected");  // one guest; a new connection replaces the old one
				UseSocket(incoming);
				sConn = incoming;
				sConnected = true;
				Logf("local: a guest connected");
			}
		}
		if (!sHost && !sConnected) {
			if (sConnecting) {
				CheckConnecting();
			}
			else if (sConn == INVALID_SOCKET && (int)(GetTickCount() - sNextTry) >= 0) {
				sNextTry = GetTickCount() + kRetryMs;
				TryConnect();
			}
		}
		if (sConnected) {
			Flush();
		}
		if (sConnected) {
			Read();
		}
	}

	bool Connected()
	{
		return sConnected;
	}

	bool Send(int channel, const void* data, uint32_t size, bool reliable)
	{
		if (!sPort || channel < 0 || channel >= kChannels || size > kMaxMessage) {
			return false;
		}
		if (!reliable && (!sConnected || sOut.size() - sOutSent > kUnreliableLimit)) {
			return true;  // dropped, as an unreliable message may be
		}
		uint8_t header[5];
		memcpy(header, &size, 4);
		header[4] = (uint8_t)channel;
		sOut.insert(sOut.end(), header, header + 5);
		sOut.insert(sOut.end(), (const uint8_t*)data, (const uint8_t*)data + size);
		return true;
	}

	bool Receive(int channel, std::vector<uint8_t>& out)
	{
		if (channel < 0 || channel >= kChannels) {
			return false;
		}
		Queue& q = sQueues[channel];
		if (q.head >= q.items.size()) {
			if (!q.items.empty()) {
				q.items.clear();
				q.head = 0;
			}
			return false;
		}
		out.swap(q.items[q.head]);
		std::vector<uint8_t>().swap(q.items[q.head]);
		++q.head;
		return true;
	}

	void Shutdown()
	{
		Stop();
		std::vector<uint8_t>().swap(sOut);
		std::vector<uint8_t>().swap(sIn);
		for (Queue& q : sQueues) {
			std::vector<std::vector<uint8_t>>().swap(q.items);
		}
	}
}
