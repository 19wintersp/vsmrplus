#include <cstdint>
#include <cstring>

#include <fstream>
#include <memory>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <windows.h>
#include <gdiplus.h>
#include <gdiplusgraphics.h>

#include <EuroScopePlugIn.hpp>

namespace EuroScope = EuroScopePlugIn;

#define PLUGIN_NAME    "vSMR+"
#define PLUGIN_VERSION "0.2.0"
#define PLUGIN_AUTHORS "Patrick Winters"
#define PLUGIN_LICENSE "GNU GPLv3"

#define COLOUR_CLOSED  0xff, 0x96, 0x00, 0x00
#define COLOUR_HOTSPOT 0x80, 0xd9, 0x46, 0xef
#define COLOUR_STUP    0xff, 0x10, 0xb9, 0x81
#define COLOUR_PUSH    0xff, 0x3b, 0x82, 0xf6
#define COLOUR_WARN    0xff, 0xf9, 0x73, 0x16

const int OBJECT_TYPE_HOTSPOT = 1;
const int OBJECT_TYPE_DEHIGHLIGHT = 2;

const int HOTSPOT_SIZE = 16;
const int HOTSPOT_STROKE = 2;
const int HIGHLIGHT_SIZE = 24;
const int HIGHLIGHT_STROKE = 2;

const double WARN_DIST = 0.1; // nmi

struct Hotspot {
	EuroScope::CPosition position;
	std::string value;
	std::uint32_t colour;
};

class Plugin;

class Screen : public EuroScope::CRadarScreen {
private:
	Plugin *plugin;

public:
	Screen(Plugin *p) : plugin(p) {}

	void OnAsrContentToBeClosed(void) override {}
	void OnRefresh(HDC, int) override;
	void OnClickScreenObject(int, const char *, POINT, RECT, int) override;
};

class Plugin : public EuroScope::CPlugIn {
	friend class Screen;

private:
	std::vector<Hotspot> hotspot;
	std::unordered_map<std::string, const Hotspot *> hotspot_by_name;
	std::vector<std::vector<EuroScope::CPosition>> closed;
	std::unordered_set<std::string> dehighlight;

public:
	Plugin(void) : CPlugIn(
		EuroScope::COMPATIBILITY_CODE,
		PLUGIN_NAME, PLUGIN_VERSION, PLUGIN_AUTHORS, PLUGIN_LICENSE
	) {
		try {
			init();
		} catch (const std::exception &err) {
			warn(err.what());
			throw;
		}
	}

	Screen *OnRadarScreenCreated(const char *, bool, bool, bool, bool) override;
	void OnAirportRunwayActivityChanged() override;
	bool OnCompileCommand(const char *) override;
	void OnTimer(int) override;

private:
	void init();
	void warn(const char *);
	void load();
};

Plugin *instance;

void __declspec(dllexport) EuroScopePlugInInit(EuroScope::CPlugIn **ptr) {
	*ptr = instance = new Plugin();
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
	if (instance) delete instance;
}

void Screen::OnRefresh(HDC hdc, int phase) {
	using namespace Gdiplus;

	Graphics *ctx = Graphics::FromHDC(hdc);

	RECT crop = GetRadarArea();

	if (phase == EuroScope::REFRESH_PHASE_BACK_BITMAP) {
		Color hotspot_colour(Color::MakeARGB(COLOUR_HOTSPOT));
		Color closed_colour(Color::MakeARGB(COLOUR_CLOSED));

		Pen hotspot_pen(hotspot_colour, HOTSPOT_STROKE);
		SolidBrush closed_brush(closed_colour);

		for (const auto &hotspot : plugin->hotspot) {
			POINT centre = ConvertCoordFromPositionToPixel(hotspot.position);

			if (centre.x < crop.left || centre.x > crop.right) continue;
			if (centre.y < crop.top || centre.y > crop.bottom) continue;

			if (hotspot.colour) hotspot_pen.SetColor(Color(hotspot.colour));

			POINT point = { centre.x - HOTSPOT_SIZE / 2, centre.y - HOTSPOT_SIZE / 2 };
			Rect rect(point.x, point.y, HOTSPOT_SIZE, HOTSPOT_SIZE);
			ctx->DrawEllipse(&hotspot_pen, rect);
		}

		for (const auto &poly : plugin->closed) {
			Point points[poly.size()];
			for (int i = 0; i < poly.size(); i++) {
				POINT p = ConvertCoordFromPositionToPixel(poly[i]);
				points[i] = Point(p.x, p.y);
			}

			ctx->FillPolygon(&closed_brush, points, poly.size());
		}
	} else if (phase == EuroScope::REFRESH_PHASE_BEFORE_TAGS) {
		Color
			stup_colour(Color::MakeARGB(COLOUR_STUP)),
			push_colour(Color::MakeARGB(COLOUR_PUSH)),
			warn_colour(Color::MakeARGB(COLOUR_WARN));
		Pen
			stup_pen(stup_colour, HIGHLIGHT_STROKE),
			push_pen(push_colour, HIGHLIGHT_STROKE),
			warn_pen(warn_colour, HIGHLIGHT_STROKE);

		for (const auto &hotspot : plugin->hotspot) {
			POINT centre = ConvertCoordFromPositionToPixel(hotspot.position);

			if (centre.x < crop.left || centre.x > crop.right) continue;
			if (centre.y < crop.top || centre.y > crop.bottom) continue;

			RECT area = {
				centre.x - HOTSPOT_SIZE / 2, centre.y - HOTSPOT_SIZE / 2,
				centre.x + HOTSPOT_SIZE / 2, centre.y + HOTSPOT_SIZE / 2
			};

			const char *value = hotspot.value.c_str();
			AddScreenObject(OBJECT_TYPE_HOTSPOT, value, area, false, value);
		}

		for (
			auto fp = plugin->FlightPlanSelectFirst();
			fp.IsValid();
			fp = plugin->FlightPlanSelectNext(fp)
		) {
			const char *gs = fp.GetGroundState();
			Pen *pen;

			if (!std::strcmp(gs, "STUP") || !std::strcmp(gs, "PUSH")) {
				pen = gs[0] == 'P' ? &push_pen : &stup_pen;
			} else if (!std::strcmp(gs, "TAXI")) {
				if (plugin->dehighlight.contains(fp.GetCallsign())) continue;

				auto spad = fp.GetControllerAssignedData().GetScratchPadString();
				auto iter = plugin->hotspot_by_name.find(spad);
				if (iter == plugin->hotspot_by_name.cend()) continue;

				auto posn = fp.GetFPTrackPosition().GetPosition();
				if (std::get<1>(*iter)->position.DistanceTo(posn) > WARN_DIST) continue;

				auto half = HIGHLIGHT_SIZE / 2;
				POINT c = ConvertCoordFromPositionToPixel(fp.GetFPTrackPosition().GetPosition());
				RECT area = { c.x - half, c.y - half, c.x + half, c.y + half };
				AddScreenObject(OBJECT_TYPE_DEHIGHLIGHT, fp.GetCallsign(), area, false, "Dehighlight");

				pen = &warn_pen;
			} else {
				continue;
			}

			POINT centre = ConvertCoordFromPositionToPixel(fp.GetFPTrackPosition().GetPosition());
			POINT point = { centre.x - HIGHLIGHT_SIZE / 2, centre.y - HIGHLIGHT_SIZE / 2 };
			Rect rect(point.x, point.y, HIGHLIGHT_SIZE, HIGHLIGHT_SIZE);
			ctx->DrawEllipse(pen, rect);
		}
	}
}

void Screen::OnClickScreenObject(int type, const char *id, POINT, RECT, int button) {
	if (button == EuroScope::BUTTON_RIGHT) {
		if (type == OBJECT_TYPE_HOTSPOT) {
			auto fpl = plugin->FlightPlanSelectASEL();
			if (fpl.IsValid()) {
				fpl.GetControllerAssignedData().SetScratchPadString("TAXI");
				fpl.GetControllerAssignedData().SetScratchPadString(id);
			}
		} else if (type == OBJECT_TYPE_DEHIGHLIGHT) {
			plugin->dehighlight.insert(id);
		}
	}
}

Screen *Plugin::OnRadarScreenCreated(const char *, bool, bool, bool geo, bool) {
	return geo ? new Screen(this) : nullptr; // leak
}

void Plugin::OnAirportRunwayActivityChanged() {
	load();
}

bool Plugin::OnCompileCommand(const char *cmd) {
	if (!std::strcmp(cmd, ".reloadvsmrplus")) {
		load();
		return true;
	}

	return false;
}

void Plugin::OnTimer(int) {
	std::erase_if(dehighlight, [this](const auto &callsign) {
		auto fp = FlightPlanSelect(callsign.c_str());
		return !fp.IsValid() || std::strcmp(fp.GetGroundState(), "TAXI");
	});
}

void Plugin::init() {
	load();
}

void Plugin::warn(const char *msg) {
	DisplayUserMessage(PLUGIN_NAME, "Warning", msg, true, false, false, true, false);
}

static std::string get_dll_path() {
	HMODULE module_self;
	if (
		!GetModuleHandleExA(
			0
				| GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCTSTR) get_dll_path,
			&module_self
		)
	) return std::string();

	char module_filename[256];
	GetModuleFileNameA(module_self, module_filename, 256);

	return std::string(module_filename);
}

void Plugin::load() {
	std::unordered_set<std::string> active_aerodromes;

	for (
		auto el = SectorFileElementSelectFirst(EuroScope::SECTOR_ELEMENT_AIRPORT);
		el.IsValid();
		el = SectorFileElementSelectNext(el, EuroScope::SECTOR_ELEMENT_AIRPORT)
	) {
		if (el.IsElementActive(false) || el.IsElementActive(true)) {
			active_aerodromes.insert(std::string(el.GetName()));
		}
	}

	hotspot.clear();
	hotspot_by_name.clear();
	closed.clear();

	std::unordered_map<std::string, Hotspot> named_hotspot;

	std::string path = get_dll_path();
	if (path.empty()) {
		warn("get_dll_path (GetModuleHandleExA/GetModuleFileNameA) failed");
		return;
	}

	path.erase(path.find_last_of(".") + 1);
	path.append("txt");

	std::ifstream is(path);
	std::string line;
	bool active = true;
	std::uint32_t colour = 0;

	while (std::getline(is, line)) {
		if (line.empty() || line[0] == ';') continue;

		std::istringstream buf(line);
		std::vector<std::string> parts(std::istream_iterator<std::string>(buf), {});

		if (parts.empty() || parts[0].size() != 1) goto fail;
		if (line[0] != 'A' && !active) continue;

		switch (line[0]) {
		case 'A':
			if (parts.size() != 2) goto fail;

			active = active_aerodromes.find(parts[1]) != active_aerodromes.end();

			break;

		case 'C': {
			if (parts.size() % 2 != 1) goto fail;

			std::vector<EuroScope::CPosition> poly;
			for (int i = 1; i < parts.size(); i += 2) {
				const char *lat = parts[i].c_str(), *lon = parts[i + 1].c_str();
				EuroScope::CPosition pos;
				if (!pos.LoadFromStrings(lon, lat)) goto fail;

				poly.push_back(pos);
			}

			closed.push_back(std::move(poly));

			break;
		}

		case 'F':
			if (parts.size() != 2) goto fail;

			colour = std::stoll(parts[1], nullptr, 16);

			break;

		case 'H':
			if (parts.size() != 3) goto fail;

			named_hotspot[std::move(parts[2])] = { {}, std::move(parts[1]), colour };

			break;

		case 'I': {
			if (parts.size() != 4) goto fail;

			const char *lat = parts[2].c_str(), *lon = parts[3].c_str();
			EuroScope::CPosition pos;
			if (!pos.LoadFromStrings(lon, lat)) goto fail;

			hotspot.push_back({ pos, std::move(parts[1]), colour });

			break;
		}

		default:
			goto fail;
		}

		continue;

	fail:
		warn("skipping invalid line in configuration file");
	}

	for (
		auto el = SectorFileElementSelectFirst(EuroScope::SECTOR_ELEMENT_FREE_TEXT);
		el.IsValid();
		el = SectorFileElementSelectNext(el, EuroScope::SECTOR_ELEMENT_FREE_TEXT)
	) {
		decltype(named_hotspot)::iterator it;
		if ((it = named_hotspot.find(el.GetName())) != named_hotspot.end()) {
			EuroScope::CPosition pos;
			if (!el.GetPosition(&pos, 0)) continue;

			auto nh = std::get<1>(*it);
			nh.position = pos;

			hotspot.push_back(std::move(nh));
		}
	}

	for (const auto &hotspot : hotspot) {
		hotspot_by_name[hotspot.value] = &hotspot;
	}
}
