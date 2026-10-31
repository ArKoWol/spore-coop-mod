// CoopMenu.cpp : see CoopMenu.h.
//
// UTFWin has no text window we can create from code, so a window is a picture: GDI+ draws the plate (text, rows,
// buttons) into a PNG, the PNG goes into a small package in %TEMP%\CoopModUI that is registered like the creature model
// packages (Models::WritePackage), and a UTFWin window shows it. Clicks are hit-tested against the rectangles we drew.
// Every change of a plate is a new picture (a new resource key, so the game's image cache never shows an old one).
// Two pictures: the small CO-OP button plate in a corner of the main menu and the CO-OP window anchored to it.
#include "stdafx.h"
#include "CoopMenu.h"
#include "MenuWorlds.h"
#include "CoopNet.h"
#include "CoopLaunch.h"
#include "Presence.h"
#include "../Log.h"
#include "../RecentHosts.h"

#include "UiDraw.h"

#include <ctime>

#include <string>
#include <vector>

using namespace UiDraw;

namespace
{
	const int kWidth = 560;
	const DWORD kAnswerTimeoutMs = 20000;  // no answer from the host: it is not hosting, or Steam cannot reach it
	const DWORD kWorldTimeoutMs = 30000;   // the host answered but no world arrives

	// the CO-OP button plate (a picture with a 8 px margin for the shadow around the plate)
	const int kButtonW = 160, kButtonH = 64;
	const float kPlateW = 144, kPlateH = 46;  // the visible plate inside the picture, at (8, 6)
	const float kCornerGap = 16;              // from the screen edge to the visible plate

	// spike J9: the host's star is written into the menu's selection once per co-op launch, then read back
	const DWORD kStarWaitMs = 20000;  // gives up when the star or the menu object is not there after this long

	enum class State { List, Connecting, Downloading, Restarting, Ready, Leaving, Error };

	// one line of the list (JOIN_FLOW section 5): a section title or a player
	struct Row
	{
		enum Kind { Header, Invitation, Hosting, Mod, Recent };
		Kind kind = Header;
		std::string name;    // UTF-8; a Header's title
		std::string detail;  // second line: the state, or why Join is disabled
		uint64_t steamId = 0;
		bool join = false;     // has a Join button
		bool enabled = true;   // false: the button is grey and cannot be pressed, `detail` says why
		bool warn = false;     // `detail` is a reason (drawn in red)
	};

	struct Button
	{
		Gdiplus::RectF rect;
		int action = 0;  // kCancel, kBack, kById, kLeave, kRefresh, or kJoin + row index
	};
	const int kCancel = 2, kBack = 3, kById = 5, kLeave = 6, kRefresh = 7, kJoin = 100;

	const int kMaxEntries = 8;     // players listed at most (the rest is counted in the window and named in the log)
	const int kRowH = 66, kHeaderH = 26, kBannerH = 30, kEmptyH = 44, kMoreH = 22;

	// heap objects, freed in Shutdown (no static destructors: ModAPI routes new/delete to the game's heap)
	struct Ui
	{
		std::vector<Row> rows;
		int hidden = 0;        // players that did not fit
		std::string banner;    // a line above the list ("Start Steam and log in, then press Refresh"), "" = none
		int contentH = 0;      // pixels the list needs
		std::vector<Button> buttons;
		std::string worldOf;  // name of the friend whose world is (being) installed / played
		std::string planet;   // co-op launch: the planet the host plays ("" = unknown)
		std::string error;
		bool upToDate = false;  // the staged world needed no download (JOIN_FLOW M4)
	};
	Ui* sUi = nullptr;

	State sState = State::List;
	bool sVisible = false;     // the window is wanted (it is hidden outside the main menu and comes back with it)
	bool sDirty = false;
	DWORD sJoinStart = 0;      // when the join was started
	DWORD sHostSeenAt = 0;     // when the host answered
	int sProgressShown = -1;   // percent drawn last
	int sHover = -1;           // action of the button under the mouse (drawn lighter)
	bool sLeaveRequested = false;  // our Leave button (co-op launch): the restart is asked from Update
	bool sJoinRequested = false;   // JoinHost (the toast of an invitation): started from Update
	uint64_t sJoinId = 0;
	std::string* sJoinName = nullptr;  // heap: freed in Shutdown
	unsigned sRevisionShown = 0;       // Presence::Revision() the list was built from
	float sListScreenH = 0;            // the screen height the list was built for (rows that fit)

	bool PanelHandler(const UTFWin::Message& message);
	bool ButtonHandler(const UTFWin::Message& message);
	PictureWindow* sPicture = nullptr;  // the window; heap: freed in Shutdown
	PictureWindow* sButton = nullptr;   // the button plate
	bool sGdi = false;

	// the button plate
	bool sButtonDirty = true;
	bool sButtonHot = false;
	bool sToggleRequested = false;  // the button was clicked: acted on in Update, outside the UI message
	bool sCornerRead = false;
	bool sTop = false, sRight = false;  // the corner: COOP_BUTTON_CORNER = tl, tr, bl (default), br
	float sScreenW = 0, sScreenH = 0;
	float sButtonX = 0, sButtonY = 0;   // top left of the button picture
	bool sButtonLogged = false;

	// co-op launch: the window opens by itself, then the host's star is selected (J9)
	bool sLaunchOpened = false;
	DWORD sLaunchStart = 0;
	DWORD sNameCheckedAt = 0;
	DWORD sStarTriedAt = 0;
	bool sStarFound = false;
	uint32_t sHostStar = 0;
	bool sStarWritten = false;
	DWORD sStarWrittenAt = 0;
	int sStarChecks = 0;       // read-back lines logged so far
	bool sStarDone = false;    // the attempt is over (verdict logged or given up): never repeated
	bool sStarSticks = false;  // the selection still held after the last read-back

	Ui& U()
	{
		if (!sUi) {
			sUi = new Ui();
		}
		return *sUi;
	}

	// The list of the window, in this order (JOIN_FLOW section 5): invitations, friends with a world (Join, or grey with the
	// reason), friends running the mod without a world (state text), recent sessions that are not online with the mod.
	// At most kMaxEntries players and what fits the screen; the rest is counted.
	void LoadFriends()
	{
		Ui& ui = U();
		ui.rows.clear();
		ui.hidden = 0;
		ui.banner.clear();
		sRevisionShown = Presence::Revision();
		const unsigned mine = CoopNet::ProtocolVersion();
		const bool online = Presence::SteamOk() && Presence::Available();

		std::vector<Row> invitations, hosting, mods, recent;
		for (const Presence::Invite& invite : Presence::Invites()) {
			Row r;
			r.kind = Row::Invitation;
			r.name = invite.name;
			r.steamId = invite.host;
			r.join = true;
			r.detail = "invites you to " + (invite.planet.empty() ? std::string("a world") : invite.planet);
			if (const Presence::Friend* f = Presence::FindFriend(invite.host)) {
				coop::presence::Block block = coop::presence::JoinBlock(f->info, mine);
				if (block != coop::presence::Block::None) {
					r.enabled = false;
					r.warn = true;
					r.detail = coop::presence::BlockReason(block, f->info, f->name);
				}
			}
			invitations.push_back(r);
		}
		for (const Presence::Friend& f : Presence::Friends()) {
			bool invited = false;
			for (const Row& r : invitations) {
				invited = invited || r.steamId == f.id;
			}
			if (invited) {
				continue;
			}
			Row r;
			r.name = f.name;
			r.steamId = f.id;
			coop::presence::Block block = coop::presence::JoinBlock(f.info, mine);
			if (coop::presence::HasSession(f.info)) {
				r.kind = Row::Hosting;
				r.join = true;
				r.detail = coop::presence::StateText(f.info);
				if (block != coop::presence::Block::None) {
					r.enabled = false;
					r.warn = true;
					r.detail = coop::presence::BlockReason(block, f.info, f.name);
				}
				hosting.push_back(r);
			}
			else {
				r.kind = Row::Mod;
				r.detail = coop::presence::StateText(f.info);
				if (f.info.protocol != mine) {
					r.warn = true;
					r.detail = coop::presence::BlockReason(coop::presence::Block::Protocol, f.info, f.name);
				}
				mods.push_back(r);
			}
		}
		for (size_t i = 0; i < 10; ++i) {
			uint64_t id = 0;
			long long when = 0;
			char name[128] = {};
			if (!RecentHosts::Get(i, id, name, sizeof(name), when)) {
				break;
			}
			bool listed = Presence::FindFriend(id) != nullptr;
			for (const Row& r : invitations) {
				listed = listed || r.steamId == id;
			}
			if (listed) {
				continue;  // already above, with its state
			}
			Row r;
			r.kind = Row::Recent;
			r.steamId = id;
			r.name = name[0] ? name : std::to_string(id);
			r.join = true;
			char detail[128] = "joined before";
			time_t t = (time_t)when;
			if (struct tm* local = localtime(&t)) {
				strftime(detail, sizeof(detail), online ? "joined %d.%m %H:%M  -  not online with the mod" : "joined %d.%m %H:%M  -  online state not checked", local);
			}
			r.detail = detail;
			recent.push_back(r);
		}

		if (!Presence::SteamOk()) {
			ui.banner = "Start Steam and log in, then press Refresh";  // E1
		}
		else if (!Presence::Available()) {
			ui.banner = "No friends list: steam_api_coop.dll is too old (update.bat)";
		}

		// what fits: the screen's height minus the fixed parts of the window and the button plate
		float screenH = sScreenH > 0 ? sScreenH : 720;
		sListScreenH = sScreenH;
		int budget = (int)screenH - 16 - kButtonH - 140 - 56 - (ui.banner.empty() ? 0 : kBannerH) - kMoreH;
		int entries = 0, used = 0, total = 0;
		const struct { const char* title; std::vector<Row>* rows; } sections[] = {
			{ "Invitations", &invitations }, { "Friends hosting", &hosting }, { "Friends running the mod", &mods },
			{ "Recent sessions", &recent },
		};
		for (const auto& section : sections) {
			total += (int)section.rows->size();
			bool titled = false;
			for (const Row& r : *section.rows) {
				int need = kRowH + (titled ? 0 : kHeaderH);
				if (entries >= kMaxEntries || used + need > budget) {
					++ui.hidden;
					Logf("menu: list: %s '%s' does not fit (at most %d players, %d px free)", section.title, r.name.c_str(), kMaxEntries, budget - used);
					continue;
				}
				if (!titled) {
					titled = true;
					Row header;
					header.kind = Row::Header;
					header.name = section.title;
					ui.rows.push_back(header);
				}
				ui.rows.push_back(r);
				used += need;
				++entries;
			}
		}
		ui.contentH = used + (ui.banner.empty() ? 0 : kBannerH) + (total == 0 ? kEmptyH : 0) + (ui.hidden ? kMoreH : 0);
	}

	bool InMainMenu()
	{
		return GameModeManager.GetActiveModeID() == (uint32_t)GameModeIDs::kGGEMode;
	}

	// ------------------------------------------------------------------ drawing
	// Styled after the game's own star card: a light glossy plate, an orange title with a dark outline, white button
	// labels with an outline, glossy green pill buttons, a blue glossy progress bar.

	void AddButton(Gdiplus::Graphics& g, const Gdiplus::RectF& r, const wchar_t* label, int action, bool primary)
	{
		UiDraw::Button(g, r, label, primary, sHover == action);
		Button b;
		b.rect = r;
		b.action = action;
		U().buttons.push_back(b);
	}

	// draws the window for the current state into PNG bytes
	bool Render(std::vector<uint8_t>& png, int& height)
	{
		Ui& ui = U();
		ui.buttons.clear();
		height = sState == State::List ? 140 + ui.contentH + 56 : 262;
		const int pad = 10;  // room for the shadow around the plate

		Gdiplus::Bitmap bitmap(kWidth, height, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.Clear(C(0, 0, 0, 0));

		// the plate: shadow, dark rim, light glossy body (like the star card)
		Gdiplus::RectF plate((float)pad, (float)pad, (float)(kWidth - 2 * pad), (float)(height - 2 * pad - 4));
		for (int i = 4; i >= 1; --i) {
			FillRound(g, Gdiplus::RectF(plate.X - i, plate.Y - i + 4, plate.Width + 2 * i, plate.Height + 2 * i), 26.0f + i,
				C(22, 0, 0, 0), C(22, 0, 0, 0));
		}
		FillRound(g, plate, 26, C(255, 92, 104, 122), C(255, 58, 66, 82));
		Gdiplus::RectF body(plate.X + 3, plate.Y + 3, plate.Width - 6, plate.Height - 6);
		FillRound(g, body, 23, C(255, 250, 251, 253), C(255, 196, 204, 214));
		Gloss(g, body, 23, 120);

		const C ink(255, 32, 48, 72), soft(255, 92, 106, 126);
		const C orange(255, 255, 196, 64), brown(255, 92, 44, 0), green(255, 60, 150, 30);
		float left = plate.X + 28, right = plate.X + plate.Width - 26, width = right - left;

		Text(g, L"CO-OP", 30, true, orange, Gdiplus::RectF(left, plate.Y + 12, 300, 42), Align::Left, 2.2f, brown);
		Text(g, L"Play in a friend's world. Your own worlds stay untouched.", 14, false, soft,
			Gdiplus::RectF(left, plate.Y + 54, width, 22));

		float y = plate.Y + 86;
		if (sState == State::List) {
			const C red(255, 190, 50, 40);
			if (!ui.banner.empty()) {
				Text(g, Wide(ui.banner), 14, true, red, Gdiplus::RectF(left, y, width, 26));
				y += kBannerH;
			}
			if (ui.rows.empty()) {
				Text(g, L"No friend is online with the mod right now.", 14, false, soft, Gdiplus::RectF(left, y, width, 34));
			}
			for (size_t i = 0; i < ui.rows.size(); ++i) {
				const Row& f = ui.rows[i];
				if (f.kind == Row::Header) {
					Text(g, Wide(f.name), 13, true, soft, Gdiplus::RectF(left - 4, y, width, kHeaderH - 4));
					y += kHeaderH;
					continue;
				}
				Gdiplus::RectF row(left - 10, y, width + 20, 58);
				if (f.kind == Row::Invitation) {
					FillRound(g, row, 16, C(255, 255, 238, 190), C(255, 255, 248, 222));  // warm: somebody asks for you
					StrokeRound(g, row, 16, C(255, 214, 168, 80), 1.2f);
				}
				else {
					FillRound(g, row, 16, C(255, 214, 222, 232), C(255, 236, 240, 246));
					StrokeRound(g, row, 16, C(255, 168, 180, 196), 1.2f);
				}
				float textW = f.join ? width - 150 : width - 20;
				Text(g, Wide(f.name), 18, true, ink, Gdiplus::RectF(row.X + 18, y + 6, textW, 26));
				Text(g, Wide(f.detail), 12.5f, false, f.warn ? red : soft, Gdiplus::RectF(row.X + 18, y + 31, textW, 20));
				if (f.join) {
					Gdiplus::RectF join(row.X + row.Width - 118, y + 12, 104, 34);
					if (f.enabled) {
						AddButton(g, join, L"Join", kJoin + (int)i, true);
					}
					else {
						UiDraw::ButtonDisabled(g, join, L"Join");
					}
				}
				y += kRowH;
			}
			if (ui.hidden) {
				Text(g, Wide("+" + std::to_string(ui.hidden) + " more not shown"), 12.5f, false, soft, Gdiplus::RectF(left, y, width, kMoreH));
			}
			AddButton(g, Gdiplus::RectF(left - 4, plate.Y + plate.Height - 50, 96, 34), L"Refresh", kRefresh, false);
			Text(g, L"Or a copied Steam ID:", 12.5f, false, soft,
				Gdiplus::RectF(left + 100, plate.Y + plate.Height - 48, width - 300, 34));
			AddButton(g, Gdiplus::RectF(right - 196, plate.Y + plate.Height - 50, 196, 34), L"Join by Steam ID", kById, false);
		}
		else if (sState == State::Connecting) {
			Text(g, Wide("Connecting to " + ui.worldOf + "..."), 20, true, ink, Gdiplus::RectF(left, y, width, 30));
			Text(g, L"Your friend must be hosting: creature stage, Host co-op.", 13, false, soft, Gdiplus::RectF(left, y + 36, width, 22));
			AddButton(g, Gdiplus::RectF(right - 110, plate.Y + plate.Height - 52, 110, 36), L"Cancel", kCancel, false);
		}
		else if (sState == State::Restarting) {
			Text(g, Wide(ui.upToDate ? "Your copy of " + ui.worldOf + "'s world is up to date" : ui.worldOf + "'s world is here"), 20, true, green,
				Gdiplus::RectF(left, y, width, 30));
			Text(g, L"The game restarts by itself to open it. Your own worlds are kept.", 13, false, ink, Gdiplus::RectF(left, y + 36, width, 22));
		}
		else if (sState == State::Downloading) {
			float progress = std::min(1.0f, CoopNet::State().progress);
			Text(g, Wide("Joining " + ui.worldOf + "'s world"), 20, true, ink, Gdiplus::RectF(left, y, width, 30));
			Gdiplus::RectF groove(left, y + 42, width, 20);
			FillRound(g, groove, 10, C(255, 70, 80, 98), C(255, 120, 130, 148));
			if (progress > 0.03f) {
				Gdiplus::RectF fill(groove.X + 2, groove.Y + 2, (groove.Width - 4) * progress, groove.Height - 4);
				FillRound(g, fill, 8, C(255, 130, 220, 255), C(255, 30, 130, 215));
				Gloss(g, fill, 8, 140);
			}
			wchar_t line[96];
			swprintf(line, 96, L"Downloading the world   %d%%", (int)(progress * 100));
			Text(g, line, 13, false, soft, Gdiplus::RectF(left, y + 66, width, 22));
			AddButton(g, Gdiplus::RectF(right - 110, plate.Y + plate.Height - 52, 110, 36), L"Cancel", kCancel, false);
		}
		else if (sState == State::Ready) {
			// a co-op launch: the friend's world is Game0 for this run; Play is pressed on the galaxy's own card
			std::string line = (ui.worldOf.empty() ? std::string("Your friend") : ui.worldOf) + "'s world: press Play"
				+ (ui.planet.empty() ? "" : " on " + ui.planet);
			Text(g, Wide(line), line.size() > 38 ? 17.0f : 20.0f, true, green, Gdiplus::RectF(left, y, width, 30));
			Text(g, sStarSticks ? L"Its star is selected for you. If no card shows, click the star." : L"Click its star in the galaxy, then press Play on the card.",
				13, false, ink, Gdiplus::RectF(left, y + 36, width, 22));
			Text(g, L"It is a copy: the host's game keeps the real world.", 12.5f, false, soft, Gdiplus::RectF(left, y + 62, width, 20));
			// the way out is Leave (the game restarts in the own world)
			AddButton(g, Gdiplus::RectF(right - 110, y + 92, 110, 36), L"Leave", kLeave, false);
		}
		else if (sState == State::Leaving) {
			Text(g, Wide("Leaving " + ui.worldOf + "'s world"), 20, true, ink, Gdiplus::RectF(left, y, width, 30));
			Text(g, L"The game restarts and opens your own worlds.", 13, false, soft, Gdiplus::RectF(left, y + 36, width, 22));
		}
		else {
			Text(g, L"Could not join", 21, true, C(255, 200, 50, 40), Gdiplus::RectF(left, y, width, 30));
			Text(g, Wide(ui.error), 14, false, ink, Gdiplus::RectF(left, y + 36, width, 40));
			AddButton(g, Gdiplus::RectF(left - 4, plate.Y + plate.Height - 52, 110, 36), L"Back", kBack, false);
		}

		return EncodePng(bitmap, png);
	}

	// the CO-OP button inside its picture
	Gdiplus::RectF ButtonRect()
	{
		return Gdiplus::RectF(18, 14, kPlateW - 20, 30);
	}

	bool RenderButton(std::vector<uint8_t>& png)
	{
		Gdiplus::Bitmap bitmap(kButtonW, kButtonH, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.Clear(C(0, 0, 0, 0));
		Plate(g, Gdiplus::RectF(8, 6, kPlateW, kPlateH), 18);
		// green = open the window, blue while it is open (the same button closes it)
		UiDraw::Button(g, ButtonRect(), L"CO-OP", !sVisible, sButtonHot, 16);
		return EncodePng(bitmap, png);
	}

	// ------------------------------------------------------------------ the join

	void BeginJoin(uint64_t steamId, const std::string& name)
	{
		Ui& ui = U();
		ui.worldOf = name;
		Logf("menu: join %s (%llu)", name.c_str(), (unsigned long long)steamId);
		Presence::DismissInvite(steamId, true);  // an invitation of this host is answered
		sDirty = true;
		if (!CoopNet::Join(steamId)) {
			ui.error = "Steam did not start. Is Steam running and are you logged in?";
			sState = State::Error;
			return;
		}
		sJoinStart = GetTickCount();
		sHostSeenAt = 0;
		sProgressShown = -1;
		sState = State::Connecting;
	}

	void StartJoin(int index)
	{
		Ui& ui = U();
		if (index < 0 || index >= (int)ui.rows.size() || !ui.rows[index].join || !ui.rows[index].enabled) {
			return;
		}
		BeginJoin(ui.rows[index].steamId, ui.rows[index].name);
	}

	// "Join by Steam ID": the ID comes from the clipboard (17 digits, 7656...); it is added to the list as a friend and joined
	void JoinFromClipboard()
	{
		std::string digits;
		if (OpenClipboard(nullptr)) {
			if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
				if (const wchar_t* text = (const wchar_t*)GlobalLock(data)) {
					for (const wchar_t* c = text; *c && digits.size() < 20; ++c) {
						if (*c >= L'0' && *c <= L'9') {
							digits += (char)*c;
						}
						else if (!digits.empty()) {
							break;
						}
					}
					GlobalUnlock(data);
				}
			}
			CloseClipboard();
		}
		Ui& ui = U();
		if (digits.size() != 17 || digits.compare(0, 4, "7656") != 0) {
			ui.error = "No Steam ID in the clipboard. Copy your friend's Steam ID (17 digits, starts with 7656) and press the button again.";
			sState = State::Error;
			sDirty = true;
			Logf("menu: join by Steam ID: clipboard has no Steam ID ('%s')", digits.c_str());
			return;
		}
		Logf("menu: join by Steam ID %s", digits.c_str());
		std::string name = digits;
		uint64_t steamId = _strtoui64(digits.c_str(), nullptr, 10);
		if (const Presence::Friend* friendOnline = Presence::FindFriend(steamId)) {
			name = friendOnline->name;
		}
		BeginJoin(steamId, name);
	}

	// follows a running join: answer, world download, restart; errors when nothing happens
	void FollowJoin()
	{
		if (sState != State::Connecting && sState != State::Downloading) {
			return;
		}
		Ui& ui = U();
		CoopNet::JoinState s = CoopNet::State();
		DWORD now = GetTickCount();
		if (s.versionMismatch) {
			ui.error = ui.worldOf + " runs another version of the mod. Both run update.bat, then try again.";
		}
		else if (!s.error.empty()) {
			ui.error = s.error;  // the download failed twice or was cut off (game/WorldTransfer.cpp)
		}
		else if (!s.hostSeen && now - sJoinStart > kAnswerTimeoutMs) {
			ui.error = ui.worldOf + " does not answer. Is " + ui.worldOf + " hosting (creature stage, coop host)?";
		}
		else if (s.hostSeen && !s.receiving && sHostSeenAt && now - sHostSeenAt > kWorldTimeoutMs) {
			ui.error = ui.worldOf + " answered but sends no world. Ask " + ui.worldOf + " to type coop sendworld.";
		}
		if (!ui.error.empty()) {
			Logf("menu: join failed: %s", ui.error.c_str());
			CoopNet::Cancel();
			sState = State::Error;
			sDirty = true;
			return;
		}
		if (s.hostSeen && !sHostSeenAt) {
			sHostSeenAt = now;
		}
		if (s.staged) {
			Logf("menu: world of %s %s, the game restarts", ui.worldOf.c_str(), s.upToDate ? "already up to date here" : "received");
			ui.upToDate = s.upToDate;
			sState = State::Restarting;
			sDirty = true;
		}
		else if (s.receiving) {
			int percent = (int)(s.progress * 100);
			if (sState != State::Downloading || percent / 2 != sProgressShown / 2) {
				sState = State::Downloading;
				sProgressShown = percent;
				sDirty = true;
			}
		}
	}

	// ------------------------------------------------------------------ the windows

	void Hide()
	{
		if (sPicture) {
			sPicture->Hide();
		}
		sVisible = false;
		sButtonDirty = true;  // the button's colour follows
	}

	bool PanelHandler(const UTFWin::Message& message)
	{
		if (message.IsType(UTFWin::kMsgMouseDown) || message.IsType(UTFWin::kMsgMouseWheel)) {
			return true;  // keep clicks on the window away from the galaxy
		}
		if (message.IsType(UTFWin::kMsgMouseMove) || message.IsType(UTFWin::kMsgMouseLeave)) {
			int hover = -1;
			if (message.IsType(UTFWin::kMsgMouseMove)) {
				float mx = message.Mouse.mouseX, my = message.Mouse.mouseY;
				for (const Button& b : U().buttons) {
					if (mx >= b.rect.X && mx <= b.rect.X + b.rect.Width && my >= b.rect.Y && my <= b.rect.Y + b.rect.Height) {
						hover = b.action;
					}
				}
			}
			if (hover != sHover) {
				sHover = hover;
				sDirty = true;
			}
			return false;
		}
		if (!message.IsType(UTFWin::kMsgMouseUp)) {
			return false;
		}
		float x = message.Mouse.mouseX, y = message.Mouse.mouseY;
		for (const Button& b : U().buttons) {
			if (x >= b.rect.X && x <= b.rect.X + b.rect.Width && y >= b.rect.Y && y <= b.rect.Y + b.rect.Height) {
				Logf("menu: button %d", b.action);
				if (b.action == kLeave) {
					sLeaveRequested = true;
				}
				else if (b.action == kCancel || b.action == kBack) {
					if (b.action == kCancel) {
						CoopNet::Cancel();
					}
					U().error.clear();
					sState = State::List;
					LoadFriends();
					sDirty = true;
				}
				else if (b.action == kById) {
					JoinFromClipboard();
				}
				else if (b.action == kRefresh) {
					Presence::Refresh();
					sDirty = true;
				}
				else if (b.action >= kJoin) {
					StartJoin(b.action - kJoin);
				}
				break;
			}
		}
		return true;
	}

	bool ButtonHandler(const UTFWin::Message& message)
	{
		if (message.IsType(UTFWin::kMsgMouseDown) || message.IsType(UTFWin::kMsgMouseWheel)) {
			return true;  // the click is ours, not the galaxy's
		}
		float x = message.Mouse.mouseX, y = message.Mouse.mouseY;
		Gdiplus::RectF r = ButtonRect();
		bool inside = x >= r.X && x <= r.X + r.Width && y >= r.Y && y <= r.Y + r.Height;
		if (message.IsType(UTFWin::kMsgMouseMove) || message.IsType(UTFWin::kMsgMouseLeave)) {
			bool hot = inside && message.IsType(UTFWin::kMsgMouseMove);
			if (hot != sButtonHot) {
				sButtonHot = hot;
				sButtonDirty = true;
			}
			return false;
		}
		if (message.IsType(UTFWin::kMsgMouseUp)) {
			if (inside) {
				sToggleRequested = true;
			}
			return true;
		}
		return false;
	}

	void ReadCorner()
	{
		if (sCornerRead) {
			return;
		}
		sCornerRead = true;
		const char* corner = getenv("COOP_BUTTON_CORNER");  // tl, tr, bl, br: a way to move the button without a new build
		if (corner && strlen(corner) == 2) {
			sTop = corner[0] == 't' || corner[0] == 'T';
			sRight = corner[1] == 'r' || corner[1] == 'R';
		}
		else {
			sTop = false;
			sRight = false;
		}
	}

	// the button picture's top left, and the screen rectangle of the visible plate
	void ButtonOrigin(float screenW, float screenH)
	{
		sButtonX = sRight ? screenW - kButtonW - (kCornerGap - 8) : kCornerGap - 8;
		sButtonY = sTop ? kCornerGap - 6 : screenH - kButtonH - (kCornerGap - 12);
	}

	void RedrawButton(UTFWin::IWindow* main)
	{
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		ReadCorner();
		const Math::Rectangle& screen = main->GetArea();
		sScreenW = screen.GetWidth();
		sScreenH = screen.GetHeight();
		ButtonOrigin(sScreenW, sScreenH);
		std::vector<uint8_t> png;
		if (!RenderButton(png)) {
			Logf("menu: drawing the CO-OP button failed");
			return;
		}
		if (!sButton) {
			sButton = new PictureWindow(ButtonHandler);
		}
		if (!sButton->Show(png, sButtonX, sButtonY, kButtonW, kButtonH)) {
			return;
		}
		sButtonDirty = false;
		if (!sButtonLogged) {
			sButtonLogged = true;
			Logf("menu: CO-OP button plate at %.0f,%.0f-%.0f,%.0f (screen %.0fx%.0f, corner %c%c; COOP_BUTTON_CORNER=tl|tr|bl|br moves it)",
				sButtonX + 8, sButtonY + 6, sButtonX + 8 + kPlateW, sButtonY + 6 + kPlateH, sScreenW, sScreenH,
				sTop ? 't' : 'b', sRight ? 'r' : 'l');
		}
	}

	void Redraw()
	{
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		std::vector<uint8_t> png;
		int height = 0;
		if (!Render(png, height)) {
			Logf("menu: drawing the window failed");
			return;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return;
		}
		const Math::Rectangle& screen = main->GetArea();
		ReadCorner();
		ButtonOrigin(screen.GetWidth(), screen.GetHeight());
		// anchored to the button plate: the window's visible edge lines up with the plate's (the picture has a 10 px shadow
		// margin), on the side of the plate that faces the screen's inside
		float plateL = sButtonX + 8, plateT = sButtonY + 6, plateR = plateL + kPlateW, plateB = plateT + kPlateH;
		float x = sRight ? plateR + 10 - kWidth : plateL - 10;
		float y = sTop ? plateB + 8 - 10 : plateT - 8 - (height - 14);
		x = std::max(8.0f, std::min(x, screen.GetWidth() - kWidth - 8.0f));
		y = std::max(8.0f, std::min(y, screen.GetHeight() - height - 8.0f));
		if (!sPicture) {
			sPicture = new PictureWindow(PanelHandler);
		}
		if (!sPicture->Show(png, x, y, kWidth, height)) {
			return;
		}
		sVisible = true;
		sDirty = false;
	}

	void Show()
	{
		if (sState != State::Ready && sState != State::Downloading && sState != State::Leaving && sState != State::Connecting
			&& sState != State::Restarting) {
			sState = State::List;
			LoadFriends();
		}
		sDirty = true;
		sVisible = true;
		sButtonDirty = true;
	}

	// ------------------------------------------------------------------ the co-op launch

	// In a co-op launch (the host's world is Game0): opens the window by itself, names the host's planet and selects the
	// host's star once (spike J9, docs/features/JOIN_FLOW.md section 4).
	void FollowCoopLaunch()
	{
		Ui& ui = U();
		DWORD now = GetTickCount();
		if (!sLaunchOpened) {
			sLaunchOpened = true;
			sLaunchStart = now;
			sState = State::Ready;
			sVisible = true;
			sDirty = true;
			Logf("menu: co-op launch: the window opens by itself");
		}
		if (ui.worldOf.empty() || now - sNameCheckedAt > 2000) {
			sNameCheckedAt = now;  // the Steam name can arrive a few seconds after the join
			std::string name = CoopLaunch::HostName(CoopLaunch::Host());
			if (name != ui.worldOf) {
				ui.worldOf = name;
				sDirty = true;
			}
		}
		if (sStarDone) {
			return;
		}
		if (!sStarWritten) {
			if (now - sLaunchStart > kStarWaitMs) {
				sStarDone = true;
				Logf("menu: J9: gave up after %lu ms (host star found %d): the text names the planet only", (unsigned long)(now - sLaunchStart),
					sStarFound ? 1 : 0);
				return;
			}
			if (!sStarFound && now - sStarTriedAt > 500) {
				sStarTriedAt = now;
				std::string planet;
				if (MenuWorlds::HostPlanet(sHostStar, planet)) {
					sStarFound = true;
					ui.planet = planet;
					sDirty = true;
					Logf("menu: the host's world: planet '%s' on star %08x (newest creature-stage save of Game0)", planet.c_str(), sHostStar);
				}
			}
			// COOP_NO_SELECT turns the spike off (the text alone guides the player) if the raw write ever misbehaves
			if (sStarFound && !getenv("COOP_NO_SELECT") && MenuWorlds::SelectStar(sHostStar)) {
				sStarWritten = true;
				sStarWrittenAt = now;
			}
			return;
		}
		// read back: does the game's selection keep our value (and later: did the galaxy show its card)?
		DWORD since = now - sStarWrittenAt;
		const DWORD checkAt[3] = { 100, 1000, 3000 };
		if (sStarChecks < 3 && since >= checkAt[sStarChecks]) {
			uint32_t now_id = MenuWorlds::SelectedStar();
			Logf("menu: J9: %lu ms after the write the selected star reads %08x (%s)", (unsigned long)since, now_id,
				now_id == sHostStar ? "kept" : "CHANGED, the game does not follow");
			++sStarChecks;
			if (sStarChecks == 3) {
				sStarSticks = now_id == sHostStar;
				sStarDone = true;
				sDirty = true;
				Logf("menu: J9 verdict: %s", sStarSticks ? "the selection holds (check on screen: card, highlight, does Play use it?)"
					: "the selection did not hold; the text is the fallback");
			}
		}
	}
}

namespace CoopMenu
{
	void Update()
	{
		if (!InMainMenu()) {
			// keep the state, show it again in the menu
			Presence::WatchFriends(Presence::kWatchWindow, false);
			if (sPicture && sPicture->Visible()) {
				sPicture->Hide();
			}
			if (sButton && sButton->Visible()) {
				sButton->Hide();
			}
			return;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return;
		}
		if (CoopLaunch::Active()) {
			FollowCoopLaunch();
		}
		if (sToggleRequested) {
			sToggleRequested = false;
			if (sVisible) {
				Logf("menu: CO-OP button: window closed");
				Hide();
			}
			else {
				Logf("menu: CO-OP button: window opened");
				Show();
			}
		}
		if (sLeaveRequested) {
			sLeaveRequested = false;
			if (CoopNet::Leave()) {
				sState = State::Leaving;
				sDirty = true;
			}
		}
		if (sJoinRequested) {
			sJoinRequested = false;
			bool busy = sState == State::Connecting || sState == State::Downloading || sState == State::Restarting
				|| sState == State::Leaving || sState == State::Ready;
			if (busy || !sJoinName) {
				Logf("menu: join of %llu ignored, the window is busy (state %d)", (unsigned long long)sJoinId, (int)sState);
			}
			else {
				sVisible = true;  // the window shows the join
				sButtonDirty = true;
				BeginJoin(sJoinId, *sJoinName);
			}
		}
		FollowJoin();
		const Math::Rectangle& screen = main->GetArea();
		if (screen.GetWidth() != sScreenW || screen.GetHeight() != sScreenH) {
			sButtonDirty = true;  // the screen changed size: place both plates again
			sDirty = true;
		}
		// the list follows the friends: scanned every 5 s while the window shows it, rebuilt when something changed
		bool listShown = sVisible && sState == State::List;
		Presence::WatchFriends(Presence::kWatchWindow, listShown);
		if (listShown && (Presence::Revision() != sRevisionShown || sScreenH != sListScreenH)) {
			LoadFriends();
			sDirty = true;
		}
		if (sButtonDirty || !sButton || !sButton->Visible()) {
			RedrawButton(main);
		}
		if (sVisible && (sDirty || (sPicture && !sPicture->Visible()))) {
			Redraw();
		}
	}

	void Open()
	{
		Show();
		Redraw();
	}

	void JoinHost(uint64_t steamId, const std::string& name)
	{
		// started from Update, outside the UI message of the toast's click
		if (!sJoinName) {
			sJoinName = new std::string();
		}
		*sJoinName = name;
		sJoinId = steamId;
		sJoinRequested = true;
	}

	void Shutdown()
	{
		if (sPicture) {
			sPicture->Remove();
			delete sPicture;
			sPicture = nullptr;
		}
		if (sButton) {
			sButton->Remove();
			delete sButton;
			sButton = nullptr;
		}
		delete sUi;
		sUi = nullptr;
		delete sJoinName;
		sJoinName = nullptr;
		if (sGdi) {
			StopGdi();
			sGdi = false;
		}
	}
}
