#ifndef __PHONE_MENU_H__
#define __PHONE_MENU_H__

#if !defined(LITE_VERSION)
#include <MenuItemInterface.h>

class PhoneMenu : public MenuItemInterface {
public:
    PhoneMenu() : MenuItemInterface("Phone") {}

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
