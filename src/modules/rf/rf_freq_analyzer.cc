#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "rf_freq_analyzer.h"
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "modules/others/audio.h"
#include "rf_utils.h"
#include <globals.h>

namespace {

static const float kAnalyzerFreqs[] = {
    300.00f, 303.87f, 310.00f, 315.00f, 318.00f, 390.00f, 418.00f, 433.42f,
    433.92f, 434.42f, 434.77f, 868.35f, 868.80f, 915.00f, 925.00f,
};
static constexpr int kAnalyzerFreqCount = sizeof(kAnalyzerFreqs) / sizeof(kAnalyzerFreqs[0]);

struct PeakEntry {
    float freq = 0.0f;
    int rssi = -110;
};

static void drawAnalyzerHeader(const char *badge, uint16_t badgeCol) {
    uint16_t bg = bruceConfig.bgColor;
    int hy = STATUS_BAR_HEIGHT - 3;
    int hh = 21;
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
    uiDrawText16(bruce_i18n::tr("Freq Analyzer"), tx, hy + 2, tftWidth - tx - 64, uiTextBrightColor(), bg, 0);
    tft.setTextSize(FP);
    tft.setTextColor(badgeCol, bg);
    tft.drawRightString(badge, tftWidth - 8, hy + (hh - LH * FP) / 2, 1);
    tft.drawFastHLine(6, hy + hh + 1, tftWidth - 12, uiBorderDimColor());
}

} // namespace

void rf_freq_analyzer() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("CC1101 required", true);
        return;
    }

    if (!initRfModule("rx", 433.92f)) {
        displayError("CC1101 not found!", true);
        return;
    }

    bool prevTouchZone = touchZoneOutsideFooterEnabled;
#if defined(HAS_TOUCH)
    touchZoneOutsideFooterEnabled = false;
#endif

    int thresholdDbm = -72;
    float lockedFreq = 0.0f;
    int lockedRssi = -110;
    uint32_t lastPeakMs = 0;
    PeakEntry history[4];
    int historyCount = 0;

    drawMainBorder(true);
    drawAnalyzerHeader("SCAN", uiTextMutedColor());

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t surfHi = uiSurfaceHiColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();

    int cardX = 10;
    int cardW = tftWidth - 20;
    int heroY = STATUS_BAR_HEIGHT + 24;
    int heroH = 74;
    int histY = heroY + heroH + 8;
    int histH = tftHeight - histY - 18;

    auto drawStaticCards = [&]() {
        tft.fillRoundRect(cardX, heroY, cardW, heroH, 8, surf);
        tft.drawRoundRect(cardX, heroY, cardW, heroH, 8, bdim);
        tft.fillRoundRect(cardX, histY, cardW, histH, 8, surf);
        tft.drawRoundRect(cardX, histY, cardW, histH, 8, bdim);
        tft.setTextSize(FP);
        tft.setTextColor(muted, surf);
        tft.drawString("PEAK HISTORY", cardX + 10, histY + 6, 1);
        tft.setTextColor(muted, bg);
        tft.drawCentreString("SEL=Set RF Freq  UP/DN=Thresh  ESC=Back", tftWidth / 2, tftHeight - 13, 1);
    };

    auto renderHero = [&](float liveFreq, int liveRssi, bool triggered) {
        tft.fillRect(cardX + 4, heroY + 4, cardW - 8, heroH - 8, surf);
        char freqBuf[24];
        if (lockedFreq > 0.0f) snprintf(freqBuf, sizeof(freqBuf), "%.2f MHz", lockedFreq);
        else snprintf(freqBuf, sizeof(freqBuf), "---.-- MHz");

        tft.setTextSize(FG);
        tft.setTextColor(triggered ? pri : bright, surf);
        tft.drawString(freqBuf, cardX + 12, heroY + 10, 1);

        char thrBuf[20];
        snprintf(thrBuf, sizeof(thrBuf), "THR %ddBm", thresholdDbm);
        tft.setTextSize(FP);
        tft.setTextColor(muted, surf);
        tft.drawRightString(thrBuf, cardX + cardW - 10, heroY + 8, 1);

        char rssiBuf[28];
        snprintf(rssiBuf, sizeof(rssiBuf), "LIVE %.2fM  %ddBm", liveFreq, liveRssi);
        tft.setTextColor(muted, surf);
        tft.drawString(rssiBuf, cardX + 12, heroY + 40, 1);

        // Live RSSI meter bar
        int barX = cardX + 12;
        int barW = cardW - 24;
        int barY = heroY + 56;
        int barH = 8;
        tft.fillRoundRect(barX, barY, barW, barH, 3, surfHi);
        int fillW = map(constrain(liveRssi, -100, -25), -100, -25, 0, barW);
        if (fillW > 2) {
            tft.fillRoundRect(barX, barY, fillW, barH, 3, (liveRssi >= thresholdDbm) ? pri : muted);
        }
        // Threshold tick marker
        int thrX = barX + map(constrain(thresholdDbm, -100, -25), -100, -25, 0, barW);
        tft.drawFastVLine(thrX, barY - 2, barH + 4, bright);
    };

    auto renderHistory = [&]() {
        tft.fillRect(cardX + 6, histY + 18, cardW - 12, histH - 22, surf);
        int maxRows = max(1, (histH - 22) / 14);
        tft.setTextSize(FP);
        if (historyCount == 0) {
            tft.setTextColor(muted, surf);
            tft.drawString("Waiting for Sub-GHz burst...", cardX + 10, histY + 22, 1);
            return;
        }
        for (int i = 0; i < historyCount && i < maxRows; i++) {
            int ry = histY + 20 + i * 14;
            char left[24], right[16];
            snprintf(left, sizeof(left), "#%d  %.2f MHz", i + 1, history[i].freq);
            snprintf(right, sizeof(right), "%d dBm", history[i].rssi);
            tft.setTextColor(i == 0 ? bright : muted, surf);
            tft.drawString(left, cardX + 10, ry, 1);
            tft.setTextColor(i == 0 ? pri : muted, surf);
            tft.drawRightString(right, cardX + cardW - 10, ry, 1);
        }
    };

    drawStaticCards();
    renderHero(433.92f, -100, false);
    renderHistory();

    while (true) {
        float bestFreq = kAnalyzerFreqs[0];
        int bestRssi = -115;

        for (int i = 0; i < kAnalyzerFreqCount; i++) {
            if (EscPress || SelPress) break;
            setMHZ(kAnalyzerFreqs[i]);
            vTaskDelay(pdMS_TO_TICKS(3));
            int rssi = ELECHOUSE_cc1101.getRssi();
            tft.drawPixel(0, 0, 0); // flush shared SPI bus
            if (rssi > bestRssi) {
                bestRssi = rssi;
                bestFreq = kAnalyzerFreqs[i];
            }
        }

        bool triggered = (bestRssi >= thresholdDbm);
        if (triggered) {
            lockedFreq = bestFreq;
            lockedRssi = bestRssi;
            if (millis() - lastPeakMs > 600) {
                if (bruceConfig.soundEnabled) playTone(1650, 35, 2, PLAYBACK_ASYNC);
                if (historyCount == 0 || fabs(history[0].freq - bestFreq) > 0.05f ||
                    bestRssi > history[0].rssi + 4) {
                    for (int k = min(historyCount, 3); k > 0; k--) history[k] = history[k - 1];
                    history[0] = {bestFreq, bestRssi};
                    if (historyCount < 4) historyCount++;
                    renderHistory();
                }
                lastPeakMs = millis();
            }
            drawAnalyzerHeader("LOCK", pri);
        } else {
            drawAnalyzerHeader("SCAN", muted);
        }

        renderHero(bestFreq, bestRssi, triggered);

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x;
            int ty = touchPoint.y;
            touchPoint.pressed = false;
            if (tx < 42 && ty < STATUS_BAR_HEIGHT + 24) {
                EscPress = true;
            } else if (ty >= heroY && ty < heroY + heroH && lockedFreq > 0.0f) {
                SelPress = true;
            }
        }
#endif

        if (check(EscPress)) break;
        if (check(UpPress) || check(NextPress)) {
            thresholdDbm = min(-40, thresholdDbm + 3);
        } else if (check(DownPress) || check(PrevPress)) {
            thresholdDbm = max(-95, thresholdDbm - 3);
        } else if (check(SelPress) && lockedFreq > 0.0f) {
            bruceConfigPins.setRfFreq(lockedFreq);
            char msg[32];
            snprintf(msg, sizeof(msg), "RF Freq -> %.2f MHz", lockedFreq);
            displaySuccess(msg, true);
            drawMainBorder(true);
            drawAnalyzerHeader("SCAN", muted);
            drawStaticCards();
            renderHero(bestFreq, bestRssi, false);
            renderHistory();
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    deinitRfModule();
    touchZoneOutsideFooterEnabled = prevTouchZone;
}
#endif
