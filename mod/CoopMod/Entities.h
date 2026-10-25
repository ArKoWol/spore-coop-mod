// Entities.h : looking at the entities (the game's "nouns") of the running simulation.
// Spike S1 (docs/PLAN.md section 10): what exists in the world, so it can be replicated.
#pragma once

// Writes a summary to the console and the log, and the full list to CoopDump.txt next to the game exe.
// `coop dump` calls this.
void DumpEntities();
