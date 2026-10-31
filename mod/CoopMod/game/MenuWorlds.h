// MenuWorlds.h : the main menu's galaxy (GGE mode): the selected star and the mode-change log.
//
// The main menu lists saved games as stars with kStarFlagSaveGame (StarManager::mSavedGameStarRecords, docs/FACTS.md section 6).
// In a co-op launch Game0 is the host's world; the guest's CO-OP window (game/CoopMenu.cpp) names the planet to play and
// selects its star here (spike J9, docs/features/JOIN_FLOW.md section 4). The galaxy itself is never changed by the mod.
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

namespace MenuWorlds
{
	// the StarID selected in the main menu's galaxy ([GlobalGGEUI + 0xFC] + 0x7C), 0 if unknown
	uint32_t SelectedStar();

	// spike J9: writes the StarID at the place the menu reads its selection from; false if the menu object is not there yet.
	// Logs the value before, the one written and the one read back (`menu: J9:` line). Nothing else is called.
	bool SelectStar(uint32_t starId);

	// the saved-game star the host plays: the newest creature-stage save of Game0 (newest .spo, named after its played planet)
	// and that planet's name (UTF-8); false if there is none (or the list is still empty)
	bool HostPlanet(uint32_t& starId, std::string& planetName);

	// every frame: logs changes of the menu's selected star (`select:` lines)
	void Update();

	// AttachDetours: logs every mode change (`mode:` lines with the callers) and tells LoadTrace about Play
	void AttachStartupHooks();

	// Dispose, between PrepareDetours and CommitDetours: removes the hooks
	void DetachHook();
}
