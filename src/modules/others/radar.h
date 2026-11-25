#ifndef __RADAR_H__
#define __RADAR_H__

#if !defined(LITE_VERSION)

enum RadarInitialMode {
    RADAR_INIT_BOTH = 0,
    RADAR_INIT_WIFI = 1,
    RADAR_INIT_BLE = 2,
};

void radar_setup(RadarInitialMode initialMode = RADAR_INIT_BOTH);
void radar_locate_setup();

#endif // !LITE_VERSION

#endif // __RADAR_H__
