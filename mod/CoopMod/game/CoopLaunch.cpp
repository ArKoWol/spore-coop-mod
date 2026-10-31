// CoopLaunch.cpp : see CoopLaunch.h.
#include "stdafx.h"
#include "CoopLaunch.h"
#include "CoopNet.h"
#include "UiDraw.h"
#include "../Log.h"
#include "../RecentHosts.h"
#include "../../../core/coop_launch.h"

#include <string>
#include <vector>

using namespace UiDraw;

namespace
{
	// plain buffers, no std::string: they are filled in DllMain and live until the process ends (see FACTS section 3)
	bool sActive = false;
	uint64_t sHost = 0;
	char sNotice[360] = {};
	char sOffer[64] = {};  // name of the backup the restore question offers, "" = no question

	enum class Mode { None, Question, Notice, Restarting };

	const int kRestore = 1, kLater = 2, kOk = 3;
	const int kWidth = 460;

	struct Hit
	{
		Gdiplus::RectF rect;
		int action = 0;
	};
	std::vector<Hit>* sHits = nullptr;  // heap: freed in Shutdown
	PictureWindow* sPicture = nullptr;
	bool sGdi = false;

	bool sOfferChecked = false;
	bool sNoticeDismissed = false;
	bool sRestarting = false;
	Mode sShown = Mode::None;
	int sHover = 0;
	bool sDirty = true;
	int sClicked = 0;  // acted on in Update, outside the UI message
	float sScreenW = 0;

	std::string GamesDir()
	{
		const char* appdata = getenv("APPDATA");
		return std::string(appdata ? appdata : "") + "\\Spore\\Games";
	}

	void SetNotice(const std::string& text)
	{
		strncpy_s(sNotice, text.c_str(), _TRUNCATE);
	}

	bool InMainMenu()
	{
		return GameModeManager.GetActiveModeID() == (uint32_t)GameModeIDs::kGGEMode;
	}

	bool Handler(const UTFWin::Message& message)
	{
		if (message.IsType(UTFWin::kMsgMouseDown) || message.IsType(UTFWin::kMsgMouseWheel)) {
			return true;  // the click is ours, not the galaxy's
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

	// splits an ASCII message at spaces into lines of at most `chars` characters
	std::vector<std::wstring> Wrap(const std::string& text, size_t chars)
	{
		std::vector<std::wstring> lines;
		std::string line;
		size_t at = 0;
		while (at < text.size()) {
			size_t end = text.find(' ', at);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string word = text.substr(at, end - at);
			if (!line.empty() && line.size() + 1 + word.size() > chars) {
				lines.push_back(Wide(line));
				line.clear();
			}
			line += (line.empty() ? "" : " ") + word;
			at = end + 1;
		}
		if (!line.empty()) {
			lines.push_back(Wide(line));
		}
		return lines;
	}

	bool Render(Mode mode, std::vector<uint8_t>& png, int& height)
	{
		if (!sHits) {
			sHits = new std::vector<Hit>();
		}
		sHits->clear();
		std::vector<std::wstring> lines;
		if (mode == Mode::Notice) {
			lines = Wrap(sNotice, 54);
		}
		height = mode == Mode::Question ? 184 : mode == Mode::Restarting ? 112 : 112 + 20 * (int)lines.size();
		Gdiplus::Bitmap bitmap(kWidth, height, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.Clear(C(0, 0, 0, 0));

		Gdiplus::RectF plate(8, 6, kWidth - 16.0f, height - 18.0f);
		Plate(g, plate, 20);
		const C ink(255, 32, 48, 72), soft(255, 92, 106, 126), orange(255, 255, 196, 64), brown(255, 92, 44, 0);
		float left = plate.X + 20, width = plate.Width - 40;
		Text(g, L"CO-OP", 22, true, orange, Gdiplus::RectF(left, plate.Y + 8, 100, 30), Align::Left, 1.8f, brown);

		if (mode == Mode::Question) {
			Text(g, Wide(std::string("Your own saves are in a backup (") + sOffer + ")."), 13.5f, false, ink,
				Gdiplus::RectF(left, plate.Y + 42, width, 22));
			Text(g, L"Restore them?", 16, true, ink, Gdiplus::RectF(left, plate.Y + 64, width, 24));
			Text(g, L"The game restarts once. Nothing is deleted.", 12, false, soft, Gdiplus::RectF(left, plate.Y + 90, width, 20));
			Gdiplus::RectF restore(plate.X + plate.Width - 236, plate.Y + plate.Height - 44, 104, 32);
			Button(g, restore, L"Restore", true, sHover == kRestore, 14);
			AddHit(restore, kRestore);
			Gdiplus::RectF later(plate.X + plate.Width - 124, plate.Y + plate.Height - 44, 108, 32);
			Button(g, later, L"Not now", false, sHover == kLater, 14);
			AddHit(later, kLater);
		}
		else if (mode == Mode::Restarting) {
			Text(g, L"Restarting to bring your saves back...", 14, true, ink, Gdiplus::RectF(left, plate.Y + 46, width, 24));
		}
		else {
			float y = plate.Y + 42;
			for (const std::wstring& line : lines) {
				Text(g, line, 13.5f, false, ink, Gdiplus::RectF(left, y, width, 22));
				y += 20;
			}
			Gdiplus::RectF ok(plate.X + plate.Width - 100, plate.Y + plate.Height - 44, 84, 32);
			Button(g, ok, L"OK", false, sHover == kOk, 14);
			AddHit(ok, kOk);
		}
		return EncodePng(bitmap, png);
	}

	void Hide()
	{
		if (sPicture) {
			sPicture->Hide();
		}
	}
}

namespace CoopLaunch
{
	void Prepare()
	{
		coop::launch::Result r = coop::launch::Prepare(GamesDir());
		for (const std::string& line : r.log) {
			Logf("launch: %s", line.c_str());
		}
		sActive = r.coopLaunch;
		sHost = r.host;
		SetNotice(r.message);
		Logf("launch: start-up folders done: co-op launch %d (host %llu), restored %d, migrated %d, failed %d%s%s", sActive ? 1 : 0,
			(unsigned long long)sHost, r.restored ? 1 : 0, r.migrated ? 1 : 0, r.failed ? 1 : 0, r.message.empty() ? "" : ", notice: ",
			r.message.c_str());
	}

	bool Active() { return sActive; }
	uint64_t Host() { return sHost; }
	const char* Notice() { return sNotice; }

	std::string HostName(uint64_t id)
	{
		for (size_t i = 0; i < 10; ++i) {
			uint64_t known = 0;
			long long when = 0;
			char name[128] = {};
			if (!RecentHosts::Get(i, known, name, sizeof(name), when)) {
				break;
			}
			if (known == id && name[0]) {
				return name;
			}
		}
		std::string steam = CoopNet::Name(id);
		return steam.empty() || steam == "[unknown]" ? std::to_string(id) : steam;
	}

	void Update()
	{
		if (!InMainMenu()) {
			Hide();
			sDirty = true;  // draw it again on the next entry into the menu
			return;
		}
		if (!sOfferChecked) {
			sOfferChecked = true;
			if (!sActive) {
				strncpy_s(sOffer, coop::launch::MigrationOffer(GamesDir()).c_str(), _TRUNCATE);
				if (sOffer[0]) {
					Logf("launch: the restore question is asked (backup %s)", sOffer);
				}
			}
		}
		if (sClicked) {
			int action = sClicked;
			sClicked = 0;
			Logf("launch: button %d", action);
			if (action == kRestore) {
				coop::launch::MarkAsked(GamesDir());
				if (coop::launch::RequestMigration(GamesDir())) {
					sOffer[0] = 0;
					sRestarting = true;
					CoopNet::RestartGame();
				}
				else {
					sOffer[0] = 0;
					SetNotice("Could not write CoopMigrate.txt in the Games folder, your saves stay in the backup.");
					sNoticeDismissed = false;
				}
			}
			else if (action == kLater) {
				coop::launch::MarkAsked(GamesDir());
				sOffer[0] = 0;
			}
			else if (action == kOk) {
				sNoticeDismissed = true;
			}
			sDirty = true;
		}
		Mode want = sRestarting ? Mode::Restarting : sOffer[0] ? Mode::Question : (sNotice[0] && !sNoticeDismissed) ? Mode::Notice : Mode::None;
		if (want == Mode::None) {
			Hide();
			return;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return;
		}
		float screenW = main->GetArea().GetWidth();
		if (want != sShown || screenW != sScreenW) {
			sShown = want;
			sScreenW = screenW;
			sDirty = true;
		}
		if (!sDirty && sPicture && sPicture->Visible()) {
			return;
		}
		if (!sGdi && !(sGdi = StartGdi())) {
			return;
		}
		std::vector<uint8_t> png;
		int height = 0;
		if (!Render(want, png, height)) {
			return;
		}
		if (!sPicture) {
			sPicture = new PictureWindow(Handler);
		}
		if (sPicture->Show(png, (screenW - kWidth) / 2, 56, kWidth, height)) {
			sDirty = false;
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
