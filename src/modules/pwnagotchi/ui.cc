/*
Thanks to thoses developers for their projects:
* @7h30th3r0n3 : https://github.com/7h30th3r0n3/Evil-M5Core2 and https://github.com/7h30th3r0n3/PwnGridSpam
* @viniciusbo : https://github.com/viniciusbo/m5-palnagotchi
* @sduenasg : https://github.com/sduenasg/pio_palnagotchi

Thanks to @bmorcelli for his help doing a better code.
*/
#if !defined(LITE_VERSION)
#include "ui.h"
#include "../wifi/sniffer.h"
#include "core/i18n.h"

int32_t display_w;
int32_t display_h;
int32_t canvas_h;
int32_t canvas_center_x;
int32_t canvas_top_h;
int32_t canvas_bot_h;

static inline bool petIsCompact() { return display_h < 160; }

// Draw expressive chibi Nezha Demon Child (哪吒魔童) vector mascot reacting live to mood_id
static void drawNezhaMascot(int cx, int cy, int r, uint8_t moodId, bool broken) {
    uint16_t pri = bruceConfig.priColor;
    const uint16_t kCrimson = 0xE8C4; // Signature Nezha crimson ribbon & lotus fire
    const uint16_t kGold = 0xFE00;    // Qiankun Ring & Wind-Fire Wheel gold
    const uint16_t kHair = 0x18E3;    // Deep obsidian-ink hair
    const uint16_t kHairRim = 0x4208; // Subtle hair outline
    const uint16_t kSkin = 0xFF3A;    // Warm ivory chibi face
    const uint16_t kSmoke = 0x2965;   // Iconic Demon Child smokey eye rim
    const uint16_t kBlush = 0xFB4D;   // Soft cheek blush

    // 1. Hun Tian Ling (混天绫) flowing crimson silk ribbon behind shoulders
    int ribY = cy + r - 2;
    tft.fillRoundRect(cx - r - r / 2, ribY, r * 3, max(4, r / 4), 2, kCrimson);
    tft.fillTriangle(cx - r - r / 2, ribY, cx - r - r * 3 / 4, ribY + r / 3, cx - r, ribY + max(4, r / 4), kCrimson);
    tft.fillTriangle(cx + r + r / 2, ribY, cx + r + r * 3 / 4, ribY + r / 3, cx + r, ribY + max(4, r / 4), kCrimson);

    // Subtle Wind-Fire Wheel (风火轮) gold rings at flanks
    int wfR = max(3, r / 4);
    tft.drawCircle(cx - r - wfR / 2, ribY + wfR + 1, wfR, kGold);
    tft.drawCircle(cx + r + wfR / 2, ribY + wfR + 1, wfR, kGold);

    // 2. Twin Hair Buns (双丸子头) + red silk hair ties
    int bunR = max(5, r * 4 / 9);
    int bunDx = r * 5 / 6;
    int bunDy = r * 4 / 5;
    for (int side = -1; side <= 1; side += 2) {
        int bx = cx + side * bunDx;
        int by = cy - bunDy;
        tft.fillCircle(bx, by, bunR, kHair);
        tft.drawCircle(bx, by, bunR, kHairRim);
        // Red silk tie at base of bun + small ribbon tail
        int tx = cx + side * (bunDx - bunR / 2);
        int ty = cy - bunDy + bunR / 2;
        tft.fillCircle(tx, ty, max(2, bunR / 3), kCrimson);
        tft.drawLine(tx, ty, tx + side * (bunR + 2), ty + 2, kCrimson);
        tft.drawLine(tx, ty + 1, tx + side * (bunR + 2), ty + 3, kCrimson);
    }

    // 3. Head + Dark Hair Crown + Spiky Bangs (刘海)
    tft.fillCircle(cx, cy, r, kSkin);
    tft.fillCircle(cx, cy - r / 5, r - 1, kHair);
    tft.fillCircle(cx, cy + r / 8, r - 2, kSkin);
    // Spiky bangs across top of forehead
    tft.fillTriangle(cx - r * 3 / 4, cy - r / 2, cx - r / 3, cy - r * 7 / 8, cx - r / 4, cy - r / 3, kHair);
    tft.fillTriangle(cx + r * 3 / 4, cy - r / 2, cx + r / 3, cy - r * 7 / 8, cx + r / 4, cy - r / 3, kHair);
    tft.fillTriangle(cx - r / 5, cy - r * 7 / 8, cx + r / 5, cy - r * 7 / 8, cx, cy - r / 2 - 1, kHair);
    tft.drawCircle(cx, cy, r, kHairRim);

    // 4. Qiankun Ring (乾坤圈) golden collar under chin
    tft.fillRoundRect(cx - r / 2, cy + r - 2, r, max(3, r / 6), 2, kGold);

    // 5. Forehead Demon-Child Flame / Lotus Mark (魔丸火印)
    int fmY = cy - r * 2 / 5;
    int fmH = max(3, r / 5);
    int fmW = max(2, r / 8);
    uint16_t markCol = (moodId == 7 || moodId == 8 || moodId == 21) ? pri : kCrimson;
    tft.fillTriangle(cx, fmY - fmH, cx - fmW, fmY, cx + fmW, fmY, markCol);
    tft.fillTriangle(cx, fmY + fmH - 1, cx - fmW, fmY, cx + fmW, fmY, kGold);

    // 6. Mood-driven expressive eyes & mouth
    int eyeDx = max(5, r * 2 / 5);
    int eyeY = cy + r / 12;
    int eyeR = max(3, r / 5);
    int mouthY = cy + r / 2;

    bool isHappy = (moodId == 5 || moodId == 6 || moodId == 9 || moodId == 10 || moodId == 11 || moodId == 13 || moodId == 22);
    bool isFierce = (moodId == 7 || moodId == 8 || moodId == 12 || moodId == 21);
    bool isSad = (moodId == 15 || moodId == 17 || moodId == 18);

    if (broken || moodId == 19 || moodId == 20) {
        // X X eyes
        for (int s = -1; s <= 1; s += 2) {
            int ex = cx + s * eyeDx;
            tft.drawLine(ex - eyeR, eyeY - eyeR, ex + eyeR, eyeY + eyeR, kCrimson);
            tft.drawLine(ex - eyeR, eyeY + eyeR, ex + eyeR, eyeY - eyeR, kCrimson);
        }
        tft.drawFastHLine(cx - eyeR, mouthY, eyeR * 2, kHair);
    } else if (moodId == 0) {
        // Sleeping peaceful eyes + zZ
        for (int s = -1; s <= 1; s += 2) {
            int ex = cx + s * eyeDx;
            tft.drawFastHLine(ex - eyeR, eyeY, eyeR * 2, kHair);
            tft.drawPixel(ex - eyeR, eyeY - 1, kHair);
            tft.drawPixel(ex + eyeR, eyeY - 1, kHair);
        }
        tft.drawCircle(cx, mouthY, max(1, r / 12), kHair);
        tft.setTextSize(1);
        tft.setTextColor(uiTextMutedColor(), uiSurfaceColor());
        tft.drawString("zZ", cx + r + 2, cy - r, 1);
    } else if (moodId == 1 || moodId == 16) {
        // Awakening / Bored half-lidded eyes
        for (int s = -1; s <= 1; s += 2) {
            int ex = cx + s * eyeDx;
            tft.fillRect(ex - eyeR, eyeY - 1, eyeR * 2 + 1, 2, kHair);
        }
        tft.drawFastHLine(cx - eyeR, mouthY, eyeR * 2, kHair);
    } else if (isHappy) {
        // Cheerful smiling crescent ^ ^ eyes + blush + grin
        for (int s = -1; s <= 1; s += 2) {
            int ex = cx + s * eyeDx;
            tft.drawLine(ex - eyeR, eyeY + 1, ex, eyeY - eyeR + 1, kHair);
            tft.drawLine(ex, eyeY - eyeR + 1, ex + eyeR, eyeY + 1, kHair);
            tft.drawLine(ex - eyeR, eyeY + 2, ex, eyeY - eyeR + 2, kHair);
            tft.drawLine(ex, eyeY - eyeR + 2, ex + eyeR, eyeY + 2, kHair);
            tft.fillCircle(ex + s * 2, eyeY + eyeR + 2, max(1, r / 10), kBlush);
        }
        // Mischievous grin
        tft.drawLine(cx - eyeR, mouthY - 1, cx, mouthY + 2, kHair);
        tft.drawLine(cx, mouthY + 2, cx + eyeR, mouthY - 1, kHair);
    } else {
        // Iconic Nezha Demon Child smokey eyes (fierce / observing / sad)
        int lookDx = (moodId == 3) ? 1 : ((moodId == 4) ? -1 : 0);
        uint16_t pupilCol = isFierce ? kCrimson : (isSad ? 0x5AEB : kHair);
        for (int s = -1; s <= 1; s += 2) {
            int ex = cx + s * eyeDx;
            tft.fillCircle(ex, eyeY, eyeR + 1, kSmoke);
            tft.fillCircle(ex, eyeY, eyeR, TFT_WHITE);
            tft.fillCircle(ex + lookDx, eyeY, max(2, eyeR * 2 / 3), pupilCol);
            tft.drawPixel(ex + lookDx - 1, eyeY - 1, TFT_WHITE); // eye highlight glint
            if (isFierce) {
                // Sharp slanted battle brows
                tft.drawLine(ex - s * eyeR, eyeY - eyeR - 1, ex + s * (eyeR + 1), eyeY - eyeR - 3, kHair);
                tft.drawLine(ex - s * eyeR, eyeY - eyeR, ex + s * (eyeR + 1), eyeY - eyeR - 2, kHair);
            } else if (isSad) {
                tft.drawLine(ex - s * eyeR, eyeY - eyeR - 2, ex + s * eyeR, eyeY - eyeR, kHair);
            }
        }
        if (isFierce) {
            // Confident smirk
            tft.drawLine(cx - eyeR, mouthY + 1, cx + eyeR, mouthY - 1, kHair);
        } else if (isSad) {
            tft.drawLine(cx - eyeR, mouthY + 2, cx, mouthY, kHair);
            tft.drawLine(cx, mouthY, cx + eyeR, mouthY + 2, kHair);
        } else {
            tft.drawFastHLine(cx - eyeR + 1, mouthY, eyeR * 2 - 2, kHair);
        }
    }
}

void initUi() {
    invalidateStatusBarCache();
    display_w = tftWidth;
    display_h = tftHeight;
    canvas_top_h = petIsCompact() ? 19 : 24;
    canvas_bot_h = display_h - (petIsCompact() ? 18 : 22);
    canvas_h = canvas_bot_h - canvas_top_h;
    canvas_center_x = display_w / 2;

    tft.setTextSize(FP);
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

String getRssiBars(signed int rssi) {
    if (rssi == -1000) return "";
    if (rssi >= -67) return "[####]";
    if (rssi >= -70) return "[### ]";
    if (rssi >= -80) return "[##  ]";
    return "[#   ]";
}

void drawTopCanvas() {
    uint16_t barBg = (bruceConfig.bgColor == 0x0000) ? 0x0841 : uiSurfaceColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t pri = bruceConfig.priColor;

    tft.fillRect(0, 0, display_w, canvas_top_h, barBg);
    tft.drawFastHLine(0, canvas_top_h - 1, display_w, bdim);

    int x = 6;
#if defined(HAS_TOUCH)
    int cy = (canvas_top_h - 1) / 2;
    tft.drawLine(12, cy - 4, 7, cy, uiTextMutedColor());
    tft.drawLine(11, cy - 4, 6, cy, uiTextMutedColor());
    tft.drawLine(7, cy, 12, cy + 4, uiTextMutedColor());
    tft.drawLine(6, cy, 11, cy + 4, uiTextMutedColor());
    x = 20;
#endif
    tft.fillCircle(x + 2, (canvas_top_h - 1) / 2, 2, pri);
    x += 8;

    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);
    tft.setTextColor(bright, barBg);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "NEZHA CH%02d HS:%d", ch, num_HS);
    tft.drawString(buffer, x, (canvas_top_h - 1 - LH * FP) / 2, 1);
}

void drawTime() {
    uint16_t barBg = (bruceConfig.bgColor == 0x0000) ? 0x0841 : uiSurfaceColor();
    unsigned long ellapsed = millis() / 1000;
    int8_t h = ellapsed / 3600;
    int sr = ellapsed % 3600;
    int8_t m = sr / 60;
    int8_t s = sr % 60;
    char right_str[32];
    snprintf(right_str, sizeof(right_str), "%d%% %02d:%02d:%02d", getBattery(), h, m, s);

    int clearW = 98;
    tft.fillRect(display_w - clearW, 0, clearW, canvas_top_h - 1, barBg);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), barBg);
    tft.setTextDatum(TL_DATUM);
    tft.drawRightString(right_str, display_w - 6, (canvas_top_h - 1 - LH * FP) / 2, 1);
}

void drawBottomCanvas() {
    uint16_t barBg = (bruceConfig.bgColor == 0x0000) ? 0x0841 : uiSurfaceColor();
    int botH = display_h - canvas_bot_h;
    tft.fillRect(0, canvas_bot_h, display_w, botH, barBg);
    tft.drawFastHLine(0, canvas_bot_h, display_w, uiBorderDimColor());

    tft.setTextSize(FP);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(bruceConfig.priColor, barBg);
    String badge = bruce_i18n::isChinese() ? "哪吒AI" : "NEZHA AI";
    int bh = (FP == 1 && bruce_i18n::containsNonAscii(badge)) ? 12 : (LH * FP);
    tft.drawRightString(badge, display_w - 6, canvas_bot_h + (botH - bh) / 2, 1);
}

void drawFooterData(uint8_t friends_run, uint8_t friends_tot, const String &last_friend_name, signed int rssi) {
    uint16_t barBg = (bruceConfig.bgColor == 0x0000) ? 0x0841 : uiSurfaceColor();
    int botH = display_h - canvas_bot_h;
    int clearW = display_w - 64;
    tft.fillRect(0, canvas_bot_h + 1, clearW, botH - 1, barBg);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), barBg);
    tft.setTextDatum(TL_DATUM);

    String stats = "PEERS 0 (0)";
    if (friends_run > 0) {
        stats = "PEERS " + String(friends_run) + "(" + String(friends_tot) + ") " +
                last_friend_name.substring(0, 10) + " " + getRssiBars(rssi);
    }
    tft.drawString(
        bruce_i18n::utf8TruncateToWidth(stats, clearW - 10, FP), 6, canvas_bot_h + (botH - LH * FP) / 2, 1
    );
}

void drawMood(const String &face, const String &phrase, bool broken) {
    uint16_t bg = bruceConfig.bgColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t surfHi = uiSurfaceHiColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();
    bool compact = petIsCompact();

    int padX = compact ? 6 : 10;
    int padY = compact ? 3 : 6;
    int cardX = padX;
    int cardY = canvas_top_h + padY;
    int cardW = display_w - 2 * padX;
    int cardH = canvas_bot_h - canvas_top_h - 2 * padY;

    tft.fillRect(0, canvas_top_h, display_w, canvas_bot_h - canvas_top_h, bg);
    tft.fillRoundRect(cardX, cardY, cardW, cardH, compact ? 6 : 9, surf);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, compact ? 6 : 9, broken ? TFT_RED : bdim);

    uint8_t moodId = getCurrentMoodId();
    tft.setTextDatum(TL_DATUM);

    if (compact) {
        // Compact 240x135 side-by-side: Chibi Nezha on left, kaomoji & speech on right
        int mascotCx = cardX + 36;
        int mascotCy = cardY + cardH / 2 + 2;
        drawNezhaMascot(mascotCx, mascotCy, 15, moodId, broken);

        int textX = cardX + 74;
        int textW = cardW - 82;
        uiDrawText16(face, textX, cardY + 10, textW, broken ? TFT_RED : pri, surf, 0);

        String trPhrase = bruce_i18n::tr(phrase);
        std::vector<String> lines = bruce_i18n::utf8WrapToWidth(trPhrase, textW, FP);
        tft.setTextSize(FP);
        tft.setTextColor(bright, surf);
        int py = cardY + 34;
        for (size_t i = 0; i < lines.size() && i < 3; i++) {
            tft.drawString(lines[i], textX, py + (int)i * (LH * FP + 4), 1);
        }
    } else {
        // Spacious 320x240 centered portrait stage: Chibi Nezha + kaomoji pill + speech caption
        int mascotCy = cardY + 56;
        drawNezhaMascot(canvas_center_x, mascotCy, 23, moodId, broken);

        int pillW = 92;
        int pillH = 22;
        int pillX = canvas_center_x - pillW / 2;
        int pillY = cardY + 102;
        tft.fillRoundRect(pillX, pillY, pillW, pillH, 6, surfHi);
        uiDrawText16(face, canvas_center_x, pillY + 3, pillW - 10, broken ? TFT_RED : pri, surfHi, 1);

        String trPhrase = bruce_i18n::tr(phrase);
        int maxW = cardW - 24;
        if (uiMeasureText16(trPhrase) <= maxW) {
            uiDrawText16(trPhrase, canvas_center_x, cardY + cardH - 36, maxW, bright, surf, 1);
        } else {
            std::vector<String> lines = bruce_i18n::utf8WrapToWidth(trPhrase, maxW, FP);
            tft.setTextSize(FP);
            tft.setTextColor(bright, surf);
            int startY = cardY + cardH - (int)min((size_t)2, lines.size()) * (LH * FP + 4) - 10;
            for (size_t i = 0; i < lines.size() && i < 2; i++) {
                tft.drawCentreString(lines[i], canvas_center_x, startY + (int)i * (LH * FP + 4), 1);
            }
        }

        // Subtle muted interaction hint at very bottom inside card
        tft.setTextSize(FP);
        tft.setTextColor(muted, surf);
        tft.drawCentreString(bruce_i18n::tr("OK to show menu"), canvas_center_x, cardY + cardH - 13, 1);
    }
}

void updateUi(bool show_toolbars) {
    String mood_face = getCurrentMoodFace();
    String mood_phrase = getCurrentMoodPhrase();
    bool mood_broken = isCurrentMoodBroken();

    if (show_toolbars) {
        drawTopCanvas();
        drawTime();
        drawBottomCanvas();
        drawFooterData(
            getPwngridRunTotalPeers(),
            getPwngridTotalPeers(),
            getPwngridLastFriendName(),
            getPwngridClosestRssi()
        );
    }

    drawMood(mood_face, mood_phrase, mood_broken);

#if defined(HAS_TOUCH)
    TouchFooter();
#endif
}
#endif
