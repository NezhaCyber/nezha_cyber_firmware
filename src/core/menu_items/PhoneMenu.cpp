#if !defined(LITE_VERSION)
#include "PhoneMenu.h"

#include "core/display.h"
#include "core/utils.h"
#include "modules/others/phone_suite.h"

void PhoneMenu::optionsMenu() {
    options = {
        {"Phone Home",   phone_home_setup    },
        {"Music Player", media_music_player  },
        {"Photo Viewer", media_photo_viewer  },
        {"Notes",        phone_notes_app     },
        {"Calculator",   phone_calculator_app},
        {"Stopwatch",    phone_stopwatch_app },
        {"Torch / SOS",  phone_flashlight_app},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Phone");
}

void PhoneMenu::drawIcon(float scale) {
    clearIconArea();
    int cx = iconCenterX;
    int cy = iconCenterY;

    int w = max(12, (int)(scale * 34.0f));
    int h = max(18, (int)(scale * 54.0f));
    int rad = max(2, (int)(scale * 4.0f));
    int x = cx - w / 2;
    int y = cy - h / 2;

    // Smartphone body
    tft.fillRoundRect(x, y, w, h, rad, bruceConfig.priColor);
    // Inner screen bezel
    int padX = max(2, (int)(scale * 3.0f));
    int padTop = max(3, (int)(scale * 6.0f));
    int padBot = max(4, (int)(scale * 8.0f));
    tft.fillRect(x + padX, y + padTop, w - 2 * padX, h - padTop - padBot, bruceConfig.bgColor);

    // Speaker notch + home button pill
    int spkW = max(4, w / 3);
    tft.drawFastHLine(cx - spkW / 2, y + max(1, padTop / 2), spkW, bruceConfig.bgColor);
    tft.fillCircle(cx, y + h - max(2, padBot / 2), max(1, (int)(scale * 2.0f)), bruceConfig.bgColor);

    // Mini 2x2 app tiles on phone screen
    int screenW = w - 2 * padX;
    int screenH = h - padTop - padBot;
    int tile = max(2, min(screenW, screenH) / 4);
    int gap = max(2, tile / 2);
    int gx = cx - tile - gap / 2;
    int gy = y + padTop + (screenH - (2 * tile + gap)) / 2;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            tft.fillRoundRect(
                gx + c * (tile + gap), gy + r * (tile + gap), tile, tile, 1, bruceConfig.priColor
            );
        }
    }
}
#endif
