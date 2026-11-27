/**
 * @file timer.cc
 * @author Aleksei Gor (https://github.com/AlekseiGor) - Reviewed and optimized by Senape3000
 * @brief Timer - Optimized implementation
 * @version 0.2
 * @date 2026-01-25
 */

#include "timer.h"
#include "core/display.h"
#include "core/i18n.h"
#include "core/utils.h"
#include "modules/others/audio.h"

// Constants for better maintainability
#define DELAY_VALUE 150
#define INPUT_POLL_DELAY 50          // Delay between input checks to save CPU
#define DISPLAY_UPDATE_INTERVAL 1000 // Update display every second
#define MAX_HOURS 99
#define MAX_MINUTES 59
#define MAX_SECONDS 59

// Enum for better readability
enum SettingMode {
    SETTING_HOURS = 0,
    SETTING_MINUTES = 1,
    SETTING_SECONDS = 2,
    SETTING_SOUND = 3,
    SETTING_COMPLETE = 4
};

Timer::Timer() { setup(); }

Timer::~Timer() {
    tft.fillScreen(bruceConfig.bgColor);
    backToMenu();
}

void Timer::setup() {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    playSoundOnFinish = true; // Default: sound enabled
    SettingMode settingMode = SETTING_HOURS;

    char timeString[12];
    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 16;
    int cardY = BORDER_PAD_Y + 24;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 16 : 22);
    uint16_t surf = uiSurfaceColor();

    fontSize = 4;
    for (uint8_t i = 4; i > 0; i--) {
        if (i * LW * 8 < (cardW - 24)) {
            fontSize = i;
            break;
        }
    }
    timerX = tftWidth / 2;
    timerY = cardY + (compact ? 12 : 26);
    underlineY = timerY + fontSize * LH + 4;

    drawMainBorderWithTitle("Set a timer", true);
    uiDrawCard(cardX, cardY, cardW, cardH, true);
    printCenterFootnote("PREV/NEXT: +/-  SEL: Next  ESC: Back");
    delay(DELAY_VALUE);
    check(SelPress);
    check(EscPress);

    bool dirty = true;
    while (true) {
        if (dirty) {
            snprintf(timeString, sizeof(timeString), "%02d:%02d:%02d", hours % 100, minutes % 100, seconds % 100);
            tft.fillRect(cardX + 6, timerY - 2, cardW - 12, fontSize * LH + 10, surf);
            tft.setTextDatum(TL_DATUM);
            tft.setTextSize(fontSize);
            tft.setTextColor(uiTextBrightColor(), surf);
            tft.drawCentreString(timeString, timerX, timerY, 1);

            clearUnderline();
            switch (settingMode) {
                case SETTING_HOURS:
                    underlineHours();
                    drawSoundOption(false);
                    break;
                case SETTING_MINUTES:
                    underlineMinutes();
                    drawSoundOption(false);
                    break;
                case SETTING_SECONDS:
                    underlineSeconds();
                    drawSoundOption(false);
                    break;
                case SETTING_SOUND:
                    drawSoundOption(true);
                    break;
                default: break;
            }
            dirty = false;
        }

        if (check(EscPress)) { return; }

        if (check(NextPress) || check(UpPress)) {
            switch (settingMode) {
                case SETTING_HOURS: hours = (hours >= MAX_HOURS) ? 0 : hours + 1; break;
                case SETTING_MINUTES: minutes = (minutes >= MAX_MINUTES) ? 0 : minutes + 1; break;
                case SETTING_SECONDS: seconds = (seconds >= MAX_SECONDS) ? 0 : seconds + 1; break;
                case SETTING_SOUND: playSoundOnFinish = !playSoundOnFinish; break;
                default: break;
            }
            dirty = true;
        }

        if (check(PrevPress) || check(DownPress)) {
            switch (settingMode) {
                case SETTING_HOURS: hours = (hours <= 0) ? MAX_HOURS : hours - 1; break;
                case SETTING_MINUTES: minutes = (minutes <= 0) ? MAX_MINUTES : minutes - 1; break;
                case SETTING_SECONDS: seconds = (seconds <= 0) ? MAX_SECONDS : seconds - 1; break;
                case SETTING_SOUND: playSoundOnFinish = !playSoundOnFinish; break;
                default: break;
            }
            dirty = true;
        }

        if (check(SelPress)) {
            settingMode = static_cast<SettingMode>(settingMode + 1);
            if (settingMode >= SETTING_COMPLETE) {
                if (hours > 0 || minutes > 0 || seconds > 0) {
                    duration = (hours * 3600 + minutes * 60 + seconds) * 1000;
                    break;
                }
                settingMode = SETTING_HOURS;
            }
            dirty = true;
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }

    loop();
}

bool Timer::responsiveDelay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        if (check(SelPress) || check(EscPress)) {
            return true; // Button Pressed
        }
        delay(10); // Check every 10ms
    }
    return false; // Timeout
}

void Timer::loop() {
    unsigned long startMillis = millis();
    unsigned long lastUpdateMillis = 0;

    int lastSeconds = -1;
    char timeString[12];
    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 16;
    int cardY = BORDER_PAD_Y + 24;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 16 : 22);
    uint16_t surf = uiSurfaceColor();

    drawMainBorderWithTitle("Timer", true);
    uiDrawCard(cardX, cardY, cardW, cardH, true);
    printCenterFootnote("ESC: Cancel");

    int trackX = cardX + 14;
    int trackW = cardW - 28;
    int barH = compact ? 6 : 8;
    int barY = cardY + cardH - (compact ? 14 : 20);

    while (true) {
        unsigned long currentMillis = millis();
        unsigned long elapsedMillis = currentMillis - startMillis;

        if (check(EscPress)) { break; }

        if (elapsedMillis >= (unsigned long)duration) {
            if (playSoundOnFinish) { playAlarmPattern(); }
            break;
        }

        if (lastSeconds < 0 || currentMillis - lastUpdateMillis >= 250) {
            unsigned long remainingMillis = (unsigned long)duration - elapsedMillis;
            int seconds = (remainingMillis / 1000) % 60;
            int minutes = (remainingMillis / 60000) % 60;
            int hours = (remainingMillis / 3600000) % 100;

            if (seconds != lastSeconds) {
                snprintf(timeString, sizeof(timeString), "%02d:%02d:%02d", hours, minutes, seconds);
                int timeY = cardY + (cardH - fontSize * LH - barH) / 2 - 2;
                tft.fillRect(cardX + 6, timeY - 2, cardW - 12, fontSize * LH + 4, surf);
                tft.setTextDatum(TL_DATUM);
                tft.setTextSize(fontSize);
                tft.setTextColor(uiTextBrightColor(), surf);
                tft.drawCentreString(timeString, timerX, timeY, 1);

                // Live themed countdown progress bar
                tft.fillRoundRect(trackX, barY, trackW, barH, barH / 2, uiSurfaceHiColor());
                int fillW = (duration > 0) ? (int)((uint64_t)remainingMillis * trackW / (uint64_t)duration) : 0;
                if (fillW > 0) {
                    tft.fillRoundRect(trackX, barY, min(fillW, trackW), barH, barH / 2, bruceConfig.priColor);
                }

                drawStatusBar();
                lastSeconds = seconds;
            }
            lastUpdateMillis = currentMillis;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Timer::playAlarmPattern() {
    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 18;
    int cardY = BORDER_PAD_Y + 26;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 14 : 22);
    uint16_t surf = uiSurfaceColor();

    drawMainBorderWithTitle("Timer finished!", true);
    uiDrawCard(cardX, cardY, cardW, cardH, true);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, bruceConfig.priColor);

    uiDrawText16(
        bruce_i18n::tr("TIME'S UP!"),
        tftWidth / 2,
        cardY + cardH / 2 - (compact ? 14 : 18),
        cardW - 16,
        bruceConfig.priColor,
        surf,
        1
    );
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), surf);
    tft.drawCentreString(
        bruce_i18n::tr("Press SEL or BACK to stop"), tftWidth / 2, cardY + cardH / 2 + (compact ? 6 : 10), 1
    );

    while (true) {
        if (check(SelPress) || check(EscPress)) { break; }
        _tone(2000, 1000);
        if (check(SelPress) || check(EscPress)) { break; }
        if (responsiveDelay(100)) { break; }
        _tone(6000, 1000);
        if (check(SelPress) || check(EscPress)) { break; }
        if (responsiveDelay(50)) { break; }
        _tone(6000, 1000);
        if (check(SelPress) || check(EscPress)) { break; }
        if (responsiveDelay(800)) { break; }
    }
}

void Timer::clearUnderline() {
    tft.fillRect(BORDER_PAD_X + 8, underlineY - 1, tftWidth - 2 * BORDER_PAD_X - 16, 4, uiSurfaceColor());
}

void Timer::underlineHours() {
    tft.fillRoundRect(timerX - (4 * LW * fontSize), underlineY, 2 * LW * fontSize, 3, 1, bruceConfig.priColor);
}

void Timer::underlineMinutes() {
    tft.fillRoundRect(timerX - (LW * fontSize), underlineY, 2 * LW * fontSize, 3, 1, bruceConfig.priColor);
}

void Timer::underlineSeconds() {
    tft.fillRoundRect(timerX + (2 * LW * fontSize), underlineY, 2 * LW * fontSize, 3, 1, bruceConfig.priColor);
}

void Timer::drawSoundOption(bool highlight) {
    bool compact = (tftHeight < 160);
    int optionY = underlineY + (compact ? 8 : 14);
    uint16_t surf = uiSurfaceColor();
    uint16_t pillBg = highlight ? bruceConfig.priColor : uiSurfaceHiColor();
    uint16_t pillFg = highlight ? uiTextOnAccentColor(bruceConfig.priColor) : uiTextMutedColor();

    tft.fillRect(BORDER_PAD_X + 10, optionY - 2, tftWidth - 2 * BORDER_PAD_X - 20, 20, surf);

    String optionText =
        String(bruce_i18n::tr("Sound")) + ": " + String(bruce_i18n::tr(playSoundOnFinish ? "ON" : "OFF"));
    int pillW = min(tftWidth - 40, bruce_i18n::utf8PixelWidth(optionText, FP) + 22);
    int pillH = 16;
    int pillX = timerX - pillW / 2;
    tft.fillRoundRect(pillX, optionY, pillW, pillH, 8, pillBg);
    if (!highlight) tft.drawRoundRect(pillX, optionY, pillW, pillH, 8, uiBorderDimColor());
    tft.setTextSize(FP);
    tft.setTextColor(pillFg, pillBg);
    int th = (FP == 1 && bruce_i18n::containsNonAscii(optionText)) ? 12 : (LH * FP);
    tft.drawCentreString(optionText, timerX, optionY + (pillH - th) / 2, 1);
}
