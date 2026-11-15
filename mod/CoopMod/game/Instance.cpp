// Instance.cpp : two game copies on one PC (see Instance.h). The mutex and the save folder are the two things the copies
// would otherwise share (docs/FACTS.md section 2).
#include "stdafx.h"
#include "Instance.h"
#include "../Log.h"

#include <shlobj.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "shell32.lib")

namespace
{
	uint16_t PortFromEnv(const char* name, uint16_t fallback)
	{
		const char* text = getenv(name);
		int value = text ? atoi(text) : 0;
		return value > 1024 && value < 65536 ? (uint16_t)value : fallback;
	}

	// Spore refuses to start a second copy ("another instance of the game is already running", the named mutex
	// Global\SporeEdi). With COOP_MULTI the second copy gets an unnamed mutex instead. Only that one: Steam's overlay creates
	// its own named mutexes in the game process, which must stay shared.
	HANDLE(WINAPI* sOrigCreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR) = CreateMutexA;
	bool sMutexHooked = false;

	HANDLE WINAPI CreateMutexA_hook(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name)
	{
		HANDLE handle = sOrigCreateMutexA(attributes, initialOwner, name);
		if (name && strstr(name, "SporeEdi") && handle && GetLastError() == ERROR_ALREADY_EXISTS) {
			Logf("multi: mutex '%s' already existed, using a private one", name);
			CloseHandle(handle);
			handle = sOrigCreateMutexA(attributes, initialOwner, nullptr);
		}
		return handle;
	}

	// The game finds %APPDATA% with SHGetFolderPathW(CSIDL_APPDATA), not from the environment variable (its only shell
	// folder import). With COOP_APPDATA set, that call answers with our folder: saves, GraphicsCache.package, Preferences.
	HRESULT(WINAPI* sOrigSHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR) = SHGetFolderPathW;
	bool sFolderHooked = false;
	wchar_t sAppData[MAX_PATH] = {};  // static buffer, no std::wstring: see the allocator note in dllmain.cpp Dispose

	HRESULT WINAPI SHGetFolderPathW_hook(HWND owner, int folder, HANDLE token, DWORD flags, LPWSTR path)
	{
		static unsigned logged = 0;  // one log line per folder id (bit per id below 32)
		int id = folder & 0xFF;
		if (id < 32 && !(logged & (1u << id))) {
			logged |= 1u << id;
			Logf("multi: the game asks for shell folder %d%s", id, id == CSIDL_APPDATA ? " (APPDATA, redirected)" : "");
		}
		if (id == CSIDL_APPDATA && path) {
			wcsncpy_s(path, MAX_PATH, sAppData, _TRUNCATE);
			if (folder & CSIDL_FLAG_CREATE) {
				CreateDirectoryW(sAppData, nullptr);
			}
			return S_OK;
		}
		return sOrigSHGetFolderPathW(owner, folder, token, flags, path);
	}
}

namespace Instance
{
	void AttachHooks()
	{
		if (getenv("COOP_MULTI")) {
			sMutexHooked = true;
			DetourAttach((PVOID*)&sOrigCreateMutexA, (PVOID)CreateMutexA_hook);
		}
		const wchar_t* appdata = _wgetenv(L"COOP_APPDATA");
		if (appdata && *appdata) {
			wcsncpy_s(sAppData, appdata, _TRUNCATE);
			sFolderHooked = true;
			DetourAttach((PVOID*)&sOrigSHGetFolderPathW, (PVOID)SHGetFolderPathW_hook);
			Logf("multi: APPDATA of this copy is %ls", sAppData);
		}
	}

	void DetachHooks()
	{
		if (sMutexHooked) {
			DetourDetach((PVOID*)&sOrigCreateMutexA, (PVOID)CreateMutexA_hook);
		}
		if (sFolderHooked) {
			DetourDetach((PVOID*)&sOrigSHGetFolderPathW, (PVOID)SHGetFolderPathW_hook);
		}
	}

	uint16_t ConsolePort()
	{
		return PortFromEnv("COOP_CONSOLE_PORT", 27780);
	}

	uint16_t LocalPort()
	{
		return PortFromEnv("COOP_LOCAL_PORT", 27790);
	}

	bool NoSteam()
	{
		return getenv("COOP_NO_STEAM") != nullptr;
	}
}
