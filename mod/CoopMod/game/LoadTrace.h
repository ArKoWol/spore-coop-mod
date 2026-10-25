// LoadTrace.h : logs how the game loads a saved world (research for joining a friend's world without a restart,
// docs/features/JOIN_FLOW.md). From the Play click (SetActiveModeByName("LoadGame")) until a few seconds after the
// creature stage starts, it logs the planet database records the game opens, the planet record resources it asks for,
// the planet/star record lookups and the planets made from records. Lines start with "load:". Off unless the environment
// variable COOP_LOAD_TRACE is set (it hooks resource lookups).
#pragma once

#include <windows.h>

namespace LoadTrace
{
	// AttachDetours, between PrepareDetours and CommitDetours
	void Attach();
	// Dispose, between PrepareDetours and CommitDetours
	void Detach();

	// the menu's Play was pressed (SetActiveModeByName("LoadGame")): start logging
	void Begin(const char* what);
	// a mode change by name, to end the trace a while after the game is entered
	void OnMode(const char* name);

	// once the planet databases exist: hooks Database::OpenRecord of their class
	void HookDatabases(HMODULE module);
}
