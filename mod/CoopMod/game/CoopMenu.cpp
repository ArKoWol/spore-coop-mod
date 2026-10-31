// CoopMenu.cpp : see CoopMenu.h.
//
// UTFWin has no text window we can create from code, so the window is a picture: GDI+ draws the panel (text, rows,
// buttons) into a PNG, the PNG goes into a small package in %TEMP%\CoopModUI that is registered like the creature model
// packages (Models::WritePackage), and a UTFWin window shows it. Clicks are hit-tested against the rectangles we drew.
// Every change of the panel is a new picture (a new resource key, so the game's image cache never shows an old one).
#include "stdafx.h"
#include "CoopMenu.h"
#include "MenuWorlds.h"
#include "CoopNet.h"
#include "CoopLaunch.h"
#include "../Log.h"
#include "../Models.h"
#include "../RecentHosts.h"

#include <Spore\Simulator\cStarRecord.h>
#include <Spore\Simulator\SubSystem\StarManager.h>

#include "UiDraw.h"

#include <ctime>

#include <string>
#include <vector>

using namespace Simulator;
using namespace UiDraw;

namespace
{
	const char* kStarName = "CO-OP";
	const int kWidth = 560;
	const DWORD kAnswerTimeoutMs = 20000;  // no answer from the host: it is not hosting, or Steam cannot reach it
	const DWORD kWorldTimeoutMs = 30000;   // the host answered but no world arrives
	const float kPlanetGap = 16;  // from the planet's centre window to ours (the planet overlaps our window a little: intended)

	enum class State { List, Connecting, Downloading, Restarting, Ready, Leaving, Error };

	struct Friend
	{
		std::string name;    // UTF-8
		std::string detail;  // second line
		uint64_t steamId = 0;
	};

	struct Button
	{
		Gdiplus::RectF rect;
		int action = 0;  // kCancel, kBack, kPlay, or kJoin + friend index
	};
	const int kCancel = 2, kBack = 3, kPlay = 4, kById = 5, kLeave = 6, kJoin = 100;

	// heap objects, freed in Shutdown (no static destructors: ModAPI routes new/delete to the game's heap)
	struct Ui
	{
		std::vector<Friend> friends;
		std::vector<Button> buttons;
		std::string worldOf;  // name of the friend whose world is (being) installed
		std::string error;
	};
	Ui* sUi = nullptr;

	State sState = State::List;
	bool sVisible = false;
	bool sDirty = false;
	DWORD sJoinStart = 0;      // when the join was started
	DWORD sHostSeenAt = 0;     // when the host answered
	int sProgressShown = -1;   // percent drawn last
	int sHover = -1;  // action of the button under the mouse (drawn lighter)
	bool sPlayRequested = false;  // our Play button: the load starts from Update, outside the UI message
	bool sLeaveRequested = false; // our Leave button (co-op launch): the restart is asked from Update
	DWORD sPressedAt = 0;         // when our Play pressed the card's Play (to try a second way if nothing happened)
	bool sLoadSeen = false;       // the game started the load (HoldPlay saw LoadGame while ready)

	// the card's own Play button (found with coop carddump): hidden while the CO-OP star has no world yet; a direct
	// SetActiveModeByName("LoadGame") crashed, so our Play presses this one
	const uint32_t kCardPlayButton = 0x0375408c;
	eastl::intrusive_ptr<UTFWin::IWindow>* sHiddenPlay = nullptr;
	DWORD sPlayCheckedAt = 0;

	cStarRecordPtr* sStar = nullptr;  // the CO-OP star (heap: intrusive_ptr has a destructor)
	bool sStarFailed = false;
	uint32_t sLastSelected = 0;

	bool PanelHandler(const UTFWin::Message& message);
	PictureWindow* sPicture = nullptr;  // heap: freed in Shutdown
	bool sGdi = false;


	// the game's card of the selected star: found by its caption ("CO-OP..."), followed so our window sits next to it
	// and closes with it
	bool sCardSeen = false;
	DWORD sCardLostAt = 0;
	Math::Rectangle sCardArea;
	DWORD sCardCheckedAt = 0;
	bool sCardLogged = false;

	Ui& U()
	{
		if (!sUi) {
			sUi = new Ui();
		}
		return *sUi;
	}

	std::string Game0Dir()
	{
		const char* appdata = getenv("APPDATA");
		return std::string(appdata ? appdata : "") + "\\Spore\\Games\\";
	}

	void LoadFriends()
	{
		Ui& ui = U();
		ui.friends.clear();
		for (size_t i = 0; i < 5; ++i) {
			uint64_t id = 0;
			long long when = 0;
			char name[128] = {};
			if (!RecentHosts::Get(i, id, name, sizeof(name), when)) {
				break;
			}
			Friend f;
			f.steamId = id;
			f.name = name[0] ? name : std::to_string(id);
			char detail[128] = "joined before";
			time_t t = (time_t)when;
			if (struct tm* local = localtime(&t)) {
				strftime(detail, sizeof(detail), "joined %d.%m %H:%M  -  online state not checked yet", local);
			}
			f.detail = detail;
			ui.friends.push_back(f);
		}
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

	// the big round green Play button of the game's cards, with a white triangle, and a label next to it
	void AddPlayButton(Gdiplus::Graphics& g, float cx, float cy, float radius, const std::wstring& label, float labelWidth)
	{
		Gdiplus::RectF circle(cx - radius, cy - radius, radius * 2, radius * 2);
		Gdiplus::RectF pill(cx - radius, cy - radius, radius * 2 + labelWidth, radius * 2);
		FillRound(g, Gdiplus::RectF(pill.X, pill.Y + 4, pill.Width, pill.Height), radius, C(80, 0, 0, 0), C(80, 0, 0, 0));
		FillRound(g, pill, radius, C(255, 30, 90, 15), C(255, 20, 60, 8));
		Gdiplus::RectF body(pill.X + 3, pill.Y + 3, pill.Width - 6, pill.Height - 6);
		FillRound(g, body, radius - 3, C(255, 160, 240, 90), C(255, 50, 150, 25));
		Gloss(g, body, radius - 3, 160);
		// triangle
		float s = radius * 0.55f;
		Gdiplus::PointF tri[3] = { Gdiplus::PointF(cx - s * 0.55f, cy - s), Gdiplus::PointF(cx - s * 0.55f, cy + s),
			Gdiplus::PointF(cx + s, cy) };
		Gdiplus::Pen rim(C(255, 25, 80, 10), 3);
		rim.SetLineJoin(Gdiplus::LineJoinRound);
		g.DrawPolygon(&rim, tri, 3);
		Gdiplus::SolidBrush white(C(255, 255, 255, 255));
		g.FillPolygon(&white, tri, 3);
		Text(g, label, 20, true, C(255, 255, 255, 255), Gdiplus::RectF(cx + radius * 0.9f, cy - radius, labelWidth, radius * 2),
			Align::Left, 1.8f, C(255, 25, 80, 10));
		Button b;
		b.rect = pill;
		b.action = kPlay;
		U().buttons.push_back(b);
	}

	// draws the panel for the current state into PNG bytes
	bool Render(std::vector<uint8_t>& png, int& height)
	{
		Ui& ui = U();
		ui.buttons.clear();
		int rows = (int)ui.friends.size();
		height = sState == State::List ? 140 + rows * 66 + 56 : 262;
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

		const C ink(255, 32, 48, 72), soft(255, 92, 106, 126), white(255, 255, 255, 255);
		const C orange(255, 255, 196, 64), brown(255, 92, 44, 0), green(255, 60, 150, 30);
		float left = plate.X + 28, right = plate.X + plate.Width - 26, width = right - left;

		Text(g, L"CO-OP", 30, true, orange, Gdiplus::RectF(left, plate.Y + 12, 300, 42), Align::Left, 2.2f, brown);
		Text(g, L"Play in a friend's world. Your own worlds stay untouched.", 14, false, soft,
			Gdiplus::RectF(left, plate.Y + 54, width, 22));

		float y = plate.Y + 86;
		if (sState == State::List) {
			for (int i = 0; i < rows; ++i) {
				const Friend& f = ui.friends[i];
				Gdiplus::RectF row(left - 10, y, width + 20, 58);
				FillRound(g, row, 16, C(255, 214, 222, 232), C(255, 236, 240, 246));
				StrokeRound(g, row, 16, C(255, 168, 180, 196), 1.2f);
				Text(g, Wide(f.name), 18, true, ink, Gdiplus::RectF(row.X + 18, y + 6, width - 150, 26));
				Text(g, Wide(f.detail), 12.5f, false, soft, Gdiplus::RectF(row.X + 18, y + 31, width - 150, 20));
				AddButton(g, Gdiplus::RectF(row.X + row.Width - 118, y + 12, 104, 34), L"Join", kJoin + i, true);
				y += 66;
			}
			Text(g, L"Copy a friend's Steam ID, then:", 12.5f, false, soft,
				Gdiplus::RectF(left, plate.Y + plate.Height - 48, width - 200, 34));
			AddButton(g, Gdiplus::RectF(right - 196, plate.Y + plate.Height - 50, 196, 34), L"Join by Steam ID", kById, false);
		}
		else if (sState == State::Connecting) {
			Text(g, Wide("Connecting to " + ui.worldOf + "..."), 20, true, ink, Gdiplus::RectF(left, y, width, 30));
			Text(g, L"Your friend must be hosting: creature stage, then coop host.", 13, false, soft, Gdiplus::RectF(left, y + 36, width, 22));
			AddButton(g, Gdiplus::RectF(right - 110, plate.Y + plate.Height - 52, 110, 36), L"Cancel", kCancel, false);
		}
		else if (sState == State::Restarting) {
			Text(g, Wide(ui.worldOf + "'s world is here"), 20, true, green, Gdiplus::RectF(left, y, width, 30));
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
			Text(g, Wide(ui.worldOf + "'s world is ready!"), 21, true, green, Gdiplus::RectF(left, y, width, 30));
			Text(g, L"It is a copy: the host's game keeps the real world.", 12.5f, false, soft, Gdiplus::RectF(left, y + 30, width, 20));
			// the card's own Play stands just left of this window now: point at it
			Gdiplus::PointF arrow[3] = { Gdiplus::PointF(left - 6, y + 92), Gdiplus::PointF(left + 18, y + 76),
				Gdiplus::PointF(left + 18, y + 108) };
			Gdiplus::SolidBrush arrowBrush(green);
			g.FillPolygon(&arrowBrush, arrow, 3);
			Text(g, L"Press Play to enter", 19, true, C(255, 255, 255, 255), Gdiplus::RectF(left + 26, y + 76, 230, 32),
				Align::Left, 1.8f, C(255, 25, 80, 10));
			if (CoopNet::State().installedAtStart) {
				// a co-op launch: the friend's world is Game0 for this run, the way out is Leave (restart in the own world)
				AddButton(g, Gdiplus::RectF(right - 110, y + 74, 110, 36), L"Leave", kLeave, false);
			}
			else {
				AddButton(g, Gdiplus::RectF(right - 200, y + 74, 200, 36), L"Choose another friend", kBack, false);
			}
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
		(void)white;

		return EncodePng(bitmap, png);
	}

	// ------------------------------------------------------------------ the UTFWin window

	void Show();
	bool CardPlayScreenArea(Math::Rectangle& out);


	void StartJoin(int index)
	{
		Ui& ui = U();
		if (index < 0 || index >= (int)ui.friends.size()) {
			return;
		}
		const Friend& f = ui.friends[index];
		ui.worldOf = f.name;
		Logf("menu: join %s (%llu)", f.name.c_str(), (unsigned long long)f.steamId);
		sDirty = true;
		if (!CoopNet::Join(f.steamId)) {
			ui.error = "Steam did not start. Is Steam running and are you logged in?";
			sState = State::Error;
			return;
		}
		sJoinStart = GetTickCount();
		sHostSeenAt = 0;
		sProgressShown = -1;
		sState = State::Connecting;
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
		Friend f;
		f.steamId = _strtoui64(digits.c_str(), nullptr, 10);
		f.name = digits;
		f.detail = "from the clipboard";
		ui.friends.insert(ui.friends.begin(), f);
		Logf("menu: join by Steam ID %s", digits.c_str());
		StartJoin(0);
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
			Logf("menu: world of %s received, the game restarts", ui.worldOf.c_str());
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

	void StartJoin(int index);

	void Hide()
	{
		if (sPicture) {
			sPicture->Hide();
		}
		sVisible = false;
	}

	bool PanelHandler(const UTFWin::Message& message)
	{
		if (message.IsType(UTFWin::kMsgMouseDown) || message.IsType(UTFWin::kMsgMouseWheel)) {
			return true;  // keep clicks on the panel away from the galaxy
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
				if (b.action == kPlay) {
					sPlayRequested = true;
				}
				else if (b.action == kLeave) {
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
				else if (b.action >= kJoin) {
					StartJoin(b.action - kJoin);
				}
				break;
			}
		}
		return true;
	}

	void Redraw()
	{
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		std::vector<uint8_t> png;
		int height = 0;
		if (!Render(png, height)) {
			Logf("menu: drawing the panel failed");
			return;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return;
		}
		const Math::Rectangle& screen = main->GetArea();
		// next to the CO-OP star's card (the card's 3D planet is drawn over any UI, so the window must not cover it);
		// without a card: right of the centre
		float x, y;
		Math::Rectangle playArea;
		if (sState == State::Ready && CardPlayScreenArea(playArea)) {
			x = playArea.x2 + 4;
			y = (playArea.y1 + playArea.y2) / 2 - height / 2.0f;
		}
		else if (sCardSeen && sCardArea.GetWidth() > 0) {
			// sCardArea is the small window at the planet's centre: the window starts just right of it, so the planet's
			// 3D model (drawn over any UI) overlaps the window's left part a little; vertically centred on the planet
			float cy = (sCardArea.y1 + sCardArea.y2) / 2;
			x = sCardArea.x2 + kPlanetGap;
			y = cy - height / 2.0f;
			if (x + kWidth > screen.GetWidth() - 8) {
				x = sCardArea.x1 - kPlanetGap - kWidth;  // no room on the right: left of the planet
			}
		}
		else {
			x = screen.GetWidth() / 2 + 300;
			y = (screen.GetHeight() - height) / 2;
		}
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
		if (sState != State::Ready && sState != State::Downloading && sState != State::Leaving) {
			sState = State::List;
			LoadFriends();
		}
		sDirty = true;
		sVisible = true;
	}

	// takes stars named "CO-OP..." out of the saved-game list that an earlier run left in the galaxy (if the game saved it)
	void RemoveStaleStars()
	{
		cStarManager* stars = cStarManager::Get();
		if (!stars) {
			return;
		}
		auto& list = stars->mSavedGameStarRecords;
		for (size_t i = 0; i < list.size();) {
			cStarRecord* s = list[i].get();
			bool coop = s && s->mName.size() >= 5 && s->mName[0] == u'C' && s->mName[1] == u'O' && s->mName[2] == u'-'
				&& s->mName[3] == u'O' && s->mName[4] == u'P';
			if (coop && !(sStar && sStar->get() == s)) {
				Logf("menu: removing a CO-OP star left from an earlier run (%08x)", s->mKey.internalValue);
				s->mFlags &= ~(kStarFlagSaveGame | kStarFlagPotentialSaveGame);
				list.erase(list.begin() + i);
				continue;
			}
			++i;
		}
	}

	bool IsCoopCaption(const char16_t* c)
	{
		return c && c[0] == u'C' && c[1] == u'O' && c[2] == u'-' && c[3] == u'O' && c[4] == u'P';
	}

	// a visible window (not ours) whose caption starts with "CO-OP"; invisible subtrees are skipped
	UTFWin::IWindow* FindCoopCaption(UTFWin::IWindow* window, int depth)
	{
		if (!window || (sPicture && window == sPicture->Window()) || depth > 14 || !(window->GetFlags() & UTFWin::kWinFlagVisible)) {
			return nullptr;
		}
		if (depth > 0 && IsCoopCaption(window->GetCaption())) {
			return window;
		}
		for (UTFWin::IWindow* child : window->children()) {
			if (UTFWin::IWindow* found = FindCoopCaption(child, depth + 1)) {
				return found;
			}
		}
		return nullptr;
	}

	// true while the card of the CO-OP star is on screen; sCardArea = its area (the caption's ancestors that are
	// still card-sized)
	bool CardVisible()
	{
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		UTFWin::IWindow* caption = main ? FindCoopCaption(main, 0) : nullptr;
		if (!caption) {
			return false;
		}
		float screenW = main->GetArea().GetWidth();
		UTFWin::IWindow* card = caption;
		for (int up = 0; up < 6; ++up) {
			UTFWin::IWindow* parent = card->GetParent();
			if (!parent || parent == main || parent->GetRealArea().GetWidth() > screenW * 0.5f) {
				break;
			}
			card = parent;
		}
		sCardArea = card->GetRealArea();
		if (!sCardLogged) {
			sCardLogged = true;
			Logf("menu: star card found (caption window %08x, card %08x at %.0f,%.0f-%.0f,%.0f)", caption->GetControlID(),
				card->GetControlID(), sCardArea.x1, sCardArea.y1, sCardArea.x2, sCardArea.y2);
		}
		return true;
	}

	void DumpTree(UTFWin::IWindow* w, int depth, int maxDepth)
	{
		if (!w || depth > maxDepth) {
			return;
		}
		const Math::Rectangle& a = w->GetRealArea();
		std::string caption;
		if (const char16_t* c = w->GetCaption()) {
			for (; *c && caption.size() < 40; ++c) {
				caption += *c < 128 ? (char)*c : '?';
			}
		}
		Logf("card: %*s%08x %s%s vis %d area %.0f,%.0f-%.0f,%.0f '%s'", depth * 2, "", w->GetControlID(),
			w->Cast(UTFWin::IButton::TYPE) ? "BUTTON " : "", (sPicture && w == sPicture->Window()) ? "OURS " : "", (w->GetFlags() & UTFWin::kWinFlagVisible) ? 1 : 0,
			a.x1, a.y1, a.x2, a.y2, caption.c_str());
		for (UTFWin::IWindow* child : w->children()) {
			DumpTree(child, depth + 1, maxDepth);
		}
	}

	// the Play button of the card that shows the CO-OP star (its caption's container holds it)
	UTFWin::IWindow* FindCardPlay()
	{
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		UTFWin::IWindow* caption = main ? FindCoopCaption(main, 0) : nullptr;
		UTFWin::IWindow* container = caption ? caption->GetParent() : nullptr;
		if (!container) {
			return nullptr;
		}
		for (UTFWin::IWindow* child : container->children()) {
			if (child->GetControlID() == kCardPlayButton && child->Cast(UTFWin::IButton::TYPE)) {
				return child;
			}
		}
		return nullptr;
	}

	// the card's Play in screen coordinates (window areas are relative to their parent)
	bool CardPlayScreenArea(Math::Rectangle& out)
	{
		UTFWin::IWindow* play = FindCardPlay();
		if (!play) {
			return false;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		float ox = 0, oy = 0;
		for (UTFWin::IWindow* w = play->GetParent(); w && w != main; w = w->GetParent()) {
			ox += w->GetRealArea().x1;
			oy += w->GetRealArea().y1;
		}
		const Math::Rectangle& a = play->GetRealArea();
		out = Math::Rectangle(a.x1 + ox, a.y1 + oy, a.x2 + ox, a.y2 + oy);
		return true;
	}

	void RestoreCardPlay()
	{
		if (sHiddenPlay) {
			if (*sHiddenPlay) {
				(*sHiddenPlay)->SetVisible(true);
			}
			delete sHiddenPlay;
			sHiddenPlay = nullptr;
			Logf("menu: the card's Play is shown again");
		}
	}

	// sends a left click (down + up at its centre) through a window, as if the player clicked it
	void ClickWindow(UTFWin::IWindow* window)
	{
		const Math::Rectangle& a = window->GetRealArea();
		UTFWin::Message m;
		memset(&m, 0, sizeof(m));
		m.source = window;
		m.eventType = UTFWin::kMsgMouseDown;
		m.Mouse.mouseX = a.GetWidth() / 2;
		m.Mouse.mouseY = a.GetHeight() / 2;
		m.Mouse.mouseState = kMouseLeftButtonDown;
		m.Mouse.mouseButton = kMouseButtonLeft;
		bool down = window->SendMsg(m);
		m.eventType = UTFWin::kMsgMouseUp;
		m.Mouse.mouseState = 0;
		bool up = window->SendMsg(m);
		Logf("menu: clicked the card's Play (down %d, up %d)", down ? 1 : 0, up ? 1 : 0);
	}

	// second way: a button-click message with the button's command id, up through its parents
	void SendButtonClick(UTFWin::IWindow* window)
	{
		UTFWin::Message m;
		memset(&m, 0, sizeof(m));
		m.source = window;
		m.eventType = UTFWin::kMsgButtonClick;
		m.ButtonClick.commandID = window->GetCommandID();
		int level = 0;
		for (UTFWin::IWindow* w = window; w; w = w->GetParent(), ++level) {
			if (w->SendMsg(m)) {
				Logf("menu: button-click message (command %08x) handled at level %d", m.ButtonClick.commandID, level);
				return;
			}
		}
		Logf("menu: button-click message (command %08x) not handled", m.ButtonClick.commandID);
	}

	bool InMainMenu()
	{
		return GameModeManager.GetActiveModeID() == (uint32_t)GameModeIDs::kGGEMode;
	}
}

namespace CoopMenu
{
	void EnsureStar()
	{
		// leaving a game without saving reloads the galaxy from disk: our star is no longer in the saved-game list
		if (sStar) {
			bool listed = false;
			if (cStarManager* stars = cStarManager::Get()) {
				for (const cStarRecordPtr& s : stars->mSavedGameStarRecords) {
					listed = listed || s.get() == sStar->get();
				}
			}
			if (!listed) {
				Logf("menu: the CO-OP star is gone from the galaxy (reloaded?), adding it again");
				MenuWorlds::Forget(sStar->get());
				delete sStar;
				sStar = nullptr;
				sLastSelected = 0;
			}
		}
		if (!sStar && !sStarFailed) {
			RemoveStaleStars();
			// a friend's world already installed survives the new star (AddWorld copies the template over the save)
			std::string keep = Game0Dir() + "Game0\\CO-OP.keep", save = Game0Dir() + "Game0\\CO-OP.spo";
			bool kept = sState == State::Ready && MoveFileExA(save.c_str(), keep.c_str(), MOVEFILE_REPLACE_EXISTING);
			cStarRecord* star = MenuWorlds::AddWorld(kStarName, true);
			if (kept) {
				MoveFileExA(keep.c_str(), save.c_str(), MOVEFILE_REPLACE_EXISTING);
				if (star) {
					MenuWorlds::RenameWorld(star, ("CO-OP: " + U().worldOf).c_str());
				}
			}
			if (star) {
				sStar = new cStarRecordPtr(star);
				Logf("menu: CO-OP star %08x added", star->mKey.internalValue);
				CoopNet::JoinState s = CoopNet::State();
				if (getenv("COOP_MOVE_SAVE")) {
					U().worldOf = "Moved save";  // research (COOP_MOVE_SAVE): play the moved copy of the own save at once
					sState = State::Ready;
				}
				else if (s.installedAtStart && s.host && sState != State::Ready) {
					// the host's world was installed at this start (and the mod joins the host again): the CO-OP star is a
					// copy of the host's newest creature save, so Play enters the host's world
					U().worldOf = CoopLaunch::HostName(s.host);
					MenuWorlds::RenameWorld(star, ("CO-OP: " + U().worldOf).c_str());
					sState = State::Ready;
					sDirty = true;
					Logf("menu: the host's world (%s) is installed: CO-OP star ready", U().worldOf.c_str());
				}
			}
			else {
				sStarFailed = true;  // e.g. a new universe without any save to copy from
				Logf("menu: no CO-OP star (no saved game to use as the template)");
			}
		}
	}

	void Update()
	{
		// every frame in every mode: leaving a game without saving reloads the galaxy from disk, and the menu builds its
		// star markers right then, before it is entered; re-adding later is not drawn
		if (sStar) {
			EnsureStar();
		}
		if (!InMainMenu()) {
			if (sVisible && sPicture) {
				sPicture->Hide();  // keep the state, show it again in the menu
			}
			return;
		}
		EnsureStar();
		uint32_t selected = MenuWorlds::SelectedStar();
		bool coopSelected = sStar && selected == sStar->get()->mKey.internalValue;
		if (selected != sLastSelected) {
			sLastSelected = selected;
			if (coopSelected) {
				Show();
			}
		}
		else if (coopSelected && !sVisible && GetTickCount() - sCardCheckedAt > 250) {
			// CO-OP clicked again after its card was closed (the selection keeps its value): open with the card
			sCardCheckedAt = GetTickCount();
			if (CardVisible()) {
				sCardSeen = true;
				Show();
			}
		}
		// the window lives with the star's card: placed next to it, closed when it closes (a download keeps running)
		if (sVisible && GetTickCount() - sCardCheckedAt > 150) {
			sCardCheckedAt = GetTickCount();
			Math::Rectangle before = sCardArea;
			if (CardVisible()) {
				if (!sCardSeen || before.x1 != sCardArea.x1 || before.y1 != sCardArea.y1) {
					sDirty = true;  // first seen or moved: place the window again
				}
				sCardSeen = true;
				sCardLostAt = 0;
			}
			else if (sCardSeen) {
				if (!sCardLostAt) {
					sCardLostAt = GetTickCount();
				}
				else if (GetTickCount() - sCardLostAt > 300) {
					Logf("menu: the star card closed, closing the window");
					sCardSeen = false;
					sCardLostAt = 0;
					Hide();
				}
			}
		}
		if (selected != (sStar ? sStar->get()->mKey.internalValue : 0)) {
			RestoreCardPlay();  // the card is shared by all stars: other stars keep their Play
			if (sVisible) {
				Hide();
			}
		}
		else if (GetTickCount() - sPlayCheckedAt > 150) {
			sPlayCheckedAt = GetTickCount();
			bool ready = sState == State::Ready;
			if (!ready && !sHiddenPlay) {
				if (UTFWin::IWindow* play = FindCardPlay()) {
					sHiddenPlay = new eastl::intrusive_ptr<UTFWin::IWindow>(play);
					play->SetVisible(false);
					Logf("menu: the card's Play is hidden until a friend's world is ready");
				}
			}
			else if (ready && sHiddenPlay) {
				RestoreCardPlay();
			}
		}
		if (sPressedAt && GetTickCount() - sPressedAt > 700) {
			sPressedAt = 0;
			if (!sLoadSeen) {
				if (UTFWin::IWindow* play = FindCardPlay()) {
					SendButtonClick(play);
				}
			}
		}
		if (sLeaveRequested) {
			sLeaveRequested = false;
			if (CoopNet::Leave()) {
				sState = State::Leaving;
				sDirty = true;
			}
		}
		if (sPlayRequested) {
			sPlayRequested = false;
			if (sState == State::Ready && selected == (sStar ? sStar->get()->mKey.internalValue : 0)) {
				// SetActiveModeByName("LoadGame") straight from here crashed (SporeApp.exe+0x293df0): the card's own Play
				// prepares the load first, so press that one
				RestoreCardPlay();
				if (UTFWin::IWindow* play = FindCardPlay()) {
					play->SetVisible(true);
					sLoadSeen = false;
					sPressedAt = GetTickCount();
					ClickWindow(play);
				}
				else {
					Logf("menu: our Play: the card's Play button was not found");
				}
				return;
			}
			Logf("menu: our Play ignored (state %d, selected %08x)", (int)sState, selected);
		}
		FollowJoin();
		if (sVisible && (sDirty || (sPicture && !sPicture->Visible()))) {
			Redraw();
		}
	}

	bool HoldPlay()
	{
		if (!InMainMenu() || !sStar || MenuWorlds::SelectedStar() != sStar->get()->mKey.internalValue) {
			return false;
		}
		if (sState == State::Ready) {
			Logf("menu: Play on the CO-OP star: loading %s's world", U().worldOf.c_str());
			sLoadSeen = true;
			return false;
		}
		Logf("menu: Play on the CO-OP star without a world: window instead");
		Show();
		return true;
	}

	void DumpCard()
	{
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		UTFWin::IWindow* caption = main ? FindCoopCaption(main, 0) : nullptr;
		if (!caption) {
			Logf("card: no CO-OP card on screen (select the CO-OP star first)");
			return;
		}
		UTFWin::IWindow* w = caption;
		int up = 0;
		while (w->GetParent() && w->GetParent() != main) {
			const Math::Rectangle& a = w->GetRealArea();
			Logf("card: ancestor %d: %08x area %.0f,%.0f-%.0f,%.0f", up++, w->GetControlID(), a.x1, a.y1, a.x2, a.y2);
			w = w->GetParent();
		}
		Logf("card: top under main: %08x", w->GetControlID());
		UTFWin::IWindow* start = caption;
		for (int i = 0; i < 3 && start->GetParent() && start->GetParent() != main; ++i) {
			start = start->GetParent();
		}
		DumpTree(start, 0, 5);
	}

	void Open()
	{
		Show();
		Redraw();
	}

	void Shutdown()
	{
		RestoreCardPlay();
		if (sPicture) {
			sPicture->Remove();
			delete sPicture;
			sPicture = nullptr;
		}
		if (sStar) {
			MenuWorlds::RemoveWorld(sStar->get());
			delete sStar;
			sStar = nullptr;
		}
		delete sUi;
		sUi = nullptr;
		if (sGdi) {
			StopGdi();
			sGdi = false;
		}
	}
}
