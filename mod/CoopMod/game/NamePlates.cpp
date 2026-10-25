// NamePlates.cpp : see NamePlates.h.
#include "stdafx.h"
#include "NamePlates.h"
#include "CoopNet.h"
#include "UiDraw.h"
#include "../Log.h"

#include <Spore\App\cViewer.h>

#include <string>
#include <vector>

using namespace UiDraw;

namespace
{
	const float kMaxDistance = 120;   // metres from the camera; farther creatures get no plate
	const float kHeightFactor = 1.6f; // plate this many bounding radii above the creature's position

	struct Plate
	{
		PictureWindow* picture = nullptr;
		std::string* name = nullptr;  // heap (no static destructors)
		int width = 0, height = 0;
		bool own = false;
	};
	Plate sRemote, sOwn;
	bool sGdi = false;

	bool Ignore(const UTFWin::Message& message)
	{
		return false;  // plates do not take the mouse
	}

	// draws a plate for `name`: a small light glossy pill, the name in dark ink (own: smaller, grey-blue)
	bool Draw(Plate& p, const std::string& name)
	{
		if (!sGdi && !(sGdi = StartGdi())) {
			return false;
		}
		std::wstring text = Wide(name);
		float size = p.own ? 13.0f : 16.0f;
		// measure the text
		Gdiplus::Bitmap probe(1, 1, PixelFormat32bppARGB);
		Gdiplus::Graphics pg(&probe);
		Gdiplus::FontFamily family(L"Segoe UI");
		Gdiplus::Font font(&family, size, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
		Gdiplus::RectF bounds;
		pg.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &bounds);
		p.width = (int)bounds.Width + 34;
		p.height = (int)size + 22;
		Gdiplus::Bitmap bitmap(p.width, p.height, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&bitmap);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
		g.Clear(C(0, 0, 0, 0));
		Gdiplus::RectF plate(5, 3, p.width - 10.0f, p.height - 10.0f);
		UiDraw::Plate(g, plate, plate.Height / 2);
		Text(g, text, size, true, p.own ? C(255, 92, 106, 126) : C(255, 32, 48, 72), plate, Align::Center);
		std::vector<uint8_t> png;
		if (!EncodePng(bitmap, png)) {
			return false;
		}
		if (!p.picture) {
			p.picture = new PictureWindow(Ignore);
		}
		if (!p.name) {
			p.name = new std::string();
		}
		*p.name = name;
		return p.picture->Show(png, -1000, -1000, p.width, p.height);
	}

	// world point -> window pixels, from three camera rays (a perspective camera's rays, scaled to unit depth along the
	// view direction, are linear in the pixel coordinates)
	bool ToScreen(App::cViewer* viewer, const Vector3& world, float screenW, float screenH, float& x, float& y)
	{
		float cx = screenW / 2, cy = screenH / 2, step = 100;
		Vector3 eye, d0, dx, dy;
		if (!viewer->GetCameraToPoint(cx, cy, eye, d0) || !viewer->GetCameraToPoint(cx + step, cy, eye, dx)
			|| !viewer->GetCameraToPoint(cx, cy + step, eye, dy)) {
			return false;
		}
		Vector3 forward = d0.Normalized();
		float fx = dx.Dot(forward), fy = dy.Dot(forward);
		if (fx <= 0 || fy <= 0) {
			return false;
		}
		Vector3 right = dx / fx - forward, down = dy / fy - forward;  // per `step` pixels
		Vector3 v = world - eye;
		float depth = v.Dot(forward);
		if (depth < 1 || depth > kMaxDistance) {
			return false;
		}
		Vector3 q = v / depth - forward;
		float rr = right.Dot(right), dd = down.Dot(down);
		if (rr <= 0 || dd <= 0) {
			return false;
		}
		// GetCameraToPoint measures the vertical in the same units as the horizontal (seen in the game: vertical offsets came
		// out 1920/1080 times too big), so the vertical step is scaled by the screen's aspect
		x = cx + step * q.Dot(right) / rr;
		y = cy + step * q.Dot(down) / dd * (screenH / screenW);
		return x > -50 && x < screenW + 50 && y > -50 && y < screenH + 50;
	}

	void Place(Plate& p, Simulator::cSpatialObject* creature, const std::string& name, App::cViewer* viewer, float screenW,
		float screenH)
	{
		if (!creature || name.empty()) {
			if (p.picture) p.picture->Hide();
			return;
		}
		if (!p.name || *p.name != name || !p.picture) {
			if (!Draw(p, name)) {
				return;
			}
		}
		const Vector3& pos = creature->GetPosition();
		Vector3 up = pos.Normalized();  // the planet is a sphere around the origin
		float radius = creature->GetBoundingRadius();
		if (radius <= 0 || radius > 20) {
			radius = 1;
		}
		Vector3 head = pos + up * (radius * kHeightFactor);
		float x, y;
		if (!viewer || !ToScreen(viewer, head, screenW, screenH, x, y)) {
			p.picture->Hide();
			return;
		}
		p.picture->MoveTo(x - p.width / 2.0f, y - p.height);
	}

	void HideAll()
	{
		if (sRemote.picture) sRemote.picture->Hide();
		if (sOwn.picture) sOwn.picture->Hide();
	}

	void Free(Plate& p)
	{
		if (p.picture) {
			p.picture->Remove();
			delete p.picture;
			p.picture = nullptr;
		}
		delete p.name;
		p.name = nullptr;
	}

	std::string NameOf(uint64_t id, const char* fallback)
	{
		std::string name = id ? CoopNet::Name(id) : "";
		return name.empty() || name == "[unknown]" ? fallback : name;
	}
}

namespace NamePlates
{
	void Update()
	{
		CoopNet::HostState host = CoopNet::Hosting();
		Simulator::cCreatureAnimal* puppet = CoopNet::Puppet();
		bool session = host.hosting || host.joined || puppet;  // a puppet without a session: the one-PC `coop echo` test
		Simulator::cCreatureAnimal* avatar = GameNounManager.GetAvatar();
		if (!session || !Simulator::IsCreatureGame() || !avatar
			|| GameModeManager.GetActiveModeID() != (uint32_t)GameModeIDs::kGameCreature) {
			HideAll();
			return;
		}
		UTFWin::IWindow* main = WindowManager.GetMainWindow();
		App::cViewer* viewer = App::GetViewer();
		if (!main || !viewer) {
			HideAll();
			return;
		}
		float w = main->GetArea().GetWidth(), h = main->GetArea().GetHeight();
		sOwn.own = true;
		uint64_t peer = CoopNet::PeerId();
		std::string remote = peer ? NameOf(peer, "Friend") : puppet ? NameOf(host.myId, "You") + " (echo)" : "";
		Place(sRemote, puppet, remote, viewer, w, h);
		Place(sOwn, avatar, NameOf(host.myId, "You"), viewer, w, h);
	}

	void Shutdown()
	{
		Free(sRemote);
		Free(sOwn);
		if (sGdi) {
			StopGdi();
			sGdi = false;
		}
	}
}
