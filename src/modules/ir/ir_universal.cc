#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "ir_universal.h"
#include "TV-B-Gone.h" // for checkIrTxPin()
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "ir_utils.h"
#include <IRsend.h>
#include <globals.h>

namespace {

enum UnivIrBrand : uint8_t {
    BRAND_ALL = 0,
    BRAND_SAMSUNG,
    BRAND_LG,
    BRAND_SONY,
    BRAND_NEC,
    BRAND_PANASONIC,
    BRAND_COUNT
};

static const char *const kBrandNames[BRAND_COUNT] = {
    "ALL (Universal)", "Samsung", "LG", "Sony", "NEC / TCL", "Panasonic"
};

static const char *const kBtnLabels[6] = {
    "POWER", "MUTE", "VOL +", "CH +", "VOL -", "CH -"
};

// Standard well-known remote codes per brand: {PWR, MUTE, VOL+, CH+, VOL-, CH-}
static const uint32_t kSamsungCodes[6] = {0xE0E040BF, 0xE0E0F00F, 0xE0E0E01F, 0xE0E048B7, 0xE0E0D02F, 0xE0E008F7};
static const uint32_t kLgCodes[6] = {0x20DF10EF, 0x20DF906F, 0x20DF40BF, 0x20DF00FF, 0x20DFC03F, 0x20DF807F};
static const uint32_t kSonyCodes[6] = {0xA90, 0x290, 0x490, 0x090, 0xC90, 0x890};
static const uint32_t kNecCodes[6] = {0x00FF02FD, 0x00FF08F7, 0x00FF807F, 0x00FF40BF, 0x00FF8877, 0x00FFC03F};
static const uint32_t kPanaCodes[6] = {0x40040100, 0x40040100, 0x40040100, 0x40040100, 0x40040100, 0x40040100};
static const uint32_t kPanaData[6] = {0xBCBD, 0x4C4D, 0x0405, 0x2C2D, 0x8485, 0xACAD};

static void sendBrandButton(IRsend &ir, UnivIrBrand brand, int btn) {
    switch (brand) {
        case BRAND_SAMSUNG: ir.sendSAMSUNG(kSamsungCodes[btn], 32); break;
        case BRAND_LG: ir.sendNEC(kLgCodes[btn], 32); break;
        case BRAND_SONY: ir.sendSony(kSonyCodes[btn], 12, 2); break;
        case BRAND_NEC: ir.sendNEC(kNecCodes[btn], 32); break;
        case BRAND_PANASONIC: ir.sendPanasonic(0x4004, (uint32_t)(0x01000000UL | kPanaData[btn])); break;
        default: break;
    }
    (void)kPanaCodes;
}

} // namespace

void ir_universal_remote() {
    checkIrTxPin();
    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);
    IRsend irsend(bruceConfigPins.irTx);
    irsend.begin();

    bool prevTouchZone = touchZoneOutsideFooterEnabled;
#if defined(HAS_TOUCH)
    touchZoneOutsideFooterEnabled = false;
#endif

    int brand = BRAND_ALL;
    int cursor = 0; // 0..5 = buttons, 6 = brand bar
    String statusMsg = "Ready";
    bool redrawAll = true;
    bool redrawPad = true;

    const int hy = STATUS_BAR_HEIGHT - 3;
    const int hh = 21;
    const int brandY = hy + hh + 6;
    const int brandH = 24;
    const int padTop = brandY + brandH + 8;
    const int cols = 2;
    const int rows = 3;
    const int gap = 8;
    const int padX = 12;
    const int btnW = (tftWidth - padX * 2 - gap) / cols;
    const int btnH = (tftHeight - padTop - 22 - gap * (rows - 1)) / rows;

    auto drawHeader = [&](bool txOn) {
        uint16_t bg = bruceConfig.bgColor;
        tft.fillRect(0, hy, tftWidth, hh + 2, bg);
        int tx = 10;
#if defined(HAS_TOUCH)
        int cy = hy + hh / 2;
        tft.drawLine(16, cy - 4, 11, cy, uiTextMutedColor());
        tft.drawLine(15, cy - 4, 10, cy, uiTextMutedColor());
        tft.drawLine(11, cy, 16, cy + 4, uiTextMutedColor());
        tft.drawLine(10, cy, 15, cy + 4, uiTextMutedColor());
        tx = 25;
#endif
        uiDrawText16(bruce_i18n::tr("Universal IR"), tx, hy + 2, tftWidth - tx - 56, uiTextBrightColor(), bg, 0);
        tft.fillCircle(tftWidth - 28, hy + hh / 2, 4, txOn ? TFT_GREEN : uiSurfaceHiColor());
        tft.setTextSize(FP);
        tft.setTextColor(uiTextMutedColor(), bg);
        tft.drawRightString("IR", tftWidth - 8, hy + (hh - LH * FP) / 2, 1);
        tft.drawFastHLine(6, hy + hh + 1, tftWidth - 12, uiBorderDimColor());
    };

    auto drawStatus = [&]() {
        int fy = tftHeight - 16;
        tft.fillRect(8, fy, tftWidth - 16, 14, bruceConfig.bgColor);
        tft.setTextSize(FP);
        tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
        tft.drawCentreString(statusMsg, tftWidth / 2, fy + 2, 1);
    };

    auto fireButton = [&](int btnIdx) {
        drawHeader(true);
        statusMsg = String("TX ") + kBrandNames[brand] + " " + kBtnLabels[btnIdx];
        drawStatus();
        if (brand == BRAND_ALL) {
            for (int b = BRAND_SAMSUNG; b < BRAND_COUNT; b++) {
                sendBrandButton(irsend, (UnivIrBrand)b, btnIdx);
                vTaskDelay(pdMS_TO_TICKS(35));
            }
        } else {
            sendBrandButton(irsend, (UnivIrBrand)brand, btnIdx);
        }
        digitalWrite(bruceConfigPins.irTx, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(60));
        drawHeader(false);
    };

    while (true) {
        if (redrawAll) {
            drawMainBorder(true);
            drawHeader(false);
            redrawAll = false;
            redrawPad = true;
        }
        if (redrawPad) {
            uint16_t pri = bruceConfig.priColor;
            uint16_t surf = uiSurfaceColor();
            uint16_t bdim = uiBorderDimColor();
            uint16_t bright = uiTextBrightColor();
            uint16_t onAcc = uiTextOnAccentColor(pri);

            // Brand selector pill bar
            bool brandSel = (cursor == 6);
            uint16_t bBg = brandSel ? pri : surf;
            uint16_t bFg = brandSel ? onAcc : bright;
            tft.fillRoundRect(padX, brandY, tftWidth - padX * 2, brandH, 6, bBg);
            if (!brandSel) tft.drawRoundRect(padX, brandY, tftWidth - padX * 2, brandH, 6, bdim);
            String brandStr = String("< ") + kBrandNames[brand] + " >";
            uiDrawText16(brandStr, tftWidth / 2, brandY + (brandH - 16) / 2, tftWidth - padX * 2 - 12, bFg, bBg, 1);

            // 2x3 Tactile Remote Pad
            for (int i = 0; i < 6; i++) {
                int r = i / cols;
                int c = i % cols;
                int bx = padX + c * (btnW + gap);
                int by = padTop + r * (btnH + gap);
                bool sel = (cursor == i);
                uint16_t fill = sel ? pri : surf;
                uint16_t fg = sel ? onAcc : (i == 0 ? 0xF986 : bright);
                tft.fillRoundRect(bx, by, btnW, btnH, 8, fill);
                if (!sel) tft.drawRoundRect(bx, by, btnW, btnH, 8, i == 0 ? 0xA820 : bdim);
                uiDrawText16(kBtnLabels[i], bx + btnW / 2, by + (btnH - 16) / 2, btnW - 10, fg, fill, 1);
            }
            drawStatus();
            redrawPad = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x;
            int ty = touchPoint.y;
            touchPoint.pressed = false;
            if (tx < 42 && ty < brandY) {
                EscPress = true;
            } else if (ty >= brandY && ty < brandY + brandH) {
                brand = (tx < tftWidth / 2) ? (brand + BRAND_COUNT - 1) % BRAND_COUNT : (brand + 1) % BRAND_COUNT;
                redrawPad = true;
            } else if (ty >= padTop) {
                for (int i = 0; i < 6; i++) {
                    int r = i / cols, c = i % cols;
                    int bx = padX + c * (btnW + gap);
                    int by = padTop + r * (btnH + gap);
                    if (tx >= bx && tx < bx + btnW && ty >= by && ty < by + btnH) {
                        cursor = i;
                        redrawPad = true;
                        fireButton(i);
                        break;
                    }
                }
            }
        }
#endif

        if (check(EscPress)) break;
        if (check(UpPress)) {
            if (cursor == 6) cursor = 4;
            else if (cursor < 2) cursor = 6;
            else cursor -= 2;
            redrawPad = true;
        } else if (check(DownPress)) {
            if (cursor == 6) cursor = 0;
            else if (cursor >= 4) cursor = 6;
            else cursor += 2;
            redrawPad = true;
        } else if (check(PrevPress)) {
            if (cursor == 6) brand = (brand + BRAND_COUNT - 1) % BRAND_COUNT;
            else cursor = (cursor + 6) % 7;
            redrawPad = true;
        } else if (check(NextPress)) {
            if (cursor == 6) brand = (brand + 1) % BRAND_COUNT;
            else cursor = (cursor + 1) % 7;
            redrawPad = true;
        } else if (check(SelPress)) {
            if (cursor == 6) {
                brand = (brand + 1) % BRAND_COUNT;
                redrawPad = true;
            } else {
                fireButton(cursor);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    digitalWrite(bruceConfigPins.irTx, LED_OFF);
    touchZoneOutsideFooterEnabled = prevTouchZone;
}
#endif
