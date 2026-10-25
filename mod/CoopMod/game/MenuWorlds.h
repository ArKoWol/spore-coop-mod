// MenuWorlds.h : spike "a friend's world in the open universe" (docs/features/JOIN_FLOW.md).
//
// The main menu (GGE mode, the galaxy) shows saved games as stars with kStarFlagSaveGame; StarManager keeps them in
// mSavedGameStarRecords. These commands look at that list and try to add an empty test world to it while the menu is open,
// to see whether the menu can show a new world without restarting the game.
// Result (2026-10-25): it can. The galaxy shows a star added to mSavedGameStarRecords at once, with its card (name, stage,
// date, creature), without any UI rebuild. Calling GlobalGGEUI::InitializeUI/Initialize a second time is NOT needed and
// makes the game crash at exit, so those calls were removed.
#pragma once

#include <windows.h>
#include <string>

namespace Simulator { class cStarRecord; }

namespace MenuWorlds
{
	// coop slots: logs and prints the active mode and every saved-game star (id, name, flags, stage, planets).
	void Dump();

	// coop addworld: copies the first saved-game star's save fields onto a free star of the same kind, names it testN and
	// adds it to the saved-game list. Nothing is written to disk by us (the game may save it later: use coop delworld).
	// coop addcenter: the same on a star near the galaxy's centre.
	void AddTestWorld(bool nearCenter);

	// For the co-op star (game/CoopMenu.cpp): adds a world star named `name` (also its played planet and save file
	// "<name>.spo", a copy of the template save), next to a save or near the centre; null on failure.
	Simulator::cStarRecord* AddWorld(const char* name, bool nearCenter);
	// the save file of a world we added ("" if none)
	std::string SavePath(Simulator::cStarRecord* star);
	// the name shown in the galaxy (the planet and the save file keep theirs)
	void RenameWorld(Simulator::cStarRecord* star, const char* name);
	// drops our record of a world the game already threw away (galaxy reloaded): nothing in the game is touched
	void Forget(Simulator::cStarRecord* star);
	// takes one world out again (flags restored, save file deleted)
	void RemoveWorld(Simulator::cStarRecord* star);

	// coop delworld: removes every test world added in this run (flags restored, taken out of the list).
	void RemoveTestWorlds();

	// coop menureload: enters the main menu mode again (not needed for the list, kept for experiments).
	void ReloadMenu();

	// coop holdplay: hooks cGamePersistenceManager::LoadGame (first use) and toggles holding Play on a test world.
	void SetHoldPlay(HMODULE module);

	// coop playnow: starts the held load.
	void PlayNow();

	// AttachDetours: hooks cGameModeManager::SetActiveMode (logs every mode change; holds leaving the menu when holdplay is on).
	void AttachStartupHooks();

	// the StarID selected in the main menu's galaxy ([GlobalGGEUI + 0xFC] + 0x7C), 0 if unknown
	uint32_t SelectedStar();

	// every frame: logs changes of the menu's selected star (spike)
	void Update();

	// coop ggefind: looks for the newest test world (id / record / planets) in the main menu object, to find where the menu
	// keeps the selected star. Run it once with the star selected and once without.
	void FindSelected();

	// once the game modes exist: hooks the main menu mode's OnEnter (puts the CO-OP star back before the menu is built)
	void HookMenuEnter(HMODULE module);

	// Dispose, between PrepareDetours and CommitDetours: removes the hooks and the message listener.
	void DetachHook();

	// Dispose: drops our references (does not touch the game).
	void Shutdown();
}
