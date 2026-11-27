#ifndef __RADAR_H__
#define __RADAR_H__

#if !defined(LITE_VERSION)

enum RadarInitialMode {
    RADAR_INIT_BOTH = 0,
    RADAR_INIT_WIFI = 1,
    RADAR_INIT_BLE = 2,
    RADAR_INIT_ALERTS = 3,
    RADAR_INIT_OPEN = 4,
};

enum RadarInitialView {
    RADAR_VIEW_SCOPE = 0,
    RADAR_VIEW_TABLE = 1,
    RADAR_VIEW_FOX_SPEC = 2,
    RADAR_VIEW_THREAT = 3,
};

void radar_setup(
    RadarInitialMode initialMode = RADAR_INIT_BOTH, RadarInitialView initialView = RADAR_VIEW_SCOPE
);
void radar_locate_setup();

#endif // !LITE_VERSION

#endif // __RADAR_H__
