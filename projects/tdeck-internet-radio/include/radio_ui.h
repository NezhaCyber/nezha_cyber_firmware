#pragma once

#include "config.h"
#include <TFT_eSPI.h>

struct RadioUiState {
    String stationName;
    String stationGenre;
    int stationIndex = 0;
    int stationCount = 0;

    String codec = "---";
    uint32_t bitrateKbps = 0;
    String icyTitle = "Connecting...";
    String statusText = "BOOT";

    int volume = RADIO_VOL_DEFAULT;
    bool muted = false;

    bool wifiConnected = false;
    int wifiRssi = -100;
    String ipAddress = "0.0.0.0";

    uint8_t bufferHealthPct = 0;
    uint32_t freePsramKb = 0;
    bool sdMounted = false;
};

class RadioUI {
public:
    void begin();
    void showSplash(const char *subtitle);
    void showStatusOverlay(const char *title, const String &line1, const String &line2 = "");
    void render(const RadioUiState &state, bool fullRedraw = false);
    void tickMarquee(const RadioUiState &state);

private:
    TFT_eSPI _tft;
    int _marqueeOffset = 0;
    uint32_t _lastMarqueeMs = 0;
    String _lastMarqueeText;

    void drawTopBar(const RadioUiState &state);
    void drawStationCard(const RadioUiState &state);
    void drawMarqueeBox(const RadioUiState &state);
    void drawBottomDeck(const RadioUiState &state);
};
