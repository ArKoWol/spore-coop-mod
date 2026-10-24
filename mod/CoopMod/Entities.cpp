// Entities.cpp : looking at the entities (the game's "nouns") of the running simulation (spike S1).
#include "stdafx.h"
#include "Entities.h"
#include "Log.h"

#include <cstdio>
#include <map>
#include <string>

namespace
{
	const char* NounName(uint32_t noun)
	{
		using namespace Simulator;
		switch (noun) {
		case kCreatureAnimal: return "CreatureAnimal";
		case kCreatureCitizen: return "CreatureCitizen";
		case kHerd: return "Herd";
		case kNest: return "Nest";
		case kEgg: return "Egg";
		case kGamePlant: return "GamePlant";
		case kFruit: return "Fruit";
		case kFruitGroup: return "FruitGroup";
		case kRock: return "Rock";
		case kObstacle: return "Obstacle";
		case kInteractiveOrnament: return "InteractiveOrnament";
		case kOrnament: return "Ornament";
		case kMovableDestructibleOrnament: return "MovableDestructibleOrnament";
		case kGameplayMarker: return "GameplayMarker";
		case kAnimalTrap: return "AnimalTrap";
		case kHitSphere: return "HitSphere";
		case kSoundLoopObject: return "SoundLoopObject";
		case kPlaceableSound: return "PlaceableSound";
		case kPlaceableEffect: return "PlaceableEffect";
		case kPlayer: return "Player";
		case kPlayerInventory: return "PlayerInventory";
		case kGameTerrainCursor: return "GameTerrainCursor";
		case kCreatureSpeciesMission: return "CreatureSpeciesMission";
		case kCreatureTutorialMission: return "CreatureTutorialMission";
		case kFindMission: return "FindMission";
		case kGameData: return "GameData";
		case kPlanet: return "Planet";
		case kStar: return "Star";
		case kEmpire: return "Empire";
		case kTribe: return "Tribe";
		default: return "?";
		}
	}
}

void DumpEntities()
{
	std::map<uint32_t, int> counts;
	int total = 0;

	FILE* file = fopen("CoopDump.txt", "w");
	if (file) {
		fprintf(file, "Entity dump. creature stage: %d, avatar: %p\n", Simulator::IsCreatureGame() ? 1 : 0,
			(void*)GameNounManager.GetAvatar());
		fprintf(file, "politicalId noun(hex) name | x y z | extra\n");
	}

	for (auto it = GameNounManager.mNouns.begin(); it != GameNounManager.mNouns.end(); ++it) {
		Simulator::cGameData* object = &*it;
		const uint32_t noun = object->GetNounID();
		++counts[noun];
		++total;
		if (!file) {
			continue;
		}

		fprintf(file, "%08x %08x %s", object->GetPoliticalID(), noun, NounName(noun));

		if (auto spatial = object_cast<Simulator::cSpatialObject>(object)) {
			const Vector3& p = spatial->GetPosition();
			fprintf(file, " | %.2f %.2f %.2f", p.x, p.y, p.z);
		}
		if (auto creature = object_cast<Simulator::cCreatureBase>(object)) {
			fprintf(file, " | species %08x/%08x/%08x", creature->mSpeciesKey.instanceID, creature->mSpeciesKey.typeID,
				creature->mSpeciesKey.groupID);
		}
		if (auto combatant = object_cast<Simulator::cCombatant>(object)) {
			fprintf(file, " | hp %.1f/%.1f", combatant->mHealthPoints, combatant->mMaxHealthPoints);
		}
		if (object == GameNounManager.GetAvatar()) {
			fprintf(file, " | AVATAR");
		}
		fprintf(file, "\n");
	}

	if (file) {
		fclose(file);
	}

	App::ConsolePrintF("coop dump: %d entities in %d kinds (full list in CoopDump.txt)", total, (int)counts.size());
	Logf("dump: %d entities in %d kinds, creature stage %d", total, (int)counts.size(), Simulator::IsCreatureGame() ? 1 : 0);
	for (auto& entry : counts) {
		App::ConsolePrintF("  %-26s %5d  (0x%x)", NounName(entry.first), entry.second, entry.first);
		Logf("dump:   %-26s %5d  (0x%x)", NounName(entry.first), entry.second, entry.first);
	}
}
