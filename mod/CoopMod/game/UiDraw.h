// UiDraw.h : the look and the plumbing shared by the mod's in-game windows (rules: docs/features/UI_STYLE.md).
//
// UTFWin has no text window we can create from code, so a window is a picture: GDI+ draws it, the PNG goes into a small
// package in %TEMP%\CoopModUI (Models::WritePackage) and a UTFWin window shows it (PictureWindow). Clicks are hit-tested
// against the rectangles drawn.
#pragma once

#include <algorithm>
namespace Gdiplus
{
	using std::min;
	using std::max;
}
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <string>
#include <vector>

namespace UiDraw
{
	typedef Gdiplus::Color C;

	// GDI+ for everyone who draws: Start before the first picture, Stop in Shutdown (counted)
	bool StartGdi();
	void StopGdi();

	void RoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius);
	void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const C& top, const C& bottom);
	void StrokeRound(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, const C& color, float width);
	// a glossy highlight over the upper half of a rounded shape
	void Gloss(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, BYTE alpha);

	enum class Align { Left, Center };
	// text as a path: optional dark outline, then the fill (the game draws its headings this way)
	void Text(Gdiplus::Graphics& g, const std::wstring& s, float size, bool bold, const C& fill, const Gdiplus::RectF& r,
		Align align = Align::Left, float outline = 0, const C& outlineColor = C(255, 60, 30, 0));

	// the light glossy plate of the game's star card: shadow, dark rim, body, gloss; returns the inner body rectangle
	Gdiplus::RectF Plate(Gdiplus::Graphics& g, const Gdiplus::RectF& plate, float radius);
	// a glossy pill button: primary = green (do it), else blue; hot = under the mouse (lighter)
	void Button(Gdiplus::Graphics& g, const Gdiplus::RectF& r, const wchar_t* label, bool primary, bool hot, float textSize = 16);

	// PNG bytes of a bitmap
	bool EncodePng(Gdiplus::Bitmap& bitmap, std::vector<uint8_t>& png);

	std::wstring Wide(const std::string& utf8);

	// a UTFWin window that shows a picture; `handler` gets the window's mouse messages (return true = consumed)
	class PictureWindow
	{
	public:
		typedef bool (*Handler)(const UTFWin::Message& message);
		explicit PictureWindow(Handler handler) : mHandler(handler) {}

		// shows `png` (width x height) at x, y of the main window, above the other windows
		bool Show(std::vector<uint8_t>& png, float x, float y, int width, int height);
		void Hide();
		bool Visible() const;
		// Dispose: takes the window out of the main window
		void Remove();
		UTFWin::IWindow* Window() const { return mWindow; }

		Handler mHandler;

	private:
		UTFWin::Window* mWindow = nullptr;
		UTFWin::ImageDrawable* mDrawable = nullptr;
		ImagePtr* mImage = nullptr;
	};
}
