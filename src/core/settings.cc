#include "settings.h"
#include "core/i18n.h"
#include "core/led_control.h"
#include "core/wifi/wifi_common.h"
#include "current_year.h"
#include "display.h"
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
#include "modules/bjs_interpreter/interpreter.h"
#endif
#include "modules/ble_api/ble_api.hpp"
#if !defined(LITE_VERSION)
#include "modules/others/audio.h"
#endif
#include "modules/others/qrcode_menu.h"
#include "modules/rf/rf_utils.h" // for initRfModule
#include "mykeyboard.h"
#include "powerSave.h"
#include "sd_functions.h"
#include "settingsColor.h"
#include "utils.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <globals.h>
#include <nvs.h>
#if defined(HAS_RESISTIVE_TOUCH)
#include <CYD28_TouchscreenR.h>
#endif

int currentScreenBrightness = -1;

// This function comes from interface.h
void _setBrightness(uint8_t brightval) {}

/*
 *  Function: setBrightness
 *  set brightness value
 */
void setBrightness(uint8_t brightval, bool save) {
    if (bruceConfig.bright > 100) bruceConfig.setBright(100);
    _setBrightness(brightval);
    delay(10);

    currentScreenBrightness = brightval;
    if (save) { bruceConfig.setBright(brightval); }
}

/*
 *  Function: getBrightness
 *  get brightness value
 */
void getBrightness() {
    if (bruceConfig.bright > 100) {
        bruceConfig.setBright(100);
        _setBrightness(bruceConfig.bright);
        delay(10);
        setBrightness(100);
    }

    _setBrightness(bruceConfig.bright);
    delay(10);

    currentScreenBrightness = bruceConfig.bright;
}

/*
 *  Function: gsetRotation
 *  get/set rotation value
 */
int gsetRotation(bool set) {
    int getRot = bruceConfigPins.rotation;
    int result = ROTATION;
    int mask = ROTATION > 1 ? -2 : 2;

    options = {
        {"Default",         [&]() { result = ROTATION; }                        },
        {"Landscape (180)", [&]() { result = ROTATION + mask; }                 },
#if TFT_WIDTH >= 170 && TFT_HEIGHT >= 240
        {"Portrait (+90)",  [&]() { result = ROTATION > 0 ? ROTATION - 1 : 3; } },
        {"Portrait (-90)",  [&]() { result = ROTATION == 3 ? 0 : ROTATION + 1; }},

#endif
    };
    addOptionToMainMenu();
    if (set) loopOptions(options);
    else result = getRot;

    if (result > 3 || result < 0) {
        result = ROTATION;
        set = true;
    }
    if (set) {
        bruceConfigPins.setRotation(result);
        tft.setRotation(result);
        tft.setRotation(result); // must repeat, sometimes ESP32S3 miss one SPI command and it just
                                 // jumps this step and don't rotate
    }
    returnToMenu = true;

    if (result & 0b01) { // if 1 or 3
        tftWidth = TFT_HEIGHT;
#if defined(HAS_TOUCH)
        tftHeight = TFT_WIDTH - TOUCH_FOOTER_HEIGHT;
#else
        tftHeight = TFT_WIDTH;
#endif
    } else { // if 2 or 0
        tftWidth = TFT_WIDTH;
#if defined(HAS_TOUCH)
        tftHeight = TFT_HEIGHT - TOUCH_FOOTER_HEIGHT;
#else
        tftHeight = TFT_HEIGHT;
#endif
    }
    return result;
}

/*
 *  Function: setBrightnessMenu
 *  Handles Menu to set brightness (live-preview slider; drag on touch
 *  boards, Next/Prev to step by 5%, Sel to apply, Esc to cancel)
 */
void setBrightnessMenu() {
    returnToMenu = false;
    bool prevTouchZone = touchZoneOutsideFooterEnabled;
#if defined(HAS_TOUCH)
    touchZoneOutsideFooterEnabled = false;
#endif

    int original = bruceConfig.bright;
    int val = (bruceConfig.bright / 5) * 5;
    if (val > 100) val = 100;
    if (val < 0) val = 0;

    bool compact = (tftHeight < 160);
    const int titleH = compact ? (LH * FP + 4) : 18;
    const int hintH = LH * FP;
    const int sliderH = compact ? 8 : 10;
    const int thumbR = compact ? 6 : 7;
    const int spacing = compact ? 10 : 14;
    const int padTop = compact ? 10 : 14;
    const int padBot = compact ? 8 : 12;
    const int padSide = compact ? 14 : 18;

    int contentWidth = tftWidth - (compact ? 24 : 44);
    int contentHeight = padTop + titleH + spacing + thumbR * 2 + spacing + hintH + padBot;
    int boxX = (tftWidth - contentWidth) / 2;
    int boxY = (tftHeight - contentHeight) / 2;

    int trackX = boxX + padSide;
    int trackW = contentWidth - 2 * padSide;
    int trackCenterY = boxY + padTop + titleH + spacing + thumbR;
    int trackY = trackCenterY - sliderH / 2;

    uint16_t surf = uiSurfaceColor();
    uint16_t surfHi = uiSurfaceHiColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();
    uint16_t pri = bruceConfig.priColor;

    bool redraw = true;
    bool first_draw = true;
    while (1) {
        if (redraw) {
            if (first_draw) {
                tft.fillRoundRect(boxX, boxY, contentWidth, contentHeight, 8, surf);
                tft.drawRoundRect(boxX, boxY, contentWidth, contentHeight, 8, bdim);
                tft.setTextSize(FP);
                tft.setTextColor(muted, surf);
                tft.drawCentreString(
                    bruce_i18n::tr("Sel to apply"), boxX + contentWidth / 2,
                    trackCenterY + thumbR + spacing, 1
                );
                first_draw = false;
            }

            // Header row: label left, percentage right
            tft.fillRect(boxX + padSide, boxY + padTop, contentWidth - 2 * padSide, titleH, surf);
            String pctStr = String(val) + "%";
            if (!compact) {
                uiDrawText16(
                    bruce_i18n::tr("Brightness"), boxX + padSide, boxY + padTop, contentWidth - 2 * padSide - 48,
                    bright, surf, 0
                );
                uiDrawText16(pctStr, boxX + contentWidth - padSide, boxY + padTop, 48, pri, surf, 2);
            } else {
                tft.setTextSize(FP);
                tft.setTextColor(bright, surf);
                tft.drawString(bruce_i18n::tr("Brightness"), boxX + padSide, boxY + padTop + 2, 1);
                tft.setTextColor(pri, surf);
                tft.drawRightString(pctStr, boxX + contentWidth - padSide, boxY + padTop + 2, 1);
            }

            // Slider band clear + modern track + solid fill + crisp thumb knob
            tft.fillRect(trackX - thumbR - 2, trackCenterY - thumbR - 2, trackW + (thumbR + 2) * 2, (thumbR + 2) * 2, surf);
            tft.fillRoundRect(trackX, trackY, trackW, sliderH, sliderH / 2, surfHi);
            int fillW = (trackW * val) / 100;
            if (fillW >= sliderH) {
                tft.fillRoundRect(trackX, trackY, fillW, sliderH, sliderH / 2, pri);
            } else if (fillW > 0) {
                tft.fillRoundRect(trackX, trackY, sliderH, sliderH, sliderH / 2, pri);
            }
            int knobX = trackX + fillW;
            tft.fillCircle(knobX, trackCenterY, thumbR, bright);
            tft.fillCircle(knobX, trackCenterY, max(2, thumbR - 4), pri);

            setBrightness(val, false);
            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            const int touchX = touchPoint.x;
            const int touchY = touchPoint.y;
            touchPoint.Clear();

            const int touchPadding = 16;
            if (touchX >= boxX && touchX <= boxX + contentWidth && touchY >= trackCenterY - thumbR - touchPadding &&
                touchY <= trackCenterY + thumbR + touchPadding) {
                int clampedX = touchX;
                if (clampedX < trackX) clampedX = trackX;
                if (clampedX > trackX + trackW) clampedX = trackX + trackW;
                int newVal = ((clampedX - trackX) * 100 + trackW / 2) / trackW;
                newVal = (newVal / 5) * 5;
                if (newVal > 100) newVal = 100;
                if (newVal < 0) newVal = 0;
                if (newVal != val) {
                    val = newVal;
                    redraw = true;
                }
            }
        }
#endif

        if (check(NextPress) || check(UpPress)) {
            val += 5;
            if (val > 100) val = 100;
            redraw = true;
        }
        if (check(PrevPress) || check(DownPress)) {
            val -= 5;
            if (val < 0) val = 0;
            redraw = true;
        }
        if (check(SelPress)) {
            setBrightness(val);
            break;
        }
        if (check(EscPress) || returnToMenu) {
            setBrightness(original, false);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    touchZoneOutsideFooterEnabled = prevTouchZone;
    tft.fillScreen(bruceConfig.bgColor);
}

/*
 *  Function: setSleepMode
 *  Turn screen off and reduces cpu clock
 */
void setSleepMode() {
    sleepModeOn();
    while (1) {
        if (check(AnyKeyPress)) {
            sleepModeOff();
            returnToMenu = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*
 *  Function: setDimmerTimeMenu
 *  Handles Menu to set dimmer time
 */
void setDimmerTimeMenu() {
    int idx = 0;
    if (bruceConfig.dimmerSet == 10) idx = 0;
    else if (bruceConfig.dimmerSet == 20) idx = 1;
    else if (bruceConfig.dimmerSet == 30) idx = 2;
    else if (bruceConfig.dimmerSet == 60) idx = 3;
    else if (bruceConfig.dimmerSet == 0) idx = 4;
    options = {
        {"10s",      [=]() { bruceConfig.setDimmer(10); }, bruceConfig.dimmerSet == 10},
        {"20s",      [=]() { bruceConfig.setDimmer(20); }, bruceConfig.dimmerSet == 20},
        {"30s",      [=]() { bruceConfig.setDimmer(30); }, bruceConfig.dimmerSet == 30},
        {"60s",      [=]() { bruceConfig.setDimmer(60); }, bruceConfig.dimmerSet == 60},
        {"Disabled", [=]() { bruceConfig.setDimmer(0); },  bruceConfig.dimmerSet == 0 },
    };
    loopOptions(options, idx);
}

/*
 *  Function: setUIColor
 *  Set and store main UI color
 */
void setUIColor() {

    while (1) {
        options.clear();
        int idx = UI_COLOR_COUNT;
        int i = 0;
        for (const auto &mapping : UI_COLORS) {
            if (bruceConfig.priColor == mapping.priColor && bruceConfig.secColor == mapping.secColor &&
                bruceConfig.bgColor == mapping.bgColor) {
                idx = i;
            }

            options.emplace_back(
                mapping.name,
                [=, &mapping]() {
                    uint16_t secColor = mapping.secColor;
                    uint16_t bgColor = mapping.bgColor;
                    bruceConfig.setUiColor(mapping.priColor, &secColor, &bgColor);
                },
                idx == i,
                nullptr,
                nullptr,
                false,
                true,
                true,
                mapping.priColor
            );
            ++i;
        }

        options.push_back(
            {"Custom Color",
             [=]() {
                 uint16_t oldPriColor = bruceConfig.priColor;
                 uint16_t oldSecColor = bruceConfig.secColor;
                 uint16_t oldBgColor = bruceConfig.bgColor;

                 if (setCustomUIColorMenu()) {
                     bruceConfig.setUiColor(
                         bruceConfig.priColor, &bruceConfig.secColor, &bruceConfig.bgColor
                     );
                 } else {
                     bruceConfig.priColor = oldPriColor;
                     bruceConfig.secColor = oldSecColor;
                     bruceConfig.bgColor = oldBgColor;
                 }
                 tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
             },
             idx == UI_COLOR_COUNT}
        );

        options.push_back(
            {"Invert Color",
             [=]() {
                 bruceConfig.setColorInverted(!bruceConfig.colorInverted);
                 tft.invertDisplay(bruceConfig.colorInverted);
             },
             bruceConfig.colorInverted > 0}
        );

        addOptionToMainMenu();

        int selectedOption = loopOptions(options, idx);
        if (selectedOption == -1 || selectedOption == options.size() - 1) return;
    }
}

/*
 *  Function: setNezhaCrestMenu
 *  Choose the 哪吒1 / 哪吒2 UI motif / crest style across HUD & Launcher
 */
void setNezhaCrestMenu() {
    std::vector<Option> crestOptions = {
        {"Nezha 1: Flame",       []() { bruceConfig.setNezhaThemeStyle(1); invalidateStatusBarCache(); }, bruceConfig.nezhaThemeStyle == 1},
        {"Nezha 2: Sea Rage",    []() { bruceConfig.setNezhaThemeStyle(4); invalidateStatusBarCache(); }, bruceConfig.nezhaThemeStyle == 4},
        {"Nezha 2: Dragon Seal", []() { bruceConfig.setNezhaThemeStyle(5); invalidateStatusBarCache(); }, bruceConfig.nezhaThemeStyle == 5},
        {"Lotus Seal",           []() { bruceConfig.setNezhaThemeStyle(2); invalidateStatusBarCache(); }, bruceConfig.nezhaThemeStyle == 2},
        {"Ice-Fire Twin",        []() { bruceConfig.setNezhaThemeStyle(3); invalidateStatusBarCache(); }, bruceConfig.nezhaThemeStyle == 3},
        {"Minimal",              []() { bruceConfig.setNezhaThemeStyle(0); invalidateStatusBarCache(); }, bruceConfig.nezhaThemeStyle == 0},
        {"Back",                 []() {}                                                                                                  },
    };
    int initialIdx = 0;
    if (bruceConfig.nezhaThemeStyle == 4) initialIdx = 1;
    else if (bruceConfig.nezhaThemeStyle == 5) initialIdx = 2;
    else if (bruceConfig.nezhaThemeStyle == 2) initialIdx = 3;
    else if (bruceConfig.nezhaThemeStyle == 3) initialIdx = 4;
    else if (bruceConfig.nezhaThemeStyle == 0) initialIdx = 5;
    loopOptions(crestOptions, MENU_TYPE_SUBMENU, "Theme Crest", initialIdx);
}

/*
 *  Function: setNezhaThemeGallery
 *  Live interactive 24-pack 哪吒1 (魔童降世) & 哪吒2 (魔童闹海) Theme Studio
 */
void setNezhaThemeGallery() {
    int idx = 0;
    for (int i = 0; i < NEZHA_THEME_COUNT; i++) {
        if (bruceConfig.priColor == NEZHA_THEME_PACKS[i].priColor &&
            bruceConfig.secColor == NEZHA_THEME_PACKS[i].secColor &&
            bruceConfig.bgColor == NEZHA_THEME_PACKS[i].bgColor) {
            idx = i;
            break;
        }
    }

    auto applyPack = [&](int i) {
        const auto &p = NEZHA_THEME_PACKS[i];
        uint16_t sec = p.secColor;
        uint16_t bg = p.bgColor;
        bruceConfig.removeTheme();
        bruceConfig.themePath = "";
        bruceConfig.theme.fs = 0;
        bruceConfig.nezhaThemeStyle = p.crestStyle;
        bruceConfig.setUiColor(p.priColor, &sec, &bg);
#ifdef HAS_RGB_LED
        bruceConfig.ledColor = p.ledRgb;
        ledSetup();
#endif
        bruceConfig.saveFile();
        invalidateStatusBarCache();
#if !defined(LITE_VERSION)
        if (bruceConfig.soundEnabled) {
            _tone(780, 40);
            _tone(1040, 55);
        }
#endif
    };

#if defined(BRUCE_COMPACT_CJK_FONT)
    // Compact 4MB Flash boards: clean direct list of all 24 哪吒1 & 哪吒2 theme packs
    std::vector<Option> packOpts;
    packOpts.reserve(NEZHA_THEME_COUNT + 1);
    for (int i = 0; i < NEZHA_THEME_COUNT; i++) {
        const auto &p = NEZHA_THEME_PACKS[i];
        bool active = (bruceConfig.priColor == p.priColor && bruceConfig.secColor == p.secColor &&
                       bruceConfig.bgColor == p.bgColor);
        packOpts.emplace_back(p.name, [&, i]() { applyPack(i); }, active, nullptr, nullptr, false, true, true, p.priColor);
    }
    packOpts.push_back({"Back", []() {}});
    loopOptions(packOpts, MENU_TYPE_SUBMENU, "Nezha Themes", idx);
    return;
#else
    const bool compact = (tftHeight < 180);
#if defined(HAS_TOUCH)
    bool prevTouchZone = touchZoneOutsideFooterEnabled;
    touchZoneOutsideFooterEnabled = false;
#endif

    auto drawGallery = [&](int i, bool justApplied) {
        const auto &p = NEZHA_THEME_PACKS[i];
        const bool isChapter2 = (p.chapter == 2);
        uint16_t bg = p.bgColor;
        uint16_t pri = p.priColor;
        uint16_t sec = p.secColor;
        uint16_t surf = (bg == 0x0000) ? 0x10A2 : blendColors(bg, TFT_WHITE, 26);
        uint16_t brd = blendColors(bg, TFT_WHITE, 64);
        uint16_t bright = 0xF7BE;
        uint16_t muted = blendColors(bg, TFT_WHITE, 145);
        uint16_t onAcc = uiTextOnAccentColor(pri);

        bool isActive = (bruceConfig.priColor == pri && bruceConfig.secColor == sec && bruceConfig.bgColor == bg);

        tft.fillScreen(bg);

        // Top Header Bar (edge-to-edge full-screen, with tappable Chapter 1 / Chapter 2 jump pill)
        int hy = 0;
        int hh = compact ? 18 : 24;
        int tx = 8;
#if defined(HAS_TOUCH)
        if (!compact) {
            int cy = hy + hh / 2;
            tft.drawLine(16, cy - 4, 11, cy, muted);
            tft.drawLine(15, cy - 4, 10, cy, muted);
            tft.drawLine(11, cy, 16, cy + 4, muted);
            tft.drawLine(10, cy, 15, cy + 4, muted);
            tx = 24;
        }
#endif
        drawNezhaCrest(tx + 4, hy + hh / 2, compact ? 4 : 5, pri, sec, p.crestStyle);
        tx += compact ? 12 : 14;
        const char *hdrKey = isChapter2 ? "Nezha 2 Edition" : "Nezha 1 Edition";
        uiDrawText16(bruce_i18n::tr(hdrKey), tx, hy + (hh - 16) / 2, tftWidth - tx - 92, bright, bg, 0);

        // Top-right tappable chapter toggle badge + counter
        int chPillW = compact ? 42 : 48;
        int chPillH = compact ? 14 : 18;
        int chPillX = tftWidth - chPillW - (compact ? 38 : 44);
        int chPillY = hy + (hh - chPillH) / 2;
        uint16_t chFill = isChapter2 ? sec : pri;
        uint16_t chFg = uiTextOnAccentColor(chFill);
        tft.fillRoundRect(chPillX, chPillY, chPillW, chPillH, 4, chFill);
        tft.setTextSize(FP);
        tft.setTextColor(chFg, chFill);
        String chTag = bruce_i18n::tr(isChapter2 ? "Nezha 2" : "Nezha 1");
        int chTh = (FP == 1 && bruce_i18n::containsNonAscii(chTag)) ? 12 : (LH * FP);
        tft.drawCentreString(
            bruce_i18n::utf8TruncateToWidth(chTag, chPillW - 4, FP),
            chPillX + chPillW / 2,
            chPillY + (chPillH - chTh) / 2,
            1
        );

        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%02d/%02d", i + 1, NEZHA_THEME_COUNT);
        tft.setTextSize(FP);
        tft.setTextColor(sec, bg);
        tft.drawRightString(cnt, tftWidth - 6, hy + (hh - LH * FP) / 2, 1);

        // Hun Tian Ling (哪吒1) / Sea-Flame (哪吒2) accent underline
        int lineY = hy + hh + 1;
        tft.drawFastHLine(6, lineY, tftWidth - 12, brd);
        tft.drawFastHLine(6, lineY, (tftWidth - 12) / 3, pri);
        tft.drawFastHLine(6 + (tftWidth - 12) / 3, lineY, 24, sec);
        if (isChapter2) {
            tft.drawFastHLine(tftWidth - 28, lineY, 22, sec);
        }

        // Bottom 3-Button Dock geometry
        int dockH = compact ? 22 : 28;
        int dockY = tftHeight - dockH - (compact ? 3 : 6);

        // Main Live Preview Card with 哪吒魔童 corner seal brackets
        int cardX = compact ? 4 : 8;
        int cardY = lineY + (compact ? 4 : 7);
        int cardW = tftWidth - cardX * 2;
        int cardH = dockY - cardY - (compact ? 4 : 7);
        tft.fillRoundRect(cardX, cardY, cardW, cardH, compact ? 6 : 8, surf);
        tft.drawRoundRect(cardX, cardY, cardW, cardH, compact ? 6 : 8, pri);
        drawNezhaCornerBrackets(cardX, cardY, cardW, cardH, pri, sec);

        // Left: Live 哪吒1 (魔童降世) or 哪吒2 (魔童闹海) Medallion rendered in this theme's palette
        int medW = compact ? 66 : 108;
        int medX = cardX + (compact ? 4 : 8);
        int medY = cardY + (compact ? 4 : 8);
        int medH = cardH - (compact ? 8 : 16);
        tft.fillRoundRect(medX, medY, medW, medH, 6, bg);
        tft.drawRoundRect(medX, medY, medW, medH, 6, brd);

        int mx = medX + medW / 2;
        int my = medY + medH / 2 + (compact ? 2 : (isChapter2 ? 2 : 4));
        int mr = compact ? 16 : 26;

        if (isChapter2) {
            // 哪吒2·魔童闹海: Blazing Flame Aura + Crashing Sea-Wave Base
            tft.drawCircle(mx, my, mr + (compact ? 3 : 7), pri);
            if (!compact) tft.drawCircle(mx, my, mr + 4, sec);
            // Blazing flame aura tongues behind hair
            int fTop = my - mr - (compact ? 4 : 8);
            tft.fillTriangle(mx, fTop, mx - (compact ? 6 : 10), my - (compact ? 6 : 10), mx + (compact ? 6 : 10), my - (compact ? 6 : 10), pri);
            tft.fillTriangle(mx - (compact ? 8 : 14), fTop + 4, mx - (compact ? 12 : 20), my - 2, mx - 3, my - (compact ? 6 : 10), sec);
            tft.fillTriangle(mx + (compact ? 8 : 14), fTop + 4, mx + (compact ? 12 : 20), my - 2, mx + 3, my - (compact ? 6 : 10), sec);
        } else {
            // 哪吒1·魔童降世: Sacred Qiankun Ring Halo + Sweeping Hun Tian Ling Ribbons
            tft.drawCircle(mx, my, mr + (compact ? 3 : 6), sec);
            if (!compact) tft.drawCircle(mx, my, mr + 5, blendColors(bg, sec, 180));
            tft.drawLine(mx - mr - (compact ? 6 : 11), my + (compact ? 6 : 12), mx - mr + 2, my - 2, pri);
            tft.drawLine(mx + mr + (compact ? 6 : 11), my + (compact ? 6 : 12), mx + mr - 2, my - 2, pri);
            tft.drawLine(mx - mr - (compact ? 5 : 10), my + (compact ? 8 : 14), mx - mr + 3, my, sec);
            tft.drawLine(mx + mr + (compact ? 5 : 10), my + (compact ? 8 : 14), mx + mr - 3, my, sec);
        }

        // Twin 哪吒魔童 Hair Buns + Silk Ties
        int bunR = compact ? 5 : 8;
        int bunDx = (mr * 3) / 4;
        int bunDy = (mr * 3) / 4;
        tft.fillCircle(mx - bunDx, my - bunDy, bunR + 1, pri);
        tft.fillCircle(mx + bunDx, my - bunDy, bunR + 1, pri);
        tft.fillCircle(mx - bunDx, my - bunDy, bunR, 0x18C4);
        tft.fillCircle(mx + bunDx, my - bunDy, bunR, 0x18C4);
        tft.fillRoundRect(mx - bunDx - 3, my - bunDy + bunR - 2, 8, 2, 1, isChapter2 ? sec : pri);
        tft.fillRoundRect(mx + bunDx - 5, my - bunDy + bunR - 2, 8, 2, 1, isChapter2 ? sec : pri);

        // Stylized 哪吒魔童 Face Silhouette + Layered Bangs + Forehead Crest
        int faceR = compact ? 11 : 17;
        tft.fillCircle(mx, my, faceR + 1, 0x18C4);
        tft.fillCircle(mx, my + 1, faceR - 1, 0xFED6);
        tft.fillRect(mx - faceR + 1, my - faceR, (faceR - 1) * 2, compact ? 5 : 7, 0x18C4);
        tft.fillTriangle(mx - faceR + 2, my - (compact ? 6 : 10), mx - (compact ? 5 : 8), my - (compact ? 1 : 2), mx - 2, my - (compact ? 6 : 10), 0x18C4);
        tft.fillTriangle(mx + faceR - 2, my - (compact ? 6 : 10), mx + (compact ? 5 : 8), my - (compact ? 1 : 2), mx + 2, my - (compact ? 6 : 10), 0x18C4);
        // Smoky eyes (哪吒2 adds glowing inner iris highlight)
        int ex = compact ? 4 : 6;
        int ey = my + (compact ? 2 : 3);
        tft.fillRoundRect(mx - ex - 3, ey - 2, 6, 4, 1, 0x18C4);
        tft.fillRoundRect(mx + ex - 3, ey - 2, 6, 4, 1, 0x18C4);
        tft.fillCircle(mx - ex, ey, compact ? 1 : 2, pri);
        tft.fillCircle(mx + ex, ey, compact ? 1 : 2, isChapter2 ? sec : pri);
        tft.drawPixel(mx - ex - 1, ey - 1, TFT_WHITE);
        tft.drawPixel(mx + ex - 1, ey - 1, TFT_WHITE);
        // Signature Forehead Crest (chapter-matched!)
        drawNezhaCrest(mx, my - (compact ? 4 : 6), compact ? 4 : 6, pri, sec, p.crestStyle);
        // Confident grin
        tft.drawLine(mx - (compact ? 3 : 4), my + (compact ? 7 : 10), mx + (compact ? 3 : 5), my + (compact ? 6 : 9), 0x18C4);

        if (isChapter2) {
            // Crashing Sea Waves along the base of the 哪吒2 medallion
            int wy = medY + medH - (compact ? 6 : 10);
            int step = compact ? 8 : 12;
            for (int wx = medX + 4; wx + step < medX + medW - 4; wx += step) {
                tft.drawLine(wx, wy + 3, wx + step / 2, wy - 2, sec);
                tft.drawLine(wx + step / 2, wy - 2, wx + step, wy + 3, pri);
            }
        }

        // Right: Live Theme Details & Swatches
        int rx = medX + medW + (compact ? 6 : 10);
        int rw = cardX + cardW - rx - (compact ? 5 : 8);
        int ry = medY + (compact ? 1 : 4);

        uiDrawText16(bruce_i18n::tr(p.name), rx, ry, rw, bright, surf, 0);
        ry += compact ? 15 : 21;

        uiDrawText16(bruce_i18n::tr(p.motto), rx, ry, rw, sec, surf, 0);
        ry += compact ? 16 : 24;

        // 3 Live Color Swatches (PRI / SEC / BG)
        int swGap = 4;
        int swW = (rw - swGap * 2) / 3;
        int swH = compact ? 14 : 20;
        uint16_t swCols[3] = {pri, sec, bg};
        const char *swLbls[3] = {"PRI", "SEC", "BG"};
        for (int s = 0; s < 3; s++) {
            int sx = rx + s * (swW + swGap);
            tft.fillRoundRect(sx, ry, swW, swH, 4, swCols[s]);
            tft.drawRoundRect(sx, ry, swW, swH, 4, brd);
            tft.setTextSize(FP);
            tft.setTextColor(uiTextOnAccentColor(swCols[s]), swCols[s]);
            tft.drawCentreString(swLbls[s], sx + swW / 2, ry + (swH - LH * FP) / 2, 1);
        }
        ry += swH + (compact ? 5 : 10);

        // Live UI Progress Preview (non-compact)
        if (!compact) {
            int barH = 7;
            tft.fillRoundRect(rx, ry, rw, barH, 3, bg);
            tft.fillRoundRect(rx + 1, ry + 1, (rw - 2) * 2 / 3, barH - 2, 2, pri);
            tft.fillRoundRect(rx + 1 + (rw - 2) * 2 / 3 - 4, ry + 1, 10, barH - 2, 2, sec);
            ry += barH + 10;
        }

        const char *badgeKey = justApplied ? "Applied!" : (isActive ? "Active" : "Tap Apply");
        int pillH = compact ? 16 : 22;
        uint16_t pillFill = (justApplied || isActive) ? pri : bg;
        uint16_t pillFg = (justApplied || isActive) ? onAcc : muted;
        tft.fillRoundRect(rx, ry, rw, pillH, 4, pillFill);
        tft.drawRoundRect(rx, ry, rw, pillH, 4, (justApplied || isActive) ? pri : brd);
        uiDrawText16(bruce_i18n::tr(badgeKey), rx + rw / 2, ry + (pillH - 16) / 2, rw - 4, pillFg, pillFill, 1);

        // Bottom 3-Button Tactile Dock: < Prev | Apply | Next >
        int margin = compact ? 4 : 8;
        int gap = compact ? 4 : 6;
        int btnW = (tftWidth - margin * 2 - gap * 2) / 3;
        const char *btnKeys[3] = {"< Prev", "Apply", "Next >"};
        for (int b = 0; b < 3; b++) {
            int bx = margin + b * (btnW + gap);
            bool primary = (b == 1);
            uint16_t bFill = primary ? pri : surf;
            uint16_t bFg = primary ? onAcc : bright;
            uint16_t bBrd = primary ? pri : brd;
            tft.fillRoundRect(bx, dockY, btnW, dockH, 5, bFill);
            tft.drawRoundRect(bx, dockY, btnW, dockH, 5, bBrd);
            uiDrawText16(bruce_i18n::tr(btnKeys[b]), bx + btnW / 2, dockY + (dockH - 16) / 2, btnW - 4, bFg, bFill, 1);
        }
    };

    bool dirty = true;
    bool justApplied = false;
    inputLock();
    SelPress = EscPress = PrevPress = NextPress = UpPress = DownPress = false;
#if defined(HAS_TOUCH)
    touchPoint.Clear();
#endif
    inputUnlock();

    while (true) {
        if (dirty) {
            drawGallery(idx, justApplied);
            dirty = false;
        }

        if (check(EscPress)) break;

        if (check(PrevPress) || check(UpPress)) {
            idx = (idx + NEZHA_THEME_COUNT - 1) % NEZHA_THEME_COUNT;
            justApplied = false;
            dirty = true;
        }
        if (check(NextPress) || check(DownPress)) {
            idx = (idx + 1) % NEZHA_THEME_COUNT;
            justApplied = false;
            dirty = true;
        }
        if (check(SelPress)) {
            applyPack(idx);
            justApplied = true;
            dirty = true;
        }

#if defined(HAS_TOUCH)
        {
            bool hasTouch = false;
            int tx = 0;
            int ty = 0;
            inputLock();
            if (touchPoint.pressed) {
                hasTouch = true;
                tx = touchPoint.x;
                ty = touchPoint.y;
                touchPoint.Clear();
            }
            inputUnlock();

            if (hasTouch) {
                if (!compact && tx < 64 && ty <= 34) {
                    break;
                }
                // Tap top-right Chapter pill to jump directly between 哪吒1 (0) and 哪吒2 (12)
                if (ty <= 34 && tx >= tftWidth - 104) {
                    idx = (NEZHA_THEME_PACKS[idx].chapter == 1) ? NEZHA_CHAPTER2_START : 0;
                    justApplied = false;
                    dirty = true;
                } else {
                    int dockH = compact ? 22 : 28;
                    int dockY = tftHeight - dockH - (compact ? 3 : 6);
                    if (ty >= dockY - 8) {
                        int third = tftWidth / 3;
                        if (tx < third) {
                            idx = (idx + NEZHA_THEME_COUNT - 1) % NEZHA_THEME_COUNT;
                            justApplied = false;
                            dirty = true;
                        } else if (tx < third * 2) {
                            applyPack(idx);
                            justApplied = true;
                            dirty = true;
                        } else {
                            idx = (idx + 1) % NEZHA_THEME_COUNT;
                            justApplied = false;
                            dirty = true;
                        }
                    } else if (ty > 34) {
                        // Tap preview card directly to Apply
                        applyPack(idx);
                        justApplied = true;
                        dirty = true;
                    }
                }
            }
        }
#endif
        delay(15);
    }

#if defined(HAS_TOUCH)
    touchZoneOutsideFooterEnabled = prevTouchZone;
    inputLock();
    touchPoint.Clear();
    inputUnlock();
#endif
    SelPress = EscPress = PrevPress = NextPress = UpPress = DownPress = false;
    tft.fillScreen(bruceConfig.bgColor);
    invalidateStatusBarCache();
#endif // defined(BRUCE_COMPACT_CJK_FONT)
}

/*
 *  Function: setMainMenuStyleMenu
 *  Choose how the main menu presents the modules
 */
void setMainMenuStyleMenu() {
    std::vector<Option> styleOptions = {
        {"Carousel",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_CAROUSEL); },
         bruceConfig.mainMenuStyle == MAIN_MENU_CAROUSEL},
        {"Grid",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_GRID); },
         bruceConfig.mainMenuStyle == MAIN_MENU_GRID    },
        {"List",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_LIST); },
         bruceConfig.mainMenuStyle == MAIN_MENU_LIST    },
        {"Cards",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_CARDS); },
         bruceConfig.mainMenuStyle == MAIN_MENU_CARDS   },
        {"Dock",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_DOCK); },
         bruceConfig.mainMenuStyle == MAIN_MENU_DOCK    },
        {"Split",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_SPLIT); },
         bruceConfig.mainMenuStyle == MAIN_MENU_SPLIT   },
        {"Compact Grid",
         []() { bruceConfig.setMainMenuStyle(MAIN_MENU_COMPACT); },
         bruceConfig.mainMenuStyle == MAIN_MENU_COMPACT },
        {"Back", []() {}                                },
    };
    int initialIdx = (bruceConfig.mainMenuStyle >= 0 && bruceConfig.mainMenuStyle < MAIN_MENU_STYLE_COUNT)
                         ? bruceConfig.mainMenuStyle
                         : 0;
    loopOptions(styleOptions, MENU_TYPE_SUBMENU, "Menu Layout", initialIdx);
}

/*
 *  Function: setLanguageMenu
 *  Choose firmware UI language (English default, Simplified Chinese)
 */
void setLanguageMenu() {
    std::vector<Option> langOptions = {
        {"简体中文", []() { bruceConfig.setLanguage("zh-CN"); }, bruceConfig.language == "zh-CN"},
        {"English",  []() { bruceConfig.setLanguage("en"); },    bruceConfig.language == "en"   },
        {"Back",     []() {}                                                                     },
    };
    int initialIdx = (bruceConfig.language == "en") ? 1 : 0;
    loopOptions(langOptions, MENU_TYPE_SUBMENU, "Firmware Language", initialIdx);
}

uint16_t alterOneColorChannel565(uint16_t color, int newR, int newG, int newB) {
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;

    if (newR != 256) r = newR & 0x1F;
    if (newG != 256) g = newG & 0x3F;
    if (newB != 256) b = newB & 0x1F;

    return (r << 11) | (g << 5) | b;
}

bool setCustomUIColorMenu() {
    while (1) {
        options = {
            {"Primary",    [=]() { setCustomUIColorChoiceMenu(1); }},
            {"Secondary",  [=]() { setCustomUIColorChoiceMenu(2); }},
            {"Background", [=]() { setCustomUIColorChoiceMenu(3); }},
            {"Save",       [=]() {}                                },
            {"Cancel",     [=]() {}                                }
        };

        int selectedOption = loopOptions(options);
        if (selectedOption == -1 || selectedOption == options.size() - 1) {
            return false;
        } else if (selectedOption == 3) {
            return true;
        }
    }
}

void setCustomUIColorChoiceMenu(int colorType) {
    while (1) {
        options = {
            {"Red Channel",   [=]() { setCustomUIColorSettingMenuR(colorType); }},
            {"Green Channel", [=]() { setCustomUIColorSettingMenuG(colorType); }},
            {"Blue Channel",  [=]() { setCustomUIColorSettingMenuB(colorType); }},
            {"Back",          [=]() {}                                          }
        };

        int selectedOption = loopOptions(options);
        if (selectedOption == -1 || selectedOption == options.size() - 1) return;
    }
}

void setCustomUIColorSettingMenuR(int colorType) {
    setCustomUIColorSettingMenu(colorType, 1, [](uint16_t baseColor, int i) {
        return alterOneColorChannel565(baseColor, i, 256, 256);
    });
}

void setCustomUIColorSettingMenuG(int colorType) {
    setCustomUIColorSettingMenu(colorType, 2, [](uint16_t baseColor, int i) {
        return alterOneColorChannel565(baseColor, 256, i, 256);
    });
}

void setCustomUIColorSettingMenuB(int colorType) {
    setCustomUIColorSettingMenu(colorType, 3, [](uint16_t baseColor, int i) {
        return alterOneColorChannel565(baseColor, 256, 256, i);
    });
}

constexpr const char *colorTypes[] = {
    "Background", // 0
    "Primary",    // 1
    "Secondary"   // 2
};

constexpr const char *rgbNames[] = {
    "Blue", // 0
    "Red",  // 1
    "Green" // 2
};

void setCustomUIColorSettingMenu(
    int colorType, int rgb, std::function<uint16_t(uint16_t, int)> colorGenerator
) {
    uint16_t color = (colorType == 1)   ? bruceConfig.priColor
                     : (colorType == 2) ? bruceConfig.secColor
                                        : bruceConfig.bgColor;

    options.clear();

    static auto hoverFunctionPriColor = [](void *pointer, bool shouldRender) -> bool {
        uint16_t colorToSet = *static_cast<uint16_t *>(pointer);
        // Serial.printf("Setting primary color to: %04X\n", colorToSet);
        bruceConfig.priColor = colorToSet;
        return false;
    };
    static auto hoverFunctionSecColor = [](void *pointer, bool shouldRender) -> bool {
        uint16_t colorToSet = *static_cast<uint16_t *>(pointer);
        // Serial.printf("Setting secondary color to: %04X\n", colorToSet);
        bruceConfig.secColor = colorToSet;
        return false;
    };

    static auto hoverFunctionBgColor = [](void *pointer, bool shouldRender) -> bool {
        uint16_t colorToSet = *static_cast<uint16_t *>(pointer);
        // Serial.printf("Setting bg color to: %04X\n", colorToSet);
        bruceConfig.bgColor = colorToSet;
        tft.fillScreen(bruceConfig.bgColor);
        return false;
    };

    static uint16_t colorStorage[32];
    int selectedIndex = 0;
    int i = 0;
    int index = 0;

    if (rgb == 1) {
        selectedIndex = (color >> 11) & 0x1F;
    } else if (rgb == 2) {
        selectedIndex = ((color >> 5) & 0x3F);
    } else {
        selectedIndex = color & 0x1F;
    }

    while (i <= (rgb == 2 ? 63 : 31)) {
        if (i == 0 || (rgb == 2 && (i + 1) % 2 == 0) || (rgb != 2)) {
            uint16_t updatedColor = colorGenerator(color, i);
            colorStorage[index] = updatedColor;

            options.emplace_back(
                String(i),
                [colorType, updatedColor]() {
                    if (colorType == 1) bruceConfig.priColor = updatedColor;
                    else if (colorType == 2) bruceConfig.secColor = updatedColor;
                    else bruceConfig.bgColor = updatedColor;
                },
                selectedIndex == i,
                (colorType == 1 ? hoverFunctionPriColor
                                : (colorType == 2 ? hoverFunctionSecColor : hoverFunctionBgColor)),
                &colorStorage[index]
            );
            ++index;
        }
        ++i;
    }

    addOptionToMainMenu();

    int selectedOption = loopOptions(
        options,
        MENU_TYPE_SUBMENU,
        (String(colorType == 1 ? "Primary" : (colorType == 2 ? "Secondary" : "Background")) + " - " +
         (rgb == 1 ? "Red" : (rgb == 2 ? "Green" : "Blue")))
            .c_str(),
        (rgb != 2) ? selectedIndex : (selectedIndex > 0 ? (selectedIndex + 1) / 2 : 0)
    );
    if (selectedOption == -1 || selectedOption == options.size() - 1) {
        if (colorType == 1) {
            bruceConfig.priColor = color;
        } else if (colorType == 2) {
            bruceConfig.secColor = color;
        } else {
            bruceConfig.bgColor = color;
        }
        return;
    }
}

/*
 *  Function: setSoundConfig - 01/2026 - Refactored "ConfigMenu" (this function manteined for
 * retrocompatibility)
 *  Enable or disable sound
 */
void setSoundConfig() {
    options = {
        {"Sound off", [=]() { bruceConfig.setSoundEnabled(0); }, bruceConfig.soundEnabled == 0},
        {"Sound on",  [=]() { bruceConfig.setSoundEnabled(1); }, bruceConfig.soundEnabled == 1},
    };
    loopOptions(options, bruceConfig.soundEnabled);
}

/*
 *  Function: setSoundVolume
 *  Set sound volume
 */
void setSoundVolume() {
    options = {
        {"10%",  [=]() { bruceConfig.setSoundVolume(10); },  bruceConfig.soundVolume == 10 },
        {"20%",  [=]() { bruceConfig.setSoundVolume(20); },  bruceConfig.soundVolume == 20 },
        {"30%",  [=]() { bruceConfig.setSoundVolume(30); },  bruceConfig.soundVolume == 30 },
        {"40%",  [=]() { bruceConfig.setSoundVolume(40); },  bruceConfig.soundVolume == 40 },
        {"50%",  [=]() { bruceConfig.setSoundVolume(50); },  bruceConfig.soundVolume == 50 },
        {"60%",  [=]() { bruceConfig.setSoundVolume(60); },  bruceConfig.soundVolume == 60 },
        {"70%",  [=]() { bruceConfig.setSoundVolume(70); },  bruceConfig.soundVolume == 70 },
        {"80%",  [=]() { bruceConfig.setSoundVolume(80); },  bruceConfig.soundVolume == 80 },
        {"90%",  [=]() { bruceConfig.setSoundVolume(90); },  bruceConfig.soundVolume == 90 },
        {"100%", [=]() { bruceConfig.setSoundVolume(100); }, bruceConfig.soundVolume == 100},
    };
    loopOptions(options, bruceConfig.soundVolume);
}

#ifdef HAS_RGB_LED
/*
 *  Function: setLedBlinkConfig - 01/2026 - Refactored "ConfigMenu" (this function manteined for
 * retrocompatibility)
 *  Enable or disable led blink
 */
void setLedBlinkConfig() {
    options = {
        {"Led Blink off", [=]() { bruceConfig.setLedBlinkEnabled(0); }, bruceConfig.ledBlinkEnabled == 0},
        {"Led Blink on",  [=]() { bruceConfig.setLedBlinkEnabled(1); }, bruceConfig.ledBlinkEnabled == 1},
    };
    loopOptions(options, bruceConfig.ledBlinkEnabled);
}
#endif

/*
 *  Function: setWifiStartupConfig
 *  Enable or disable wifi connection at startup
 */
void setWifiStartupConfig() {
    options = {
        {"Disable", [=]() { bruceConfig.setWifiAtStartup(0); }, bruceConfig.wifiAtStartup == 0},
        {"Enable",  [=]() { bruceConfig.setWifiAtStartup(1); }, bruceConfig.wifiAtStartup == 1},
    };
    loopOptions(options, bruceConfig.wifiAtStartup);
}

/*
 *  Function: addEvilWifiMenu
 *  Handles Menu to add evil wifi names into config list
 */
void addEvilWifiMenu() {
    String apName = keyboard("", 30, "Evil Portal SSID");
    if (apName != "\x1B") bruceConfig.addEvilWifiName(apName);
}

/*
 *  Function: removeEvilWifiMenu
 *  Handles Menu to remove evil wifi names from config list
 */
void removeEvilWifiMenu() {
    options = {};

    for (const auto &wifi_name : bruceConfig.evilWifiNames) {
        options.push_back({wifi_name.c_str(), [wifi_name]() { bruceConfig.removeEvilWifiName(wifi_name); }});
    }

    options.push_back({"Cancel", [=]() { backToMenu(); }});

    loopOptions(options);
}

/*
 *  Function: setEvilEndpointCreds
 *  Handles menu for changing the endpoint to access captured creds
 */
void setEvilEndpointCreds() {
    String userInput = keyboard(bruceConfig.evilPortalEndpoints.getCredsEndpoint, 30, "Evil creds endpoint");
    if (userInput != "\x1B") bruceConfig.setEvilEndpointCreds(userInput);
}

/*
 *  Function: setEvilEndpointSsid
 *  Handles menu for changing the endpoint to change evilSsid
 */
void setEvilEndpointSsid() {
    String userInput = keyboard(bruceConfig.evilPortalEndpoints.setSsidEndpoint, 30, "Evil creds endpoint");
    if (userInput != "\x1B") bruceConfig.setEvilEndpointSsid(userInput);
}

/*
 *  Function: setEvilAllowGetCredentials
 *  Handles menu for toggling access to the credential list endpoint
 */

void setEvilAllowGetCreds() {
    options = {
        {"Disallow",
         [=]() { bruceConfig.setEvilAllowGetCreds(false); },
         bruceConfig.evilPortalEndpoints.allowGetCreds == false},
        {"Allow",
         [=]() { bruceConfig.setEvilAllowGetCreds(true); },
         bruceConfig.evilPortalEndpoints.allowGetCreds == true },
    };
    loopOptions(options, bruceConfig.evilPortalEndpoints.allowGetCreds);
}

/*
 *  Function: setEvilAllowGetCredentials
 *  Handles menu for toggling access to the change SSID endpoint
 */

void setEvilAllowSetSsid() {
    options = {
        {"Disallow",
         [=]() { bruceConfig.setEvilAllowSetSsid(false); },
         bruceConfig.evilPortalEndpoints.allowSetSsid == false},
        {"Allow",
         [=]() { bruceConfig.setEvilAllowSetSsid(true); },
         bruceConfig.evilPortalEndpoints.allowSetSsid == true },
    };
    loopOptions(options, bruceConfig.evilPortalEndpoints.allowSetSsid);
}

/*
 *  Function: setEvilAllowEndpointDisplay
 *  Handles menu for toggling the display of the Evil Portal endpoints
 */

void setEvilAllowEndpointDisplay() {
    options = {
        {"Disallow",
         [=]() { bruceConfig.setEvilAllowEndpointDisplay(false); },
         bruceConfig.evilPortalEndpoints.showEndpoints == false},
        {"Allow",
         [=]() { bruceConfig.setEvilAllowEndpointDisplay(true); },
         bruceConfig.evilPortalEndpoints.showEndpoints == true },
    };
    loopOptions(options, bruceConfig.evilPortalEndpoints.showEndpoints);
}

/*
 * Function: setEvilPasswordMode
 * Handles menu for setting the evil portal password mode
 */
void setEvilPasswordMode() {
    options = {
        {"Save 'password'",
         [=]() { bruceConfig.setEvilPasswordMode(FULL_PASSWORD); },
         bruceConfig.evilPortalPasswordMode == FULL_PASSWORD  },
        {"Save 'p******d'",
         [=]() { bruceConfig.setEvilPasswordMode(FIRST_LAST_CHAR); },
         bruceConfig.evilPortalPasswordMode == FIRST_LAST_CHAR},
        {"Save '*hidden*'",
         [=]() { bruceConfig.setEvilPasswordMode(HIDE_PASSWORD); },
         bruceConfig.evilPortalPasswordMode == HIDE_PASSWORD  },
        {"Save length",
         [=]() { bruceConfig.setEvilPasswordMode(SAVE_LENGTH); },
         bruceConfig.evilPortalPasswordMode == SAVE_LENGTH    },
    };
    loopOptions(options, bruceConfig.evilPortalPasswordMode);
}

/*
 * Function: setEvilGatewayIp
 * Handles menu for setting the Evil Portal gateway IP
 */
void setEvilGatewayIp() {
    options = {
        {"172.0.0.1",
         [=]() { bruceConfig.setEvilGatewayIp("172.0.0.1"); },
         bruceConfig.evilPortalGatewayIp == "172.0.0.1"},
        {"192.168.4.1",
         [=]() { bruceConfig.setEvilGatewayIp("192.168.4.1"); },
         bruceConfig.evilPortalGatewayIp == "192.168.4.1"},
        {"Custom", [=]() {
             String ip = num_keyboard("", 15, "Gateway Addr");
             bruceConfig.setEvilGatewayIp(ip);
         }},
    };
    loopOptions(options, bruceConfig.evilPortalGatewayIp == "192.168.4.1" ? 1 : 0);
}

/*
 *  Function: setRFModuleMenu
 *  Handles Menu to set the RF module in use
 */
void setRFModuleMenu() {
    int result = 0;
    int idx = 0;
    int presetIdx = -1; // -1 = plain "CC1101" (board's default CC1101_bus wiring, set in _setup_gpio())
    if (bruceConfigPins.rfModule == M5_RF_MODULE) idx = 0;
    else if (bruceConfigPins.rfModule == CC1101_SPI_MODULE) {
        idx = 1;
        for (size_t i = 0; i < bruceConfigPins.CC1101_presets.size(); i++) {
            const BruceConfigPins::SPIPins &p = bruceConfigPins.CC1101_presets[i].pins;
            if (bruceConfigPins.CC1101_bus.sck == p.sck && bruceConfigPins.CC1101_bus.mosi == p.mosi &&
                bruceConfigPins.CC1101_bus.cs == p.cs) {
                idx = 2 + (int)i;
                break;
            }
        }
    }

    options = {
        {"M5 RF433T/R", [&]() { result = M5_RF_MODULE; }     },
        {"CC1101",      [&]() { result = CC1101_SPI_MODULE; }},
    };
    for (size_t i = 0; i < bruceConfigPins.CC1101_presets.size(); i++) {
        options.push_back({bruceConfigPins.CC1101_presets[i].label, [&, i]() {
                               result = CC1101_SPI_MODULE;
                               presetIdx = (int)i;
                           }});
    }
    loopOptions(options, idx);
    if (result == CC1101_SPI_MODULE) {
        if (presetIdx >= 0) {
            const BruceConfigPins::SPIPinPreset &preset = bruceConfigPins.CC1101_presets[presetIdx];
            bruceConfigPins.setCC1101Pins(preset.pins);
            bruceConfigPins.setNrf24Pins(preset.pins);
        }
        if (initRfModule()) {
            bruceConfigPins.setRfModule(CC1101_SPI_MODULE);
            deinitRfModule();
            return;
        }
        // else display an error
        displayError("CC1101 not found", true);
        if (presetIdx >= 0 && bruceConfigPins.CC1101_presets[presetIdx].wiringQrUrl)
            qrcode_display(bruceConfigPins.CC1101_presets[presetIdx].wiringQrUrl);
        while (!check(AnyKeyPress)) vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    // fallback to "M5 RF433T/R" on errors
    bruceConfigPins.setRfModule(M5_RF_MODULE);
}

/*
 *  Function: setRFFreqMenu
 *  Handles Menu to set the default frequency for the RF module
 */
void setRFFreqMenu() {
    float result = 433.92;
    String freq_str = num_keyboard(String(bruceConfigPins.rfFreq), 10, "Default frequency:");
    if (freq_str == "\x1B") return;
    if (freq_str.length() > 1) {
        result = freq_str.toFloat();          // returns 0 if not valid
        if (result >= 280 && result <= 928) { // TODO: check valid freq according to current module?
            bruceConfigPins.setRfFreq(result);
            return;
        }
    }
    // else
    displayError("Invalid frequency");
    bruceConfigPins.setRfFreq(433.92); // reset to default
    delay(1000);
}

/*
 *  Function: setRFIDModuleMenu
 *  Handles Menu to set the RFID module in use
 */
void setRFIDModuleMenu() {
    options = {
        {"M5 RFID2",
         [=]() { bruceConfigPins.setRfidModule(M5_RFID2_MODULE); },
         bruceConfigPins.rfidModule == M5_RFID2_MODULE                            },
#ifdef M5STICK
        {"PN532 I2C G33",
         [=]() { bruceConfigPins.setRfidModule(PN532_I2C_MODULE); },
         bruceConfigPins.rfidModule == PN532_I2C_MODULE                           },
        {"PN532 I2C G36",
         [=]() { bruceConfigPins.setRfidModule(PN532_I2C_SPI_MODULE); },
         bruceConfigPins.rfidModule == PN532_I2C_SPI_MODULE                       },
#else
        {"PN532 on I2C",
         [=]() { bruceConfigPins.setRfidModule(PN532_I2C_MODULE); },
         bruceConfigPins.rfidModule == PN532_I2C_MODULE},
#endif
        {"PN532 on SPI",
         [=]() { bruceConfigPins.setRfidModule(PN532_SPI_MODULE); },
         bruceConfigPins.rfidModule == PN532_SPI_MODULE                           },
        {"RC522 on SPI",
         [=]() { bruceConfigPins.setRfidModule(RC522_SPI_MODULE); },
         bruceConfigPins.rfidModule == RC522_SPI_MODULE                           },
#if !defined(LITE_VERSION)
        {"ST25R3916 SPI",
         [=]() { bruceConfigPins.setRfidModule(ST25R3916_SPI_MODULE); },
         bruceConfigPins.rfidModule == ST25R3916_SPI_MODULE                       },
        {"ST25R3916 I2C",
         [=]() { bruceConfigPins.setRfidModule(ST25R3916_I2C_MODULE); },
         bruceConfigPins.rfidModule == ST25R3916_I2C_MODULE                       },
#ifdef CAP_NFC_SS_PIN
        // M5Stack Cap CC1101: its NFC half is an ST25R3916 on the default SPI port.
        // https://docs.m5stack.com/en/cap/Cap_CC1101
        {"CC1101 M5 Cap",
         [=]() {
             bruceConfigPins.setSR25RPins(
                 {(gpio_num_t)CAP_NFC_SCK_PIN,
                  (gpio_num_t)CAP_NFC_MISO_PIN,
                  (gpio_num_t)CAP_NFC_MOSI_PIN,
                  (gpio_num_t)CAP_NFC_SS_PIN,
                  (gpio_num_t)CAP_NFC_IRQ_PIN,
                  GPIO_NUM_NC}
             );
             bruceConfigPins.setRfidModule(ST25R3916_SPI_MODULE);
         },                                                              bruceConfigPins.rfidModule == ST25R3916_SPI_MODULE &&
             bruceConfigPins.ST25R_bus.cs == (gpio_num_t)CAP_NFC_SS_PIN},
#endif
#endif
    };
    loopOptions(options, bruceConfigPins.rfidModule);
}

/*
 *  Function: addMifareKeyMenu
 *  Handles Menu to add MIFARE keys into config list
 */
void addMifareKeyMenu() {
    String key = keyboard("", 12, "MIFARE key");
    if (key != "\x1B") bruceConfig.addMifareKey(key);
}

/*
 *  Function: setClock
 *  Handles Menu to set timezone to NTP
 */
const char *ntpServer = "pool.ntp.org";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, 0, 0);

void setClock() {
#if defined(HAS_RTC)
    RTC_TimeTypeDef TimeStruct;
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
#endif

    options = {
        {"Via NTP Set Timezone",                                                 [&]() { bruceConfig.setAutomaticTimeUpdateViaNTP(true); } },
        {"Set Time Manually",                                                    [&]() { bruceConfig.setAutomaticTimeUpdateViaNTP(false); }},
        {("Daylight Savings " + String(bruceConfig.dst ? "On" : "Off")).c_str(),
         [&]() {
             bruceConfig.setDST(!bruceConfig.dst);
             updateClockTimezone();
             returnToMenu = true;
         }                                                                                                                                 },
        {(bruceConfig.clock24hr ? "24-Hour Format" : "12-Hour Format"),          [&]() {
             bruceConfig.setClock24Hr(!bruceConfig.clock24hr);
             returnToMenu = true;
         }                                                          }
    };

    addOptionToMainMenu();
    loopOptions(options);

    if (returnToMenu) return;

    if (bruceConfig.automaticTimeUpdateViaNTP) {
        if (!wifiConnected) wifiConnectMenu();

        options.clear();

#ifndef LITE_VERSION

        struct TimezoneMapping {
            const char *name;
            float offset;
        };

        constexpr TimezoneMapping timezoneMappings[] = {
            {"UTC-12 (Baker Island, Howland Island)",     -12  },
            {"UTC-11 (Niue, Pago Pago)",                  -11  },
            {"UTC-10 (Honolulu, Papeete)",                -10  },
            {"UTC-9 (Anchorage, Gambell)",                -9   },
            {"UTC-9.5 (Marquesas Islands)",               -9.5 },
            {"UTC-8 (Los Angeles, Vancouver, Tijuana)",   -8   },
            {"UTC-7 (Denver, Phoenix, Edmonton)",         -7   },
            {"UTC-6 (Mexico City, Chicago, Tegucigalpa)", -6   },
            {"UTC-5 (New York, Toronto, Lima)",           -5   },
            {"UTC-4 (Caracas, Santiago, La Paz)",         -4   },
            {"UTC-3 (Brasilia, Sao Paulo, Montevideo)",   -3   },
            {"UTC-2 (South Georgia, Mid-Atlantic)",       -2   },
            {"UTC-1 (Azores, Cape Verde)",                -1   },
            {"UTC+0 (London, Lisbon, Casablanca)",        0    },
            {"UTC+0.5 (Tehran)",                          0.5  },
            {"UTC+1 (Berlin, Paris, Rome)",               1    },
            {"UTC+2 (Cairo, Athens, Johannesburg)",       2    },
            {"UTC+3 (Moscow, Riyadh, Nairobi)",           3    },
            {"UTC+3.5 (Tehran)",                          3.5  },
            {"UTC+4 (Dubai, Baku, Muscat)",               4    },
            {"UTC+4.5 (Kabul)",                           4.5  },
            {"UTC+5 (Islamabad, Karachi, Tashkent)",      5    },
            {"UTC+5.5 (New Delhi, Mumbai, Colombo)",      5.5  },
            {"UTC+5.75 (Kathmandu)",                      5.75 },
            {"UTC+6 (Dhaka, Almaty, Omsk)",               6    },
            {"UTC+6.5 (Yangon, Cocos Islands)",           6.5  },
            {"UTC+7 (Bangkok, Jakarta, Hanoi)",           7    },
            {"UTC+8 (Beijing, Singapore, Perth)",         8    },
            {"UTC+8.75 (Eucla)",                          8.75 },
            {"UTC+9 (Tokyo, Seoul, Pyongyang)",           9    },
            {"UTC+9.5 (Adelaide, Darwin)",                9.5  },
            {"UTC+10 (Sydney, Melbourne, Vladivostok)",   10   },
            {"UTC+10.5 (Lord Howe Island)",               10.5 },
            {"UTC+11 (Solomon Islands, Nouméa)",          11   },
            {"UTC+12 (Auckland, Fiji, Kamchatka)",        12   },
            {"UTC+12.75 (Chatham Islands)",               12.75},
            {"UTC+13 (Tonga, Phoenix Islands)",           13   },
            {"UTC+14 (Kiritimati)",                       14   }
        };

        int idx = 0;
        int i = 0;
        for (const auto &mapping : timezoneMappings) {
            if (bruceConfig.tmz == mapping.offset) { idx = i; }

            options.emplace_back(
                mapping.name, [=, &mapping]() { bruceConfig.setTmz(mapping.offset); }, idx == i
            );
            ++i;
        }

#else
        constexpr float timezoneOffsets[] = {-12, -11, -10,  -9.5, -9,  -8,    -7, -6, -5,   -4,
                                             -3,  -2,  -1,   0,    0.5, 1,     2,  3,  3.5,  4,
                                             4.5, 5,   5.5,  5.75, 6,   6.5,   7,  8,  8.75, 9,
                                             9.5, 10,  10.5, 11,   12,  12.75, 13, 14};

        int idx = 0;
        int i = 0;
        for (const auto &offset : timezoneOffsets) {
            if (bruceConfig.tmz == offset) idx = i;

            options.emplace_back(
                ("UTC" + String(offset >= 0 ? "+" : "") + String(offset)).c_str(),
                [=]() { bruceConfig.setTmz(offset); },
                bruceConfig.tmz == offset
            );
            ++i;
        }

#endif

        addOptionToMainMenu();

        loopOptions(options, idx);

        updateClockTimezone();

    } else {
        int hr, mn, am = 0; // Initialize am to default value
        options = {};
        for (int i = 0; i < 12; i++) {
            String tmp = String(i < 10 ? "0" : "") + String(i);
            options.push_back({tmp.c_str(), [&]() { delay(1); }});
        }

        hr = loopOptions(options, MENU_TYPE_SUBMENU, "Set Hour");
        options.clear();

        for (int i = 0; i < 60; i++) {
            String tmp = String(i < 10 ? "0" : "") + String(i);
            options.push_back({tmp.c_str(), [&]() { delay(1); }});
        }

        mn = loopOptions(options, MENU_TYPE_SUBMENU, "Set Minute");
        options.clear();

        options = {
            {"AM", [&]() { am = 0; } },
            {"PM", [&]() { am = 12; }},
        };

        loopOptions(options);

#if defined(HAS_RTC)
        TimeStruct.Hours = hr + am;
        TimeStruct.Minutes = mn;
        TimeStruct.Seconds = 0;
        _rtc.SetTime(&TimeStruct);
        _rtc.GetTime(&_time);
        _rtc.GetDate(&_date);

        struct tm timeinfo = {};
        timeinfo.tm_sec = _time.Seconds;
        timeinfo.tm_min = _time.Minutes;
        timeinfo.tm_hour = _time.Hours;
        timeinfo.tm_mday = _date.Date;
        timeinfo.tm_mon = _date.Month > 0 ? _date.Month - 1 : 0;
        timeinfo.tm_year = _date.Year >= 1900 ? _date.Year - 1900 : 0;
        time_t epoch = mktime(&timeinfo);
        struct timeval tv = {.tv_sec = epoch};
        settimeofday(&tv, nullptr);
#else
        rtc.setTime(0, mn, hr + am, 20, 06, CURRENT_YEAR); // send me a gift, @Pirata!
        struct tm t = rtc.getTimeStruct();
        time_t epoch = mktime(&t);
        struct timeval tv = {.tv_sec = epoch};
        settimeofday(&tv, nullptr);
#endif
        clock_set = true;
    }
}

void runClockLoop(bool showMenuHint) {
    unsigned long lastTick = 0;
    unsigned long hintStartTime = millis();
    bool hintVisible = showMenuHint;
    bool firstDraw = true;

#if defined(HAS_RTC)
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
    _rtc.GetTime(&_time);
#endif

    bool compact = (tftHeight < 160);
    int cardX = compact ? 10 : 18;
    int cardY = STATUS_BAR_HEIGHT + (compact ? 4 : 12);
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 8 : 16);
    uint16_t surf = uiSurfaceColor();

    drawMainBorder(true);
    uiDrawCard(cardX, cardY, cardW, cardH, true);
    delay(200);

    for (;;) {
        unsigned long now = millis();
        if (firstDraw || now - lastTick >= 1000) {
            firstDraw = false;
            lastTick = now;
#if defined(HAS_RTC)
            struct tm nowTm = _rtc.getTimeStruct();
#else
            struct tm nowTm = rtc.getTimeStruct();
#endif
            updateTimeStr(nowTm);
            drawStatusBar();

            uint8_t f_size = 4;
            for (uint8_t i = 4; i > 0; i--) {
                if (i * LW * (int)strlen(timeStr) < (cardW - 24)) {
                    f_size = i;
                    break;
                }
            }
            int timeH = f_size * LH;
            int timeY = cardY + (cardH - timeH) / 2 - (compact ? 4 : 8);

            // Clear time band inside card without flickering the card border
            tft.fillRect(cardX + 6, timeY - 2, cardW - 12, timeH + 4, surf);
            tft.setTextDatum(TL_DATUM);
            tft.setTextSize(f_size);
            tft.setTextColor(uiTextBrightColor(), surf);
            tft.drawCentreString(timeStr, tftWidth / 2, timeY, 1);

            // Date & 12h/24h indicator row
            char dateBuf[28];
            if (nowTm.tm_year + 1900 >= 2020) {
                snprintf(
                    dateBuf,
                    sizeof(dateBuf),
                    "%04d-%02d-%02d  %s",
                    nowTm.tm_year + 1900,
                    nowTm.tm_mon + 1,
                    nowTm.tm_mday,
                    bruceConfig.clock24hr ? "24H" : "12H"
                );
            } else {
                snprintf(dateBuf, sizeof(dateBuf), "%s", bruceConfig.clock24hr ? "24H" : "12H");
            }
            int dateY = timeY + timeH + (compact ? 6 : 12);
            tft.fillRect(cardX + 8, dateY, cardW - 16, LH + 4, surf);
            tft.setTextSize(FP);
            tft.setTextColor(bruceConfig.priColor, surf);
            tft.drawCentreString(dateBuf, tftWidth / 2, dateY, 1);

            // "OK to show menu" hint management
            int hintY = cardY + cardH - (compact ? 14 : 18);
            if (hintVisible && (now - hintStartTime < 5000)) {
                tft.setTextSize(FP);
                tft.setTextColor(uiTextMutedColor(), surf);
                tft.drawCentreString(bruce_i18n::tr("OK to show menu"), tftWidth / 2, hintY, 1);
            } else if (hintVisible && (now - hintStartTime >= 5000)) {
                tft.fillRect(cardX + 8, hintY - 1, cardW - 16, LH + 6, surf);
                hintVisible = false;
            }
        }

        if (check(SelPress)) {
            tft.fillScreen(bruceConfig.bgColor);
            if (showMenuHint) {
                break;
            } else {
                returnToMenu = true;
                break;
            }
        }

        if (check(EscPress)) {
            tft.fillScreen(bruceConfig.bgColor);
            returnToMenu = true;
            break;
        }

        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
}

/*
 *  Function: gsetIrTxPin
 *  get or set IR Tx Pin
 */
int gsetIrTxPin(bool set) {
    int result = bruceConfigPins.irTx;

    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = IR_TX_PINS;
        int idx = 100;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == bruceConfigPins.irTx && idx == 100) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != bruceConfigPins.SDCARD_bus.cs && i != bruceConfigPins.SDCARD_bus.mosi &&
                i != bruceConfigPins.SDCARD_bus.miso)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { bruceConfigPins.setIrTxPin(pin.second); },
                     pin.second == bruceConfigPins.irTx}
                );
        }

        loopOptions(options, idx);
        options.clear();

        Serial.println("Saved pin: " + String(bruceConfigPins.irTx));
    }

    returnToMenu = true;
    return bruceConfigPins.irTx;
}

void setIrTxRepeats() {
    uint8_t chRpts = 0; // Chosen Repeats

    options = {
        {"None",             [&]() { chRpts = 0; } },
        {"5  (+ 1 initial)", [&]() { chRpts = 5; } },
        {"10 (+ 1 initial)", [&]() { chRpts = 10; }},
        {"Custom",           [&]() {
             // up to 99 repeats
             String rpt =
                 num_keyboard(String(bruceConfigPins.irTxRepeats), 2, "Nbr of Repeats (+ 1 initial)");
             chRpts = static_cast<uint8_t>(rpt.toInt());
         }                       },
    };
    addOptionToMainMenu();

    loopOptions(options);

    if (returnToMenu) return;

    bruceConfigPins.setIrTxRepeats(chRpts);
}
/*
 *  Function: gsetIrRxPin
 *  get or set IR Rx Pin
 */
int gsetIrRxPin(bool set) {
    int result = bruceConfigPins.irRx;

    if (result < 0) bruceConfigPins.setIrRxPin(GROVE_SCL);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = IR_RX_PINS;
        int idx = -1;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == bruceConfigPins.irRx && idx < 0) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != bruceConfigPins.SDCARD_bus.cs && i != bruceConfigPins.SDCARD_bus.mosi &&
                i != bruceConfigPins.SDCARD_bus.miso)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { bruceConfigPins.setIrRxPin(pin.second); },
                     pin.second == bruceConfigPins.irRx}
                );
        }

        loopOptions(options);
    }

    returnToMenu = true;
    return bruceConfigPins.irRx;
}

/*
 *  Function: gsetRfTxPin
 *  get or set RF Tx Pin
 */
int gsetRfTxPin(bool set) {
    int result = bruceConfigPins.rfTx;

    if (result < 0) bruceConfigPins.setRfTxPin(bruceConfigPins.i2c_bus.sda);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = RF_TX_PINS;
        int idx = -1;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == bruceConfigPins.rfTx && idx < 0) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != bruceConfigPins.SDCARD_bus.cs && i != bruceConfigPins.SDCARD_bus.mosi &&
                i != bruceConfigPins.SDCARD_bus.miso)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { bruceConfigPins.setRfTxPin(pin.second); },
                     pin.second == bruceConfigPins.rfTx}
                );
        }

        loopOptions(options);
        options.clear();
    }

    returnToMenu = true;
    return bruceConfigPins.rfTx;
}

/*
 *  Function: gsetRfRxPin
 *  get or set FR Rx Pin
 */
int gsetRfRxPin(bool set) {
    int result = bruceConfigPins.rfRx;

    if (result < 0) bruceConfigPins.setRfRxPin(GROVE_SCL);
    if (set) {
        options.clear();
        std::vector<std::pair<const char *, int>> pins;
        pins = RF_RX_PINS;
        int idx = -1;
        int j = 0;
        for (auto pin : pins) {
            if (pin.second == bruceConfigPins.rfRx && idx < 0) idx = j;
            j++;
#ifdef ALLOW_ALL_GPIO_FOR_IR_RF
            int i = pin.second;
            if (i != TFT_CS && i != TFT_RST && i != TFT_SCLK && i != TFT_MOSI && i != TFT_BL &&
                i != TOUCH_CS && i != bruceConfigPins.SDCARD_bus.cs && i != bruceConfigPins.SDCARD_bus.mosi &&
                i != bruceConfigPins.SDCARD_bus.miso)
#endif
                options.push_back(
                    {pin.first,
                     [=]() { bruceConfigPins.setRfRxPin(pin.second); },
                     pin.second == bruceConfigPins.rfRx}
                );
        }

        loopOptions(options);
        options.clear();
    }

    returnToMenu = true;
    return bruceConfigPins.rfRx;
}

/*
 *  Function: setStartupApp
 *  Handles Menu to set startup app
 */
void setStartupApp() {
    int idx = 0;
    if (bruceConfig.startupApp == "") idx = 0;

    options = {
        {"None", [=]() { bruceConfig.setStartupApp(""); }, bruceConfig.startupApp == ""}
    };

    int index = 0;
    for (String appName : startupApp.getAppNames()) {
        index++;
        if (bruceConfig.startupApp == appName) idx = index;

        options.push_back({appName.c_str(), [=]() {
                               bruceConfig.setStartupApp(appName);
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
                               if (appName == "JS Interpreter") {
                                   options = getScriptsOptionsList("", true);
                                   loopOptions(options, MENU_TYPE_SUBMENU, "Startup Script");
                               }
#endif
                           }});
    }

    loopOptions(options, idx);
    options.clear();
}

/*
 *  Function: setGpsBaudrateMenu
 *  Handles Menu to set the baudrate for the GPS module
 */
void setGpsBaudrateMenu() {
    options = {
        {"9600 bps",   [=]() { bruceConfigPins.setGpsBaudrate(9600); },  bruceConfigPins.gpsBaudrate == 9600 },
        {"19200 bps",  [=]() { bruceConfigPins.setGpsBaudrate(19200); }, bruceConfigPins.gpsBaudrate == 19200},
        {"38400 bps",  [=]() { bruceConfigPins.setGpsBaudrate(38400); }, bruceConfigPins.gpsBaudrate == 38400},
        {"57600 bps",  [=]() { bruceConfigPins.setGpsBaudrate(57600); }, bruceConfigPins.gpsBaudrate == 57600},
        {"115200 bps",
         [=]() { bruceConfigPins.setGpsBaudrate(115200); },
         bruceConfigPins.gpsBaudrate == 115200                                                               },
    };

    loopOptions(options, bruceConfigPins.gpsBaudrate);
}

/*
 *  Function: setWifiApSsidMenu
 *  Handles Menu to set the WiFi AP SSID
 */
void setWifiApSsidMenu() {
    const bool isDefault = bruceConfig.wifiAp.ssid == "NezhaNet";

    options = {
        {"Default (NezhaNet)",
         [=]() { bruceConfig.setWifiApCreds("NezhaNet", bruceConfig.wifiAp.pwd); },
         isDefault                                                                            },
        {"Custom",
         [=]() {
             String newSsid = keyboard(bruceConfig.wifiAp.ssid, 32, "WiFi AP SSID:");
             if (newSsid != "\x1B") {
                 if (!newSsid.isEmpty()) bruceConfig.setWifiApCreds(newSsid, bruceConfig.wifiAp.pwd);
                 else displayError("SSID cannot be empty", true);
             }
         },                                                                         !isDefault},
    };
    addOptionToMainMenu();

    loopOptions(options, isDefault ? 0 : 1);
}

/*
 *  Function: setWifiApPasswordMenu
 *  Handles Menu to set the WiFi AP Password
 */
void setWifiApPasswordMenu() {
    const bool isDefault = bruceConfig.wifiAp.pwd == "nezhanet";

    options = {
        {"Default (nezhanet)",
         [=]() { bruceConfig.setWifiApCreds(bruceConfig.wifiAp.ssid, "nezhanet"); },
         isDefault                                                                             },
        {"Custom",
         [=]() {
             String newPassword = keyboard(bruceConfig.wifiAp.pwd, 32, "WiFi AP Password:", true);
             if (newPassword != "\x1B") {
                 if (!newPassword.isEmpty()) bruceConfig.setWifiApCreds(bruceConfig.wifiAp.ssid, newPassword);
                 else displayError("Password cannot be empty", true);
             }
         },                                                                          !isDefault},
    };
    addOptionToMainMenu();

    loopOptions(options, isDefault ? 0 : 1);
}

/*
 *  Function: setWifiApCredsMenu
 *  Handles Menu to configure WiFi AP Credentials
 */
void setWifiApCredsMenu() {
    options = {
        {"SSID",     setWifiApSsidMenu    },
        {"Password", setWifiApPasswordMenu},
    };
    addOptionToMainMenu();

    loopOptions(options);
}

/*
 *  Function: setNetworkCredsMenu
 *  Main Menu for setting Network credentials (BLE & WiFi)
 */
void setNetworkCredsMenu() {
    options = {
        {"WiFi AP Creds", setWifiApCredsMenu}
    };
    addOptionToMainMenu();

    loopOptions(options);
}

/*
 *  Function: setBadUSBBLEMenu
 *  Main Menu for setting Bad USB/BLE options
 */
void setBadUSBBLEMenu() {
    options = {
        {"Keyboard Layout", setBadUSBBLEKeyboardLayoutMenu},
        {"Key Delay",       setBadUSBBLEKeyDelayMenu      },
        {"Show Output",     setBadUSBBLEShowOutputMenu    },
    };
    addOptionToMainMenu();

    loopOptions(options);
}

/*
 *  Function: setBadUSBBLEKeyboardLayoutMenu
 *  Main Menu for setting Bad USB/BLE Keyboard Layout
 */
void setBadUSBBLEKeyboardLayoutMenu() {
    uint8_t opt = bruceConfig.badUSBBLEKeyboardLayout;

    options.clear();
    options = {
        {"US International",      [&]() { opt = 0; } },
        {"Danish",                [&]() { opt = 1; } },
        {"English (UK)",          [&]() { opt = 2; } },
        {"French (AZERTY)",       [&]() { opt = 3; } },
        {"German",                [&]() { opt = 4; } },
        {"Hungarian",             [&]() { opt = 5; } },
        {"Italian",               [&]() { opt = 6; } },
        {"Polish",                [&]() { opt = 7; } },
        {"Portuguese (Brazil)",   [&]() { opt = 8; } },
        {"Portuguese (Portugal)", [&]() { opt = 9; } },
        {"Slovenian",             [&]() { opt = 10; }},
        {"Spanish",               [&]() { opt = 11; }},
        {"Swedish",               [&]() { opt = 12; }},
        {"Turkish",               [&]() { opt = 13; }},
    };
    addOptionToMainMenu();

    loopOptions(options, opt);

    if (opt != bruceConfig.badUSBBLEKeyboardLayout) { bruceConfig.setBadUSBBLEKeyboardLayout(opt); }
}

/*
 *  Function: setBadUSBBLEKeyDelayMenu
 *  Main Menu for setting Bad USB/BLE Keyboard Key Delay
 */
void setBadUSBBLEKeyDelayMenu() {
    String delayStr = num_keyboard(String(bruceConfig.badUSBBLEKeyDelay), 3, "Key Delay (ms):");
    if (delayStr != "\x1B") {
        uint16_t delayVal = static_cast<uint16_t>(delayStr.toInt());
        if (delayVal <= 500) {
            bruceConfig.setBadUSBBLEKeyDelay(delayVal);
        } else if (delayVal != 0) {
            displayError("Invalid key delay value (0 to 500)", true);
        }
    }
}

/*
 *  Function: setBadUSBBLEShowOutputMenu
 *  Main Menu for setting Bad USB/BLE Show Output
 */
void setBadUSBBLEShowOutputMenu() {
    options.clear();
    options = {
        {"Enable",  [&]() { bruceConfig.setBadUSBBLEShowOutput(true); } },
        {"Disable", [&]() { bruceConfig.setBadUSBBLEShowOutput(false); }},
    };
    addOptionToMainMenu();

    loopOptions(options, bruceConfig.badUSBBLEShowOutput ? 0 : 1);
}

/*
 *  Function: setMacAddressMenu - @IncursioHack
 *  Handles Menu to configure WiFi MAC Address
 */
void setMacAddressMenu() {
    String currentMAC = bruceConfig.wifiMAC;
    if (currentMAC == "") currentMAC = WiFi.macAddress();

    options.clear();
    options = {
        {"Default MAC (" + WiFi.macAddress() + ")",
         [&]() { bruceConfig.setWifiMAC(""); },
         bruceConfig.wifiMAC == ""},
        {"Set Custom MAC",
         [&]() {
             String newMAC = keyboard(bruceConfig.wifiMAC, 17, "XX:YY:ZZ:AA:BB:CC");
             if (newMAC == "\x1B") return;
             if (newMAC.length() == 17) {
                 bruceConfig.setWifiMAC(newMAC);
             } else {
                 displayError("Invalid MAC format");
             }
         }, bruceConfig.wifiMAC != ""},
        {"Random MAC", [&]() {
             uint8_t randomMac[6];
             for (int i = 0; i < 6; i++) randomMac[i] = random(0x00, 0xFF);
             char buf[18];
             snprintf(
                 buf,
                 sizeof(buf),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 randomMac[0],
                 randomMac[1],
                 randomMac[2],
                 randomMac[3],
                 randomMac[4],
                 randomMac[5]
             );
             bruceConfig.setWifiMAC(String(buf));
         }}
    };

    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_REGULAR, ("Current: " + currentMAC).c_str());
}

/*
 *  Function: setSPIPins
 *  Main Menu to manually set SPI Pins
 */
void setSPIPinsMenu(BruceConfigPins::SPIPins &value) {
    uint8_t opt = 0;
    bool changed = false;
    BruceConfigPins::SPIPins points = value;

RELOAD:
    options = {
        {String("SCK =" + String(points.sck)).c_str(), [&]() { opt = 1; }},
        {String("MISO=" + String(points.miso)).c_str(), [&]() { opt = 2; }},
        {String("MOSI=" + String(points.mosi)).c_str(), [&]() { opt = 3; }},
        {String("CS  =" + String(points.cs)).c_str(), [&]() { opt = 4; }},
        {String("CE/GDO0=" + String(points.io0)).c_str(), [&]() { opt = 5; }},
        {String("NC/GDO2=" + String(points.io2)).c_str(), [&]() { opt = 6; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options);
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            bruceConfigPins.setSpiPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.sck + 1;
        else if (opt == 2) index = points.miso + 1;
        else if (opt == 3) index = points.mosi + 1;
        else if (opt == 4) index = points.cs + 1;
        else if (opt == 5) index = points.io0 + 1;
        else if (opt == 6) index = points.io2 + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, index);
        options.clear();
        if (opt == 1) points.sck = sel;
        else if (opt == 2) points.miso = sel;
        else if (opt == 3) points.mosi = sel;
        else if (opt == 4) points.cs = sel;
        else if (opt == 5) points.io0 = sel;
        else if (opt == 6) points.io2 = sel;
        changed = true;
        goto RELOAD;
    }
}

/*
 *  Function: setUARTPins
 *  Main Menu to manually set SPI Pins
 */
void setUARTPinsMenu(BruceConfigPins::UARTPins &value) {
    uint8_t opt = 0;
    bool changed = false;
    BruceConfigPins::UARTPins points = value;

RELOAD:
    options = {
        {String("RX = " + String(points.rx)).c_str(), [&]() { opt = 1; }},
        {String("TX = " + String(points.tx)).c_str(), [&]() { opt = 2; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options);
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            bruceConfigPins.setUARTPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.rx + 1;
        else if (opt == 2) index = points.tx + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, index);
        options.clear();
        if (opt == 1) points.rx = sel;
        else if (opt == 2) points.tx = sel;
        changed = true;
        goto RELOAD;
    }
}

/*
 *  Function: setI2CPins
 *  Main Menu to manually set SPI Pins
 */
void setI2CPinsMenu(BruceConfigPins::I2CPins &value) {
#if defined(SOC_HP_I2C_NUM) && SOC_HP_I2C_NUM < 2
    // On SoCs with a single HP I2C peripheral, Grove I2C shares hardware with the system I2C bus
    // (touch/RTC/PMIC), so its pins can't be changed independently when the board has a real,
    // distinct system I2C bus.
    if (bruceConfigPins.sys_i2c.sda >= 0 && bruceConfigPins.sys_i2c.scl >= 0) {
        displayError("I2C Pins cannot be changed on this board", true);
        return;
    }
#endif
    uint8_t opt = 0;
    bool changed = false;
    BruceConfigPins::I2CPins points = value;

RELOAD:
    options = {
        {String("SDA = " + String(points.sda)).c_str(), [&]() { opt = 1; }},
        {String("SCL = " + String(points.scl)).c_str(), [&]() { opt = 2; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options);
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            bruceConfigPins.setI2CPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.sda + 1;
        else if (opt == 2) index = points.scl + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, index);
        options.clear();
        if (opt == 1) points.sda = sel;
        else if (opt == 2) points.scl = sel;
        changed = true;
        goto RELOAD;
    }
}

/*
 *  Function: setSpeakerPinsMenu
 *  Menu to manually set the I2S speaker pins (was the BCLK/WCLK/DOUT/MCLK -D macros)
 */
void setSpeakerPinsMenu(BruceConfigPins::SpeakerPins &value) {
    uint8_t opt = 0;
    bool changed = false;
    BruceConfigPins::SpeakerPins points = value;

RELOAD:
    options = {
        {String("BCLK=" + String(points.bclk)).c_str(), [&]() { opt = 1; }},
        {String("WCLK=" + String(points.ws)).c_str(), [&]() { opt = 2; }},
        {String("DOUT=" + String(points.dout)).c_str(), [&]() { opt = 3; }},
        {String("MCLK=" + String(points.mclk)).c_str(), [&]() { opt = 4; }},
        {"Save Config", [&]() { opt = 7; }, changed},
        {"Main Menu", [&]() { opt = 0; }},
    };

    loopOptions(options);
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            bruceConfigPins.setSpeakerPins(value);
        }
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 1) index = points.bclk + 1;
        else if (opt == 2) index = points.ws + 1;
        else if (opt == 3) index = points.dout + 1;
        else if (opt == 4) index = points.mclk + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, index);
        options.clear();
        if (opt == 1) points.bclk = sel;
        else if (opt == 2) points.ws = sel;
        else if (opt == 3) points.dout = sel;
        else if (opt == 4) points.mclk = sel;
        changed = true;
        goto RELOAD;
    }
}

/*
 *  Function: setMicPinsMenu
 *  Menu to manually set the microphone wiring (was PIN_CLK/PIN_DATA/PIN_BCLK/PIN_WS)
 */
void setMicPinsMenu(BruceConfigPins::MicPins &value) {
    uint8_t opt = 0;
    bool changed = false;
    BruceConfigPins::MicPins points = value;

RELOAD:
    {
        const char *typeName = points.type == MIC_TYPE_I2S_PHILIPS ? "I2S Philips"
                               : points.type == MIC_TYPE_I2S_MSB   ? "I2S MSB"
                                                                   : "PDM";
        options = {
            {String("Type=" + String(typeName)).c_str(), [&]() { opt = 1; }},
            {String("CLK/BCLK=" + String(points.clk)).c_str(), [&]() { opt = 2; }},
            {String("WS  =" + String(points.ws)).c_str(), [&]() { opt = 3; }},
            {String("DATA=" + String(points.data)).c_str(), [&]() { opt = 4; }},
            {"Save Config", [&]() { opt = 7; }, changed},
            {"Main Menu", [&]() { opt = 0; }},
        };
    }

    loopOptions(options);
    if (opt == 0) return;
    else if (opt == 7) {
        if (changed) {
            value = points;
            bruceConfigPins.setMicPins(value);
        }
    } else if (opt == 1) {
        options = {
            {"PDM",         [&]() { points.type = MIC_TYPE_PDM; }        },
            {"I2S MSB",     [&]() { points.type = MIC_TYPE_I2S_MSB; }    },
            {"I2S Philips", [&]() { points.type = MIC_TYPE_I2S_PHILIPS; }},
        };
        loopOptions(options, points.type);
        options.clear();
        changed = true;
        goto RELOAD;
    } else {
        options = {};
        gpio_num_t sel = GPIO_NUM_NC;
        int index = 0;
        if (opt == 2) index = points.clk + 1;
        else if (opt == 3) index = points.ws + 1;
        else if (opt == 4) index = points.data + 1;
        for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
            String tmp = String(i);
            options.push_back({tmp.c_str(), [i, &sel]() { sel = (gpio_num_t)i; }});
        }
        loopOptions(options, index);
        options.clear();
        if (opt == 2) points.clk = sel;
        else if (opt == 3) points.ws = sel;
        else if (opt == 4) points.data = sel;
        changed = true;
        goto RELOAD;
    }
}

/*
 *  Function: setBuzzerPinMenu
 *  Menu to manually set the buzzer pin (was the BUZZ_PIN -D macro).
 *  -1 disables the buzzer.
 */
void setBuzzerPinMenu() {
    options = {};
    int sel = bruceConfigPins.buzzer;
    for (int8_t i = -1; i <= GPIO_NUM_MAX; i++) {
        String tmp = String(i);
        options.push_back({tmp.c_str(), [i, &sel]() { sel = i; }});
    }
    loopOptions(options, bruceConfigPins.buzzer + 1);
    options.clear();
    bruceConfigPins.setBuzzerPin(sel);
}

/*
 *  Function: setTheme
 *  Menu to change Theme
 */
void setTheme() {
    FS *fs = nullptr;
    options = {
        {"Nezha Themes",
         [&]() {
             setNezhaThemeGallery();
             fs = nullptr;
         }                                     },
        {"Little FS", [&]() { fs = &LittleFS; }},
        {"Default",
         [&]() {
             bruceConfig.removeTheme();
             bruceConfig.themePath = "";
             bruceConfig.theme.fs = 0;
             bruceConfig.nezhaThemeStyle = 1;
             uint16_t sec = DEFAULT_SECCOLOR;
             uint16_t bg = TFT_BLACK;
             bruceConfig.setUiColor(DEFAULT_PRICOLOR, &sec, &bg);
#ifdef HAS_RGB_LED
             bruceConfig.ledBright = 50;
             bruceConfig.ledColor = 0xFF2820;
             bruceConfig.ledEffect = 0;
             bruceConfig.ledEffectSpeed = 5;
             bruceConfig.ledEffectDirection = 1;
             ledSetup();
#endif
             bruceConfig.saveFile();
             invalidateStatusBarCache();
             fs = nullptr;
         }                                     },
        {"Main Menu", [&]() { fs = nullptr; }  }
    };
    if (setupSdCard()) {
        options.insert(options.begin() + 1, {"SD Card", [&]() { fs = &SD; }});
    }
    loopOptions(options, MENU_TYPE_SUBMENU, "UI Theme");
    if (fs == nullptr) return;

    String filepath = loopSD(*fs, true, "JSON");
    if (bruceConfig.openThemeFile(fs, filepath, true)) {
        bruceConfig.themePath = filepath;
        if (fs == &LittleFS) bruceConfig.theme.fs = 1;
        else if (fs == &SD) bruceConfig.theme.fs = 2;
        else bruceConfig.theme.fs = 0;

        bruceConfig.saveFile();
    }
}
#if !defined(LITE_VERSION)
BLE_API bleApi;
static bool ble_api_enabled = false;

void enableBLEAPI() {
    if (!ble_api_enabled) {
        // displayWarning("BLE API require huge amount of RAM.");
        // displayWarning("Some features may stop working.");
        Serial.println(ESP.getFreeHeap());
        bleApi.setup();
        Serial.println(ESP.getFreeHeap());
    } else {
        bleApi.end();
    }

    ble_api_enabled = !ble_api_enabled;

    // Give the user visual feedback about the new state, otherwise the toggle
    // looks like it does nothing and gets pressed repeatedly (which cycles the
    // BLE stack setup/teardown and can corrupt the GATT table).
    if (ble_api_enabled) {
        displayInfo("BLE API ON > Advertising as 'Nezha Cyber'", true);
    } else {
        displayInfo("BLE API OFF", true);
    }
}

bool appStoreInstalled() {
    FS *fs;
    if (!getFsStorage(fs)) {
        log_i("Fail getting filesystem");
        return false;
    }

    return fs->exists("/BruceJS/Tools/App Store.bin");
}

#include <HTTPClient.h>
void installAppStoreJS() {

    if (!WiFi.isConnected()) { wifiConnectMenu(WIFI_STA); }
    if (!WiFi.isConnected()) {
        displayWarning("WiFi not connected", true);
        return;
    }

    FS *fs;
    if (!getFsStorage(fs)) {
        log_i("Fail getting filesystem");
        return;
    }

    if (!fs->exists("/BruceJS")) {
        if (!fs->mkdir("/BruceJS")) {
            displayWarning("Failed to create /BruceJS directory", true);
            return;
        }
    }

    if (!fs->exists("/BruceJS/Tools")) {
        if (!fs->mkdir("/BruceJS/Tools")) {
            displayWarning("Failed to create /BruceJS/Tools directory", true);
            return;
        }
    }

    HTTPClient http;
    http.begin("http://appstore.bruce.computer/service/appstorebin/");
    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        displayWarning("Failed to download App Store", true);
        return;
    }

    File file = fs->open("/BruceJS/Tools/App Store.bin", FILE_WRITE);
    if (!file) {
        displayWarning("Failed to save App Store", true);
        return;
    }
    file.print(http.getString());
    http.end();
    file.close();

    // Check for old App Store.js and delete if found
    if (fs->exists("/BruceJS/Tools/App Store.js")) {
        fs->remove("/BruceJS/Tools/App Store.js");
    }

    displaySuccess("App Store installed", true);
    displaySuccess("Goto JS Interpreter -> Tools -> App Store", true);
}
#endif

#if defined(HAS_RESISTIVE_TOUCH)
extern CYD28_TouchR touch; // defined by hal/inputs/touch.cc

static constexpr const char *TOUCH_CAL_NAMESPACE = "touch_cal";

static bool validTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    return x0 > 0 && x1 > 0 && y0 > 0 && y1 > 0 && x0 != x1 && y0 != y1;
}

static esp_err_t readTouchCalibrationItems(
    nvs_handle_t handle, uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot
) {
    esp_err_t err = nvs_get_u16(handle, "x0", &x0);
    err |= nvs_get_u16(handle, "x1", &x1);
    err |= nvs_get_u16(handle, "y0", &y0);
    err |= nvs_get_u16(handle, "y1", &y1);
    err |= nvs_get_u8(handle, "r", &rot);
    if (err == ESP_OK) return err;

    // Older builds used single-letter keys.
    err = nvs_get_u16(handle, "x", &x0);
    err |= nvs_get_u16(handle, "X", &x1);
    err |= nvs_get_u16(handle, "y", &y0);
    err |= nvs_get_u16(handle, "Y", &y1);
    err |= nvs_get_u8(handle, "r", &rot);
    return err;
}

static bool getTouchCalibration(uint16_t &x0, uint16_t &x1, uint16_t &y0, uint16_t &y1, uint8_t &rot) {
    x0 = x1 = y0 = y1 = 0;
    rot = 0;

    nvs_handle_t handle;
    if (nvs_open(TOUCH_CAL_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        log_i("getTouchCalibration: no %s namespace found", TOUCH_CAL_NAMESPACE);
        return false;
    }
    esp_err_t err = readTouchCalibrationItems(handle, x0, x1, y0, y1, rot);
    nvs_close(handle);
    rot &= 0x07;
    return err == ESP_OK && validTouchCalibration(x0, x1, y0, y1);
}

// Loads the calibration from the NVS namespace "touch_cal" and applies it to the touch driver.
// Returns false when there is no valid calibration stored.
bool loadTouchCalibration() {
    uint16_t x0, x1, y0, y1;
    uint8_t rot;

    if (!getTouchCalibration(x0, x1, y0, y1, rot)) {
        Serial.println("loadTouchCalibration: Failed to load valid calibration data");
        return false;
    }

    uint16_t parameters[5] = {x0, x1, y0, y1, rot};
    touch.setTouch(parameters);
    Serial.printf(
        "loadTouchCalibration: Loaded calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n", x0, x1, y0, y1, rot
    );
    return true;
}

bool saveTouchCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1, uint8_t rot) {
    if (!validTouchCalibration(x0, x1, y0, y1)) {
        Serial.printf(
            "saveTouchCalibration: Invalid calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n",
            x0,
            x1,
            y0,
            y1,
            rot
        );
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(TOUCH_CAL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.printf("saveTouchCalibration: Failed to open %s namespace\n", TOUCH_CAL_NAMESPACE);
        return false;
    }

    rot &= 0x07;
    err = nvs_set_u16(handle, "x0", x0);
    err |= nvs_set_u16(handle, "x1", x1);
    err |= nvs_set_u16(handle, "y0", y0);
    err |= nvs_set_u16(handle, "y1", y1);
    err |= nvs_set_u8(handle, "r", rot);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        Serial.printf(
            "saveTouchCalibration: Saved calibration - x0:%u x1:%u y0:%u y1:%u rot:%u\n", x0, x1, y0, y1, rot
        );
        return true;
    }
    Serial.printf("saveTouchCalibration: Failed to save calibration data: %s\n", esp_err_to_name(err));
    return false;
}

// Asks the user to touch the 4 corners, computes the calibration, applies it and stores it in NVS.
void calibrateTouch() {
    // The input task reads the same touch driver: keep it out while the raw readings are taken.
    inputLock();
    tft.setRotation(0);
    tft.fillScreen(bruceConfig.bgColor);
    wakeUpScreen();
    const uint16_t _w = tft.width();
    const uint16_t _h = tft.height();

    struct RawTouchPoint {
        uint16_t x;
        uint16_t y;
    };

    auto drawCenteredLine = [&](const char *text, int16_t y) {
        tft.drawCentreString(bruce_i18n::tr(text), _w / 2, y, 1);
    };

    tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
    tft.setTextSize(FP);
    const int16_t lineHeight = LH + 4;
    int16_t y = (_h - lineHeight * 4) / 2;
    drawCenteredLine(bruce_i18n::tr("Nezha Cyber Touch Calibration"), y);
    y += lineHeight + 2;
    tft.drawFastHLine(24, y, _w - 48, uiBorderDimColor());
    y += 6;
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    drawCenteredLine("Touch the screen corners", y);
    y += lineHeight;
    drawCenteredLine("indicated by the arrows", y);
    delay(500);

    auto drawArrow = [&](uint8_t corner) {
        tft.fillRect(0, 0, 30, 30, bruceConfig.bgColor);
        tft.fillRect(0, _h - 30, 30, 30, bruceConfig.bgColor);
        tft.fillRect(_w - 30, 0, 30, 30, bruceConfig.bgColor);
        tft.fillRect(_w - 30, _h - 30, 30, 30, bruceConfig.bgColor);
        const int16_t edge = 0;
        const int16_t len = 28;
        const int16_t head = 8;
        const bool right = corner == 1 || corner == 2;
        const bool bottom = corner >= 2;
        const int16_t x0 = right ? _w - edge : edge;
        const int16_t y0 = bottom ? _h - edge : edge;
        const int16_t sx = right ? -1 : 1;
        const int16_t sy = bottom ? -1 : 1;

        tft.drawLine(x0 + sx * len, y0 + sy * len, x0, y0, bruceConfig.priColor);
        tft.drawLine(x0, y0, x0 + sx * head, y0, bruceConfig.priColor);
        tft.drawLine(x0, y0, x0, y0 + sy * head, bruceConfig.priColor);
        tft.drawLine(x0 + 1, y0 + sy, x0 + sx * (head + 1), y0 + sy, bruceConfig.priColor);
        tft.drawLine(x0 + sx, y0 + 1, x0 + sx, y0 + sy * (head + 1), bruceConfig.priColor);
    };

    auto logRaw = [&](const char *phase) {
        static unsigned long lastLog = 0;
        if (millis() - lastLog < 1000) return;
        lastLog = millis();
        auto r = touch.getPointRaw();
        Serial.printf(
            "calibrateTouch[%s]: raw x=%d y=%d z=%d isrWake=%d\n", phase, r.x, r.y, r.z, touch.isrWake
        );
    };
    auto readRawPoint = [&]() {
        while (touch.touched()) {
            logRaw("release");
            delay(10);
        }
        while (!touch.touched()) {
            logRaw("wait");
            delay(10);
        }

        uint32_t sx = 0;
        uint32_t sy = 0;
        const uint8_t samples = 6;
        for (uint8_t i = 0; i < samples; ++i) {
            auto p = touch.getPointRaw();
            sx += p.x;
            sy += p.y;
            delay(18);
        }

        while (touch.touched()) { delay(10); }
        return RawTouchPoint{uint16_t(sx / samples), uint16_t(sy / samples)};
    };

    RawTouchPoint p[4];
    for (uint8_t i = 0; i < 4; ++i) {
        drawArrow(i);
        p[i] = readRawPoint();
    }

    const int32_t leftRawX = (int32_t(p[0].x) + p[3].x) / 2;
    const int32_t rightRawX = (int32_t(p[1].x) + p[2].x) / 2;
    const int32_t leftRawY = (int32_t(p[0].y) + p[3].y) / 2;
    const int32_t rightRawY = (int32_t(p[1].y) + p[2].y) / 2;

    const uint8_t swapXY = abs(rightRawX - leftRawX) < abs(rightRawY - leftRawY);
    const uint8_t invertX = swapXY ? p[0].y > p[1].y : p[0].x > p[1].x;
    const uint8_t invertY = swapXY ? p[0].x > p[3].x : p[0].y > p[3].y;
    // Same bit layout the Launcher stores (swapXY and invertX are inverted for compatibility with
    // the CYD28_TouchscreenR orientation handling), so both firmwares can share the NVS values.
    const uint8_t rot = !swapXY | (invertY << 1) | (!invertX << 2);
    uint16_t xMin = !swapXY ? p[0].y : p[0].x;
    uint16_t xMax = xMin;
    uint16_t yMin = !swapXY ? p[0].x : p[0].y;
    uint16_t yMax = yMin;
    for (uint8_t i = 1; i < 4; ++i) {
        const uint16_t rx = !swapXY ? p[i].y : p[i].x;
        const uint16_t ry = !swapXY ? p[i].x : p[i].y;
        if (rx < xMin) xMin = rx;
        if (rx > xMax) xMax = rx;
        if (ry < yMin) yMin = ry;
        if (ry > yMax) yMax = ry;
    }

    uint16_t parameters[5] = {xMin, xMax, yMin, yMax, rot};
    touch.setTouch(parameters);
    saveTouchCalibration(xMin, xMax, yMin, yMax, rot);

    Serial.printf(
        "calibrateTouch: x0:%u x1:%u y0:%u y1:%u rot:%u swap:%u invX:%u invY:%u\n",
        xMin,
        xMax,
        yMin,
        yMax,
        rot,
        swapXY,
        invertX,
        invertY
    );
    tft.setRotation(bruceConfigPins.rotation);
    tft.fillScreen(bruceConfig.bgColor);
    wakeUpScreen();
    inputUnlock();
}
#endif
