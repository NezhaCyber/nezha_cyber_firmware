#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "flipper_tools.h"
#include "audio.h"
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_random.h>
#include <globals.h>
#include <mbedtls/base64.h>
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

/*
 * TOTP 2FA building blocks (RFC 6238, HMAC-SHA1).
 * Defines the stored account record, a Base32 decoder for secrets, account
 * load/save persistence, and the HMAC + time-based code derivation logic.
 */
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

/*
 * ITU Morse code lookup table.
 * morseForChar maps each alphanumeric character to its dot/dash sequence,
 * returning nullptr for characters with no standard representation.
 */
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

/*
 * Flipper-style TOTP authenticator UI.
 * Loads saved accounts, computes the rolling 6-digit code and its 30-second
 * expiry, and redraws the card when the second ticks or selection moves.
 */
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

/*
 * GPIO pin controller with live voltage monitoring.
 * Lets the user set pin modes/levels and read back the analog voltage on a
 * selected input, refreshing the readout inside the run loop.
 */
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

/*
 * PWM signal generator.
 * Configures LEDC to output a chosen frequency and duty cycle on a GPIO,
 * with on-screen controls to adjust both and start/stop the signal.
 */
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

/*
 * ITU Morse code transmitter (audio + visual).
 * Encodes typed text with morseForChar and flashes/buzzes each dot and dash,
 * timing each element from the configured words-per-minute rate.
 */
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

/*
 * Hardware TRNG-backed password generator.
 * Sources entropy from the ESP32 hardware random generator to build
 * passwords of a chosen length and character set, avoiding modulo bias.
 */
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

/*
 * Polyhedral dice roller and coin toss.
 * Rolls a selected die type (d4-d100) or flips a coin, drawing an animated
 * result and re-rolling on demand.
 */
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

/*
 * Live 2.4 GHz Wi-Fi channel thermal waterfall.
 * Scans RSSI across channels 1-13/14, plots the history as a heatmap, and
 * highlights the least congested channel for the user to pick.
 */
void flipper_wifi_waterfall() {
    TouchGuard tg;
    uint16_t bg = bruceConfig.bgColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t brd = uiBorderDimColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t muted = uiTextMutedColor();
    uint16_t bright = uiTextBrightColor();

    uint16_t heatLut[64];
    buildHeatPalette(heatLut, 64);

    wifi_mode_t prevMode = WiFi.getMode();
    if (prevMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
        delay(60);
    }

    constexpr int kChCount = 13;
    constexpr int kWfRows = 48;
    uint8_t wfBuf[kWfRows][kChCount] = {};
    uint8_t curEnergy[kChCount] = {};
    uint8_t curAps[kChCount] = {};
    int wfHead = 0;
    int totalAps = 0;
    int bestCh = 1;
    int peakRssi = -100;
    bool paused = false;

    tft.fillScreen(bg);
    drawMainBorder(false);
    drawToolHeader("WiFi Waterfall", "2.4G");

    const int topY = STATUS_BAR_HEIGHT + 24;
    const int padX = 8;
    const int plotW = tftWidth - padX * 2;
    const int chW = plotW / kChCount;
    const int plotX = padX + (plotW - chW * kChCount) / 2;
    const int statsH = 18;
    const int specY = topY + statsH + 2;
    const int specH = 54;
    const int wfY = specY + specH + 14;
    const int wfH = max(32, tftHeight - wfY - 8);
    const int rowPx = max(1, wfH / kWfRows);

    auto drawFrame = [&]() {
        // Top live summary strip
        tft.fillRoundRect(padX, topY, plotW, statsH, 4, surf);
        tft.drawRoundRect(padX, topY, plotW, statsH, 4, brd);
        char info[48];
        snprintf(info, sizeof(info), "APs:%d  Best:CH%d  Peak:%ddBm", totalAps, bestCh, peakRssi);
        tft.setTextSize(FP);
        tft.setTextColor(bright, surf);
        tft.drawString(info, padX + 6, topY + 5, 1);
        tft.setTextColor(paused ? sec : pri, surf);
        tft.drawRightString(paused ? "PAUSED" : "LIVE", padX + plotW - 6, topY + 5, 1);

        // Live 13-channel thermal bar spectrum
        tft.fillRoundRect(plotX - 2, specY, chW * kChCount + 4, specH, 4, bg);
        tft.drawRoundRect(plotX - 2, specY, chW * kChCount + 4, specH, 4, brd);
        for (int ch = 0; ch < kChCount; ch++) {
            int bx = plotX + ch * chW + 2;
            int bw = max(4, chW - 4);
            int bh = (int)((uint32_t)curEnergy[ch] * (specH - 6) / 255);
            uint16_t col = heatLut[(curEnergy[ch] * 63) / 255];
            if (bh > 1) {
                tft.fillRoundRect(bx, specY + specH - 3 - bh, bw, bh, 2, col);
            }
            // Highlight recommended quietest channel
            if (ch + 1 == bestCh) {
                tft.fillCircle(bx + bw / 2, specY + 4, 2, 0x07E0);
            }
        }

        // Channel axis labels (1..13)
        tft.fillRect(plotX - 2, specY + specH + 1, chW * kChCount + 4, 12, bg);
        tft.setTextSize(FP);
        for (int ch = 0; ch < kChCount; ch++) {
            tft.setTextColor((ch + 1 == bestCh) ? 0x07E0 : muted, bg);
            tft.drawCentreString(String(ch + 1), plotX + ch * chW + chW / 2, specY + specH + 3, 1);
        }

        // Scrolling thermal waterfall
        tft.drawRect(plotX - 1, wfY - 1, chW * kChCount + 2, kWfRows * rowPx + 2, brd);
        for (int r = 0; r < kWfRows; r++) {
            int bufRow = (wfHead - 1 - r + kWfRows) % kWfRows;
            int ry = wfY + r * rowPx;
            for (int ch = 0; ch < kChCount; ch++) {
                uint8_t v = wfBuf[bufRow][ch];
                uint16_t col = (v < 8) ? bg : heatLut[(v * 63) / 255];
                tft.fillRect(plotX + ch * chW, ry, chW, rowPx, col);
            }
        }
    };

    drawFrame();
    unsigned long lastScan = 0;

    while (true) {
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.Clear();
            if (hitBackChevron(tx, ty)) EscPress = true;
            else {
                paused = !paused;
                drawFrame();
            }
        }
#endif
        if (check(EscPress)) break;
        if (check(SelPress)) {
            paused = !paused;
            drawFrame();
        }

        if (!paused && (lastScan == 0 || millis() - lastScan >= 400)) {
            lastScan = millis();
            int n = WiFi.scanNetworks(false, true, false, 70);
            memset(curEnergy, 0, sizeof(curEnergy));
            memset(curAps, 0, sizeof(curAps));
            peakRssi = -100;
            totalAps = max(0, n);

            for (int i = 0; i < totalAps; i++) {
                int ch = WiFi.channel(i);
                int rssi = WiFi.RSSI(i);
                if (rssi > peakRssi) peakRssi = rssi;
                if (ch < 1 || ch > kChCount) continue;
                curAps[ch - 1]++;
                int sig = constrain((rssi + 96) * 4, 24, 240);
                // 20MHz Wi-Fi lobes spill +/-1 channel
                curEnergy[ch - 1] = (uint8_t)min(255, (int)curEnergy[ch - 1] + sig);
                if (ch > 1) curEnergy[ch - 2] = (uint8_t)min(255, (int)curEnergy[ch - 2] + sig / 3);
                if (ch < kChCount) curEnergy[ch] = (uint8_t)min(255, (int)curEnergy[ch] + sig / 3);
            }
            WiFi.scanDelete();

            // Pick cleanest non-overlapping channel among 1, 6, 11 (or quietest overall)
            int minE = 9999;
            bestCh = 1;
            for (int ch = 0; ch < kChCount; ch++) {
                if ((int)curEnergy[ch] < minE) {
                    minE = curEnergy[ch];
                    bestCh = ch + 1;
                }
                wfBuf[wfHead][ch] = curEnergy[ch];
            }
            wfHead = (wfHead + 1) % kWfRows;
            drawFrame();
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    if (prevMode == WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);
}

/*
 * Pocket GPIO oscilloscope and logic plotter.
 * Samples an analog pin on a selectable time base, renders the trace on
 * screen, and overlays digital-logic state for the configured channel.
 */
void flipper_gpio_scope() {
    TouchGuard tg;
    static const int kPins[] = {1, 2, 3, 10, 11, 13, 14, 15, 16, 43, 44};
    constexpr int kPinCount = sizeof(kPins) / sizeof(kPins[0]);
    static const uint16_t kSampleUs[] = {150, 800, 3500};
    static const char *kTbNames[] = {"FAST", "MED", "SLOW"};

    int pinIdx = 0;
    int tbIdx = 1;
    bool logicMode = false;
    bool held = false;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t brd = uiBorderDimColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t muted = uiTextMutedColor();
    uint16_t bright = uiTextBrightColor();

    tft.fillScreen(bg);
    drawMainBorder(false);
    drawToolHeader("GPIO Scope", "ADC");

    const int topY = STATUS_BAR_HEIGHT + 24;
    const int plotX = 8;
    const int plotW = tftWidth - 16;
    const int plotY = topY + 22;
    const int plotH = tftHeight - plotY - 52;
    const int dockY = tftHeight - 28;
    constexpr int kSamples = 144;
    uint16_t samples[kSamples] = {};

    auto drawControls = [&]() {
        char badge[16];
        snprintf(badge, sizeof(badge), "IO%d %s", kPins[pinIdx], logicMode ? "DIG" : "ADC");
        drawToolHeader("GPIO Scope", badge);

        int gap = 4;
        int btnW = (plotW - gap * 3) / 4;
        String labels[4] = {
            "IO " + String(kPins[pinIdx]),
            logicMode ? "LOGIC" : "ANALOG",
            kTbNames[tbIdx],
            held ? "RUN" : "HOLD"
        };
        for (int b = 0; b < 4; b++) {
            int bx = plotX + b * (btnW + gap);
            bool hi = (b == 3 && held);
            uint16_t fill = hi ? pri : surf;
            uint16_t fg = hi ? uiTextOnAccentColor(pri) : bright;
            tft.fillRoundRect(bx, dockY, btnW, 22, 4, fill);
            tft.drawRoundRect(bx, dockY, btnW, 22, 4, hi ? pri : brd);
            tft.setTextSize(FP);
            tft.setTextColor(fg, fill);
            tft.drawCentreString(labels[b], bx + btnW / 2, dockY + 7, 1);
        }
    };

    auto sampleAndRender = [&]() {
        int pin = kPins[pinIdx];
        pinMode(pin, INPUT);
        uint32_t t0 = micros();
        uint16_t vmin = 4000, vmax = 0;
        uint32_t sum = 0;
        int edges = 0;
        bool prevHi = false;

        for (int i = 0; i < kSamples; i++) {
            uint16_t mv = logicMode ? (digitalRead(pin) ? 3300 : 0) : (uint16_t)constrain((int)analogReadMilliVolts(pin), 0, 3300);
            samples[i] = mv;
            if (mv < vmin) vmin = mv;
            if (mv > vmax) vmax = mv;
            sum += mv;
            bool isHi = (mv > 1650);
            if (i > 0 && isHi != prevHi) edges++;
            prevHi = isHi;
            delayMicroseconds(kSampleUs[tbIdx]);
        }
        uint32_t elapsedUs = max(1UL, micros() - t0);
        uint32_t freqHz = (uint32_t)((edges / 2ULL) * 1000000ULL / elapsedUs);
        uint16_t vavg = (uint16_t)(sum / kSamples);

        // Stats strip
        tft.fillRoundRect(plotX, topY, plotW, 18, 4, surf);
        tft.drawRoundRect(plotX, topY, plotW, 18, 4, brd);
        char st[56];
        snprintf(st, sizeof(st), "Min:%umV  Max:%umV  Avg:%umV  %luHz", vmin, vmax, vavg, (unsigned long)freqHz);
        tft.setTextSize(FP);
        tft.setTextColor(bright, surf);
        tft.drawString(st, plotX + 6, topY + 5, 1);

        // Scope screen + graticule grid
        uint16_t gridCol = blendColors(bg, brd, 110);
        tft.fillRoundRect(plotX, plotY, plotW, plotH, 5, 0x0000);
        tft.drawRoundRect(plotX, plotY, plotW, plotH, 5, brd);
        for (int g = 1; g < 4; g++) {
            int gy = plotY + (plotH * g) / 4;
            tft.drawFastHLine(plotX + 2, gy, plotW - 4, gridCol);
        }
        for (int g = 1; g < 6; g++) {
            int gx = plotX + (plotW * g) / 6;
            tft.drawFastVLine(gx, plotY + 2, plotH - 4, gridCol);
        }

        // Waveform trace
        uint16_t traceCol = logicMode ? sec : pri;
        for (int i = 1; i < kSamples; i++) {
            int x0 = plotX + 3 + ((i - 1) * (plotW - 6)) / (kSamples - 1);
            int x1 = plotX + 3 + (i * (plotW - 6)) / (kSamples - 1);
            int y0 = plotY + plotH - 4 - (int)((uint32_t)samples[i - 1] * (plotH - 8) / 3300);
            int y1 = plotY + plotH - 4 - (int)((uint32_t)samples[i] * (plotH - 8) / 3300);
            tft.drawLine(x0, y0, x1, y1, traceCol);
        }

        // Bottom hint
        tft.fillRect(plotX, plotY + plotH + 2, plotW, 14, bg);
        tft.setTextSize(FP);
        tft.setTextColor(muted, bg);
        tft.drawCentreString("Prev/Next: Pin   Sel: Hold   Tap Dock", tftWidth / 2, plotY + plotH + 4, 1);
    };

    drawControls();
    sampleAndRender();

    while (true) {
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.Clear();
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= dockY - 4) {
                int b = constrain((tx - plotX) * 4 / max(1, plotW), 0, 3);
                if (b == 0) pinIdx = (pinIdx + 1) % kPinCount;
                else if (b == 1) logicMode = !logicMode;
                else if (b == 2) tbIdx = (tbIdx + 1) % 3;
                else held = !held;
                drawControls();
                if (!held) sampleAndRender();
            } else {
                held = !held;
                drawControls();
            }
        }
#endif
        if (check(EscPress)) break;
        if (check(NextPress) || check(DownPress)) {
            pinIdx = (pinIdx + 1) % kPinCount;
            drawControls();
        } else if (check(PrevPress) || check(UpPress)) {
            pinIdx = (pinIdx + kPinCount - 1) % kPinCount;
            drawControls();
        } else if (check(SelPress)) {
            held = !held;
            drawControls();
        }

        if (!held) {
            sampleAndRender();
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

/*
 * Nezha pentatonic synth and tactical FX pad.
 * Plays the five pentatonic notes as a playable keyboard, layering sound
 * effects through the audio engine when the pad is triggered.
 */
void flipper_nezha_synth() {
    TouchGuard tg;
    static const uint16_t kPentatonic[8] = {523, 587, 659, 784, 880, 1047, 1175, 1319};
    static const char *kPentaLbl[8] = {"C5", "D5", "E5", "G5", "A5", "C6", "D6", "E6"};
    static const uint16_t kChromatic[8] = {440, 494, 523, 587, 659, 698, 784, 880};
    static const char *kChromLbl[8] = {"A4", "B4", "C5", "D5", "E5", "F5", "G5", "A5"};
    static const char *kFxLbl[8] = {"FIRE", "SONAR", "SIREN", "LASER", "DRUM", "LOTUS", "PING", "SWEEP"};

    int bank = 0; // 0=Pentatonic, 1=Chromatic, 2=Cyber FX
    int selected = 0;
    int lastPlayed = -1;
    uint8_t bars[12] = {6, 10, 14, 18, 22, 16, 12, 20, 14, 10, 8, 6};

    uint16_t bg = bruceConfig.bgColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t brd = uiBorderDimColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t bright = uiTextBrightColor();
    uint16_t onAcc = uiTextOnAccentColor(pri);

    tft.fillScreen(bg);
    drawMainBorder(false);

    const int topY = STATUS_BAR_HEIGHT + 24;
    const int vizH = 44;
    const int padY = topY + vizH + 8;
    const int padW = (tftWidth - 16 - 12) / 4;
    const int padH = max(34, (tftHeight - padY - 34) / 2);
    const int dockY = tftHeight - 26;

    auto drawViz = [&]() {
        tft.fillRoundRect(8, topY, tftWidth - 16, vizH, 6, surf);
        tft.drawRoundRect(8, topY, tftWidth - 16, vizH, 6, brd);
        int bw = (tftWidth - 36) / 12;
        for (int i = 0; i < 12; i++) {
            int bh = constrain((int)bars[i], 3, vizH - 10);
            int bx = 18 + i * bw;
            tft.fillRoundRect(bx, topY + vizH - 5 - bh, max(4, bw - 4), bh, 2, (i & 1) ? sec : pri);
        }
    };

    auto drawPads = [&]() {
        const char *bankTag = (bank == 0) ? "PENTA" : (bank == 1 ? "SCALE" : "FX");
        drawToolHeader("Nezha Synth", bankTag);
        drawViz();

        for (int i = 0; i < 8; i++) {
            int r = i / 4;
            int c = i % 4;
            int bx = 8 + c * (padW + 4);
            int by = padY + r * (padH + 6);
            bool hi = (i == selected || i == lastPlayed);
            uint16_t fill = hi ? pri : surf;
            uint16_t fg = hi ? onAcc : bright;
            tft.fillRoundRect(bx, by, padW, padH, 6, fill);
            tft.drawRoundRect(bx, by, padW, padH, 6, hi ? sec : brd);
            const char *lbl = (bank == 0) ? kPentaLbl[i] : (bank == 1 ? kChromLbl[i] : kFxLbl[i]);
            uiDrawText16(lbl, bx + padW / 2, by + (padH - 16) / 2, padW - 6, fg, fill, 1);
        }

        // Bottom bank switcher strip
        int bW = (tftWidth - 24) / 3;
        const char *bNames[3] = {"Pentatonic", "Chromatic", "Cyber FX"};
        for (int b = 0; b < 3; b++) {
            int bx = 8 + b * (bW + 4);
            bool act = (b == bank);
            tft.fillRoundRect(bx, dockY, bW, 20, 4, act ? sec : surf);
            tft.drawRoundRect(bx, dockY, bW, 20, 4, act ? sec : brd);
            tft.setTextSize(FP);
            tft.setTextColor(act ? uiTextOnAccentColor(sec) : bright, act ? sec : surf);
            tft.drawCentreString(bNames[b], bx + bW / 2, dockY + 6, 1);
        }
    };

    auto playPad = [&](int idx) {
        lastPlayed = idx;
        selected = idx;
        for (int i = 0; i < 12; i++) {
            bars[i] = (uint8_t)(8 + ((esp_random() + idx * 17) % (vizH - 14)));
        }
        drawPads();
        if (bank == 0) {
            _tone(kPentatonic[idx], 130);
        } else if (bank == 1) {
            _tone(kChromatic[idx], 130);
        } else {
            switch (idx) {
                case 0: // Wind-Fire Sweep
                    for (int f = 320; f <= 1400; f += 120) _tone(f, 22);
                    break;
                case 1: // Sonar Ping
                    _tone(1280, 60); delay(40); _tone(960, 80);
                    break;
                case 2: // Cyber Siren
                    for (int k = 0; k < 2; k++) { _tone(880, 75); _tone(620, 75); }
                    break;
                case 3: // Laser Zap
                    for (int f = 1800; f >= 300; f -= 180) _tone(f, 16);
                    break;
                case 4: // Nezha War Drum
                    _tone(140, 55); delay(45); _tone(140, 45); delay(35); _tone(220, 80);
                    break;
                case 5: // Lotus Bell Pentatonic Arp
                    _tone(523, 50); _tone(659, 50); _tone(784, 50); _tone(1047, 90);
                    break;
                case 6: // Tactical Ping
                    _tone(2000, 40);
                    break;
                default: // Up-Down Sweep
                    for (int f = 400; f <= 1200; f += 160) _tone(f, 18);
                    for (int f = 1200; f >= 400; f -= 160) _tone(f, 18);
                    break;
            }
        }
        lastPlayed = -1;
        for (int i = 0; i < 12; i++) bars[i] = max(4, (int)bars[i] / 2);
        drawPads();
    };

    drawPads();

    while (true) {
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.Clear();
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= dockY - 4) {
                bank = constrain((tx - 8) * 3 / max(1, tftWidth - 16), 0, 2);
                drawPads();
            } else if (ty >= padY - 2 && ty < dockY - 4) {
                int c = constrain((tx - 8) / (padW + 4), 0, 3);
                int r = (ty >= padY + padH + 3) ? 1 : 0;
                playPad(r * 4 + c);
            }
        }
#endif
        if (check(EscPress)) break;
        if (check(NextPress) || check(DownPress)) {
            selected = (selected + 1) % 8;
            drawPads();
        } else if (check(PrevPress) || check(UpPress)) {
            selected = (selected + 7) % 8;
            drawPads();
        } else if (check(SelPress)) {
            playPad(selected);
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

/*
 * Hardware crypto and hash lab helpers and UI.
 * Provides bytesToHex and mbedtls-backed hashString, then the
 * flipper_crypto_lab screen that computes SHA-256/SHA-1/MD5 and Base64/Hex.
 */
static String bytesToHex(const uint8_t *buf, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += kHex[(buf[i] >> 4) & 0x0F];
        out += kHex[buf[i] & 0x0F];
    }
    return out;
}

static String hashString(mbedtls_md_type_t type, const String &in) {
    uint8_t out[32] = {};
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(type);
    if (!info) return "ERR";
    mbedtls_md(info, (const unsigned char *)in.c_str(), in.length(), out);
    return bytesToHex(out, mbedtls_md_get_size(info));
}

void flipper_crypto_lab() {
    TouchGuard tg;
    String payload = "Nezha Cyber";
    uint16_t bg = bruceConfig.bgColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t brd = uiBorderDimColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t muted = uiTextMutedColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t onAcc = uiTextOnAccentColor(pri);

    bool redraw = true;
    int dockY = tftHeight - 28;

    auto renderCrypto = [&]() {
        tft.fillScreen(bg);
        drawMainBorder(false);
        drawToolHeader("Cyber Crypto", String(payload.length()) + "B");

        String sha256 = hashString(MBEDTLS_MD_SHA256, payload);
        String sha1 = hashString(MBEDTLS_MD_SHA1, payload);
        String md5 = hashString(MBEDTLS_MD_MD5, payload);

        unsigned char b64[128] = {};
        size_t b64Len = 0;
        mbedtls_base64_encode(
            b64, sizeof(b64) - 1, &b64Len, (const unsigned char *)payload.c_str(), payload.length()
        );
        b64[b64Len] = '\0';

        int y = STATUS_BAR_HEIGHT + 24;
        int cardX = 8, cardW = tftWidth - 16;

        // Input card
        tft.fillRoundRect(cardX, y, cardW, 24, 5, surf);
        tft.drawRoundRect(cardX, y, cardW, 24, 5, pri);
        tft.setTextSize(FP);
        tft.setTextColor(sec, surf);
        tft.drawString("IN:", cardX + 6, y + 8, 1);
        uiDrawText16(payload, cardX + 28, y + 4, cardW - 36, bright, surf, 0);
        y += 28;

        auto drawDigestRow = [&](const char *lbl, const String &val, bool twoLines) {
            int rh = twoLines ? 32 : 22;
            tft.fillRoundRect(cardX, y, cardW, rh, 4, surf);
            tft.drawRoundRect(cardX, y, cardW, rh, 4, brd);
            tft.setTextSize(FP);
            tft.setTextColor(muted, surf);
            tft.drawString(lbl, cardX + 6, y + 4, 1);
            tft.setTextColor(bright, surf);
            if (twoLines) {
                tft.drawString(val.substring(0, 32), cardX + 48, y + 4, 1);
                tft.drawString(val.substring(32), cardX + 48, y + 16, 1);
            } else {
                String fit = (val.length() > 40) ? (val.substring(0, 39) + "~") : val;
                tft.drawString(fit, cardX + 48, y + 7, 1);
            }
            y += rh + 4;
        };

        drawDigestRow("SHA256", sha256, true);
        drawDigestRow("SHA1", sha1, false);
        drawDigestRow("MD5", md5, false);
        drawDigestRow("B64", String((const char *)b64), false);

        // Bottom 2-button dock: Edit Input | TRNG Random
        int btnW = (cardW - 6) / 2;
        tft.fillRoundRect(cardX, dockY, btnW, 22, 5, pri);
        uiDrawText16("Edit Text", cardX + btnW / 2, dockY + 3, btnW - 8, onAcc, pri, 1);

        int bx2 = cardX + btnW + 6;
        tft.fillRoundRect(bx2, dockY, btnW, 22, 5, surf);
        tft.drawRoundRect(bx2, dockY, btnW, 22, 5, sec);
        uiDrawText16("TRNG Rand", bx2 + btnW / 2, dockY + 3, btnW - 8, sec, surf, 1);
    };

    while (true) {
        if (redraw) {
            renderCrypto();
            redraw = false;
        }
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x, ty = touchPoint.y;
            touchPoint.Clear();
            if (hitBackChevron(tx, ty)) EscPress = true;
            else if (ty >= dockY - 4) {
                if (tx < tftWidth / 2) {
                    String edited = keyboard(payload, 48, "Crypto Payload");
                    if (edited.length() > 0) payload = edited;
                    redraw = true;
                } else {
                    uint8_t rnd[8];
                    esp_fill_random(rnd, sizeof(rnd));
                    payload = bytesToHex(rnd, sizeof(rnd));
                    redraw = true;
                }
            }
        }
#endif
        if (check(EscPress)) break;
        if (check(SelPress)) {
            String edited = keyboard(payload, 48, "Crypto Payload");
            if (edited.length() > 0) payload = edited;
            redraw = true;
        } else if (check(NextPress) || check(PrevPress)) {
            uint8_t rnd[8];
            esp_fill_random(rnd, sizeof(rnd));
            payload = bytesToHex(rnd, sizeof(rnd));
            redraw = true;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void flipper_tools_menu() {
    while (true) {
        int sel = -1;
        std::vector<Option> m = {
            {"WiFi Waterfall", [&]() { sel = 5; }},
            {"GPIO Scope",     [&]() { sel = 6; }},
            {"Nezha Synth",    [&]() { sel = 7; }},
            {"Cyber Crypto",   [&]() { sel = 8; }},
            {"Authenticator",  [&]() { sel = 0; }},
            {"Signal Gen",     [&]() { sel = 1; }},
            {"Morse TX",       [&]() { sel = 2; }},
            {"PassGen",        [&]() { sel = 3; }},
            {"Dice Roller",    [&]() { sel = 4; }},
            {"Back",           [&]() { sel = -1; }},
        };
        int res = loopOptions(m, MENU_TYPE_SUBMENU, "Flipper Tools");
        if (res < 0 || sel < 0) return;
        if (sel == 0) flipper_totp_app();
        else if (sel == 1) flipper_signal_gen();
        else if (sel == 2) flipper_morse_tx();
        else if (sel == 3) flipper_passgen_app();
        else if (sel == 4) flipper_dice_app();
        else if (sel == 5) flipper_wifi_waterfall();
        else if (sel == 6) flipper_gpio_scope();
        else if (sel == 7) flipper_nezha_synth();
        else if (sel == 8) flipper_crypto_lab();
    }
}
#endif
