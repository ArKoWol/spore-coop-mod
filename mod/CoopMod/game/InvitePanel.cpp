// InvitePanel.cpp : see InvitePanel.h.
#include "stdafx.h"
#include "InvitePanel.h"
#include "CoopNet.h"
#include "CoopLaunch.h"
#include "CoopMenu.h"
#include "HostPanel.h"
#include "Presence.h"
#include "UiDraw.h"
#include "../Log.h"

#include <string>
#include <vector>

using namespace UiDraw;

namespace
{
	const int kListWidth = 380;
	const int kRowH = 44;
	const size_t kMaxListed = 6;    // more friends than this: the rest is named in the log
	const int kToastW = 640, kToastH = 74;
	const int kInvite = 1, kToastJoin = 2, kToastNotNow = 3;

	struct Hit
	{
		Gdiplus::RectF rect;
		int action = 0;
		uint64_t id = 0;
	};

	// one picture window with its hit rectangles (heap, freed in Shutdown)
	struct Win
	{
		PictureWindow* picture = nullptr;
		std::vector<Hit> hits;
		int hover = 0;          // action under the mouse
		uint64_t hoverId = 0;
		bool dirty = true;
		Hit clicked;            // acted on in Update, outside the UI message
		unsigned shownRevision = 0;
		float x = 0, y = 0;     // where the picture was last shown
		int width = 0, height = 0;
	};
	Win* sList = nullptr;
	Win* sToast = nullptr;
	bool sGdi = false;
	bool sListOpen = false;
	int sListRows = -1;  // rows drawn last (a new friend or a sent invitation redraws)

	bool Handle(Win* w, const UTFWin::Message& message)
	{
		if (!w) {
			return false;
		}
		if (message.IsType(UTFWin::kMsgMouseDown) || message.IsType(UTFWin::kMsgMouseWheel)) {
			return true;  // the click is ours, not the galaxy's or the creature's
		}
		float x = message.Mouse.mouseX, y = message.Mouse.mouseY;
		const Hit* at = nullptr;
		if (message.IsType(UTFWin::kMsgMouseMove) || message.IsType(UTFWin::kMsgMouseUp)) {
			for (const Hit& h : w->hits) {
				if (x >= h.rect.X && x <= h.rect.X + h.rect.Width && y >= h.rect.Y && y <= h.rect.Y + h.rect.Height) {
					at = &h;
				}
			}
		}
		if (message.IsType(UTFWin::kMsgMouseMove) || message.IsType(UTFWin::kMsgMouseLeave)) {
			int action = at ? at->action : 0;
			uint64_t id = at ? at->id : 0;
			if (action != w->hover || id != w->hoverId) {
				w->hover = action;
				w->hoverId = id;
				w->dirty = true;
			}
			return false;
		}
		if (message.IsType(UTFWin::kMsgMouseUp)) {
			if (at) {
				w->clicked = *at;
			}
			return true;
		}
		return false;
	}

	bool ListHandler(const UTFWin::Message& message) { return Handle(sList, message); }
	bool ToastHandler(const UTFWin::Message& message) { return Handle(sToast, message); }

	void AddHit(Win& w, const Gdiplus::RectF& r, int action, uint64_t id)
	{
		Hit h;
		h.rect = r;
		h.action = action;
		h.id = id;
		w.hits.push_back(h);
	}

	bool InMainMenu()
	{
		return GameModeManager.GetActiveModeID() == (uint32_t)GameModeIDs::kGGEMode;
	}

	void HideWin(Win* w)
	{
		if (w && w->picture) {
			w->picture->Hide();
		}
	}

	// the friends the host may invite now: running this version, in the main menu, with no world of their own
	std::vector<const Presence::Friend*> Invitable()
	{
		std::vector<const Presence::Friend*> out;
		unsigned mine = CoopNet::ProtocolVersion();
		for (const Presence::Friend& f : Presence::Friends()) {
			if (coop::presence::CanInvite(f.info, mine)) {
				out.push_back(&f);
			}
		}
		return out;
	}

	// ------------------------------------------------------------------ the host's list

	bool RenderList(Win& w, std::vector<uint8_t>& png, const std::vector<const Presence::Friend*>& friends)
	{
		w.hits.clear();
		size_t shown = std::min(friends.size(), kMaxListed);
		int body = shown ? (int)shown * kRowH : 60;
		w.width = kListWidth;
		w.height = 74 + body + 26;
		Gdiplus::Bitmap bitmap(w.width, w.height, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.Clear(C(0, 0, 0, 0));

		Gdiplus::RectF plate(8, 6, w.width - 16.0f, w.height - 18.0f);
		Plate(g, plate, 20);
		const C ink(255, 32, 48, 72), soft(255, 92, 106, 126), orange(255, 255, 196, 64), brown(255, 92, 44, 0);
		float left = plate.X + 18, textWidth = plate.Width - 36;
		Text(g, L"Invite a friend", 22, true, orange, Gdiplus::RectF(left, plate.Y + 6, textWidth, 32), Align::Left, 1.8f, brown);
		float y = plate.Y + 44;
		if (friends.empty()) {
			Text(g, L"No friend with the mod is in the main menu now.", 13, true, ink, Gdiplus::RectF(left, y, textWidth, 24));
			Text(g, L"Both need the same version (update.bat).", 12, false, soft, Gdiplus::RectF(left, y + 24, textWidth, 20));
		}
		for (size_t i = 0; i < shown; ++i) {
			const Presence::Friend& f = *friends[i];
			Gdiplus::RectF row(left - 8, y, textWidth + 16, kRowH - 6);
			FillRound(g, row, 14, C(255, 214, 222, 232), C(255, 236, 240, 246));
			StrokeRound(g, row, 14, C(255, 168, 180, 196), 1.2f);
			Text(g, Wide(f.name), 15, true, ink, Gdiplus::RectF(row.X + 14, row.Y, row.Width - 130, row.Height));
			Gdiplus::RectF button(row.X + row.Width - 104, row.Y + 5, 92, row.Height - 10);
			if (Presence::InvitedRecently(f.id)) {
				ButtonDisabled(g, button, L"Invited", 13);
			}
			else {
				Button(g, button, L"Invite", true, w.hover == kInvite && w.hoverId == f.id, 13);
				AddHit(w, button, kInvite, f.id);
			}
			y += kRowH;
		}
		return EncodePng(bitmap, png);
	}

	void UpdateList()
	{
		Win*& w = sList;
		bool hosting = CoopNet::Hosting().hosting;
		float right = 0, bottom = 0;
		bool want = sListOpen && hosting && Presence::Available() && HostPanel::Anchor(right, bottom);
		if (!want) {
			if (sListOpen && !hosting) {
				sListOpen = false;  // hosting ended: the list goes with it
			}
			HideWin(w);
			Presence::WatchFriends(Presence::kWatchInvite, false);
			return;
		}
		Presence::WatchFriends(Presence::kWatchInvite, true);
		if (!w) {
			w = new Win();
			w->picture = new PictureWindow(ListHandler);
		}
		if (w->clicked.action == kInvite) {
			uint64_t id = w->clicked.id;
			w->clicked = Hit();
			Presence::SendInvite(id);
			w->dirty = true;
		}
		std::vector<const Presence::Friend*> friends = Invitable();
		if (Presence::Revision() != w->shownRevision || (int)friends.size() != sListRows) {
			w->dirty = true;
		}
		if (!w->dirty && w->picture->Visible()) {
			return;
		}
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		if ((int)friends.size() > (int)kMaxListed) {
			Logf("invite: %d friends can be invited, the list shows %d", (int)friends.size(), (int)kMaxListed);
		}
		std::vector<uint8_t> png;
		if (!RenderList(*w, png, friends)) {
			return;
		}
		// under the host plate, right edges lined up (the picture has a margin of 8 px for the shadow)
		float x = right + 8 - w->width;
		float y = bottom + 2 - 6;
		if (w->picture->Show(png, x, y, w->width, w->height)) {
			w->dirty = false;
			w->shownRevision = Presence::Revision();
			sListRows = (int)friends.size();
		}
	}

	// ------------------------------------------------------------------ the toast

	bool RenderToast(Win& w, std::vector<uint8_t>& png, const Presence::Invite& invite)
	{
		w.hits.clear();
		w.width = kToastW;
		w.height = kToastH;
		Gdiplus::Bitmap bitmap(w.width, w.height, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.Clear(C(0, 0, 0, 0));

		Gdiplus::RectF plate(8, 6, w.width - 16.0f, w.height - 18.0f);
		Plate(g, plate, 20);
		const C ink(255, 32, 48, 72), soft(255, 92, 106, 126);
		std::string line = invite.name + " invites you to " + (invite.planet.empty() ? std::string("a world") : invite.planet);
		float buttonsW = 92 + 8 + 104;
		Text(g, Wide(line), 17, true, ink, Gdiplus::RectF(plate.X + 20, plate.Y + 4, plate.Width - buttonsW - 56, 28));
		Text(g, L"Join opens a copy of the world; your own worlds stay as they are.", 12, false, soft,
			Gdiplus::RectF(plate.X + 20, plate.Y + 30, plate.Width - buttonsW - 56, 20));
		Gdiplus::RectF join(plate.X + plate.Width - buttonsW - 16, plate.Y + (plate.Height - 34) / 2, 92, 34);
		Button(g, join, L"Join", true, w.hover == kToastJoin, 15);
		AddHit(w, join, kToastJoin, invite.host);
		Gdiplus::RectF later(join.X + join.Width + 8, join.Y, 104, 34);
		Button(g, later, L"Not now", false, w.hover == kToastNotNow, 14);
		AddHit(w, later, kToastNotNow, invite.host);
		return EncodePng(bitmap, png);
	}

	// the newest invitation that was not answered yet
	const Presence::Invite* PendingInvite()
	{
		for (const Presence::Invite& invite : Presence::Invites()) {
			if (!invite.toastDone) {
				return &invite;
			}
		}
		return nullptr;
	}

	void UpdateToast()
	{
		Win*& w = sToast;
		// only in the main menu of a normal start, while no join runs (in a game or a co-op launch the invitation was ignored)
		const Presence::Invite* invite = nullptr;
		if (InMainMenu() && !CoopLaunch::Active() && !CoopNet::Hosting().joined) {
			invite = PendingInvite();
		}
		if (!invite) {
			HideWin(w);
			if (w) {
				w->clicked = Hit();
			}
			return;
		}
		if (!w) {
			w = new Win();
			w->picture = new PictureWindow(ToastHandler);
		}
		if (w->clicked.action) {
			Hit hit = w->clicked;
			w->clicked = Hit();
			if (hit.action == kToastJoin) {
				std::string name = invite->name;
				uint64_t host = invite->host;
				Logf("invite: the toast's Join for %s", name.c_str());
				Presence::DismissInvite(host, true);
				CoopMenu::JoinHost(host, name);
			}
			else if (hit.action == kToastNotNow) {
				Logf("invite: Not now for %s", invite->name.c_str());
				Presence::DismissInvite(hit.id, false);  // the window still lists it
			}
			HideWin(w);
			w->dirty = true;
			return;
		}
		if (Presence::Revision() != w->shownRevision) {
			w->dirty = true;
		}
		if (!w->dirty && w->picture->Visible()) {
			return;
		}
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		std::vector<uint8_t> png;
		if (!RenderToast(*w, png, *invite)) {
			return;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return;
		}
		float x = (main->GetArea().GetWidth() - w->width) / 2;
		if (w->picture->Show(png, x, 10, w->width, w->height)) {
			w->dirty = false;
			w->shownRevision = Presence::Revision();
		}
	}
}

namespace InvitePanel
{
	void Update()
	{
		UpdateList();
		UpdateToast();
	}

	void Toggle()
	{
		sListOpen = !sListOpen;
		if (sList) {
			sList->dirty = true;
		}
		Logf("invite: list %s", sListOpen ? "opened" : "closed");
	}

	bool IsOpen()
	{
		return sListOpen;
	}

	void Shutdown()
	{
		for (Win* w : { sList, sToast }) {
			if (w) {
				if (w->picture) {
					w->picture->Remove();
					delete w->picture;
				}
				delete w;
			}
		}
		sList = sToast = nullptr;
		if (sGdi) {
			StopGdi();
			sGdi = false;
		}
	}
}
