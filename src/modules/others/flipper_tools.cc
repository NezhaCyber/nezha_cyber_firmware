#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "flipper_tools.h"
#include "audio.h"
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <ArduinoJson.h>
#include <esp_random.h>
#include <globals.h>
#include <mbedtls/md.h>

namespace {

struct TouchGuard {
    bool prev;
    TouchGuard() : prev(touchZoneOutsideFooterEnabled) {
#if defined(HAS_TOUCH)
        touchZoneOutsideFooterEnabled = false;
#endif
    }
    ~TouchGuard() { touchZoneOutsideFooterEnabled = prev; }
};

static void drawToolHeader(const String &title, const String &badge = "") {
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
    uiDrawText16(bruce_i18n::tr(title), tx, hy + 2, tftWidth - tx - 62, uiTextBrightColor(), bg, 0);
    if (badge.length() > 0) {
        tft.setTextSize(FP);
        tft.setTextColor(uiTextMutedColor(), bg);
        tft.drawRightString(badge, tftWidth - 8, hy + (hh - LH * FP) / 2, 1);
    }
    tft.drawFastHLine(6, hy + hh + 1, tftWidth - 12, uiBorderDimColor());
}

static bool hitBackChevron(int tx, int ty) {
    int hy = STATUS_BAR_HEIGHT - 3;
    return tx < 42 && ty >= hy - 4 && ty <= hy + 26;
}

// ============================================================================
// 1. TOTP 2FA Authenticator (RFC 6238 HMAC-SHA1)
// ============================================================================
struct TotpAccount {
    String name;
    String secret;
};

static size_t base32Decode(const String &in, uint8_t *out, size_t maxOut) {
    uint32_t buffer = 0;
    int bitsLeft = 0;
    size_t outLen = 0;
    for (size_t i = 0; i < in.length() && outLen < maxOut; i++) {
        char c = toupper(in[i]);
        int val = -1;
        if (c >= 'A' && c <= 'Z') val = c - 'A';
        else if (c >= '2' && c <= '7') val = 26 + (c - '2');
        else continue;
        buffer = (buffer << 5) | val;
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            bitsLeft -= 8;
            out[outLen++] = (uint8_t)((buffer >> bitsLeft) & 0xFF);
        }
    }
    return outLen;
}

static uint32_t computeTotp(const String &secretB32, uint64_t unixTimeUtc) {
    uint8_t key[64];
    size_t keyLen = base32Decode(secretB32, key, sizeof(key));
    if (keyLen == 0) return 0;

    uint64_t counter = unixTimeUtc / 30ULL;
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--) {
        msg[i] = (uint8_t)(counter & 0xFF);
        counter >>= 8;
    }

    uint8_t hmac[20];
    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!mdInfo || mbedtls_md_hmac(mdInfo, key, keyLen, msg, sizeof(msg), hmac) != 0) return 0;

    int offset = hmac[19] & 0x0F;
    uint32_t binCode = ((uint32_t)(hmac[offset] & 0x7F) << 24) |
                       ((uint32_t)hmac[offset + 1] << 16) |
                       ((uint32_t)hmac[offset + 2] << 8) |
                       (uint32_t)hmac[offset + 3];
    return binCode % 1000000UL;
}

static FS *totpFs() {
    if (setupSdCard()) return &SD;
    return &LittleFS;
}

static const char *kTotpDir = "/NezhaTOTP";
static const char *kTotpPath = "/NezhaTOTP/accounts.json";

static std::vector<TotpAccount> loadTotpAccounts() {
    std::vector<TotpAccount> list;
    FS *fs = totpFs();
    if (!fs || !fs->exists(kTotpPath)) return list;
    File f = fs->open(kTotpPath, FILE_READ);
    if (!f) return list;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok && doc.is<JsonArray>()) {
        for (JsonObject obj : doc.as<JsonArray>()) {
            TotpAccount acc;
            acc.name = obj["name"] | "";
            acc.secret = obj["secret"] | "";
            if (acc.name.length() > 0 && acc.secret.length() > 0) list.push_back(acc);
        }
    }
    f.close();
    return list;
}

static void saveTotpAccounts(const std::vector<TotpAccount> &list) {
    FS *fs = totpFs();
    if (!fs) return;
    if (!fs->exists(kTotpDir)) fs->mkdir(kTotpDir);
    File f = fs->open(kTotpPath, FILE_WRITE);
    if (!f) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &a : list) {
        JsonObject o = arr.add<JsonObject>();
        o["name"] = a.name;
        o["secret"] = a.secret;
    }
    serializeJson(doc, f);
    f.close();
}

static uint64_t currentUtcEpoch() {
#if defined(HAS_RTC)
    long local = (long)_rtc.getEpoch();
#else
    long local = (long)rtc.getEpoch();
#endif
    long utc = local - (long)bruceConfig.tmz * 3600L - (bruceConfig.dst ? 3600L : 0L);
    return (utc > 0) ? (uint64_t)utc : 0ULL;
}

// ============================================================================
// 2. Morse Code Table
// ============================================================================
static const char *morseForChar(char c) {
    static const char *const kAlpha[26] = {
        ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
        "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."
    };
    static const char *const kNum[10] = {
        "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
    };
    c = toupper(c);
    if (c >= 'A' && c <= 'Z') return kAlpha[c - 'A'];
    if (c >= '0' && c <= '9') return kNum[c - '0'];
    return nullptr;
}

} // namespace

// ============================================================================
// App 1: Flipper-Style TOTP 2FA Authenticator
// ============================================================================
void flipper_totp_app() {
    TouchGuard tg;
    std::vector<TotpAccount> accounts = loadTotpAccounts();
    int idx = 0;
    bool redrawAll = true;
    uint32_t lastSec = 0xFFFFFFFF;

    int cardX = 12;
    int cardW = tftWidth - 24;
    int cardY = STATUS_BAR_HEIGHT + 28;
    int cardH = tftHeight - cardY - 26;

    while (true) {
        uint64_t utc = currentUtcEpoch();
        uint32_t rem = 30 - (uint32_t)(utc % 30ULL);

        if (redrawAll || rem != lastSec) {
            lastSec = rem;
            if (redrawAll) {
                drawMainBorder(true);
                char badge[12] = "0/0";
                if (!accounts.empty()) snprintf(badge, sizeof(badge), "%d/%d", idx + 1, (int)accounts.size());
                drawToolHeader("Authenticator", badge);
                tft.fillRoundRect(cardX, cardY, cardW, cardH, 9, uiSurfaceColor());
                tft.drawRoundRect(cardX, cardY, cardW, cardH, 9, uiBorderDimColor());
                tft.setTextSize(FP);
                tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
                tft.drawCentreString("SEL=Menu  <>=Switch  ESC=Back", tftWidth / 2, tftHeight - 14, 1);
                redrawAll = false;
            }

            uint16_t surf = uiSurfaceColor();
            uint16_t surfHi = uiSurfaceHiColor();
            uint16_t pri = bruceConfig.priColor;
            uint16_t bright = uiTextBrightColor();
            uint16_t muted = uiTextMutedColor();
            tft.fillRect(cardX + 4, cardY + 4, cardW - 8, cardH - 8, surf);

            if (accounts.empty()) {
                uiDrawText16("No 2FA Accounts", tftWidth / 2, cardY + 34, cardW - 20, bright, surf, 1);
                tft.setTextSize(FP);
                tft.setTextColor(muted, surf);
                tft.drawCentreString("Press SEL to Add Account", tftWidth / 2, cardY + 66, 1);
                tft.drawCentreString("(RFC 6238 Base32 TOTP)", tftWidth / 2, cardY + 84, 1);
            } else {
                uiDrawText16(accounts[idx].name, tftWidth / 2, cardY + 14, cardW - 24, bright, surf, 1);

                uint32_t code = computeTotp(accounts[idx].secret, utc);
                char codeBuf[12];
                snprintf(codeBuf, sizeof(codeBuf), "%03lu %03lu", (unsigned long)(code / 1000), (unsigned long)(code % 1000));
                tft.setTextSize(FG + 1);
                tft.setTextColor(rem <= 5 ? 0xFB40 : pri, surf);
                tft.drawCentreString(codeBuf, tftWidth / 2, cardY + 48, 1);

                // 30-second countdown bar
                int barX = cardX + 16;
                int barW = cardW - 32;
                int barY = cardY + cardH - 36;
                tft.fillRoundRect(barX, barY, barW, 8, 4, surfHi);
                int fillW = (barW * (int)rem) / 30;
                uint16_t barCol = (rem <= 5) ? TFT_RED : ((rem <= 10) ? TFT_YELLOW : pri);
                if (fillW > 2) tft.fillRoundRect(barX, barY, fillW, 8, 4, barCol);

                char timeInfo[32];
                snprintf(timeInfo, sizeof(timeInfo), "%lus left%s", (unsigned long)rem, clock_set ? "" : " (Set Clock!)");
                tft.setTextSize(FP);
                tft.setTextColor(clock_set ? muted : TFT_YELLOW, surf);
                tft.drawCentreString(timeInfo, tftWidth / 2, barY + 14, 1);
            }
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.pressed = false;
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= cardY && ty < cardY + cardH) {
                if (!accounts.empty() && tx < cardX + 52) PrevPress = true;
                else if (!accounts.empty() && tx > cardX + cardW - 52) NextPress = true;
                else SelPress = true;
            }
        }
#endif

        if (check(EscPress)) break;
        if (!accounts.empty() && (check(NextPress) || check(DownPress))) {
            idx = (idx + 1) % (int)accounts.size();
            redrawAll = true;
        } else if (!accounts.empty() && (check(PrevPress) || check(UpPress))) {
            idx = (idx + (int)accounts.size() - 1) % (int)accounts.size();
            redrawAll = true;
        } else if (check(SelPress)) {
            int action = -1;
            std::vector<Option> opts = {
                {"Add Account",    [&]() { action = 0; }},
                {"Delete Account", [&]() { action = 1; }, false, nullptr, nullptr, false, !accounts.empty()},
                {"Back",           [&]() { action = 2; }},
            };
            loopOptions(opts, MENU_TYPE_SUBMENU, "Authenticator");
            if (action == 0) {
                String nm = keyboard("", 20, "Account Name");
                if (nm != "\x1B" && nm.length() > 0) {
                    String sec = keyboard("", 64, "Base32 Secret");
                    if (sec != "\x1B" && sec.length() >= 8) {
                        sec.replace(" ", "");
                        accounts.push_back({nm, sec});
                        saveTotpAccounts(accounts);
                        idx = (int)accounts.size() - 1;
                    }
                }
            } else if (action == 1 && !accounts.empty()) {
                accounts.erase(accounts.begin() + idx);
                saveTotpAccounts(accounts);
                if (idx >= (int)accounts.size()) idx = max(0, (int)accounts.size() - 1);
            }
            redrawAll = true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// App 2: Flipper-Style GPIO Pin Controller & Live Voltage Monitor
// ============================================================================
void flipper_gpio_control() {
    TouchGuard tg;
    struct GpioPinInfo {
        uint8_t pin;
        const char *label;
        uint8_t mode; // 0=IN, 1=OUT_HIGH, 2=OUT_LOW
    };

    // Safe user-accessible GPIO pins (avoids TFT/SPI/I2C/KB/I2S bus pins)
    std::vector<GpioPinInfo> pins;
    auto addIfSafe = [&](int8_t p, const char *lbl) {
        if (p < 0) return;
        for (const auto &e : pins) if (e.pin == (uint8_t)p) return;
        pins.push_back({(uint8_t)p, lbl, 0});
    };
    addIfSafe(bruceConfigPins.gps_bus.tx, "Grove/UART TX");
    addIfSafe(bruceConfigPins.gps_bus.rx, "Grove/UART RX");
    addIfSafe(bruceConfigPins.irTx, "IR TX");
    addIfSafe(bruceConfigPins.irRx, "IR RX");
    addIfSafe(bruceConfigPins.rfTx, "RF TX");
    addIfSafe(bruceConfigPins.rfRx, "RF RX");
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    addIfSafe(1, "GPIO 1 (ADC)");
    addIfSafe(2, "GPIO 2 (ADC)");
#endif
    if (pins.empty()) {
        displayError("No spare GPIOs", true);
        return;
    }

    for (auto &p : pins) pinMode(p.pin, INPUT);

    int cursor = 0;
    int scroll = 0;
    bool redrawAll = true;
    uint32_t lastSampleMs = 0;

    int listY = STATUS_BAR_HEIGHT + 24;
    int rowH = 26;
    int visible = max(1, (tftHeight - listY - 18) / rowH);

    auto drawRows = [&]() {
        uint16_t bg = bruceConfig.bgColor;
        uint16_t pri = bruceConfig.priColor;
        uint16_t surf = uiSurfaceColor();
        uint16_t bright = uiTextBrightColor();
        uint16_t muted = uiTextMutedColor();
        uint16_t onAcc = uiTextOnAccentColor(pri);

        for (int r = 0; r < visible; r++) {
            int i = scroll + r;
            int ry = listY + r * rowH;
            tft.fillRect(6, ry, tftWidth - 12, rowH, bg);
            if (i >= (int)pins.size()) continue;

            bool sel = (i == cursor);
            uint16_t rBg = sel ? pri : surf;
            tft.fillRoundRect(8, ry + 1, tftWidth - 16, rowH - 3, 6, rBg);

            char left[28];
            snprintf(left, sizeof(left), "IO%-2d %s", pins[i].pin, pins[i].label);
            uiDrawText16(left, 16, ry + (rowH - 16) / 2, tftWidth - 130, sel ? onAcc : bright, rBg, 0);

            int lvl = digitalRead(pins[i].pin);
            char stateBuf[24];
            if (pins[i].mode == 0) {
                int mv = analogReadMilliVolts(pins[i].pin);
                if (mv > 50 && mv < 3500) snprintf(stateBuf, sizeof(stateBuf), "IN %s %d.%02dV", lvl ? "H" : "L", mv / 1000, (mv % 1000) / 10);
                else snprintf(stateBuf, sizeof(stateBuf), "IN  %s", lvl ? "HIGH" : "LOW");
            } else if (pins[i].mode == 1) {
                snprintf(stateBuf, sizeof(stateBuf), "OUT HIGH");
            } else {
                snprintf(stateBuf, sizeof(stateBuf), "OUT LOW");
            }
            uint16_t stCol = sel ? onAcc : (pins[i].mode == 1 || lvl ? TFT_GREEN : muted);
            tft.setTextSize(FP);
            tft.setTextColor(stCol, rBg);
            tft.drawRightString(stateBuf, tftWidth - 14, ry + (rowH - LH * FP) / 2, 1);
        }
    };

    while (true) {
        if (redrawAll) {
            drawMainBorder(true);
            drawToolHeader("GPIO Control", "LIVE");
            tft.setTextSize(FP);
            tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
            tft.drawCentreString("SEL=Cycle IN/HI/LO  ESC=Back", tftWidth / 2, tftHeight - 13, 1);
            drawRows();
            redrawAll = false;
            lastSampleMs = millis();
        }
        if (millis() - lastSampleMs > 180) {
            drawRows();
            lastSampleMs = millis();
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.pressed = false;
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= listY && ty < listY + visible * rowH) {
                int tapped = scroll + (ty - listY) / rowH;
                if (tapped < (int)pins.size()) {
                    if (tapped == cursor) SelPress = true;
                    else {
                        cursor = tapped;
                        drawRows();
                    }
                }
            }
        }
#endif

        if (check(EscPress)) break;
        if (check(NextPress) || check(DownPress)) {
            cursor = (cursor + 1) % (int)pins.size();
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + visible) scroll = cursor - visible + 1;
            drawRows();
        } else if (check(PrevPress) || check(UpPress)) {
            cursor = (cursor + (int)pins.size() - 1) % (int)pins.size();
            if (cursor < scroll) scroll = cursor;
            if (cursor >= scroll + visible) scroll = cursor - visible + 1;
            drawRows();
        } else if (check(SelPress)) {
            auto &p = pins[cursor];
            p.mode = (p.mode + 1) % 3;
            if (p.mode == 0) pinMode(p.pin, INPUT);
            else {
                pinMode(p.pin, OUTPUT);
                digitalWrite(p.pin, p.mode == 1 ? HIGH : LOW);
            }
            drawRows();
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    // Safely release all pins back to INPUT on exit
    for (const auto &p : pins) pinMode(p.pin, INPUT);
}

// ============================================================================
// App 3: Flipper-Style PWM Signal Generator
// ============================================================================
void flipper_signal_gen() {
    TouchGuard tg;
    static const uint32_t kFreqPresets[] = {10, 50, 100, 440, 1000, 2000, 4000, 10000, 38000, 40000};
    static constexpr int kPresetCount = sizeof(kFreqPresets) / sizeof(kFreqPresets[0]);

    int pin = (bruceConfigPins.gps_bus.tx >= 0) ? bruceConfigPins.gps_bus.tx : bruceConfigPins.irTx;
    if (pin < 0) pin = 2;

    int freqIdx = 4; // 1000 Hz
    int dutyPct = 50;
    bool active = false;
    int cursor = 0; // 0=Freq, 1=Duty, 2=Start/Stop
    bool redraw = true;
    bool layoutDrawn = false;

    auto applyPwm = [&]() {
        if (active) {
            ledcDetach(pin);
            if (ledcAttach(pin, kFreqPresets[freqIdx], 8)) {
                ledcWrite(pin, (uint32_t)(dutyPct * 255 / 100));
            }
        } else {
            ledcDetach(pin);
            pinMode(pin, INPUT);
        }
    };

    int waveX = 12, waveY = STATUS_BAR_HEIGHT + 24, waveW = tftWidth - 24, waveH = 54;
    int rowsY = waveY + waveH + 10;
    int rowH = 28;

    while (true) {
        if (redraw) {
            if (!layoutDrawn) {
                drawMainBorder(true);
                char pbuf[12];
                snprintf(pbuf, sizeof(pbuf), "IO%d", pin);
                drawToolHeader("Signal Gen", pbuf);
                tft.setTextSize(FP);
                tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
                tft.drawCentreString("UP/DN=Select  <>=Adjust  SEL=Toggle", tftWidth / 2, tftHeight - 13, 1);
                layoutDrawn = true;
            }

            uint16_t bg = bruceConfig.bgColor;
            uint16_t pri = bruceConfig.priColor;
            uint16_t surf = uiSurfaceColor();
            uint16_t bdim = uiBorderDimColor();
            uint16_t bright = uiTextBrightColor();
            uint16_t muted = uiTextMutedColor();
            uint16_t onAcc = uiTextOnAccentColor(pri);

            // Live square-wave preview card
            tft.fillRoundRect(waveX, waveY, waveW, waveH, 8, surf);
            tft.drawRoundRect(waveX, waveY, waveW, waveH, 8, active ? pri : bdim);
            int gx = waveX + 10, gw = waveW - 20;
            int yHi = waveY + 12, yLo = waveY + waveH - 12;
            tft.drawFastHLine(gx, (yHi + yLo) / 2, gw, bdim);
            int cycles = 4;
            int cycW = gw / cycles;
            int hiW = max(2, cycW * dutyPct / 100);
            uint16_t wCol = active ? pri : muted;
            for (int c = 0; c < cycles; c++) {
                int x0 = gx + c * cycW;
                tft.drawFastVLine(x0, yHi, yLo - yHi, wCol);
                tft.drawFastHLine(x0, yHi, hiW, wCol);
                tft.drawFastVLine(x0 + hiW, yHi, yLo - yHi, wCol);
                tft.drawFastHLine(x0 + hiW, yLo, cycW - hiW, wCol);
            }

            char fVal[20], dVal[12];
            if (kFreqPresets[freqIdx] >= 1000) snprintf(fVal, sizeof(fVal), "%lu kHz", (unsigned long)(kFreqPresets[freqIdx] / 1000));
            else snprintf(fVal, sizeof(fVal), "%lu Hz", (unsigned long)kFreqPresets[freqIdx]);
            snprintf(dVal, sizeof(dVal), "%d%%", dutyPct);

            const char *labels[2] = {"Frequency", "Duty Cycle"};
            const char *vals[2] = {fVal, dVal};
            for (int i = 0; i < 2; i++) {
                int ry = rowsY + i * rowH;
                bool sel = (cursor == i);
                uint16_t rBg = sel ? pri : surf;
                tft.fillRoundRect(waveX, ry, waveW, rowH - 4, 6, rBg);
                uiDrawText16(labels[i], waveX + 10, ry + 4, 120, sel ? onAcc : bright, rBg, 0);
                uiDrawText16(String("< ") + vals[i] + " >", waveX + waveW - 10, ry + 4, 120, sel ? onAcc : pri, rBg, 2);
            }

            int btnY = rowsY + 2 * rowH + 4;
            bool bSel = (cursor == 2);
            uint16_t bFill = active ? 0xC800 : (bSel ? pri : surf);
            uint16_t bFg = (active || bSel) ? (active ? TFT_WHITE : onAcc) : pri;
            tft.fillRoundRect(waveX, btnY, waveW, 28, 7, bFill);
            if (!bSel && !active) tft.drawRoundRect(waveX, btnY, waveW, 28, 7, pri);
            uiDrawText16(active ? "STOP OUTPUT" : "START OUTPUT", tftWidth / 2, btnY + 6, waveW - 16, bFg, bFill, 1);

            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.pressed = false;
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= rowsY && ty < rowsY + rowH) {
                cursor = 0;
                freqIdx = (tx < tftWidth / 2) ? max(0, freqIdx - 1) : min(kPresetCount - 1, freqIdx + 1);
                if (active) applyPwm();
                redraw = true;
            } else if (ty >= rowsY + rowH && ty < rowsY + 2 * rowH) {
                cursor = 1;
                dutyPct = (tx < tftWidth / 2) ? max(5, dutyPct - 5) : min(95, dutyPct + 5);
                if (active) applyPwm();
                redraw = true;
            } else if (ty >= rowsY + 2 * rowH) {
                cursor = 2;
                SelPress = true;
            }
        }
#endif

        if (check(EscPress)) break;
        if (check(UpPress)) { cursor = (cursor + 2) % 3; redraw = true; }
        else if (check(DownPress)) { cursor = (cursor + 1) % 3; redraw = true; }
        else if (check(PrevPress)) {
            if (cursor == 0) freqIdx = max(0, freqIdx - 1);
            else if (cursor == 1) dutyPct = max(5, dutyPct - 5);
            if (active) applyPwm();
            redraw = true;
        } else if (check(NextPress)) {
            if (cursor == 0) freqIdx = min(kPresetCount - 1, freqIdx + 1);
            else if (cursor == 1) dutyPct = min(95, dutyPct + 5);
            if (active) applyPwm();
            redraw = true;
        } else if (check(SelPress)) {
            active = !active;
            applyPwm();
            redraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    ledcDetach(pin);
    pinMode(pin, INPUT);
}

// ============================================================================
// App 4: ITU Morse Code Audio & Visual Transmitter
// ============================================================================
void flipper_morse_tx() {
    TouchGuard tg;
    String text = "SOS NEZHA";
    int wpm = 15;
    bool redraw = true;

    auto transmit = [&]() {
        int unitMs = 1200 / wpm;
        uint16_t surf = uiSurfaceColor();
        uint16_t pri = bruceConfig.priColor;
        int cardX = 12, cardW = tftWidth - 24, cardY = STATUS_BAR_HEIGHT + 28, cardH = 126;

        for (size_t i = 0; i < text.length(); i++) {
            if (check(EscPress)) return;
            char ch = text[i];
            const char *code = morseForChar(ch);

            tft.fillRect(cardX + 6, cardY + 42, cardW - 12, 74, surf);
            char cStr[4] = {ch, '\0'};
            uiDrawText16(ch == ' ' ? "[SPACE]" : cStr, tftWidth / 2, cardY + 46, 120, uiTextBrightColor(), surf, 1);
            if (code) {
                tft.setTextSize(FG);
                tft.setTextColor(pri, surf);
                tft.drawCentreString(code, tftWidth / 2, cardY + 74, 1);

                for (size_t j = 0; code[j] != '\0'; j++) {
                    if (check(EscPress)) return;
                    int dur = (code[j] == '-') ? unitMs * 3 : unitMs;
                    tft.fillCircle(tftWidth - 24, STATUS_BAR_HEIGHT + 7, 5, pri);
                    if (bruceConfig.soundEnabled) playTone(760, dur, 0, PLAYBACK_BLOCKING);
                    else vTaskDelay(pdMS_TO_TICKS(dur));
                    tft.fillCircle(tftWidth - 24, STATUS_BAR_HEIGHT + 7, 5, uiSurfaceHiColor());
                    vTaskDelay(pdMS_TO_TICKS(unitMs));
                }
                vTaskDelay(pdMS_TO_TICKS(unitMs * 2));
            } else {
                vTaskDelay(pdMS_TO_TICKS(unitMs * 4));
            }
        }
    };

    while (true) {
        if (redraw) {
            drawMainBorder(true);
            char wb[12];
            snprintf(wb, sizeof(wb), "%d WPM", wpm);
            drawToolHeader("Morse TX", wb);

            int cardX = 12, cardW = tftWidth - 24, cardY = STATUS_BAR_HEIGHT + 28, cardH = 126;
            uint16_t surf = uiSurfaceColor();
            tft.fillRoundRect(cardX, cardY, cardW, cardH, 9, surf);
            tft.drawRoundRect(cardX, cardY, cardW, cardH, 9, uiBorderDimColor());

            uiDrawText16(text, tftWidth / 2, cardY + 14, cardW - 20, bruceConfig.priColor, surf, 1);
            tft.setTextSize(FP);
            tft.setTextColor(uiTextMutedColor(), surf);
            tft.drawCentreString("SEL=Transmit  UP=Edit  <>=WPM", tftWidth / 2, cardY + cardH - 18, 1);

            int btnY = cardY + cardH + 10;
            tft.fillRoundRect(cardX, btnY, cardW, 28, 7, bruceConfig.priColor);
            uiDrawText16("TRANSMIT MORSE", tftWidth / 2, btnY + 6, cardW - 16, uiTextOnAccentColor(bruceConfig.priColor), bruceConfig.priColor, 1);
            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.pressed = false;
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= STATUS_BAR_HEIGHT + 28 && ty < STATUS_BAR_HEIGHT + 154) UpPress = true;
            else SelPress = true;
        }
#endif

        if (check(EscPress)) break;
        if (check(NextPress)) { wpm = min(30, wpm + 2); redraw = true; }
        else if (check(PrevPress)) { wpm = max(8, wpm - 2); redraw = true; }
        else if (check(UpPress) || check(DownPress)) {
            String edited = keyboard(text, 32, "Morse Message");
            if (edited != "\x1B" && edited.length() > 0) text = edited;
            redraw = true;
        } else if (check(SelPress)) {
            transmit();
            redraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

// ============================================================================
// App 5: Hardware TRNG Password Generator
// ============================================================================
void flipper_passgen_app() {
    TouchGuard tg;
    static const char *kSets[4] = {
        "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!@#$%&*?",
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
        "0123456789ABCDEF",
        "0123456789"
    };
    static const char *kModeNames[4] = {"Strong", "AlphaNum", "Hex", "PIN"};
    static const int kLengths[5] = {8, 12, 16, 20, 24};

    int mode = 0;
    int lenIdx = 2; // 16 chars
    String currentPass;
    auto regen = [&]() {
        const char *cs = kSets[mode];
        size_t csLen = strlen(cs);
        currentPass = "";
        for (int i = 0; i < kLengths[lenIdx]; i++) {
            currentPass += cs[esp_random() % csLen];
        }
    };
    regen();

    bool redraw = true;
    while (true) {
        if (redraw) {
            drawMainBorder(true);
            char badge[16];
            snprintf(badge, sizeof(badge), "%s %d", kModeNames[mode], kLengths[lenIdx]);
            drawToolHeader("PassGen", badge);

            int cardX = 12, cardW = tftWidth - 24, cardY = STATUS_BAR_HEIGHT + 28, cardH = 98;
            uint16_t surf = uiSurfaceColor();
            tft.fillRoundRect(cardX, cardY, cardW, cardH, 9, surf);
            tft.drawRoundRect(cardX, cardY, cardW, cardH, 9, uiBorderDimColor());

            tft.setTextSize(FP);
            tft.setTextColor(uiTextMutedColor(), surf);
            tft.drawCentreString("ESP32 HARDWARE TRNG", tftWidth / 2, cardY + 12, 1);

            uiDrawText16(currentPass, tftWidth / 2, cardY + 42, cardW - 18, bruceConfig.priColor, surf, 1);

            tft.setTextSize(FP);
            tft.setTextColor(uiTextMutedColor(), surf);
            tft.drawCentreString("UP/DN=Length  <>=Charset", tftWidth / 2, cardY + cardH - 16, 1);

            int btnY = cardY + cardH + 12;
            tft.fillRoundRect(cardX, btnY, cardW, 28, 7, bruceConfig.priColor);
            uiDrawText16("GENERATE NEW", tftWidth / 2, btnY + 6, cardW - 16, uiTextOnAccentColor(bruceConfig.priColor), bruceConfig.priColor, 1);
            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.pressed = false;
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty < STATUS_BAR_HEIGHT + 126) NextPress = true;
            else SelPress = true;
        }
#endif

        if (check(EscPress)) break;
        if (check(SelPress)) { regen(); redraw = true; }
        else if (check(NextPress)) { mode = (mode + 1) % 4; regen(); redraw = true; }
        else if (check(PrevPress)) { mode = (mode + 3) % 4; regen(); redraw = true; }
        else if (check(UpPress)) { lenIdx = (lenIdx + 1) % 5; regen(); redraw = true; }
        else if (check(DownPress)) { lenIdx = (lenIdx + 4) % 5; regen(); redraw = true; }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

// ============================================================================
// App 6: Flipper-Style Polyhedral Dice & Coin Roller
// ============================================================================
void flipper_dice_app() {
    TouchGuard tg;
    static const int kSides[] = {2, 4, 6, 8, 10, 12, 20, 100};
    static const char *kDiceLabels[] = {"Coin", "d4", "d6", "d8", "d10", "d12", "d20", "d100"};
    static constexpr int kDiceCount = sizeof(kSides) / sizeof(kSides[0]);

    int dieIdx = 6; // d20
    int lastVal = 20;
    int hist[5] = {0};
    int histCount = 0;
    bool redraw = true;

    auto rollDice = [&]() {
        uint16_t surf = uiSurfaceColor();
        int cardX = 12, cardW = tftWidth - 24, cardY = STATUS_BAR_HEIGHT + 28, cardH = 112;
        for (int f = 0; f < 6; f++) {
            int tmp = 1 + (int)(esp_random() % kSides[dieIdx]);
            tft.fillRect(cardX + 10, cardY + 30, cardW - 20, 44, surf);
            tft.setTextSize(FG + 1);
            tft.setTextColor(uiTextMutedColor(), surf);
            String s = (kSides[dieIdx] == 2) ? (tmp == 1 ? "HEADS" : "TAILS") : String(tmp);
            tft.drawCentreString(s, tftWidth / 2, cardY + 38, 1);
            vTaskDelay(pdMS_TO_TICKS(35));
        }
        lastVal = 1 + (int)(esp_random() % kSides[dieIdx]);
        if (bruceConfig.soundEnabled) playTone(lastVal == kSides[dieIdx] ? 1400 : 920, 45, 2, PLAYBACK_ASYNC);
        for (int k = min(histCount, 4); k > 0; k--) hist[k] = hist[k - 1];
        hist[0] = lastVal;
        if (histCount < 5) histCount++;
    };

    while (true) {
        if (redraw) {
            drawMainBorder(true);
            drawToolHeader("Dice Roller", kDiceLabels[dieIdx]);

            int cardX = 12, cardW = tftWidth - 24, cardY = STATUS_BAR_HEIGHT + 28, cardH = 112;
            uint16_t surf = uiSurfaceColor();
            uint16_t pri = bruceConfig.priColor;
            tft.fillRoundRect(cardX, cardY, cardW, cardH, 9, surf);
            tft.drawRoundRect(cardX, cardY, cardW, cardH, 9, uiBorderDimColor());

            tft.setTextSize(FP);
            tft.setTextColor(uiTextMutedColor(), surf);
            tft.drawCentreString(String("< ") + kDiceLabels[dieIdx] + " >", tftWidth / 2, cardY + 10, 1);

            String resStr = (kSides[dieIdx] == 2) ? (lastVal == 1 ? "HEADS" : "TAILS") : String(lastVal);
            tft.setTextSize(FG + 1);
            tft.setTextColor(lastVal == kSides[dieIdx] ? TFT_GREEN : pri, surf);
            tft.drawCentreString(resStr, tftWidth / 2, cardY + 38, 1);

            if (histCount > 0) {
                String hs = "History: ";
                for (int i = 0; i < histCount; i++) {
                    if (i > 0) hs += ", ";
                    hs += String(hist[i]);
                }
                tft.setTextSize(FP);
                tft.setTextColor(uiTextMutedColor(), surf);
                tft.drawCentreString(hs, tftWidth / 2, cardY + cardH - 18, 1);
            }

            int btnY = cardY + cardH + 10;
            tft.fillRoundRect(cardX, btnY, cardW, 28, 7, pri);
            uiDrawText16("ROLL", tftWidth / 2, btnY + 6, cardW - 16, uiTextOnAccentColor(pri), pri, 1);
            redraw = false;
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.pressed = false;
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty < STATUS_BAR_HEIGHT + 62) {
                dieIdx = (tx < tftWidth / 2) ? (dieIdx + kDiceCount - 1) % kDiceCount : (dieIdx + 1) % kDiceCount;
                lastVal = kSides[dieIdx];
                histCount = 0;
                redraw = true;
            } else SelPress = true;
        }
#endif

        if (check(EscPress)) break;
        if (check(NextPress) || check(DownPress)) {
            dieIdx = (dieIdx + 1) % kDiceCount;
            lastVal = kSides[dieIdx];
            histCount = 0;
            redraw = true;
        } else if (check(PrevPress) || check(UpPress)) {
            dieIdx = (dieIdx + kDiceCount - 1) % kDiceCount;
            lastVal = kSides[dieIdx];
            histCount = 0;
            redraw = true;
        } else if (check(SelPress)) {
            rollDice();
            redraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void flipper_tools_menu() {
    while (true) {
        int sel = -1;
        std::vector<Option> m = {
            {"Authenticator", [&]() { sel = 0; }},
            {"Signal Gen",    [&]() { sel = 1; }},
            {"Morse TX",      [&]() { sel = 2; }},
            {"PassGen",       [&]() { sel = 3; }},
            {"Dice Roller",   [&]() { sel = 4; }},
            {"Back",          [&]() { sel = -1; }},
        };
        int res = loopOptions(m, MENU_TYPE_SUBMENU, "Flipper Tools");
        if (res < 0 || sel < 0) return;
        if (sel == 0) flipper_totp_app();
        else if (sel == 1) flipper_signal_gen();
        else if (sel == 2) flipper_morse_tx();
        else if (sel == 3) flipper_passgen_app();
        else if (sel == 4) flipper_dice_app();
    }
}
#endif
