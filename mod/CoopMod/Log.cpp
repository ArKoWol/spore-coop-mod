// Log.cpp : printf-style logging into CoopMod.log (in the game's working directory).
#include "stdafx.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>

namespace
{
	const long kMaxStartSize = 300 * 1024;  // a log that is bigger than this when the game starts is moved to CoopMod.old.log

	// The log is appended to, also by the launcher (its "launch" marker) and across the automatic restart, so it is not cleared at
	// every start; but a log of several days (7000 lines in the owner's test) is useless to send. Once per process.
	void RotateIfBig()
	{
		static bool done = false;
		if (done) {
			return;
		}
		done = true;
		if (FILE* f = fopen("CoopMod.log", "rb")) {
			fseek(f, 0, SEEK_END);
			long size = ftell(f);
			fclose(f);
			if (size > kMaxStartSize) {
				remove("CoopMod.old.log");
				rename("CoopMod.log", "CoopMod.old.log");
			}
		}
	}
}

void Logf(const char* format, ...)
{
	RotateIfBig();
	if (FILE* f = fopen("CoopMod.log", "a")) {
		va_list args;
		va_start(args, format);
		vfprintf(f, format, args);
		va_end(args);
		fputc('\n', f);
		fclose(f);
	}
}
