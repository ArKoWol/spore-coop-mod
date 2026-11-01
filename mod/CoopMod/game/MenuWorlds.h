// MenuWorlds.h : the main menu's galaxy (GGE mode): the selected star and the mode-change log.
//
// The main menu lists saved games as stars with kStarFlagSaveGame (StarManager::mSavedGameStarRecords, docs/FACTS.md section 6).
// In a co-op launch Game0 is the host's world; the guest's CO-OP window (game/CoopMenu.cpp) adds the CO-OP star to that galaxy
// (in memory, decision D24), names the planet to play and selects a star here (spike J9, docs/features/JOIN_FLOW.md section 4).
// The player's own galaxy is never changed by the mod.
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
	// and that planet's name (UTF-8); false if there is none (or the list is still empty). The CO-OP star is never chosen.
	bool HostPlanet(uint32_t& starId, std::string& planetName);

	// Co-op launch only (D24): makes a free star near the galaxy's centre a world star named `name` (UTF-8) with planets like the
	// host's system and a save "CO-OP.spo", an exact copy of the host's planet's save, so Play on its card enters the host's world.
	// Lives in memory only; Game0 is the host's world kept as a cache. Returns its StarID, 0 on failure.
	uint32_t AddCoopStar(uint32_t hostStar, const std::string& name);
	void RenameCoopStar(const std::string& name);
	// true while the CO-OP star is in the saved-game list (leaving a game without saving reloads the galaxy and drops it, and a
	// star added after that is not drawn: docs/FACTS.md section 6)
	bool CoopStarListed();
	// the galaxy dropped the star: forgets it and deletes CO-OP.spo (the game itself is not touched)
	void ForgetCoopStar();
	// Dispose: takes the CO-OP star out of the list again (flags and name restored, CO-OP.spo deleted)
	void RemoveCoopStar();

	// every frame: logs changes of the menu's selected star (`select:` lines)
	void Update();

	// AttachDetours: logs every mode change (`mode:` lines with the callers) and tells LoadTrace about Play
	void AttachStartupHooks();

	// Dispose, between PrepareDetours and CommitDetours: removes the hooks
	void DetachHook();
}
