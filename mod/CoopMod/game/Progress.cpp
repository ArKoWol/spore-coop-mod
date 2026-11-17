// Progress.cpp : see Progress.h.
#include "stdafx.h"
#include "Progress.h"
#include "../Log.h"

#include <Spore\Simulator\cCreatureGameData.h>

#include <cmath>

namespace
{
	const DWORD kCheckEveryMs = 250;
	const int kEventTypes = 8;         // events 0..7 (FACTS.md section 9); others are counted as type 7

	// plain values only: no static destructor may run at process exit (FACTS.md section 3)
	bool sHave = false;                // sDna holds a value read in this stage
	float sDna = 0.0f, sStartDna = 0.0f;
	float sGained = 0.0f;
	DWORD sLastCheck = 0;
	int sLastEvent = -1;
	bool sLastAllowed = false;
	void* sLastCreature = nullptr;
	DWORD sLastEventAt = 0;
	int sReached[kEventTypes] = {}, sDropped[kEventTypes] = {};
	int sLogged = 0;                   // dna lines written (capped, the counts keep going)

	bool ReadDna(float& out)
	{
		__try {
			Simulator::cCreatureGameData* data = Simulator::cCreatureGameData::Get();
			if (!data) {
				return false;
			}
			out = data->mEvolutionPoints;
			return std::isfinite(out) != 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	const char* EventName(int e)
	{
		switch (e) {
		case 0: return "killed";
		case 2: return "attacked";
		case 3: return "impressed";
		case 5: return "killed(5)";
		case 6: return "nest(6)";
		case 7: return "nest(7)";
		default: return "other";
		}
	}
}

namespace Progress
{
	void NoteRelation(int event, bool allowed, void* creature)
	{
		const int t = event >= 0 && event < kEventTypes ? event : kEventTypes - 1;
		(allowed ? sReached : sDropped)[t]++;
		sLastEvent = event;
		sLastAllowed = allowed;
		sLastCreature = creature;
		sLastEventAt = GetTickCount();
	}

	void Update(bool inGame)
	{
		if (!inGame) {
			sHave = false;
			return;
		}
		const DWORD now = GetTickCount();
		if (now - sLastCheck < kCheckEveryMs) {
			return;
		}
		sLastCheck = now;
		float dna = 0.0f;
		if (!ReadDna(dna)) {
			return;
		}
		if (!sHave) {
			sHave = true;
			sDna = sStartDna = dna;
			sGained = 0.0f;
			Logf("dna: %.1f at the start of the stage", dna);
			return;
		}
		const float delta = dna - sDna;
		if (std::fabs(delta) < 0.01f) {
			return;
		}
		sDna = dna;
		if (delta > 0.0f) {
			sGained += delta;
		}
		if (sLogged < 400) {
			++sLogged;
			if (sLastEvent >= 0 && now - sLastEventAt < 5000) {
				Logf("dna: %+.1f (now %.1f), %u ms after relation event %d %s on %p, %s", delta, dna, (unsigned)(now - sLastEventAt), sLastEvent,
					EventName(sLastEvent), sLastCreature, sLastAllowed ? "reached the game" : "DROPPED");
			}
			else {
				Logf("dna: %+.1f (now %.1f), no relation event in the last 5 s (eating, an editor, a kill paid in TakeDamage)", delta, dna);
			}
		}
	}

	void Report()
	{
		float dna = 0.0f;
		const bool have = ReadDna(dna);
		Logf("dna: report: now %.1f%s, gained %.1f since the stage started (start %.1f)", have ? dna : 0.0f, have ? "" : " (not readable)", sGained,
			sStartDna);
		for (int t = 0; t < kEventTypes; ++t) {
			if (sReached[t] || sDropped[t]) {
				Logf("dna: report: event %d %s: %d reached the game, %d dropped", t, EventName(t), sReached[t], sDropped[t]);
			}
		}
		App::ConsolePrintF("coop dna: now %.1f, gained %.1f; events written to CoopMod.log (dna: lines)", have ? dna : 0.0f, sGained);
	}
}
