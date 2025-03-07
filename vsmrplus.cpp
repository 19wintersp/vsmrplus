#include <cmath>
#include <cstdint>
#include <cstring>

#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
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
#define PLUGIN_VERSION "0.3.3"
#define PLUGIN_AUTHORS "Patrick Winters"
#define PLUGIN_LICENSE "GNU GPLv3"

#define ASR_TYPE "SMR radar display"

#define COLOUR_CLOSED  0xff, 0x96, 0x00, 0x00
#define COLOUR_HOTSPOT 0x80, 0xd9, 0x46, 0xef
#define COLOUR_STUP    0xff, 0x10, 0xb9, 0x81
#define COLOUR_PUSH    0xff, 0x3b, 0x82, 0xf6
#define COLOUR_WARN    0xff, 0xf9, 0x73, 0x16
#define COLOUR_ROSE_BG 0xff, 0xa3, 0xa3, 0xa3
#define COLOUR_ARMS_L  0xff, 0x52, 0x52, 0x52
#define COLOUR_ARMS_R  0xff, 0x73, 0x73, 0x73
#define COLOUR_NORTH_L 0xff, 0xdc, 0x26, 0x26
#define COLOUR_NORTH_R 0xff, 0xef, 0x44, 0x44

const COLORREF COLOUR_STAND[] = {
	RGB(0x66, 0x66, 0x66),
	RGB(0xcd, 0x31, 0x31),
	RGB(0x0d, 0xbc, 0x79),
	RGB(0xe5, 0xe5, 0x10),
	RGB(0x24, 0x72, 0xc8),
	RGB(0xbc, 0x3f, 0xbc),
	RGB(0x11, 0xa8, 0xcd),
	RGB(0xe5, 0xe5, 0xe5)
};

const int TAG_ITEM_STAND = 101;
const int TAG_FUNC_STAND = 201;

const int TAG_ITEM_DEHIGHLIGHT = 102;
const int TAG_FUNC_DEHIGHLIGHT = 202;

const int TAG_ITEM_PRESSURE = 103;
const int TAG_FUNC_PRESSURE_UPDATE = 203;
const int TAG_FUNC_PRESSURE_RESET  = 204;

const int OBJECT_TYPE_HOTSPOT = 1;
const int OBJECT_TYPE_DEHIGHLIGHT = 2;

const int HOTSPOT_SIZE = 16;
const int HOTSPOT_STROKE = 2;
const int HIGHLIGHT_SIZE = 24;
const int HIGHLIGHT_STROKE = 2;

const float ROSE_BORDER_WIDTH = 1;
const float ROSE_INNER_RADIUS = 6;
const float ROSE_ARM_RADIUS   = 20;
const float ROSE_NORTH_RADIUS = 32;

const char CHAR_BOX_FILLED = '\xa4';
const char CHAR_BOX_EMPTY  = '\xac';

const double WARN_DIST = 0.1; // nmi

struct Hotspot {
	EuroScope::CPosition position;
	std::string value;
	std::uint32_t colour;
};

struct StandInfo {
	char letter, prop_letter;
	size_t colour : 8, prop_colour : 8;
	std::string details;
};

class Plugin;

class Screen : public EuroScope::CRadarScreen {
private:
	Plugin *plugin;

public:
	Screen(Plugin *p) : plugin(p) {}

	void OnAsrContentToBeClosed(void) override;
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

	std::unordered_map<std::string, std::unordered_map<std::string, StandInfo>> stands;

	std::unordered_map<std::string, std::string> ac_pressure, ad_pressure;

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
	void OnFunctionCall(int, const char *, POINT, RECT) override;
	void OnGetTagItem(EuroScope::CFlightPlan, EuroScope::CRadarTarget, int, int, char[16], int *, COLORREF *, double *) override;
	void OnNewMetarReceived(const char *, const char *) override;
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

void Screen::OnAsrContentToBeClosed() {
	delete this;
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

				/* auto half = HIGHLIGHT_SIZE / 2;
				POINT c = ConvertCoordFromPositionToPixel(fp.GetFPTrackPosition().GetPosition());
				RECT area = { c.x - half, c.y - half, c.x + half, c.y + half };
				AddScreenObject(OBJECT_TYPE_DEHIGHLIGHT, fp.GetCallsign(), area, false, "Dehighlight"); */

				pen = &warn_pen;
			} else {
				continue;
			}

			POINT centre = ConvertCoordFromPositionToPixel(fp.GetFPTrackPosition().GetPosition());
			POINT point = { centre.x - HIGHLIGHT_SIZE / 2, centre.y - HIGHLIGHT_SIZE / 2 };
			Rect rect(point.x, point.y, HIGHLIGHT_SIZE, HIGHLIGHT_SIZE);
			ctx->DrawEllipse(pen, rect);
		}

		Color
			rose_bg_colour(Color::MakeARGB(COLOUR_ROSE_BG)),
			arms_l_colour(Color::MakeARGB(COLOUR_ARMS_L)),
			arms_r_colour(Color::MakeARGB(COLOUR_ARMS_R)),
			north_l_colour(Color::MakeARGB(COLOUR_NORTH_L)),
			north_r_colour(Color::MakeARGB(COLOUR_NORTH_R));
		SolidBrush
			arms_l_brush(arms_l_colour),
			arms_r_brush(arms_r_colour),
			north_l_brush(north_l_colour),
			north_r_brush(north_r_colour);
		Pen rose_bg_pen(rose_bg_colour, 2 * ROSE_BORDER_WIDTH);

		EuroScope::CPosition north, south;
		PointF vector, origin, outer[4], inner[4], points[8];
		float norm, r, k = std::numbers::sqrt2 * 0.5;

		GetDisplayArea(&south, &north);
		south.m_Longitude = north.m_Longitude;

		auto north_point = ConvertCoordFromPositionToPixel(north);
		auto south_point = ConvertCoordFromPositionToPixel(south);

		auto area = GetRadarArea();
		origin.X = area.left + 1.5 * ROSE_NORTH_RADIUS + 64;
		origin.Y = area.bottom - 1.5 * ROSE_NORTH_RADIUS;

		vector.X = north_point.x - south_point.x;
		vector.Y = north_point.y - south_point.y;

		norm = hypotf(vector.X, vector.Y);
		vector.X /= norm;
		vector.Y /= norm;

		for (int i = 0; i < 8; i++) {
			PointF *point = i % 2 ? &inner[i / 2] : &outer[i / 2];

			point->X = vector.X;
			point->Y = vector.Y;

			vector.X -= point->Y;
			vector.Y += point->X;

			vector.X *= k;
			vector.Y *= k;

			r = i ? (i % 2 ? ROSE_INNER_RADIUS : ROSE_ARM_RADIUS) : ROSE_NORTH_RADIUS;
			point->X *= r;
			point->Y *= r;

			point->X += origin.X;
			point->Y += origin.Y;

			points[i] = *point;
		}

		ctx->DrawPolygon(&rose_bg_pen, points, 8);

		points[0] = origin;

		for (int i = 0; i < 4; i++) {
			points[1] = outer[i];
			points[2] = inner[i];

			ctx->FillPolygon(i ? &arms_r_brush : &north_r_brush, points, 3);

			points[2] = inner[(i + 3) % 4];

			ctx->FillPolygon(i ? &arms_l_brush : &north_l_brush, points, 3);
		}
	}

	delete ctx;
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

Screen *Plugin::OnRadarScreenCreated(const char *name, bool, bool, bool geo, bool) {
	return geo && !std::strcmp(name, ASR_TYPE) ? new Screen(this) : nullptr;
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

void Plugin::OnFunctionCall(int code, const char *, POINT, RECT) {
	auto fp = FlightPlanSelectASEL();
	if (!fp.IsValid()) return;

	switch (code) {
		case TAG_FUNC_STAND: {
			auto it1 = stands.find(fp.GetFlightPlanData().GetOrigin());
			if (it1 == stands.cend()) return;

			auto std = fp.GetControllerAssignedData().GetFlightStripAnnotation(3);
			auto it2 = std::get<1>(*it1).find(std);
			if (it2 == std::get<1>(*it1).cend()) return;

			const std::string &details = std::get<1>(*it2).details;
			if (details.empty()) return;

			DisplayUserMessage(PLUGIN_NAME, std, details.c_str(), true, true, false, false, false);

			break;
		}

		case TAG_FUNC_DEHIGHLIGHT: {
			auto cs = fp.GetCallsign();

			if (dehighlight.contains(cs))
				dehighlight.erase(cs);
			else if (!std::strcmp(fp.GetGroundState(), "TAXI"))
				dehighlight.insert(cs);

			break;
		}

		case TAG_FUNC_PRESSURE_UPDATE: {
			auto it = ad_pressure.find(fp.GetFlightPlanData().GetOrigin());
			if (it != ad_pressure.cend())
				ac_pressure[std::string(fp.GetCallsign())] = std::get<1>(*it);

			break;
		}

		case TAG_FUNC_PRESSURE_RESET:
			ac_pressure.erase(fp.GetCallsign());
			break;
	}
}

void Plugin::OnGetTagItem(EuroScope::CFlightPlan fp, EuroScope::CRadarTarget, int code, int, char string[16], int *colour, COLORREF *rgb, double *) {
	if (!fp.IsValid()) return;

	switch (code) {
		case TAG_ITEM_STAND: {
			string[0] = 0;

			if (fp.GetDistanceFromOrigin() > 10.0) return;

			auto it1 = stands.find(fp.GetFlightPlanData().GetOrigin());
			if (it1 == stands.cend()) return;

			auto it2 = std::get<1>(*it1).find(fp.GetControllerAssignedData().GetFlightStripAnnotation(3));
			if (it2 == std::get<1>(*it1).cend()) return;

			auto stand = std::get<1>(*it2);

			char engine_type = fp.GetFlightPlanData().GetEngineType();
			bool prop = engine_type == 'P' || engine_type == 'T';

			string[0] = prop ? stand.prop_letter : stand.letter;
			string[1] = 0;

			*colour = EuroScope::TAG_COLOR_RGB_DEFINED;
			*rgb = COLOUR_STAND[prop ? stand.prop_colour : stand.colour];

			break;
		}

		case TAG_ITEM_DEHIGHLIGHT: {
			bool dehighlighted = dehighlight.contains(fp.GetCallsign());

			string[0] = dehighlighted ? CHAR_BOX_FILLED : CHAR_BOX_EMPTY;
			string[1] = 0;

			*colour = EuroScope::TAG_COLOR_DEFAULT;

			break;
		}

		case TAG_ITEM_PRESSURE: {
			string[0] = 0;

			auto it1 = ac_pressure.find(fp.GetCallsign());
			if (it1 == ac_pressure.cend()) return;

			bool ok = false;
			auto it2 = ad_pressure.find(fp.GetFlightPlanData().GetOrigin());
			if (it2 != ad_pressure.cend()) ok = std::get<1>(*it1) == std::get<1>(*it2);

			strncpy_s(string, 12, std::get<1>(*it1).c_str(), 2);

			*colour = ok ? EuroScope::TAG_COLOR_REDUNDANT : EuroScope::TAG_COLOR_INFORMATION;

			break;
		}
	}
}

void Plugin::OnNewMetarReceived(const char *ad, const char *metar) {
	// selon Annex 4, il y a jamais un "Q" avant la pression
	ad_pressure[ad] = std::string(std::strchr(metar, 'Q') + 3, 2);
}

void Plugin::OnTimer(int) {
	std::erase_if(dehighlight, [this](const auto &callsign) {
		auto fp = FlightPlanSelect(callsign.c_str());
		return !fp.IsValid() || std::strcmp(fp.GetGroundState(), "TAXI");
	});
}

void Plugin::init() {
	RegisterTagItemType("Stand information", TAG_ITEM_STAND);
	RegisterTagItemFunction("Show detailed stand information", TAG_FUNC_STAND);

	RegisterTagItemType("Handoff indicator", TAG_ITEM_DEHIGHLIGHT);
	RegisterTagItemFunction("Toggle handoff indicator", TAG_FUNC_DEHIGHLIGHT);

	RegisterTagItemType("Pressure setting", TAG_ITEM_PRESSURE);
	RegisterTagItemFunction("Update pressure setting", TAG_FUNC_PRESSURE_UPDATE);
	RegisterTagItemFunction("Reset pressure setting", TAG_FUNC_PRESSURE_RESET);

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
	stands.clear();

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

	decltype(stands)::mapped_type *current_stands;

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
			current_stands = &stands[parts[1]];

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

		case 'P': {
			if (parts.size() < 3 || parts.size() > 4) goto fail;
			if (!active) continue;

			auto &stand = current_stands->at(parts[1]);
			stand.prop_letter = parts[2][0];
			stand.prop_colour = parts.size() < 4 ? 0 : parts[3][0] - '0';

			break;
		}

		case 'S': {
			if (parts.size() < 3) goto fail;
			if (!active) continue;

			StandInfo stand;
			stand.letter = stand.prop_letter = parts[2][0];
			stand.colour = stand.prop_colour = parts.size() < 4 ? 0 : parts[3][0] - '0';

			if (parts.size() > 4) {
				size_t offset = 0;
				for (int i = 0; i < 4; i++) {
					offset = line.find_first_of("\t ", offset);
					offset = line.find_first_not_of("\t ", offset);
				}

				stand.details = std::string(line.c_str() + offset);
			}

			current_stands->insert({ parts[1], std::move(stand) });

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

	EuroScope::CPosition centre = ControllerMyself().GetPosition();
	double range = ControllerMyself().GetRange();

	for (const auto &hotspot : hotspot)
		if (hotspot.position.DistanceTo(centre) < range)
			hotspot_by_name[hotspot.value] = &hotspot;
}
