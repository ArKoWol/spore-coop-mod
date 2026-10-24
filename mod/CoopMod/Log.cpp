// Log.cpp : printf-style logging into CoopMod.log (in the game's working directory).
#include "stdafx.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>

void Logf(const char* format, ...)
{
	if (FILE* f = fopen("CoopMod.log", "a")) {
		va_list args;
		va_start(args, format);
		vfprintf(f, format, args);
		va_end(args);
		fputc('\n', f);
		fclose(f);
	}
}
