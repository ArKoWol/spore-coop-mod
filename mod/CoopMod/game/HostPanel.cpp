// HostPanel.cpp : see HostPanel.h.
#include "stdafx.h"
#include "HostPanel.h"
#include "CoopNet.h"
#include "CoopLaunch.h"
#include "UiDraw.h"
#include "InvitePanel.h"
#include "Presence.h"
#include "../Log.h"

#include <string>
#include <vector>

using namespace UiDraw;

namespace
{
	const int kWidth = 300;
	const int kHostingWidth = 380;  // hosting with the friends list available: room for the Invite button
	const int kLeaveWidth = 340;  // the guest's plate in a co-op launch: "In <host>'s world" and Leave
	const int kMargin = 16;  // from the screen's top right corner
	const int kCopy = 1, kStop = 2, kHost = 3, kLeave = 4, kInvite = 5;
	float sAnchorRight = 0, sAnchorBottom = 0;  // the visible plate on screen (0 = hidden)
	bool sInviteShown = false;                  // the Invite button was drawn as "open"

	struct Hit
	{
		Gdiplus::RectF rect;
		int action = 0;
	};
	std::vector<Hit>* sHits = nullptr;  // heap: freed in Shutdown
	PictureWindow* sPicture = nullptr;
	bool sGdi = false;

	int sHover = 0;
	bool sDirty = true;
	int sClicked = 0;          // acted on in Update, outside the UI message
	DWORD sCopiedAt = 0;       // "Copied!" shown for a moment
	CoopNet::HostState sShown; // what the picture shows
	std::string sGuestName;
	float sScreenW = 0;
	bool sLeaveShown = false;  // the picture shows the guest's Leave plate
	bool sLeaving = false;     // Leave was pressed: the game restarts
	std::string sHostName;     // the name on the Leave plate
	DWORD sNameCheckedAt = 0;

	bool Handler(const UTFWin::Message& message)
	{
		if (message.IsType(UTFWin::kMsgMouseDown) || message.IsType(UTFWin::kMsgMouseWheel)) {
			return true;  // the click is ours, not a move order for the creature
		}
		float x = message.Mouse.mouseX, y = message.Mouse.mouseY;
		int at = 0;
		if (sHits && (message.IsType(UTFWin::kMsgMouseMove) || message.IsType(UTFWin::kMsgMouseUp))) {
			for (const Hit& h : *sHits) {
				if (x >= h.rect.X && x <= h.rect.X + h.rect.Width && y >= h.rect.Y && y <= h.rect.Y + h.rect.Height) {
					at = h.action;
				}
			}
		}
		if (message.IsType(UTFWin::kMsgMouseMove) || message.IsType(UTFWin::kMsgMouseLeave)) {
			if (at != sHover) {
				sHover = at;
				sDirty = true;
			}
			return false;
		}
		if (message.IsType(UTFWin::kMsgMouseUp)) {
			sClicked = at;
			return true;
		}
		return false;
	}

	void AddHit(const Gdiplus::RectF& r, int action)
	{
		Hit h;
		h.rect = r;
		h.action = action;
		sHits->push_back(h);
	}

	bool Render(std::vector<uint8_t>& png, int& width, int& height)
	{
		if (!sHits) {
			sHits = new std::vector<Hit>();
		}
		sHits->clear();
		const CoopNet::HostState& s = sShown;
		bool canInvite = s.hosting && Presence::Available();
		width = sLeaveShown ? kLeaveWidth : canInvite ? kHostingWidth : kWidth;
		height = sLeaveShown ? 64 : s.hosting ? 150 : 96;
		Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.Clear(C(0, 0, 0, 0));

		Gdiplus::RectF plate(8, 6, width - 16.0f, height - 18.0f);
		Plate(g, plate, 20);
		const C ink(255, 32, 48, 72), soft(255, 92, 106, 126), orange(255, 255, 196, 64), brown(255, 92, 44, 0);
		const C green(255, 60, 150, 30);
		float left = plate.X + 18, textWidth = plate.Width - 36;
		if (sLeaveShown) {
			// a guest in a co-op launch: whose world this is, and the way back to the own one
			Text(g, Wide("In " + sHostName + "'s world"), 15, true, ink,
				Gdiplus::RectF(left, plate.Y + 6, textWidth - 96, plate.Height - 12));
			if (sLeaving) {
				Text(g, L"Leaving...", 14, true, soft, Gdiplus::RectF(plate.X + plate.Width - 114, plate.Y + 6, 98, plate.Height - 12));
			}
			else {
				Gdiplus::RectF leave(plate.X + plate.Width - 108, plate.Y + (plate.Height - 30) / 2, 92, 30);
				Button(g, leave, L"Leave", false, sHover == kLeave, 14);
				AddHit(leave, kLeave);
			}
			return EncodePng(bitmap, png);
		}
		Text(g, L"CO-OP", 22, true, orange, Gdiplus::RectF(left, plate.Y + 8, 100, 30), Align::Left, 1.8f, brown);

		if (!s.hosting) {
			Text(g, L"Let a friend join this world", 12, false, soft, Gdiplus::RectF(left, plate.Y + 40, textWidth, 18));
			Gdiplus::RectF host(plate.X + plate.Width - 140, plate.Y + 10, 124, 30);
			Button(g, host, L"Host co-op", true, sHover == kHost, 14);
			AddHit(host, kHost);
		}
		else {
			if (s.guestHere) {
				Text(g, Wide(sGuestName + " is here"), 15, true, green, Gdiplus::RectF(left + 84, plate.Y + 10, textWidth - 84, 26));
			}
			else {
				Text(g, L"Hosting: waiting for a friend", 13, true, ink, Gdiplus::RectF(left + 84, plate.Y + 10, textWidth - 84, 26));
			}
			bool copied = sCopiedAt && GetTickCount() - sCopiedAt < 2500;
			wchar_t line[96];
			swprintf(line, 96, L"Your Steam ID: %llu", (unsigned long long)s.myId);
			Text(g, line, 12, false, soft, Gdiplus::RectF(left, plate.Y + 42, textWidth, 18));
			Text(g, canInvite ? L"Your friend: CO-OP button (he sees you there), or Invite" : L"Your friend: CO-OP button, Join by Steam ID",
				11.5f, false, soft, Gdiplus::RectF(left, plate.Y + 60, textWidth, 18));
			Gdiplus::RectF copy(left - 2, plate.Y + plate.Height - 42, 160, 30);
			Button(g, copy, copied ? L"Copied!" : L"Copy my Steam ID", true, sHover == kCopy, 13);
			AddHit(copy, kCopy);
			Gdiplus::RectF stop(plate.X + plate.Width - 76, plate.Y + plate.Height - 42, 60, 30);
			Button(g, stop, L"Stop", false, sHover == kStop, 13);
			AddHit(stop, kStop);
			if (canInvite) {
				// green while the list is closed, blue while it is open (the same button closes it)
				sInviteShown = InvitePanel::IsOpen();
				Gdiplus::RectF invite(copy.X + copy.Width + 8, copy.Y, 76, 30);
				Button(g, invite, L"Invite", !sInviteShown, sHover == kInvite, 13);
				AddHit(invite, kInvite);
			}
		}
		return EncodePng(bitmap, png);
	}

	void CopyToClipboard(uint64_t id)
	{
		char text[32];
		snprintf(text, sizeof(text), "%llu", (unsigned long long)id);
		if (!OpenClipboard(nullptr)) {
			return;
		}
		EmptyClipboard();
		size_t n = strlen(text) + 1;
		if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, n)) {
			memcpy(GlobalLock(mem), text, n);
			GlobalUnlock(mem);
			if (!SetClipboardData(CF_TEXT, mem)) {
				GlobalFree(mem);
			}
		}
		CloseClipboard();
	}

	// the creature stage with the avatar, not paused, no editor: where the button belongs
	bool InCreatureGame()
	{
		return Simulator::IsCreatureGame() && GameNounManager.GetAvatar()
			&& GameModeManager.GetActiveModeID() == (uint32_t)GameModeIDs::kGameCreature;
	}

	void Hide()
	{
		if (sPicture) {
			sPicture->Hide();
		}
		sAnchorRight = sAnchorBottom = 0;
	}
}

namespace HostPanel
{
	bool Anchor(float& right, float& bottom)
	{
		right = sAnchorRight;
		bottom = sAnchorBottom;
		return sAnchorBottom > 0;
	}

	void Update()
	{
		CoopNet::HostState s = CoopNet::Hosting();
		bool leave = CoopLaunch::Active();  // a guest in a co-op launch gets the Leave plate instead of the host plate
		if (!InCreatureGame() || (s.joined && !leave)) {
			Hide();
			return;
		}
		if (leave != sLeaveShown) {
			sLeaveShown = leave;
			sDirty = true;
		}
		if (leave && (sHostName.empty() || GetTickCount() - sNameCheckedAt > 2000)) {
			sNameCheckedAt = GetTickCount();  // the Steam name can arrive a few seconds after the join
			std::string name = CoopLaunch::HostName(CoopLaunch::Host());
			if (name != sHostName) {
				sHostName = name;
				sDirty = true;
			}
		}
		if (sClicked) {
			int action = sClicked;
			sClicked = 0;
			Logf("hostpanel: button %d", action);
			if (action == kLeave) {
				if (CoopNet::Leave()) {
					sLeaving = true;
				}
			}
			else if (action == kHost) {
				CoopNet::Host();
				s = CoopNet::Hosting();
			}
			else if (action == kCopy) {
				CopyToClipboard(s.myId);
				sCopiedAt = GetTickCount();
			}
			else if (action == kStop) {
				CoopNet::Cancel();
				s = CoopNet::Hosting();
			}
			else if (action == kInvite) {
				InvitePanel::Toggle();
			}
			sDirty = true;
		}
		if (s.hosting && InvitePanel::IsOpen() != sInviteShown) {
			sDirty = true;  // the Invite button's colour follows the list
		}
		if (s.hosting != sShown.hosting || s.guestHere != sShown.guestHere || s.guest != sShown.guest || s.myId != sShown.myId) {
			sDirty = true;
			if (s.guest) {
				sGuestName = CoopNet::Name(s.guest);
				if (sGuestName.empty() || sGuestName == "[unknown]") {
					sGuestName = "Your friend";
				}
			}
		}
		if (sCopiedAt && GetTickCount() - sCopiedAt > 2600) {
			sCopiedAt = 0;
			sDirty = true;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return;
		}
		float screenW = main->GetArea().GetWidth();
		if (screenW != sScreenW) {
			sScreenW = screenW;
			sDirty = true;
		}
		if (!sDirty && sPicture && sPicture->Visible()) {
			return;
		}
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		sShown = s;
		std::vector<uint8_t> png;
		int width = 0, height = 0;
		if (!Render(png, width, height)) {
			return;
		}
		if (!sPicture) {
			sPicture = new PictureWindow(Handler);
		}
		if (sPicture->Show(png, screenW - width - kMargin, (float)kMargin, width, height)) {
			sDirty = false;
			sAnchorRight = screenW - kMargin - 8;      // the picture has a margin for the shadow around the plate
			sAnchorBottom = (float)(kMargin + height - 12);
		}
	}

	void Shutdown()
	{
		if (sPicture) {
			sPicture->Remove();
			delete sPicture;
			sPicture = nullptr;
		}
		delete sHits;
		sHits = nullptr;
		if (sGdi) {
			StopGdi();
			sGdi = false;
		}
	}
}
