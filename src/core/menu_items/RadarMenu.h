#ifndef __RADAR_MENU_H__
#define __RADAR_MENU_H__

#if !defined(LITE_VERSION)
#include <MenuItemInterface.h>

class RadarMenu : public MenuItemInterface {
public:
    RadarMenu() : MenuItemInterface("Radar") {}

    void optionsMenu(void);
    void drawIcon(float scale);

    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String empty;
        return empty;
    }
};

#endif
#endif
