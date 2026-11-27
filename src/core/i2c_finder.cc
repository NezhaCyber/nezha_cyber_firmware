#include "i2c_finder.h"
#include "bus_HAL.h"
#include "display.h"
#include "i18n.h"
#include "mykeyboard.h"

#define FIRST_I2C_ADDRESS 0x08 // 0x00-0x07 and 0x78-0x7F are reserved
#define LAST_I2C_ADDRESS 0x77

namespace {
bool pinsConfigured(const BruceConfigPins::I2CPins &pins) { return pins.sda >= 0 && pins.scl >= 0; }

// sys_i2c and i2c_bus wired to the same pins are one physical bus: scanned and listed once.
bool busesShared() {
    return pinsConfigured(bruceConfigPins.sys_i2c) && bruceConfigPins.i2c_bus.sda == bruceConfigPins.sys_i2c.sda &&
           bruceConfigPins.i2c_bus.scl == bruceConfigPins.sys_i2c.scl;
}

// Scan one bus and render its results inside a card section.
void drawBusScanRow(I2CBusId id, const String &label, int x, int &y, int w, uint16_t surf) {
    I2CLease lease = i2cLeaseBus(id);
    String addrList = "";
    int count = 0;
    uint16_t valCol = uiTextBrightColor();

    if (!lease) {
        addrList = bruce_i18n::tr("unavailable");
        valCol = TFT_RED;
    } else {
        for (uint8_t addr = FIRST_I2C_ADDRESS; addr <= LAST_I2C_ADDRESS; addr++) {
            if (!i2cProbe(lease, addr)) continue;
            if (count > 0) addrList += " ";
            char hexBuf[6];
            snprintf(hexBuf, sizeof(hexBuf), "0x%02X", addr);
            addrList += hexBuf;
            count++;
        }
        if (count == 0) {
            addrList = bruce_i18n::tr("none");
            valCol = uiTextMutedColor();
        } else {
            valCol = bruceConfig.priColor;
        }
    }

    int16_t badgeW = uiDrawBadge(x, y, label, count > 0 ? bruceConfig.priColor : uiSurfaceHiColor(), 15);
    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%d", count);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), surf);
    tft.drawRightString(String(cntBuf) + " dev", x + w, y + 3, 1);
    (void)badgeW;
    y += 19;

    std::vector<String> lines = bruce_i18n::utf8WrapToWidth(addrList, w, FP);
    if (lines.empty()) lines.push_back(addrList);
    tft.setTextColor(valCol, surf);
    for (size_t i = 0; i < lines.size() && i < 3; ++i) {
        tft.drawString(lines[i], x + 2, y, 1);
        y += LH * FP + 3;
    }
    y += 4;
}
} // namespace

void find_i2c_addresses() {
    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 14;
    int cardY = BORDER_PAD_Y + 23;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 18 : 22);
    uint16_t surf = uiSurfaceColor();

    drawMainBorderWithTitle("I2C Finder");
    uiDrawCard(cardX, cardY, cardW, cardH, true);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), surf);
    tft.drawCentreString(bruce_i18n::tr("Scanning I2C buses..."), tftWidth / 2, cardY + cardH / 2 - 4, 1);
    delay(200);

    uiDrawCard(cardX, cardY, cardW, cardH, true);
    int x = cardX + 10;
    int w = cardW - 20;
    int y = cardY + (compact ? 8 : 12);

    bool sysConfigured = pinsConfigured(bruceConfigPins.sys_i2c);
    bool userConfigured = pinsConfigured(bruceConfigPins.i2c_bus);
    if (!sysConfigured && !userConfigured) {
        tft.setTextSize(FP);
        tft.setTextColor(uiTextMutedColor(), surf);
        tft.drawCentreString(bruce_i18n::tr("No I2C bus configured"), tftWidth / 2, cardY + cardH / 2 - 4, 1);
    } else if (busesShared()) {
        drawBusScanRow(
            I2CBusId::Sys,
            "SYS+EXT " + String((int)bruceConfigPins.sys_i2c.sda) + "/" + String((int)bruceConfigPins.sys_i2c.scl),
            x,
            y,
            w,
            surf
        );
    } else {
        if (sysConfigured) drawBusScanRow(I2CBusId::Sys, i2cBusLabel(I2CBusId::Sys), x, y, w, surf);
        if (userConfigured && y + 24 < cardY + cardH) {
            tft.drawFastHLine(x, y - 2, w, uiBorderDimColor());
            y += 4;
            drawBusScanRow(I2CBusId::User, i2cBusLabel(I2CBusId::User), x, y, w, surf);
        }
    }

    printCenterFootnote("SEL / ESC: Back");
    check(SelPress);
    check(EscPress);
    while (1) {
        if (check(EscPress) || check(SelPress)) {
            returnToMenu = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

// First address that answers on any bus (sys_i2c first), 0 if none.
uint8_t find_first_i2c_address() {
    uint8_t found = 0;
    i2cScanAll(
        [](I2CBusId, uint8_t addr, void *ctx) {
            uint8_t *first = static_cast<uint8_t *>(ctx);
            if (*first == 0) *first = addr;
        },
        &found
    );
    return found;
}

// True if the address answers on sys_i2c or i2c_bus.
bool check_i2c_address(uint8_t i2c_address) {
    I2CDeviceQuery query{i2c_address};
    return i2cLeaseDevice(query).found();
}
