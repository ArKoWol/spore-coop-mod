// coop_launch.h : the folder logic of the co-op launch (docs/features/JOIN_FLOW.md section 3), without the game.
//
// A friend's world never replaces the player's own saves. It is kept in Games\Coop\<host id>\ and swapped in as Game0 for
// ONE launch of the game; the player's own Game0 waits as Games\Game0.own and is moved back at the next start, before the
// game opens anything. Nothing is ever deleted: a folder that cannot be moved is reported, and what was done is undone.
//
// Files under the Games directory (all names are relative to it):
//   Game0               the save folder the game uses
//   Game0.own           the player's own Game0 while a co-op launch runs
//   CoopNoOwn.txt       a co-op launch started although the player had no Game0: restore moves the host's Game0 away
//                       and does not invent one
//   Coop\<id>           the host's world as last received (a cache), Coop\<id>.incoming while it downloads
//   CoopLaunch.txt      host id; written before the restart, read and deleted at the next start
//   CoopMigrate.txt     written by the "restore my saves" question of an old install, read and deleted at the next start
//   CoopMigrateAsked.txt  the question was answered (asked once)
//   Game0.coopbackup, Game0.coopbackup2, ...   the backups the old versions made
// A world received from a host carries coop_host.txt (the host's Steam ID).
#pragma once

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace coop
{
	namespace launch
	{
		struct Result
		{
			bool coopLaunch = false;  // Game0 now holds the host's world: this run is a co-op launch
			unsigned long long host = 0;
			bool restored = false;    // the player's own Game0 came back (the last launch was a co-op launch)
			bool migrated = false;    // an old backup was moved back to Game0
			bool failed = false;      // a move failed: no co-op launch, see message
			std::string message;      // for the player (shown in the menu and the console); empty = nothing to say
			std::vector<std::string> log;  // lines for the log, in order
		};

		inline bool Exists(const std::string& path)
		{
			return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		}

		// the host id in a coop_host.txt / CoopLaunch.txt style file, 0 if the file is missing or unreadable
		inline unsigned long long ReadId(const std::string& path)
		{
			unsigned long long id = 0;
			if (FILE* f = fopen(path.c_str(), "r")) {
				if (fscanf(f, "%llu", &id) != 1) {
					id = 0;
				}
				fclose(f);
			}
			return id;
		}

		inline bool WriteId(const std::string& path, unsigned long long id)
		{
			FILE* f = fopen(path.c_str(), "w");
			if (!f) {
				return false;
			}
			fprintf(f, "%llu\n", id);
			return fclose(f) == 0;
		}

		inline bool WriteMarker(const std::string& path)
		{
			FILE* f = fopen(path.c_str(), "w");
			if (!f) {
				return false;
			}
			fprintf(f, "1\n");
			return fclose(f) == 0;
		}

		// Moves a folder; MoveFile never replaces an existing destination. A sharing violation (a virus scanner, the end of
		// the previous game) gets two more tries.
		inline bool Move(const std::string& from, const std::string& to, Result& r)
		{
			DWORD error = 0;
			for (int attempt = 0; attempt < 3; ++attempt) {
				if (MoveFileA(from.c_str(), to.c_str())) {
					r.log.push_back("moved " + from + " -> " + to);
					return true;
				}
				error = GetLastError();
				if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) {
					break;
				}
				Sleep(150);
			}
			r.log.push_back("MOVE FAILED " + from + " -> " + to + " (error " + std::to_string(error) + ")");
			return false;
		}

		struct Layout
		{
			std::string games, game0, own, noOwn, coop, launch, migrate, asked;
			explicit Layout(const std::string& gamesDir)
				: games(gamesDir), game0(gamesDir + "\\Game0"), own(gamesDir + "\\Game0.own"), noOwn(gamesDir + "\\CoopNoOwn.txt"),
				coop(gamesDir + "\\Coop"), launch(gamesDir + "\\CoopLaunch.txt"), migrate(gamesDir + "\\CoopMigrate.txt"),
				asked(gamesDir + "\\CoopMigrateAsked.txt") {}
			std::string HostDir(unsigned long long id) const { return coop + "\\" + std::to_string(id); }
			std::string HostFile(const std::string& saveDir) const { return saveDir + "\\coop_host.txt"; }
		};

		// the first name that is free: base, base.old2, base.old3, ...
		inline std::string FreeName(const std::string& base)
		{
			std::string name = base;
			for (int n = 2; Exists(name) && n < 1000; ++n) {
				name = base + ".old" + std::to_string(n);
			}
			return name;
		}

		// where the host's world goes when it leaves Game0: Coop\<id>, or Coop\unknown-<time> if its id cannot be read
		inline std::string CachePlace(const Layout& L, unsigned long long id, Result& r)
		{
			CreateDirectoryA(L.coop.c_str(), nullptr);
			if (id) {
				return FreeName(L.HostDir(id));
			}
			r.log.push_back("coop_host.txt of Game0 is unreadable, the world goes to a folder named unknown-<time>");
			return FreeName(L.coop + "\\unknown-" + std::to_string((long long)time(nullptr)));
		}

		inline std::string BackupName(int n)
		{
			return n <= 1 ? "Game0.coopbackup" : "Game0.coopbackup" + std::to_string(n);
		}

		// The oldest backup of the old versions that is the player's own save (no coop_host.txt in it): "" if there is none.
		// Backups were numbered with the first free name, so a lower number is older. The others are named in `others`.
		inline std::string OldestOwnBackup(const Layout& L, std::vector<std::string>* others)
		{
			std::string found;
			for (int n = 1; n < 1000; ++n) {
				std::string name = BackupName(n);
				std::string path = L.games + "\\" + name;
				if (!Exists(path)) {
					continue;
				}
				if (found.empty() && !Exists(L.HostFile(path))) {
					found = name;
				}
				else if (others) {
					others->push_back(name);
				}
			}
			return found;
		}

		// ------------------------------------------------------------------ step 0: the restore question of an old install
		// What the question offers: the backup that would come back (e.g. "Game0.coopbackup"), "" if there is nothing to ask.
		inline std::string MigrationOffer(const std::string& gamesDir)
		{
			Layout L(gamesDir);
			if (Exists(L.asked) || Exists(L.own) || Exists(L.noOwn) || Exists(L.migrate) || !Exists(L.game0)) {
				return std::string();
			}
			if (!ReadId(L.HostFile(L.game0))) {
				return std::string();  // Game0 is not a host's world
			}
			return OldestOwnBackup(L, nullptr);
		}

		// the answer "Restore": takes effect at the next start (Prepare)
		inline bool RequestMigration(const std::string& gamesDir)
		{
			return WriteMarker(Layout(gamesDir).migrate);
		}

		// either answer: the question is not asked again
		inline void MarkAsked(const std::string& gamesDir)
		{
			WriteMarker(Layout(gamesDir).asked);
		}

		inline void Migrate(const Layout& L, Result& r)
		{
			DeleteFileA(L.migrate.c_str());  // read once
			if (Exists(L.own) || Exists(L.noOwn)) {
				r.log.push_back("migration: a co-op launch is pending, skipped");
				return;
			}
			unsigned long long id = ReadId(L.HostFile(L.game0));
			std::vector<std::string> others;
			std::string backup = OldestOwnBackup(L, &others);
			if (!id || backup.empty()) {
				r.log.push_back("migration: Game0 is not a host's world or no backup of your own saves is left, nothing moved");
				return;
			}
			std::string cache = CachePlace(L, id, r);
			if (!Move(L.game0, cache, r)) {
				r.message = "Your own saves could not be restored: a game folder is in use. Close Spore completely and try again.";
				return;
			}
			if (!Move(L.games + "\\" + backup, L.game0, r)) {
				Move(cache, L.game0, r);  // undo: the host's world is Game0 again
				r.message = "Your own saves could not be restored: a game folder is in use. Close Spore completely and try again.";
				return;
			}
			r.migrated = true;
			r.log.push_back("migration: " + backup + " is Game0 again, the friend's world is in " + cache);
			for (const std::string& name : others) {
				r.log.push_back("migration: backup " + name + " stays where it is");
			}
			r.message = "Your own saves are back (from " + backup + ").";
		}

		// ------------------------------------------------------------------ step 1: restore
		// The last launch was a co-op launch: the host's world goes back to Coop\<id>, the player's Game0 comes back.
		inline void Restore(const Layout& L, Result& r)
		{
			bool hasOwn = Exists(L.own), noOwnMarker = Exists(L.noOwn), hasGame = Exists(L.game0);
			if (!hasOwn && !noOwnMarker) {
				return;
			}
			std::string cache;
			if (hasGame) {
				unsigned long long id = ReadId(L.HostFile(L.game0));
				if (!hasOwn && !id) {
					// the marker says the player had no Game0, but this one is not a host's world: leave it alone
					r.log.push_back("restore: CoopNoOwn.txt but Game0 is not a host's world, nothing moved");
					DeleteFileA(L.noOwn.c_str());
					return;
				}
				cache = CachePlace(L, id, r);
				if (!Move(L.game0, cache, r)) {
					r.failed = true;
					r.message = "Your own saves are still waiting in Game0.own because a game folder is in use. Close Spore completely "
						"(restart the PC if it stays) and start it again.";
					return;
				}
			}
			if (hasOwn && !Move(L.own, L.game0, r)) {
				if (hasGame && Move(cache, L.game0, r)) {
					r.log.push_back("restore: undone, Game0 is as it was");
				}
				r.failed = true;
				r.message = "Your own saves are still waiting in Game0.own because a game folder is in use. Close Spore completely "
					"(restart the PC if it stays) and start it again.";
				return;
			}
			if (noOwnMarker) {
				DeleteFileA(L.noOwn.c_str());
			}
			r.restored = hasOwn || hasGame;
			r.log.push_back(hasOwn ? "restore: your own Game0 is back"
				: hasGame ? "restore: the host's world left Game0, you had no Game0 of your own" : "restore: nothing to move");
		}

		// ------------------------------------------------------------------ step 2: the co-op launch
		inline void Launch(const Layout& L, Result& r)
		{
			if (!Exists(L.launch)) {
				return;
			}
			unsigned long long id = ReadId(L.launch);
			DeleteFileA(L.launch.c_str());  // read once
			if (!id) {
				r.log.push_back("co-op launch: CoopLaunch.txt is unreadable, starting normally");
				return;
			}
			std::string source = L.HostDir(id);
			if (!Exists(source)) {
				r.log.push_back("co-op launch: " + source + " does not exist, starting normally");
				r.message = "The friend's world is not on this PC. Join the friend again.";
				return;
			}
			bool hasGame = Exists(L.game0);
			if (hasGame) {
				if (Exists(L.own)) {
					r.log.push_back("co-op launch: Game0.own already exists, starting normally");
					r.message = "Could not open the friend's world: Game0.own is in the way. Your own saves were not touched.";
					r.failed = true;
					return;
				}
				if (!Move(L.game0, L.own, r)) {
					r.failed = true;
					r.message = "Could not open the friend's world: a game folder is in use. Your own saves were not touched. "
						"Close Spore completely and join again.";
					return;
				}
			}
			else if (!WriteMarker(L.noOwn)) {
				r.log.push_back("co-op launch: cannot write CoopNoOwn.txt, starting normally");
				r.failed = true;
				r.message = "Could not open the friend's world: no write access to the Games folder.";
				return;
			}
			if (!Move(source, L.game0, r)) {
				bool undone = hasGame ? Move(L.own, L.game0, r) : DeleteFileA(L.noOwn.c_str()) != 0;
				r.log.push_back(undone ? "co-op launch: undone" : "co-op launch: UNDO FAILED, the next start restores (Game0.own)");
				r.failed = true;
				r.message = "Could not open the friend's world: a game folder is in use. Your own saves were not touched. "
					"Close Spore completely and join again.";
				return;
			}
			r.coopLaunch = true;
			r.host = id;
			r.log.push_back("co-op launch: the world of " + std::to_string(id) + " is Game0 for this run");
		}

		// Everything the start-up does with the folders, in order: the restore question's answer, restore, co-op launch.
		// Call it before the game opens anything in Game0.
		inline Result Prepare(const std::string& gamesDir)
		{
			Result r;
			Layout L(gamesDir);
			if (Exists(L.migrate)) {
				Migrate(L, r);
			}
			Restore(L, r);
			if (!r.failed) {
				Launch(L, r);
			}
			else if (Exists(L.launch)) {
				DeleteFileA(L.launch.c_str());
				r.log.push_back("co-op launch: skipped because the restore failed");
			}
			return r;
		}

		// a world was received: the host's id for the restart (the world itself is already in Coop\<id>)
		inline bool WriteLaunch(const std::string& gamesDir, unsigned long long hostId)
		{
			return WriteId(Layout(gamesDir).launch, hostId);
		}
	}
}
