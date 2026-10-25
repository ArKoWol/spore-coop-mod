// UiDraw.cpp : see UiDraw.h.
#include "stdafx.h"
#include "UiDraw.h"
#include "../Log.h"
#include "../Models.h"

#pragma comment(lib, "gdiplus.lib")

namespace
{
	ULONG_PTR sGdiToken = 0;
	int sGdiUsers = 0;
	int sPictureCount = 0;  // every picture gets its own resource key (the game caches images by key)

	class PictureProc : public UTFWin::DefaultWinProc<UTFWin::kEventFlagBasicInput>
	{
	public:
		explicit PictureProc(UiDraw::PictureWindow* owner) : mOwner(owner) {}
		bool HandleUIMessage(UTFWin::IWindow*, const UTFWin::Message& message) override
		{
			return mOwner && mOwner->mHandler ? mOwner->mHandler(message) : false;
		}
		UiDraw::PictureWindow* mOwner;
	};
}

namespace UiDraw
{
	bool StartGdi()
	{
		if (!sGdiToken) {
			Gdiplus::GdiplusStartupInput input;
			if (Gdiplus::GdiplusStartup(&sGdiToken, &input, nullptr) != Gdiplus::Ok) {
				sGdiToken = 0;
				Logf("ui: GDI+ failed to start");
				return false;
			}
		}
		++sGdiUsers;
		return true;
	}

	void StopGdi()
	{
		if (sGdiUsers > 0 && --sGdiUsers == 0 && sGdiToken) {
			Gdiplus::GdiplusShutdown(sGdiToken);
			sGdiToken = 0;
		}
	}

	void RoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius)
	{
		float d = std::min(radius * 2, std::min(r.Width, r.Height));
		path.AddArc(r.X, r.Y, d, d, 180, 90);
		path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
		path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
		path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
		path.CloseFigure();
	}

	void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const C& top, const C& bottom)
	{
		Gdiplus::GraphicsPath path;
		RoundRect(path, r, radius);
		Gdiplus::LinearGradientBrush brush(Gdiplus::RectF(r.X, r.Y - 1, r.Width, r.Height + 2), top, bottom,
			Gdiplus::LinearGradientModeVertical);
		g.FillPath(&brush, &path);
	}

	void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const C& color, float width)
	{
		Gdiplus::GraphicsPath path;
		RoundRect(path, r, radius);
		Gdiplus::Pen pen(color, width);
		g.DrawPath(&pen, &path);
	}

	void Gloss(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, BYTE alpha)
	{
		Gdiplus::RectF top(r.X + 2, r.Y + 2, r.Width - 4, r.Height * 0.48f);
		FillRound(g, top, radius - 2, C(alpha, 255, 255, 255), C(0, 255, 255, 255));
	}

	void Text(Gdiplus::Graphics& g, const std::wstring& s, float size, bool bold, const C& fill, const Gdiplus::RectF& r,
		Align align, float outline, const C& outlineColor)
	{
		Gdiplus::FontFamily family(L"Segoe UI");
		Gdiplus::StringFormat format;
		format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
		format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		format.SetAlignment(align == Align::Center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
		Gdiplus::GraphicsPath path;
		path.AddString(s.c_str(), -1, &family, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, size, r, &format);
		if (outline > 0) {
			Gdiplus::Pen pen(outlineColor, outline * 2);
			pen.SetLineJoin(Gdiplus::LineJoinRound);
			g.DrawPath(&pen, &path);
		}
		Gdiplus::SolidBrush brush(fill);
		g.FillPath(&brush, &path);
	}

	Gdiplus::RectF Plate(Gdiplus::Graphics& g, const Gdiplus::RectF& plate, float radius)
	{
		for (int i = 4; i >= 1; --i) {
			FillRound(g, Gdiplus::RectF(plate.X - i, plate.Y - i + 4, plate.Width + 2 * i, plate.Height + 2 * i), radius + i,
				C(22, 0, 0, 0), C(22, 0, 0, 0));
		}
		FillRound(g, plate, radius, C(255, 92, 104, 122), C(255, 58, 66, 82));
		Gdiplus::RectF body(plate.X + 3, plate.Y + 3, plate.Width - 6, plate.Height - 6);
		FillRound(g, body, radius - 3, C(255, 250, 251, 253), C(255, 196, 204, 214));
		Gloss(g, body, radius - 3, 120);
		return body;
	}

	void Button(Gdiplus::Graphics& g, const Gdiplus::RectF& r, const wchar_t* label, bool primary, bool hot, float textSize)
	{
		float radius = r.Height / 2;
		FillRound(g, Gdiplus::RectF(r.X, r.Y + 3, r.Width, r.Height), radius, C(70, 0, 0, 0), C(70, 0, 0, 0));
		FillRound(g, r, radius, primary ? C(255, 40, 110, 20) : C(255, 50, 70, 100), primary ? C(255, 30, 80, 10) : C(255, 35, 50, 75));
		Gdiplus::RectF body(r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4);
		FillRound(g, body, radius - 2,
			primary ? (hot ? C(255, 200, 255, 140) : C(255, 150, 230, 80)) : (hot ? C(255, 190, 220, 250) : C(255, 140, 175, 215)),
			primary ? (hot ? C(255, 90, 200, 50) : C(255, 60, 160, 30)) : (hot ? C(255, 100, 140, 190) : C(255, 70, 105, 150)));
		Gloss(g, body, radius - 2, 150);
		Text(g, label, textSize, true, C(255, 255, 255, 255), r, Align::Center, 1.6f, primary ? C(255, 25, 80, 10) : C(255, 25, 45, 75));
	}

	bool EncodePng(Gdiplus::Bitmap& bitmap, std::vector<uint8_t>& png)
	{
		IStream* stream = nullptr;
		if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) {
			return false;
		}
		const CLSID pngEncoder = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
		bool ok = bitmap.Save(stream, &pngEncoder, nullptr) == Gdiplus::Ok;
		if (ok) {
			STATSTG stat = {};
			stream->Stat(&stat, STATFLAG_NONAME);
			png.resize((size_t)stat.cbSize.QuadPart);
			LARGE_INTEGER zero = {};
			stream->Seek(zero, STREAM_SEEK_SET, nullptr);
			ULONG read = 0;
			stream->Read(png.data(), (ULONG)png.size(), &read);
			ok = read == png.size();
		}
		stream->Release();
		return ok;
	}

	std::wstring Wide(const std::string& utf8)
	{
		if (utf8.empty()) {
			return L"";
		}
		int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring out(n > 0 ? n - 1 : 0, L'\0');
		if (n > 1) {
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &out[0], n);
		}
		return out;
	}

	bool PictureWindow::Show(std::vector<uint8_t>& png, float x, float y, int width, int height)
	{
		wchar_t temp[MAX_PATH];
		GetTempPathW(MAX_PATH, temp);
		std::wstring dir = std::wstring(temp) + L"CoopModUI";
		CreateDirectoryW(dir.c_str(), nullptr);
		char name[32];
		snprintf(name, sizeof(name), "coopui_%d", ++sPictureCount);
		wchar_t file[48];
		swprintf(file, 48, L"\\ui_%d.package", sPictureCount);
		Models::Record record;
		record.type = TypeIDs::png;
		record.group = id("coopmod");
		record.instance = id(name);
		record.data.swap(png);
		if (!Models::WritePackage(dir + file, { record })) {
			return false;
		}
		ImagePtr image;
		if (!UTFWin::Image::GetImage(ResourceKey(record.instance, record.type, record.group), image) || !image) {
			Logf("ui: the picture %s was not found after registering", name);
			return false;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		if (!main) {
			return false;
		}
		if (!mWindow) {
			mWindow = new UTFWin::Window();
			mDrawable = new UTFWin::ImageDrawable();
			mWindow->SetDrawable(mDrawable);
			mWindow->AddWinProc(new PictureProc(this));
			main->AddWindow(mWindow);
		}
		mDrawable->SetImage(image.get());
		if (!mImage) {
			mImage = new ImagePtr();
		}
		*mImage = image;  // keep the picture alive while it is shown
		mWidth = width;
		mHeight = height;
		mWindow->SetArea(Math::Rectangle(x, y, x + width, y + height));
		mWindow->SetVisible(true);
		mWindow->Invalidate();  // without it the window keeps the old picture until the mouse touches it
		main->BringToFront(mWindow);
		return true;
	}

	void PictureWindow::MoveTo(float x, float y)
	{
		if (!mWindow) {
			return;
		}
		mWindow->SetArea(Math::Rectangle(x, y, x + mWidth, y + mHeight));
		if (!mWindow->IsVisible()) {
			mWindow->SetVisible(true);
		}
	}

	void PictureWindow::Hide()
	{
		if (mWindow) {
			mWindow->SetVisible(false);
		}
	}

	bool PictureWindow::Visible() const
	{
		return mWindow && mWindow->IsVisible();
	}

	void PictureWindow::Remove()
	{
		if (mWindow) {
			if (UTFWin::IWindow* main = WindowManager.GetMainWindow()) {
				main->RemoveWindow(mWindow);
			}
			mWindow = nullptr;
			mDrawable = nullptr;
		}
		delete mImage;
		mImage = nullptr;
	}
}
