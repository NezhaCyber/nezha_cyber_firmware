#include "radio_ui.h"
#include "pins.h"

namespace {

constexpr uint16_t COL_BG      = 0x0841; // Deep matte charcoal
constexpr uint16_t COL_PANEL   = 0x10A2; // Elevated dark card
constexpr uint16_t COL_BORDER  = 0x3186; // Subtle steel border
constexpr uint16_t COL_ACCENT  = 0x07FF; // Crisp cyan
constexpr uint16_t COL_AMBER   = 0xFD20; // Broadcast amber
constexpr uint16_t COL_GREEN   = 0x07E0; // Live signal green
constexpr uint16_t COL_RED     = 0xF800; // Mute / alert red
constexpr uint16_t COL_MUTED   = 0x8410; // Secondary grey

} // namespace

void RadioUI::begin() {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    _tft.init();
    _tft.setRotation(1); // 320x240 landscape on LILYGO T-Deck
    _tft.fillScreen(COL_BG);
    _tft.setTextWrap(false);
}

void RadioUI::showSplash(const char *subtitle) {
    _tft.fillScreen(COL_BG);
    _tft.drawRoundRect(8, 8, 304, 224, 8, COL_ACCENT);
    _tft.setTextColor(COL_ACCENT, COL_BG);
    _tft.drawCentreString("T-DECK INTERNET RADIO", 160, 74, 4);
    _tft.setTextColor(TFT_WHITE, COL_BG);
    _tft.drawCentreString(subtitle, 160, 122, 2);
    _tft.setTextColor(COL_MUTED, COL_BG);
    _tft.drawCentreString("ESP32-S3 + 8MB PSRAM + MAX98357A", 160, 180, 2);
}

void RadioUI::showStatusOverlay(const char *title, const String &line1, const String &line2) {
    _tft.fillScreen(COL_BG);
    _tft.fillRoundRect(12, 24, 296, 192, 8, COL_PANEL);
    _tft.drawRoundRect(12, 24, 296, 192, 8, COL_AMBER);
    _tft.setTextColor(COL_AMBER, COL_PANEL);
    _tft.drawCentreString(title, 160, 42, 4);
    _tft.setTextColor(TFT_WHITE, COL_PANEL);
    _tft.drawCentreString(line1, 160, 102, 2);
    if (!line2.isEmpty()) {
        _tft.setTextColor(COL_ACCENT, COL_PANEL);
        _tft.drawCentreString(line2, 160, 136, 2);
    }
    _tft.setTextColor(COL_MUTED, COL_PANEL);
    _tft.drawCentreString("Type on T-Deck KB or USB Serial", 160, 184, 2);
}

void RadioUI::drawTopBar(const RadioUiState &state) {
    _tft.fillRect(0, 0, 320, 28, COL_PANEL);
    _tft.drawFastHLine(0, 28, 320, COL_BORDER);

    // Wi-Fi RSSI signal bars (4 bars)
    int bars = 0;
    if (state.wifiConnected) {
        if (state.wifiRssi > -55) bars = 4;
        else if (state.wifiRssi > -67) bars = 3;
        else if (state.wifiRssi > -78) bars = 2;
        else bars = 1;
    }
    for (int i = 0; i < 4; ++i) {
        int bh = 4 + i * 4;
        uint16_t col = (i < bars) ? COL_GREEN : COL_BORDER;
        _tft.fillRect(8 + i * 5, 22 - bh, 3, bh, col);
    }

    _tft.setTextColor(state.wifiConnected ? TFT_WHITE : COL_RED, COL_PANEL);
    char wifiTxt[36];
    if (state.wifiConnected) {
        snprintf(wifiTxt, sizeof(wifiTxt), "%ddBm  %s", state.wifiRssi, state.ipAddress.c_str());
    } else {
        snprintf(wifiTxt, sizeof(wifiTxt), "NO WIFI");
    }
    _tft.drawString(wifiTxt, 32, 6, 2);

    // Right side: SD + PSRAM badge
    char memTxt[28];
    snprintf(memTxt, sizeof(memTxt), "%s PSRAM:%luK", state.sdMounted ? "SD" : "--",
             (unsigned long)state.freePsramKb);
    _tft.setTextColor(COL_ACCENT, COL_PANEL);
    _tft.drawRightString(memTxt, 312, 6, 2);
}

void RadioUI::drawStationCard(const RadioUiState &state) {
    _tft.fillRoundRect(8, 36, 304, 72, 6, COL_PANEL);
    _tft.drawRoundRect(8, 36, 304, 72, 6, COL_BORDER);

    // Preset pill + Genre + Status badge
    char idxBuf[16];
    snprintf(idxBuf, sizeof(idxBuf), "CH %d/%d", state.stationIndex + 1, max(1, state.stationCount));
    _tft.fillRoundRect(16, 43, 62, 18, 3, COL_ACCENT);
    _tft.setTextColor(TFT_BLACK, COL_ACCENT);
    _tft.drawCentreString(idxBuf, 47, 45, 2);

    _tft.setTextColor(COL_MUTED, COL_PANEL);
    _tft.drawString(state.stationGenre, 86, 45, 2);

    // Live / Connecting pill
    uint16_t stCol = (state.statusText == "LIVE") ? COL_GREEN : COL_AMBER;
    _tft.fillCircle(244, 52, 4, stCol);
    _tft.setTextColor(stCol, COL_PANEL);
    _tft.drawString(state.statusText, 253, 45, 2);

    // Active Station Name (large high-contrast)
    String nm = state.stationName;
    if (nm.length() > 21) nm = nm.substring(0, 20) + "~";
    _tft.setTextColor(TFT_WHITE, COL_PANEL);
    _tft.drawString(nm, 16, 68, 4);

    // Codec + Bitrate right-aligned
    char rateBuf[24];
    if (state.bitrateKbps > 0) {
        snprintf(rateBuf, sizeof(rateBuf), "%s %luk", state.codec.c_str(), (unsigned long)state.bitrateKbps);
    } else {
        snprintf(rateBuf, sizeof(rateBuf), "%s", state.codec.c_str());
    }
    _tft.setTextColor(COL_AMBER, COL_PANEL);
    _tft.drawRightString(rateBuf, 304, 86, 2);
}

void RadioUI::drawMarqueeBox(const RadioUiState &state) {
    _tft.fillRoundRect(8, 114, 304, 46, 6, TFT_BLACK);
    _tft.drawRoundRect(8, 114, 304, 46, 6, COL_BORDER);

    _tft.setTextColor(COL_ACCENT, TFT_BLACK);
    _tft.drawString("NOW PLAYING / ICY METADATA", 16, 118, 1);

    String title = state.icyTitle.isEmpty() ? state.stationName : state.icyTitle;
    if (title != _lastMarqueeText) {
        _lastMarqueeText = title;
        _marqueeOffset = 0;
    }

    const int maxChars = 34;
    String slice;
    if ((int)title.length() <= maxChars) {
        slice = title;
    } else {
        String padded = title + "   ***   " + title;
        int off = _marqueeOffset % (title.length() + 9);
        slice = padded.substring(off, off + maxChars);
    }
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.drawString(slice, 16, 134, 2);
}

void RadioUI::drawBottomDeck(const RadioUiState &state) {
    _tft.fillRoundRect(8, 166, 304, 52, 6, COL_PANEL);
    _tft.drawRoundRect(8, 166, 304, 52, 6, COL_BORDER);

    // Row 1: Volume (0-21) + Mute badge
    _tft.setTextColor(state.muted ? COL_RED : TFT_WHITE, COL_PANEL);
    char volTxt[20];
    if (state.muted) snprintf(volTxt, sizeof(volTxt), "MUTE");
    else snprintf(volTxt, sizeof(volTxt), "VOL %02d/%d", state.volume, RADIO_VOL_MAX);
    _tft.drawString(volTxt, 16, 173, 2);

    int barX = 96, barW = 206, barH = 12;
    _tft.drawRect(barX, 175, barW, barH, COL_BORDER);
    int vFill = state.muted ? 0 : ((barW - 2) * state.volume) / RADIO_VOL_MAX;
    _tft.fillRect(barX + 1, 176, barW - 2, barH - 2, TFT_BLACK);
    if (vFill > 0) {
        uint16_t vCol = (state.volume > 17) ? COL_AMBER : COL_GREEN;
        _tft.fillRect(barX + 1, 176, vFill, barH - 2, vCol);
    }

    // Row 2: PSRAM Audio Buffer Health
    _tft.setTextColor(COL_ACCENT, COL_PANEL);
    char bufTxt[20];
    snprintf(bufTxt, sizeof(bufTxt), "BUF %3u%%", (unsigned)state.bufferHealthPct);
    _tft.drawString(bufTxt, 16, 196, 2);

    _tft.drawRect(barX, 198, barW, barH, COL_BORDER);
    int bFill = ((barW - 2) * constrain((int)state.bufferHealthPct, 0, 100)) / 100;
    _tft.fillRect(barX + 1, 199, barW - 2, barH - 2, TFT_BLACK);
    if (bFill > 0) {
        uint16_t bCol = (state.bufferHealthPct > 35) ? COL_ACCENT : COL_RED;
        _tft.fillRect(barX + 1, 199, bFill, barH - 2, bCol);
    }

    // Footer hint line
    _tft.fillRect(0, 222, 320, 18, COL_BG);
    _tft.setTextColor(COL_MUTED, COL_BG);
    _tft.drawCentreString("TB: U/D=Vol  L/R=Station  Click=Mute  KB: 1-9, +/-, W=WiFi", 160, 226, 1);
}

void RadioUI::render(const RadioUiState &state, bool fullRedraw) {
    if (fullRedraw) _tft.fillScreen(COL_BG);
    drawTopBar(state);
    drawStationCard(state);
    drawMarqueeBox(state);
    drawBottomDeck(state);
}

void RadioUI::tickMarquee(const RadioUiState &state) {
    if (state.icyTitle.length() <= 34) return;
    if (millis() - _lastMarqueeMs >= 260) {
        _lastMarqueeMs = millis();
        ++_marqueeOffset;
        drawMarqueeBox(state);
    }
}
