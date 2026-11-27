#if !defined(LITE_VERSION)
#include "RadarMenu.h"

#include "core/display.h"
#include "core/location_state.h"
#include "core/utils.h"
#include "modules/others/radar.h"
#include "modules/others/wireless_sentinel.h"

void RadarMenu::optionsMenu() {
    bool isStartup = (bruceConfig.startupApp == "Radar");
    // Use local vector (never global `options`) so nested menus inside Radar cannot destroy executing lambdas
    std::vector<Option> localOpts = {
        {"Start Radar (W+B)",    []() { radar_setup(RADAR_INIT_BOTH, RADAR_VIEW_SCOPE); }     },
        {"Wireless Sentinel",    []() { wireless_sentinel_menu(); }                           },
        {"Intel Table",          []() { radar_setup(RADAR_INIT_BOTH, RADAR_VIEW_TABLE); }     },
        {"Spectrum / Fox-Hunt",  []() { radar_setup(RADAR_INIT_BOTH, RADAR_VIEW_FOX_SPEC); }  },
        {"Trackers & Alerts",    []() { radar_setup(RADAR_INIT_ALERTS, RADAR_VIEW_TABLE); }   },
        {"Locate / Triangulate", []() { radar_locate_setup(); }                               },
        {"Location State",       []() { bruceLocation.showLocationInfoScreen(); }             },
        {"WiFi Radar",           []() { radar_setup(RADAR_INIT_WIFI, RADAR_VIEW_SCOPE); }     },
        {"BLE Radar",            []() { radar_setup(RADAR_INIT_BLE, RADAR_VIEW_SCOPE); }      },
        {isStartup ? "Unset Startup App" : "Set as Startup App",
         [isStartup]() {
             bruceConfig.setStartupApp(isStartup ? "" : "Radar");
             displaySuccess(isStartup ? "Startup App cleared" : "Radar set as Startup App", true);
         }},
        {"Main Menu",            []() { backToMenu(); }                                       },
    };
    loopOptions(localOpts, MENU_TYPE_SUBMENU, "Radar");
}

void RadarMenu::drawIcon(float scale) {
    clearIconArea();
    int cx = iconCenterX;
    int cy = iconCenterY;

    int r = (int)(scale * 26.0f);
    if (r < 9) r = 9;
    int rMid = (r * 2) / 3;
    int rIn = r / 3;
    int lineW = (int)(scale * 2.0f);
    if (lineW < 1) lineW = 1;

    tft.drawCircle(cx, cy, r, bruceConfig.priColor);
    if (r >= 14) tft.drawCircle(cx, cy, r - 1, bruceConfig.priColor);
    tft.drawCircle(cx, cy, rMid, bruceConfig.priColor);
    tft.drawCircle(cx, cy, rIn, bruceConfig.priColor);

    tft.drawFastHLine(cx - r, cy, 2 * r + 1, bruceConfig.priColor);
    tft.drawFastVLine(cx, cy - r, 2 * r + 1, bruceConfig.priColor);

    int sx = cx + (int)(r * 0.70f);
    int sy = cy - (int)(r * 0.70f);
    tft.drawWideLine(cx, cy, sx, sy, lineW, bruceConfig.priColor, bruceConfig.bgColor);

    int dotR = max(2, (int)(scale * 2.5f));
    tft.fillCircle(cx, cy, dotR, bruceConfig.priColor);
    tft.fillCircle(cx - rMid / 2, cy - rMid / 2, dotR, bruceConfig.priColor);
    tft.fillCircle(cx + rMid / 2, cy + rIn, dotR, bruceConfig.priColor);
}
#endif
