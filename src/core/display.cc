#include "display.h"
#include "core/logging/nzlog.h"
#include "hal/power/gauge.h"
#include "i18n.h"
#include "core/wifi/webInterface.h" // for server
#include "core/wifi/wg.h"           //for isConnectedWireguard to print wireguard lock
#include "mykeyboard.h"
#include "settings.h" //for timeStr
#include "utils.h"
#include <JPEGDecoder.h>
#include <interface.h> //for charging ischarging to print charging indicator
#include <memory>
#include <new>

#define MAX_MENU_SIZE (int)(tftHeight / 25)

uint8_t mainMenuGridColumns = 0;
bool (*gridPageTapHandler)(int x, int y, int currentIndex, int &newIndex) = nullptr;

#if defined(HAS_TOUCH)
static bool s_pageUpVisible = false, s_pageDownVisible = false;
static int s_pageUpX = 0, s_pageUpY = 0, s_pageUpW = 0, s_pageUpH = 0;
static int s_pageDownX = 0, s_pageDownY = 0, s_pageDownW = 0, s_pageDownH = 0;
static int s_pageUpTargetIndex = 0, s_pageDownTargetIndex = 0;
#endif

// Send the ST7789 into or out of sleep mode
void panelSleep(bool on) {
#if defined(ST7789_2_DRIVER) || defined(ST7789_DRIVER)
    if (on) {
        tft.writecommand(0x10); // SLPIN: panel off
        delay(5);
    } else {
        tft.writecommand(0x11); // SLPOUT: panel on
        delay(120);
    }
#endif
    // Disables tft writings on the display
    tft.setSleepMode(on);
}

// Boards without their own isCharging() fall back to the HAL (gauge IC or analog divider heuristic)
bool __attribute__((weak)) isCharging() { return hal_gauge_is_charging(); }
/***************************************************************************************
** Function name: displayScrollingText
** Description:   Scroll large texts into screen
***************************************************************************************/
void displayScrollingText(const String &text, Opt_Coord &coord, bool highlight) {
    String trText = bruce_i18n::tr(text);
    bool useFont2 = false;
#if defined(LOAD_FONT2)
    useFont2 = !bruce_i18n::containsNonAscii(trText);
#endif
    uint8_t ts = useFont2 ? 1 : FM;
    int16_t boxPx = coord.size; // drawOptions now stores exact pixel width in coord.size
    if (boxPx <= 16) return;
    int16_t textPx = uiMeasureText16(trText);
    static int i = 0;
    static long _lastmillis = 0;
    static String lastText = "";
    static uint16_t lastY = 0;
    if (trText != lastText || coord.y != lastY) {
        lastText = trText;
        lastY = coord.y;
        i = 0;
        _lastmillis = millis() + 800;
    }
    uint16_t fg = highlight ? coord.bgcolor : coord.fgcolor;
    uint16_t bg = highlight ? coord.fgcolor : coord.bgcolor;
    if (textPx <= boxPx) return;
    if (millis() > _lastmillis + 160) {
        String displayText = trText + "    ";
        const char *base = displayText.c_str();
        const char *p = base;
        int cpCount = 0;
        const char *startPtr = base;
        while (*p) {
            if (cpCount == i) startPtr = p;
            bruce_i18n::utf8NextCodepoint(p);
            ++cpCount;
        }
        if (i >= cpCount) {
            i = 0;
            startPtr = base;
        }
        String tail = displayText.substring(startPtr - base);
        tft.fillRect(coord.x, coord.y, boxPx, 16, bg);
        uiDrawText16(tail, coord.x, coord.y, boxPx, fg, bg, 0);
        if (i >= cpCount - 2) i = -1;
        _lastmillis = millis();
        i++;
        if (i == 1) _lastmillis = millis() + 900;
    }
    (void)ts;
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter(uint16_t color) {
#if defined(HAS_TOUCH) && !defined(NO_TOUCH_FOOTER)
    int fx = BORDER_OFFSET_FROM_SCREEN_EDGE;
    int fy = tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 3;
    int fw = tftWidth - 2 * BORDER_OFFSET_FROM_SCREEN_EDGE;
    int fh = TOUCH_FOOTER_HEIGHT - BORDER_OFFSET_FROM_SCREEN_EDGE + 3;
    uint16_t surf = uiSurfaceColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t fg = uiTextBrightColor();
    tft.fillRoundRect(fx, fy, fw, fh, FP + 4, surf);
    tft.drawRoundRect(fx, fy, fw, fh, FP + 4, bdim);
    tft.drawFastHLine(fx + 6, fy + 1, fw - 12, color);
    tft.drawFastVLine(tftWidth / 3, fy + 4, fh - 8, bdim);
    tft.drawFastVLine(2 * tftWidth / 3, fy + 4, fh - 8, bdim);
    tft.setTextColor(fg, surf);
    tft.setTextSize(FM);
    int ty = fy + max(1, (fh - LH * FM) / 2);
    tft.drawCentreString(bruce_i18n::tr("PREV"), tftWidth / 6, ty, 1);
    tft.drawCentreString(bruce_i18n::tr("SEL"), tftWidth / 2, ty, 1);
    tft.drawCentreString(bruce_i18n::tr("NEXT"), 5 * tftWidth / 6, ty, 1);
#else
    (void)color;
#endif
}
/***************************************************************************************
** Function name: MegaFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void MegaFooter(uint16_t color) {
#if defined(HAS_TOUCH) && !defined(NO_TOUCH_FOOTER)
    int fx = BORDER_OFFSET_FROM_SCREEN_EDGE;
    int fy = tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 3;
    int fw = tftWidth - 2 * BORDER_OFFSET_FROM_SCREEN_EDGE;
    int fh = TOUCH_FOOTER_HEIGHT - BORDER_OFFSET_FROM_SCREEN_EDGE + 3;
    uint16_t surf = uiSurfaceColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t fg = uiTextBrightColor();
    tft.fillRoundRect(fx, fy, fw, fh, FP + 4, surf);
    tft.drawRoundRect(fx, fy, fw, fh, FP + 4, bdim);
    tft.drawFastHLine(fx + 6, fy + 1, fw - 12, color);
    tft.drawFastVLine(tftWidth / 3, fy + 4, fh - 8, bdim);
    tft.drawFastVLine(2 * tftWidth / 3, fy + 4, fh - 8, bdim);
    tft.setTextColor(fg, surf);
    tft.setTextSize(FM);
    int ty = fy + max(1, (fh - LH * FM) / 2);
    tft.drawCentreString(bruce_i18n::tr("Exit"), tftWidth / 6, ty, 1);
    tft.drawCentreString(bruce_i18n::tr("UP"), tftWidth / 2, ty, 1);
    tft.drawCentreString(bruce_i18n::tr("DOWN"), 5 * tftWidth / 6, ty, 1);
#else
    (void)color;
#endif
}

/***************************************************************************************
** Function name: resetTftDisplay
** Description:   set cursor to 0,0, screen and text to default color
***************************************************************************************/
void resetTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg, uint16_t screen) {
    invalidateStatusBarCache();
    tft.setCursor(x, y);
    tft.fillScreen(screen);
    tft.setTextSize(size);
    tft.setTextColor(fc, bg);
    tft.setTextDatum(0);
}

/***************************************************************************************
** Function name: setTftDisplay
** Description:   set cursor, font color, size and bg font color
***************************************************************************************/
void setTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg) {
    if (x >= 0 && y < 0) tft.setCursor(x, tft.getCursorY());      // if -1 on x, sets only y
    else if (x < 0 && y >= 0) tft.setCursor(tft.getCursorX(), y); // if -1 on y, sets only x
    else if (x >= 0 && y >= 0) tft.setCursor(x, y);               // if x and y > 0, sets both
    tft.setTextSize(size);
    tft.setTextColor(fc, bg);
}

void turnOffDisplay() { setBrightness(0, false); }

bool wakeUpScreen() {
    previousMillis = millis();
    if (isScreenOff) {
        isScreenOff = false;
        dimmer = false;
        getBrightness();
        vTaskDelay(pdMS_TO_TICKS(200));
        return true;
    } else if (dimmer) {
        dimmer = false;
        getBrightness();
        vTaskDelay(pdMS_TO_TICKS(200));
        return true;
    }
    return false;
}

/***************************************************************************************
** Function name: wrapText
** Description:   Wrap text to fit within a maximum width, returning vector of lines
***************************************************************************************/
std::vector<String> wrapText(const String &text, int maxCharsPerLine) {
    std::vector<String> lines;
    if (maxCharsPerLine <= 0) return lines;
    if (bruce_i18n::containsNonAscii(text)) {
        return bruce_i18n::utf8WrapToWidth(text, maxCharsPerLine * LW, 1);
    }

    String remaining = text;
    while (remaining.length() > 0) {
        if (remaining.length() <= maxCharsPerLine) {
            lines.push_back(remaining);
            break;
        }
        // Find last space within maxCharsPerLine
        int splitPos = -1;
        for (int i = maxCharsPerLine - 1; i >= 0; i--) {
            if (remaining[i] == ' ' || remaining[i] == '-' || remaining[i] == '_') {
                splitPos = i;
                break;
            }
        }
        if (splitPos <= 0) {
            // No word boundary found, force split at max
            splitPos = maxCharsPerLine;
        }
        lines.push_back(remaining.substring(0, splitPos));
        remaining = remaining.substring(splitPos + 1);
    }
    return lines;
}

/***************************************************************************************
** Function name: displayRedStripe
** Description:   Display Red Stripe with information (supports multi-line text wrapping)
***************************************************************************************/
void displayRedStripe(const String &text, uint16_t fgcolor, uint16_t bgcolor) {
    if (fgcolor == bgcolor && fgcolor == TFT_WHITE) fgcolor = TFT_BLACK;

    String displayText = bruce_i18n::tr(text);
    int boxX = (tftWidth >= 280) ? 18 : 10;
    int boxW = tftWidth - 2 * boxX;
    int maxBoxPx = boxW - 24;

    bool singleLine16 = (uiMeasureText16(displayText) <= maxBoxPx);
    std::vector<String> wrappedLines;
    if (singleLine16) {
        wrappedLines.push_back(displayText);
    } else {
        wrappedLines = bruce_i18n::utf8WrapToWidth(displayText, maxBoxPx, FP);
        if (wrappedLines.empty()) wrappedLines.push_back(displayText);
    }

    int lineHeight = singleLine16 ? 18 : ((FP == 1 && bruce_i18n::containsNonAscii(displayText)) ? 14 : (LH * FP + 3));
    int boxHeight = (int)wrappedLines.size() * lineHeight + 18;
    int boxY = (tftHeight - boxHeight) / 2;

    // Clean elevated neutral sheet with crisp semantic hairline & status dot (no cliché side stripe)
    uint16_t cardBg = uiSurfaceColor();
    uint16_t textCol = uiTextBrightColor();
    tft.drawPixel(0, 0, 0);
    tft.fillRoundRect(boxX, boxY, boxW, boxHeight, 8, cardBg);
    tft.drawRoundRect(boxX, boxY, boxW, boxHeight, 8, bgcolor);
    tft.fillCircle(boxX + 10, boxY + boxHeight / 2, 3, bgcolor);

    int textCenterX = boxX + 8 + (boxW - 12) / 2;
    int startY = boxY + (boxHeight - (int)wrappedLines.size() * lineHeight) / 2 + (singleLine16 ? 1 : 0);
    if (singleLine16) {
        uiDrawText16(wrappedLines[0], textCenterX, startY, maxBoxPx, textCol, cardBg, 1);
    } else {
        tft.setTextColor(textCol, cardBg);
        tft.setTextSize(FP);
        for (size_t i = 0; i < wrappedLines.size(); i++) {
            tft.drawCentreString(wrappedLines[i], textCenterX, startY + i * lineHeight);
        }
    }
}

void drawButton(
    int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, const char *text, bool inverted = false
) {
    uint16_t accent = static_cast<uint16_t>(color);
    uint16_t fillCol = inverted ? accent : uiSurfaceHiColor();
    uint16_t fgCol = inverted ? uiTextOnAccentColor(accent) : uiTextBrightColor();

    tft.fillRoundRect(x, y, w, h, 6, fillCol);
    if (!inverted) {
        tft.drawRoundRect(x, y, w, h, 6, uiBorderDimColor());
    }

    String label = bruce_i18n::tr(text);
    if (h >= 20) {
        uiDrawText16(label, x + w / 2, y + (h - 16) / 2, w - 8, fgCol, fillCol, 1);
    } else {
        uint8_t oldDatum = tft.getTextDatum();
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(FP);
        tft.setTextColor(fgCol, fillCol);
        int th = (FP == 1 && bruce_i18n::containsNonAscii(label)) ? 12 : (LH * FP);
        tft.drawCentreString(bruce_i18n::utf8TruncateToWidth(label, w - 6, FP), x + w / 2, y + (h - th) / 2, 1);
        tft.setTextDatum(oldDatum);
    }
}

int8_t displayMessage(
    const char *message, const char *leftButton, const char *centerButton, const char *rightButton,
    uint16_t color
) {
    uint8_t oldTextDatum = tft.getTextDatum();
    bool prevTouchZone = touchZoneOutsideFooterEnabled;
#if defined(HAS_TOUCH)
    touchZoneOutsideFooterEnabled = false;
#endif

    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 16;
    int cardW = tftWidth - 2 * cardX;
    uint16_t surf = uiSurfaceColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t bdim = uiBorderDimColor();

    String msg = bruce_i18n::tr(String(message));
    std::vector<String> lines;
    int start = 0;
    while (start <= (int)msg.length()) {
        int end = msg.indexOf('\n', start);
        if (end == -1) {
            lines.push_back(msg.substring(start));
            break;
        }
        lines.push_back(msg.substring(start, end));
        start = end + 1;
    }

    int lineH = compact ? 14 : 20;
    int btnH = compact ? 20 : 26;
    int padTop = compact ? 10 : 14;
    int padGap = compact ? 8 : 14;
    int padBot = compact ? 8 : 12;
    int cardH = padTop + (int)lines.size() * lineH + padGap + btnH + padBot;
    int maxCardH = tftHeight - STATUS_BAR_HEIGHT - 8;
    if (cardH > maxCardH) cardH = maxCardH;
    int cardY = max((int)STATUS_BAR_HEIGHT, (tftHeight - cardH) / 2);

    tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, surf);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, bdim);
    if (bruceConfig.nezhaThemeStyle > 0) {
        drawNezhaRibbonRule(cardX + 10, cardY + 2, cardW - 20);
    }

    int textY = cardY + padTop;
    for (size_t i = 0; i < lines.size(); i++) {
        if (!compact) {
            uiDrawText16(lines[i], cardX + cardW / 2, textY, cardW - 20, bright, surf, 1);
        } else {
            tft.setTextDatum(TL_DATUM);
            tft.setTextSize(FP);
            tft.setTextColor(bright, surf);
            tft.drawCentreString(bruce_i18n::utf8TruncateToWidth(lines[i], cardW - 14, FP), cardX + cardW / 2, textY, 1);
        }
        textY += lineH;
    }

    const char *btnLabels[3];
    int8_t totalButtons = 0;
    if (leftButton) btnLabels[totalButtons++] = leftButton;
    if (centerButton) btnLabels[totalButtons++] = centerButton;
    if (rightButton) btnLabels[totalButtons++] = rightButton;
    if (totalButtons == 0) {
        tft.setTextDatum(oldTextDatum);
        touchZoneOutsideFooterEnabled = prevTouchZone;
        return 0;
    }

    int btnGap = 8;
    int innerW = cardW - 20;
    int btnW = (innerW - btnGap * (totalButtons - 1)) / totalButtons;
    int btnY = cardY + cardH - btnH - padBot;
    int btnX0 = cardX + 10;

    int8_t selected = 0;
    bool redraw = true;

    while (true) {
#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x;
            int ty = touchPoint.y;
            touchPoint.pressed = false;
            if (ty >= btnY - 4 && ty <= btnY + btnH + 4) {
                for (int8_t i = 0; i < totalButtons; i++) {
                    int bx = btnX0 + i * (btnW + btnGap);
                    if (tx >= bx && tx < bx + btnW) {
                        selected = i;
                        SelPress = true;
                        break;
                    }
                }
            }
        }
#endif
        if (check(PrevPress) || check(UpPress)) {
            selected = (selected - 1 + totalButtons) % totalButtons;
            redraw = true;
        }
        if (check(NextPress) || check(DownPress)) {
            selected = (selected + 1) % totalButtons;
            redraw = true;
        }
        if (check(EscPress)) {
            selected = totalButtons - 1;
            break;
        }
        if (check(SelPress)) { break; }

        if (redraw) {
            for (int8_t i = 0; i < totalButtons; i++) {
                int bx = btnX0 + i * (btnW + btnGap);
                drawButton(bx, btnY, btnW, btnH, color, btnLabels[i], selected == i);
            }
            redraw = false;
        }
        delay(10);
    }

    tft.setTextDatum(oldTextDatum);
    touchZoneOutsideFooterEnabled = prevTouchZone;
    return selected;
}

void displayError(const String &txt, bool waitKeyPress) {
    NZLOGE("UI", "%s", txt.c_str());
    displayRedStripe(txt);
    Serial.println("ERR: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displayWarning(const String &txt, bool waitKeyPress) {
    NZLOGW("UI", "%s", txt.c_str());
    displayRedStripe(txt, TFT_BLACK, TFT_YELLOW);
    Serial.println("WARN: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displayInfo(const String &txt, bool waitKeyPress) {
    NZLOGI("UI", "%s", txt.c_str());
    displayRedStripe(txt, TFT_WHITE, TFT_BLUE);
    Serial.println("INFO: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displaySuccess(const String &txt, bool waitKeyPress) {
    NZLOGI("UI", "%s", txt.c_str());
    displayRedStripe(txt, TFT_WHITE, TFT_DARKGREEN);
    Serial.println("SUCCESS: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displayTextLine(const String &txt, bool waitKeyPress) {
    NZLOGD("UI", "%s", txt.c_str());
    displayRedStripe(txt, getComplementaryColor2(bruceConfig.priColor), bruceConfig.priColor);
    Serial.println("MESSAGE: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void setPadCursor(int16_t padx, int16_t pady) {
    for (int y = 0; y < pady; y++) tft.println();
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
}

void padprintf(int16_t padx, const char *format, ...) {
    char buffer[64];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.printf("%s", buffer);
}
void padprintf(const char *format, ...) {
    char buffer[64];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    tft.setCursor(BORDER_PAD_X, tft.getCursorY());
    tft.printf("%s", buffer);
}

void padprint(const String &s, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(bruce_i18n::tr(s));
}
void padprint(const char str[], int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(bruce_i18n::tr(String(str)));
}
void padprint(char c, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(c);
}
void padprint(unsigned char b, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(b, base);
}
void padprint(int n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, base);
}
void padprint(unsigned int n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, base);
}
void padprint(long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, base);
}
void padprint(unsigned long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, base);
}
void padprint(long long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, base);
}
void padprint(unsigned long long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, base);
}
void padprint(double n, int digits, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.print(n, digits);
}

void padprintln(const String &s, int16_t padx) {
    if (s.isEmpty()) {
        tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
        tft.println(s);
        return;
    }

    String trS = bruce_i18n::tr(s);
    if (bruce_i18n::containsNonAscii(trS)) {
        int16_t maxPx = tftWidth - (padx + 1) * BORDER_PAD_X;
        std::vector<String> lines = bruce_i18n::utf8WrapToWidth(trS, maxPx, tft.getTextSize());
        for (const auto &line : lines) {
            tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
            tft.println(line);
        }
        return;
    }

    String buff;
    size_t start = 0;
    int _maxCharsInLine = (tftWidth - (padx + 1) * BORDER_PAD_X) / (FP * LW);

    // automatically split into multiple lines
    while (!(buff = trS.substring(start, start + _maxCharsInLine)).isEmpty()) {
        tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
        tft.println(buff);
        start += buff.length();
    }
}
void padprintln(const char str[], int16_t padx) {
    padprintln(String(str), padx);
}
void padprintln(char c, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(c);
}
void padprintln(unsigned char b, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(b, base);
}
void padprintln(int n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, base);
}
void padprintln(unsigned int n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, base);
}
void padprintln(long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, base);
}
void padprintln(unsigned long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, base);
}
void padprintln(long long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, base);
}
void padprintln(unsigned long long n, int base, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, base);
}
void padprintln(double n, int digits, int16_t padx) {
    tft.setCursor(padx * BORDER_PAD_X, tft.getCursorY());
    tft.println(n, digits);
}

/*********************************************************************
**  Function: loopOptions
**  Where you choose among the options in menu
**********************************************************************/
static bool s_submenuForceFull = true;

int loopOptions(
    std::vector<Option> &options, uint8_t menuType, const char *subText, int index, bool interpreter,
    bool letterShortcuts, uint16_t pageJumpSize, bool border, bool compact
) {
    if (options.empty()) return -1;

    // Some legacy menus implement "Back" by calling their parent menu function
    // recursively, which nests loopOptions() frames. Cap the nesting so a long
    // Back/forward session cannot overflow the loop task stack.
    static uint8_t s_loopDepth = 0;
    struct LoopDepthGuard {
        LoopDepthGuard() { ++s_loopDepth; }
        ~LoopDepthGuard() { --s_loopDepth; }
    } loopDepthGuard;
    if (s_loopDepth > 12) {
        NZLOGW("MENU", "menu recursion depth exceeded (%u); unwinding", (unsigned)s_loopDepth);
        return -1;
    }

    s_submenuForceFull = true;

    bool useTapToSelect = menuType == MENU_TYPE_REGULAR || menuType == MENU_TYPE_SUBMENU ||
                          (bruceConfig.mainMenuStyle != MAIN_MENU_CAROUSEL && menuType == MENU_TYPE_MAIN);

    bool prevTouchZoneOutsideFooterEnabled = touchZoneOutsideFooterEnabled;
    if (useTapToSelect) touchZoneOutsideFooterEnabled = false;

    auto findFirstEnabled = [&]() -> int {
        for (size_t i = 0; i < options.size(); i++) {
            if (options[i].enabled) return static_cast<int>(i);
        }
        return -1;
    };

    auto findNextEnabled = [&](int start, int step) -> int {
        if (options.empty()) return -1;
        int size = static_cast<int>(options.size());
        int idx = start;
        for (int i = 0; i < size; i++) {
            idx = (idx + step + size) % size;
            if (options[idx].enabled) return idx;
        }
        return -1;
    };

    if (index < 0 || index >= static_cast<int>(options.size())) index = 0;
    if (!options[index].enabled) {
        int firstEnabled = findFirstEnabled();
        if (firstEnabled < 0) return -1;
        index = firstEnabled;
    }

    Opt_Coord coord;
    bool redraw = true;
    bool exit = false;
    int devModeCounter = 0;
    static unsigned long _clock_bat_timer = millis();
    if (index >= static_cast<int>(options.size())) index = 0;
    bool firstRender = true;
    unsigned long menuOpenTs =
        0; // timestamp when this menu was first rendered (per-invocation, not shared across nested menus)
#if defined(HAS_TOUCH)
    // Integrated top-left "<" back button zone inside the menu header (replaces floating [X] box)
    bool showEscButton = useTapToSelect && (menuType == MENU_TYPE_REGULAR || menuType == MENU_TYPE_SUBMENU);
    const int escX = 4;
    const int escY = STATUS_BAR_HEIGHT - 3;
    const int escW = 36;
    const int escH = 22;
#endif
    if (!compact) drawMainBorder();
    while (1) {
        // Check for shutdown before drawing menu to avoid drawing a black bar on the screen
        if (exit) break;
        if (menuType == MENU_TYPE_MAIN) {
            checkReboot();
            if (devModeCounter >= 5 && !bruceConfig.devMode) {
                bruceConfig.setDevMode(true);
                displayInfo("Dev Mode Enabled", true);
            }
            if (millis() - _clock_bat_timer > 30000) {
                _clock_bat_timer = millis();
                drawStatusBar(); // update clock and battery status each 30s
            }
        }

        if (redraw) {
            menuOptionType = menuType; // updates menutype to the remote controller
            menuOptionLabel = subText;
            // update the hovered
            for (auto &opt : options) opt.hovered = false;
            options[index].hovered = true;

            bool renderedByLambda = false;
            if (options[index].hover)
                renderedByLambda = options[index].hover(options[index].hoverPointer, true);

            if (!renderedByLambda) {
                if (menuType == MENU_TYPE_SUBMENU) drawSubmenu(index, options, subText);
                else
                    coord = drawOptions(
                        index,
                        options,
                        bruceConfig.priColor,
                        bruceConfig.secColor,
                        bruceConfig.bgColor,
                        firstRender,
                        border,
                        compact
                    );
            }
            if (firstRender) menuOpenTs = millis();
            firstRender = false;
            redraw = false;
        }

#if defined(HAS_TOUCH)
        // Tap the "[ x ]" close button to fire EscPress, same as the top-left zone used to.
        if (showEscButton && touchPoint.pressed && touchPoint.x >= escX && touchPoint.x < escX + escW &&
            touchPoint.y >= escY && touchPoint.y < escY + escH) {
            EscPress = true;
            touchPoint.pressed = false;
        }
        // Main-menu custom tap handler (page-up/down strip in Grid/List/Split/Compact, or hero/preview launch in Dock/Split)
        if (useTapToSelect && touchPoint.pressed && menuType == MENU_TYPE_MAIN && gridPageTapHandler) {
            int newIndex = index;
            if (gridPageTapHandler(touchPoint.x, touchPoint.y, index, newIndex)) {
                if (newIndex == index) {
                    SelPress = true;
                } else {
                    index = newIndex;
                    redraw = true;
                }
                touchPoint.pressed = false;
            }
        }
        if (touchPoint.pressed) {
            if (s_pageUpVisible && touchPoint.x >= s_pageUpX && touchPoint.x < s_pageUpX + s_pageUpW &&
                touchPoint.y >= s_pageUpY && touchPoint.y < s_pageUpY + s_pageUpH) {
                index = s_pageUpTargetIndex;
                redraw = true;
                touchPoint.pressed = false;
            } else if (
                s_pageDownVisible && touchPoint.x >= s_pageDownX &&
                touchPoint.x < s_pageDownX + s_pageDownW && touchPoint.y >= s_pageDownY &&
                touchPoint.y < s_pageDownY + s_pageDownH
            ) {
                index = s_pageDownTargetIndex;
                redraw = true;
                touchPoint.pressed = false;
            }
        }
        // Tap-to-select: a tap on a different item just selects it (redraw); a second tap on the
        // item that's already selected fires SelPress to execute it, same as the physical button.
        if (useTapToSelect && touchPoint.pressed) {
            for (size_t i = 0; i < options.size(); i++) {
                if (options[i].enabled && options[i].contain(touchPoint.x, touchPoint.y)) {
                    if (static_cast<int>(i) == index) SelPress = true;
                    else {
                        index = static_cast<int>(i);
                        redraw = true;
                    }
                    break;
                }
            }
            touchPoint.pressed = false;
        }
#endif

        // handleSerialCommands(); // always use serial task for it
#ifdef HAS_KEYBOARD
        if (menuType == MENU_TYPE_MAIN && checkShortcutPress()) break;

        if (letterShortcuts) {
            char pressed_letter = checkLetterShortcutPress();
            if (pressed_letter > 0) {
                char pl = tolower(pressed_letter);
                int size = static_cast<int>(options.size());
                // start right after the current item so repeated presses cycle through duplicates
                int start = (tolower(options[index].label.c_str()[0]) == pl) ? index + 1 : 0;
                for (int i = 0; i < size; i++) {
                    int idx = (start + i) % size;
                    if (options[idx].enabled && tolower(options[idx].label.c_str()[0]) == pl) {
                        index = idx;
                        redraw = true;
                        break;
                    }
                }
            }
        }
#endif

        if (pageJumpSize > 0) {
            if (check(NextPagePress)) {
                index += pageJumpSize;
                if (index >= static_cast<int>(options.size())) index = static_cast<int>(options.size()) - 1;
                redraw = true;
            }
            if (check(PrevPagePress)) {
                index -= pageJumpSize;
                if (index < 0) index = 0;
                redraw = true;
            }
        }

        if (menuType == MENU_TYPE_REGULAR) {
            String txt = options[index].label;
            displayScrollingText(txt, coord, false);
        }
#ifdef HAS_3_BUTTONS
        if (EscPress && PrevPress) EscPress = false;
#endif
        if (menuType != MENU_TYPE_MAIN && check(EscPress)) {
            index = -1;
            NZLOGD("MENU", "back from [%s]", (subText != nullptr && subText[0] != '\0') ? subText : "menu");
            break;
        }
        if (menuType == MENU_TYPE_MAIN && mainMenuGridColumns > 1 &&
            mainMenuGridColumns < static_cast<int>(options.size())) {
            int rowStep = 0;
            if (check(UpPress)) rowStep = -1;
            else if (check(DownPress)) rowStep = 1;

            if (rowStep != 0) {
                int size = static_cast<int>(options.size());
                int cols = mainMenuGridColumns;
                int rows = (size + cols - 1) / cols;
                int col = index % cols;
                int row = index / cols + rowStep;

                if (row < 0) row = rows - 1;
                else if (row >= rows) row = 0;

                int idx = row * cols + col;
                if (idx >= size) idx = size - 1; // the last row can be shorter than the others
                if (options[idx].enabled) index = idx;

                devModeCounter = 0;
                redraw = true;
                continue;
            }
        }

#ifdef HAS_ENCODER
        int32_t rotarySteps = drainRotarySteps();
        if (rotarySteps != 0) {
            check(PrevPress);
            check(NextPress);
            check(UpPress);
            check(DownPress);
            devModeCounter = 0;
            while (rotarySteps > 0) {
                int prevEnabled = findNextEnabled(index, -1);
                if (prevEnabled < 0) break;
                index = prevEnabled;
                rotarySteps--;
                redraw = true;
            }
            while (rotarySteps < 0) {
                int nextEnabled = findNextEnabled(index, +1);
                if (nextEnabled < 0) break;
                if (!bruceConfig.devMode && nextEnabled <= index) devModeCounter++;
                index = nextEnabled;
                rotarySteps++;
                redraw = true;
            }
            vTaskDelay(4 / portTICK_PERIOD_MS);
            PrevPress = false;
            NextPress = false;
            UpPress = false;
            DownPress = false;
        } else
#endif
        {
            bool upTriggered = check(UpPress);
            if (upTriggered || PrevPress) {
                devModeCounter = 0;
#if defined(HAS_KEYBOARD) || defined(NO_TOUCH_FOOTER)
                check(PrevPress);
                int prevEnabled = findNextEnabled(index, -1);
                if (prevEnabled >= 0) index = prevEnabled;
                redraw = true;
#else
                if (upTriggered) {
                    check(PrevPress);
                    int prevEnabled = findNextEnabled(index, -1);
                    if (prevEnabled >= 0) index = prevEnabled;
                    redraw = true;
                } else {
                    long _tmp = millis();
                    bool arcDrawn = false;
#ifndef HAS_ENCODER // T-Embed doesn't need it
                    LongPress = true;
                    while (PrevPress && menuType != MENU_TYPE_MAIN) {
                        if (millis() - _tmp > 200) {
                            arcDrawn = true;
                            tft.drawArc(
                                tftWidth / 2,
                                tftHeight / 2,
                                25,
                                15,
                                0,
                                360 * (millis() - (_tmp + 200)) / 500,
                                getColorVariation(bruceConfig.priColor),
                                bruceConfig.bgColor
                            );
                        }
                        vTaskDelay(10 / portTICK_RATE_MS);
                    }
                    if (arcDrawn) {
                        tft.drawArc(
                            tftWidth / 2, tftHeight / 2, 25, 15, 0, 360, bruceConfig.bgColor, bruceConfig.bgColor
                        );
                        s_submenuForceFull = true;
                        firstRender = true;
                    }
                    LongPress = false;
#endif
                    if (millis() - _tmp > 700) { // longpress detected to exit
                        index = -1;
                        break;
                    } else {
                        check(PrevPress);
                        int prevEnabled = findNextEnabled(index, -1);
                        if (prevEnabled >= 0) index = prevEnabled;
                        redraw = true;
                    }
                }
#endif
            }
            /* DW Btn to next item */
            if (check(NextPress) || check(DownPress)) {
                int nextEnabled = findNextEnabled(index, +1);
                if (nextEnabled >= 0) {
                    if (!bruceConfig.devMode && nextEnabled <= index) devModeCounter++;
                    index = nextEnabled;
                }
                redraw = true;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }

        /* Select and run function
        forceMenuOption is set by a SerialCommand to force a selection within the menu
        */
        // Prevent immediate selection if the SEL button was already being held when the menu opened.
        // Allow a short grace period for the user to release the button first.
        static const unsigned long MENU_SELECT_IGNORE_MS = 220; // ms to ignore SEL after menu opens

        if (forceMenuOption >= 0 || (millis() - menuOpenTs > MENU_SELECT_IGNORE_MS && check(SelPress))) {
            uint16_t chosen = index;
            if (forceMenuOption >= 0) {
                chosen = forceMenuOption;
                forceMenuOption = -1; // reset SerialCommand navigation option
                Serial.print("Forcely ");
            }
            if (chosen >= options.size() || !options[chosen].enabled) continue;
            // Copy before invoking the callback: menu handlers (FM, nested
            // menus, ...) may clear/rebuild the global options vector from
            // inside the operation.
            String actionLabel = options[chosen].label;
            Serial.println("Selected: " + actionLabel);
            // Real-time activity trace: every app/menu launch lands in the log,
            // which makes a crash reproducible (last line = action in progress).
            NZLOGI(
                "MENU", "[%s] -> %s", (subText != nullptr && subText[0] != '\0') ? subText : "menu",
                actionLabel.c_str()
            );
            options[chosen].operation();
            NZLOGD(
                "MENU", "[%s] <- %s heap=%u", (subText != nullptr && subText[0] != '\0') ? subText : "menu",
                actionLabel.c_str(), (unsigned)ESP.getFreeHeap()
            );
            break;
        }
        // interpreter_start -> running the interpreter
        // interpreter -> loopOptions helper inside the Javascript
        if (interpreter_state > 0 && !interpreter) { break; }
    }

    RotaryNetSteps = 0; // reset rotary steps to avoid unexpected jumps in the next menu
    touchZoneOutsideFooterEnabled = prevTouchZoneOutsideFooterEnabled;
    return index;
}

/***************************************************************************************
** Function name: progressHandler
** Description:   Função para manipular o progresso da atualização
** Dependencia: prog_handler =>>    0 - Flash, 1 - LittleFS
***************************************************************************************/
static void drawBackChevron(int cx, int cy, uint16_t color) {
    tft.drawLine(cx + 3, cy - 4, cx - 2, cy, color);
    tft.drawLine(cx + 2, cy - 4, cx - 3, cy, color);
    tft.drawLine(cx - 2, cy, cx + 3, cy + 4, color);
    tft.drawLine(cx - 3, cy, cx + 2, cy + 4, color);
}

void progressHandler(int progress, size_t total, const String &message) {
    if (total > 0 && (size_t)progress >= total) NZLOGI("TASK", "done: %s", message.c_str());
    static String s_lastMsg = "";
    static int s_lastPct = -1;

    int pct = (total > 0) ? constrain((int)((uint64_t)progress * 100ULL / total), 0, 100) : 0;
    bool compact = (tftHeight < 160);
    int cardX = compact ? 10 : 18;
    int cardW = tftWidth - 2 * cardX;
    int cardH = compact ? 52 : 66;
    int cardY = max((int)STATUS_BAR_HEIGHT + 2, (tftHeight - cardH) / 2);
    int trackX = cardX + 12;
    int trackW = cardW - 24;
    int barH = compact ? 8 : 10;
    int barY = cardY + cardH - barH - (compact ? 11 : 14);
    uint16_t surf = uiSurfaceColor();

    bool initFrame = (progress <= 0) || (pct < s_lastPct) || (message != s_lastMsg) || (s_lastPct < 0);
    if (initFrame) {
        s_lastMsg = message;
        tft.fillRect(0, STATUS_BAR_HEIGHT - 4, tftWidth, tftHeight - STATUS_BAR_HEIGHT + 4, bruceConfig.bgColor);
        uiDrawCard(cardX, cardY, cardW, cardH, true);
        String trMsg = bruce_i18n::tr(message);
        if (!compact) {
            uiDrawText16(trMsg, cardX + 12, cardY + 12, cardW - 60, uiTextBrightColor(), surf, 0);
        } else {
            tft.setTextSize(FP);
            tft.setTextColor(uiTextBrightColor(), surf);
            tft.drawString(bruce_i18n::utf8TruncateToWidth(trMsg, cardW - 56, FP), cardX + 12, cardY + 11, 1);
        }
        tft.fillRoundRect(trackX, barY, trackW, barH, barH / 2, uiSurfaceHiColor());
    }

    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    tft.fillRect(cardX + cardW - 44, cardY + 10, 34, 16, surf);
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, surf);
    tft.drawRightString(pctBuf, cardX + cardW - 12, cardY + (compact ? 11 : 14), 1);

    int fillW = (trackW * pct) / 100;
    if (fillW > 0) {
        tft.fillRoundRect(trackX, barY, min(fillW, trackW), barH, barH / 2, bruceConfig.priColor);
        if (bruceConfig.nezhaThemeStyle > 0 && fillW >= 8) {
            int tipW = min(6, fillW - 2);
            tft.fillRoundRect(trackX + fillW - tipW, barY + 1, tipW, barH - 2, 2, bruceConfig.secColor);
        }
    }
    s_lastPct = (pct >= 100) ? -1 : pct;
}

/***************************************************************************************
** Function name: drawOptions
** Description:   Clean hardware list/modal picker with Font 2, color swatches & partial redraws
***************************************************************************************/
Opt_Coord drawOptions(
    int index, std::vector<Option> &options, uint16_t fgcolor, uint16_t selcolor, uint16_t bgcolor,
    bool firstRender, bool border, bool compact
) {
    static int last_index = 0;
    static int last_init = -1;

    Opt_Coord coord;
    uint16_t surface = border ? uiSurfaceColor() : bgcolor;
    uint16_t borderDim = uiBorderDimColor();
    uint16_t textBright = uiTextBrightColor();
    uint16_t textMuted = uiTextMutedColor();
    uint16_t textNormal = blendColors(bgcolor, textBright, 200);

    bool compactScreen = (tftHeight < 160);
    int rowHeightPx = compactScreen ? 21 : 25;
#if defined(HAS_TOUCH)
    int32_t topMargin = STATUS_BAR_HEIGHT + 18;
#else
    int32_t topMargin = border ? (STATUS_BAR_HEIGHT + 2) : (STATUS_BAR_HEIGHT - 1);
#endif
    int32_t botMargin = 8;
    int fitRows = (tftHeight - topMargin - botMargin) / rowHeightPx;
    if (compact && fitRows > 5) fitRows = 5;
    if (fitRows < 1) fitRows = 1;

    int arraySize = static_cast<int>(options.size());
    bool needsPaging = arraySize > fitRows;
#if defined(HAS_TOUCH)
    bool touchPaging = needsPaging;
#else
    bool touchPaging = false;
#endif

    struct SubmenuPage {
        int start;
        int count;
        bool pageUp;
        bool pageDown;
    };
    std::vector<SubmenuPage> pages;
    int maxRowsAcrossPages = 0;
    if (needsPaging) {
        int remaining = arraySize;
        int pageStart = 0;
        while (remaining > 0) {
            bool hasUp = touchPaging && !pages.empty();
            int maxOptions = fitRows - (hasUp ? 1 : 0);
            if (maxOptions < 1) maxOptions = 1;
            int count = remaining < maxOptions ? remaining : maxOptions;
            bool hasDown = false;
            if (touchPaging) {
                hasDown = remaining > count;
                if (hasDown) {
                    int maxWithDown = fitRows - (hasUp ? 1 : 0) - 1;
                    if (maxWithDown < 1) maxWithDown = 1;
                    count = count < maxWithDown ? count : maxWithDown;
                    if (count >= remaining) hasDown = false;
                }
            }
            pages.push_back({pageStart, count, hasUp, hasDown});
            int rowsForThisPage = count + (hasUp ? 1 : 0) + (hasDown ? 1 : 0);
            if (rowsForThisPage > maxRowsAcrossPages) maxRowsAcrossPages = rowsForThisPage;
            pageStart += count;
            remaining -= count;
        }
    }
    if (pages.empty()) pages.push_back({0, arraySize < fitRows ? arraySize : fitRows, false, false});

    int currentPage = 0;
    for (size_t p = 0; p < pages.size(); ++p) {
        currentPage = static_cast<int>(p);
        if (index >= pages[p].start && index < pages[p].start + pages[p].count) break;
    }
    int init = pages[currentPage].start;
    int menuSize = pages[currentPage].count;
    if (menuSize < 1) menuSize = 1;
    bool showPageUp = pages[currentPage].pageUp;
    bool showPageDown = pages[currentPage].pageDown;

#if defined(HAS_TOUCH)
    s_pageUpVisible = false;
    s_pageDownVisible = false;
    if (showPageUp) s_pageUpTargetIndex = pages[currentPage - 1].start;
    if (showPageDown) s_pageDownTargetIndex = pages[currentPage + 1].start;
#endif

    int totalRows = needsPaging ? maxRowsAcrossPages : menuSize;
    if (totalRows < 1) totalRows = 1;
    int32_t boxX = border ? (tftWidth >= 280 ? 14 : 6) : 4;
    int32_t boxW = tftWidth - 2 * boxX;
    int32_t contentTopY = border ? max(topMargin, (tftHeight + topMargin - totalRows * rowHeightPx) / 2) : topMargin;
    int32_t boxTopY = border ? (contentTopY - 6) : (topMargin - 2);
    int32_t boxH = border ? (rowHeightPx * totalRows + 12) : (tftHeight - boxTopY - 4);
    int32_t scrollPad = needsPaging ? 7 : 2;
    int32_t innerW = boxW - 8 - scrollPad;

    bool fullPageRepaint = firstRender || (init != last_init);
    tft.drawPixel(0, 0, bgcolor);

    if (firstRender) {
        if (border) {
            tft.fillRoundRect(boxX, boxTopY, boxW, boxH, 8, surface);
            tft.drawRoundRect(boxX, boxTopY, boxW, boxH, 8, borderDim);
        } else {
            tft.fillRect(boxX, boxTopY, boxW, boxH, surface);
        }
    } else if (init != last_init) {
        tft.fillRect(boxX + 2, contentTopY, boxW - 4, totalRows * rowHeightPx, surface);
    }

    if (fullPageRepaint) {
        for (auto &opt : options) {
            opt.w = 0;
            opt.h = 0;
        }
    }

    int32_t curY = contentTopY;

#if defined(HAS_TOUCH)
    if (showPageUp) {
        if (fullPageRepaint) {
            tft.fillRect(boxX + 4, curY, innerW, rowHeightPx, surface);
            tft.setTextColor(textMuted, surface);
            tft.setTextSize(FP);
            tft.drawCentreString(bruce_i18n::tr("-- Page Up --"), boxX + 4 + innerW / 2, curY + (rowHeightPx - LH * FP) / 2, 1);
        }
        s_pageUpVisible = true;
        s_pageUpX = boxX;
        s_pageUpY = curY;
        s_pageUpW = boxW;
        s_pageUpH = rowHeightPx;
        curY += rowHeightPx;
    }
#endif

    int endIdx = min(arraySize, init + menuSize);
    for (int i = init; i < endIdx; ++i) {
        int32_t rowY = curY;
        curY += rowHeightPx;

        options[i].x = boxX;
        options[i].y = rowY;
        options[i].w = boxW;
        options[i].h = rowHeightPx;

        bool isSel = (i == index);
        bool wasSel = (i == last_index);
        if (!fullPageRepaint && !isSel && !wasSel) continue;

        uint16_t accentCol = options[i].hasColor ? options[i].color : fgcolor;
        uint16_t rowBg = isSel ? accentCol : surface;
        uint16_t onAccent = uiTextOnAccentColor(accentCol);

        int32_t leftPad = options[i].hasColor ? 26 : 12;
        int32_t textX = boxX + leftPad;
        int32_t textY = rowY + (rowHeightPx - 16) / 2;
        int16_t rightReserve = options[i].selected ? 18 : 10;
        int16_t maxLinePx = innerW - (leftPad - 4) - rightReserve;

        tft.fillRect(boxX + 3, rowY, innerW + 2, rowHeightPx, surface);
        if (isSel) {
            tft.fillRoundRect(boxX + 4, rowY + 1, innerW, rowHeightPx - 2, 5, rowBg);
            coord.x = textX;
            coord.y = textY;
            coord.size = maxLinePx;
            coord.fgcolor = onAccent;
            coord.bgcolor = rowBg;
        }

        // Theme color swatch square when option carries an explicit color
        if (options[i].hasColor) {
            int swX = boxX + 10;
            int swY = rowY + (rowHeightPx - 10) / 2;
            tft.fillRoundRect(swX, swY, 10, 10, 3, options[i].color);
            tft.drawRoundRect(swX, swY, 10, 10, 3, isSel ? onAccent : borderDim);
        }

        uint16_t textCol;
        if (!options[i].enabled) textCol = 0x528A;
        else if (isSel) textCol = onAccent;
        else if (options[i].selected) textCol = textBright;
        else textCol = textNormal;

        String labelText = bruce_i18n::tr(options[i].label);
        uiDrawText16(labelText, textX, textY, maxLinePx, textCol, rowBg, 0);

        if (options[i].selected) {
            tft.fillCircle(boxX + 4 + innerW - 9, rowY + rowHeightPx / 2, 3, isSel ? onAccent : fgcolor);
        }
    }

#if defined(HAS_TOUCH)
    if (showPageDown) {
        if (fullPageRepaint) {
            tft.fillRect(boxX + 4, curY, innerW, rowHeightPx, surface);
            tft.setTextColor(textMuted, surface);
            tft.setTextSize(FP);
            tft.drawCentreString(bruce_i18n::tr("-- Page Down --"), boxX + 4 + innerW / 2, curY + (rowHeightPx - LH * FP) / 2, 1);
        }
        s_pageDownVisible = true;
        s_pageDownX = boxX;
        s_pageDownY = curY;
        s_pageDownW = boxW;
        s_pageDownH = rowHeightPx;
        curY += rowHeightPx;
    }
#endif

    if (fullPageRepaint) {
        int rowsDrawnThisPage = menuSize + (showPageUp ? 1 : 0) + (showPageDown ? 1 : 0);
        for (int r = rowsDrawnThisPage; r < totalRows; r++) {
            tft.fillRect(boxX + 3, curY, innerW + 2, rowHeightPx, surface);
            curY += rowHeightPx;
        }
    }

    if (needsPaging) {
        int barX = boxX + boxW - 4;
        int trackY = contentTopY + 3;
        int trackH = totalRows * rowHeightPx - 6;
        tft.fillRect(barX, trackY, 2, trackH, borderDim);
        int thumbH = max(8, trackH * menuSize / arraySize);
        int denom = max(1, arraySize - 1);
        int thumbY = trackY + (trackH - thumbH) * index / denom;
        tft.fillRect(barX, thumbY, 2, thumbH, textBright);
    }

    last_init = init;
    last_index = index;
    tft.setTextColor(fgcolor, bgcolor);
#if defined(HAS_TOUCH)
    {
        int hy = STATUS_BAR_HEIGHT - 3;
        if (firstRender) {
            tft.fillRect(0, hy, tftWidth, 18, bgcolor);
            drawBackChevron(13, hy + 9, textBright);
            tft.setTextSize(FP);
            tft.setTextColor(textMuted, bgcolor);
            tft.drawString(bruce_i18n::tr("Back"), 22, hy + (18 - LH * FP) / 2, 1);
        }
        char cntBuf[12];
        snprintf(cntBuf, sizeof(cntBuf), "%d/%d", index + 1, arraySize);
        tft.fillRect(tftWidth - 48, hy + 1, 42, 16, bgcolor);
        tft.setTextSize(FP);
        tft.setTextColor(textMuted, bgcolor);
        tft.drawRightString(cntBuf, tftWidth - 8, hy + (18 - LH * FP) / 2, 1);
    }
    TouchFooter();
#endif
    return coord;
}

/***************************************************************************************
** Function name: drawSubmenu
** Description:   Clean industrial hardware list with crisp Font 2 & partial-row redraws
***************************************************************************************/
void drawSubmenu(int index, std::vector<Option> &options, const char *title) {
    drawStatusBar();
    int menuSize = static_cast<int>(options.size());
    if (menuSize < 1) return;
    if (index < 0 || index >= menuSize) index = 0;

    static const Option *s_lastData = nullptr;
    static int s_lastSize = -1;
    static int s_lastScroll = -1;
    static int s_lastIndex = -1;
    static uint16_t s_lastPri = 0;
    static uint16_t s_lastBg = 0;
    static int16_t s_lastW = 0;
    static int16_t s_lastH = 0;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t borderDim = uiBorderDimColor();
    uint16_t textBright = uiTextBrightColor();
    uint16_t textMuted = uiTextMutedColor();
    uint16_t textNormal = blendColors(bg, textBright, 200);
    uint16_t onAccent = uiTextOnAccentColor(pri);

    bool compactScreen = (tftHeight < 160);
    int headerY = STATUS_BAR_HEIGHT - 3;
    int headerH = compactScreen ? 15 : 21;
    int listTop = headerY + headerH + 4;
    int listBottom = tftHeight - 4;
    int rowH = compactScreen ? 21 : 26;
    int visibleRows = max(1, (listBottom - listTop) / rowH);
    if (visibleRows > menuSize) visibleRows = menuSize;

    bool sameMenu = !s_submenuForceFull && (s_lastData == options.data()) && (s_lastSize == menuSize) &&
                    (s_lastPri == pri) && (s_lastBg == bg) && (s_lastW == tftWidth) && (s_lastH == tftHeight);

    int scroll = sameMenu ? s_lastScroll : 0;
    if (scroll < 0) scroll = 0;
    if (index < scroll) scroll = index;
    else if (index >= scroll + visibleRows) scroll = index - visibleRows + 1;
    int maxScroll = max(0, menuSize - visibleRows);
    if (scroll > maxScroll) scroll = maxScroll;

    bool fullRedraw = !sameMenu || (scroll != s_lastScroll);

    int rowX = 6;
    int rowW = tftWidth - 15;
    int barX = tftWidth - 5;

    // Clean header bar: integrated "<" back chevron (on touch) + crisp title + muted counter
    if (fullRedraw) {
        tft.fillRect(0, headerY, tftWidth, headerH + 2, bg);
        int titleX = 10;
#if defined(HAS_TOUCH)
        drawBackChevron(13, headerY + headerH / 2, textMuted);
        titleX = 25;
#endif
        String trTitle = bruce_i18n::tr(String(title));
        int maxTitleW = tftWidth - titleX - 48;
        if (!compactScreen) {
            uiDrawText16(trTitle, titleX, headerY + (headerH - 16) / 2, maxTitleW, textBright, bg, 0);
        } else {
            int titleH = (FP == 1 && bruce_i18n::containsNonAscii(trTitle)) ? 12 : (LH * FP);
            tft.setTextSize(FP);
            tft.setTextColor(textBright, bg);
            String fitted = bruce_i18n::utf8TruncateToWidth(trTitle, maxTitleW, FP);
            tft.drawString(fitted, titleX, headerY + (headerH - titleH) / 2, 1);
        }
        drawNezhaRibbonRule(6, headerY + headerH + 1, tftWidth - 12);
    }

    // Tabular item counter (e.g. 3/18)
    {
        char cntBuf[12];
        snprintf(cntBuf, sizeof(cntBuf), "%d/%d", index + 1, menuSize);
        int cntW = 42;
        int cntY = headerY + (headerH - LH * FP) / 2;
        tft.fillRect(tftWidth - 8 - cntW, headerY + 1, cntW, headerH - 2, bg);
        tft.setTextSize(FP);
        tft.setTextColor(textMuted, bg);
        tft.drawRightString(cntBuf, tftWidth - 8, cntY, 1);
    }

    if (fullRedraw) {
        for (auto &opt : options) {
            opt.w = 0;
            opt.h = 0;
        }
        tft.fillRect(0, listTop, tftWidth, listBottom - listTop + 2, bg);
    }

#if defined(HAS_TOUCH)
    s_pageUpVisible = (scroll > 0);
    s_pageDownVisible = (scroll + visibleRows < menuSize);
    if (s_pageUpVisible) {
        s_pageUpX = barX - 10;
        s_pageUpY = listTop;
        s_pageUpW = 15;
        s_pageUpH = (listBottom - listTop) / 2;
        s_pageUpTargetIndex = max(0, scroll - visibleRows);
    }
    if (s_pageDownVisible) {
        s_pageDownX = barX - 10;
        s_pageDownY = listTop + (listBottom - listTop) / 2;
        s_pageDownW = 15;
        s_pageDownH = (listBottom - listTop) / 2;
        s_pageDownTargetIndex = min(menuSize - 1, scroll + visibleRows);
    }
#endif

    // Render rows — only the 2 changed rows repaint during same-window scrolling
    for (int r = 0; r < visibleRows; ++r) {
        int i = scroll + r;
        int rowY = listTop + r * rowH;

        options[i].x = rowX;
        options[i].y = rowY;
        options[i].w = rowW;
        options[i].h = rowH;

        bool isSel = (i == index);
        bool wasSel = (i == s_lastIndex);
        if (!fullRedraw && !isSel && !wasSel) continue;

        uint16_t rowBg = isSel ? pri : bg;
        tft.fillRect(rowX, rowY, rowW, rowH, bg);
        if (isSel) {
            tft.fillRoundRect(rowX, rowY + 1, rowW, rowH - 2, 6, pri);
            if (bruceConfig.nezhaThemeStyle > 0) {
                tft.fillRoundRect(rowX + 3, rowY + 5, 2, rowH - 10, 1, bruceConfig.secColor);
            }
        }

        uint16_t labelCol = !options[i].enabled ? 0x528A : (isSel ? onAccent : (options[i].selected ? textBright : textNormal));
        String label = bruce_i18n::tr(options[i].label);
        int textY = rowY + (rowH - 16) / 2;
        uiDrawText16(label, rowX + 10, textY, rowW - (options[i].selected ? 32 : 24), labelCol, rowBg, 0);

        if (options[i].selected) {
            tft.fillCircle(rowX + rowW - (isSel ? 18 : 10), rowY + rowH / 2, 3, isSel ? onAccent : pri);
        }
        if (isSel) {
            int cx = rowX + rowW - 9;
            int cy = rowY + rowH / 2;
            tft.drawLine(cx - 2, cy - 3, cx + 1, cy, onAccent);
            tft.drawLine(cx - 2, cy + 3, cx + 1, cy, onAccent);
        }
    }

    // Ultra-slim 2px scrollbar track on far right
    if (menuSize > visibleRows) {
        int trackH = visibleRows * rowH - 4;
        tft.fillRect(barX, listTop + 2, 2, trackH, borderDim);
        int thumbH = max(8, trackH * visibleRows / menuSize);
        int thumbY = listTop + 2 + (trackH - thumbH) * index / max(1, menuSize - 1);
        tft.fillRect(barX, thumbY, 2, thumbH, textBright);
    }

    s_lastData = options.data();
    s_lastSize = menuSize;
    s_lastScroll = scroll;
    s_lastIndex = index;
    s_lastPri = pri;
    s_lastBg = bg;
    s_lastW = tftWidth;
    s_lastH = tftHeight;
    s_submenuForceFull = false;

#if defined(HAS_TOUCH)
    TouchFooter();
#endif
}

namespace {
struct StatusBarCache {
    bool valid = false;
    uint8_t bat = 0xFF;
    bool charging = false;
    uint8_t iconMask = 0xFF;
    bool clockSet = false;
    bool border = false;
    uint8_t langId = 0xFF;
    uint8_t nezhaStyle = 0xFF;
    uint16_t priColor = 0;
    uint16_t secColor = 0;
    uint16_t bgColor = 0;
    int16_t w = 0;
    int16_t h = 0;
    char timeBuf[16] = {0};
};
StatusBarCache s_statusBarCache;
} // namespace

void drawNezhaCrest(int cx, int cy, int r, uint16_t pri, uint16_t sec, uint8_t style) {
    if (r < 3) r = 3;
    if (style == 2) {
        // Lotus Seal (莲花玄印): 3-petal lotus silhouette
        tft.fillTriangle(cx - r, cy + r / 2, cx - r / 2, cy - r / 2, cx, cy + r / 2, pri);
        tft.fillTriangle(cx + r, cy + r / 2, cx + r / 2, cy - r / 2, cx, cy + r / 2, pri);
        tft.fillTriangle(cx - r / 2, cy + r / 2, cx, cy - r, cx + r / 2, cy + r / 2, sec);
        tft.drawFastHLine(cx - r + 1, cy + r / 2 + 1, (r - 1) * 2 + 1, pri);
    } else if (style == 3) {
        // Ice-Fire Twin (冰火双生): split Demon-Orb & Spirit-Pearl diamond
        tft.fillTriangle(cx, cy - r, cx - r + 1, cy, cx, cy + r, pri);
        tft.fillTriangle(cx, cy - r, cx + r - 1, cy, cx, cy + r, sec);
        tft.drawPixel(cx, cy, TFT_WHITE);
    } else {
        // Motong Flame (魔童烈焰): iconic Nezha forehead lotus-flame mark
        tft.fillTriangle(cx, cy - r, cx - r + 1, cy + 1, cx + r - 1, cy + 1, pri);
        tft.fillTriangle(cx, cy + r, cx - r + 2, cy, cx + r - 2, cy, pri);
        int ir = max(1, r / 2);
        tft.fillTriangle(cx, cy - ir, cx - ir, cy + 1, cx + ir, cy + 1, sec);
    }
}

void drawNezhaRibbonRule(int x, int y, int w) {
    if (w <= 0) return;
    tft.drawFastHLine(x, y, w, uiBorderDimColor());
    if (bruceConfig.nezhaThemeStyle > 0) {
        int lead = max(12, w / 4);
        int tip = max(6, min(20, w / 10));
        if (lead > w) lead = w;
        tft.drawFastHLine(x, y, lead, bruceConfig.priColor);
        if (lead + tip <= w) {
            tft.drawFastHLine(x + lead, y, tip, bruceConfig.secColor);
        }
    }
}

void uiDrawCard(int x, int y, int w, int h, bool accentRule) {
    if (w <= 4 || h <= 4) return;
    uint16_t surf = uiSurfaceColor();
    tft.fillRoundRect(x, y, w, h, 8, surf);
    tft.drawRoundRect(x, y, w, h, 8, uiBorderDimColor());
    if (accentRule && bruceConfig.nezhaThemeStyle > 0 && w > 24) {
        drawNezhaRibbonRule(x + 10, y + 2, w - 20);
    }
}

int16_t uiDrawBadge(int x, int y, const String &text, uint16_t fillCol, int h) {
    String label = bruce_i18n::tr(text);
    int16_t textPx = bruce_i18n::utf8PixelWidth(label, FP);
    int16_t padX = 6;
    int16_t w = textPx + padX * 2;
    int16_t th = (FP == 1 && bruce_i18n::containsNonAscii(label)) ? 12 : (LH * FP);
    if (h < th + 4) h = th + 4;
    uint16_t fg = uiTextOnAccentColor(fillCol);
    tft.fillRoundRect(x, y, w, h, h / 2, fillCol);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);
    tft.setTextColor(fg, fillCol);
    tft.drawString(label, x + padX, y + (h - th) / 2, 1);
    return w;
}

void uiDrawKeyValueRow(
    int x, int y, int w, const String &key, const String &val, uint16_t valCol, uint16_t bgCol
) {
    if (w <= 16) return;
    String trKey = bruce_i18n::tr(key);
    int16_t keyMax = max((int16_t)36, (int16_t)(w * 42 / 100));
    String fittedKey = bruce_i18n::utf8TruncateToWidth(trKey, keyMax, FP);
    int16_t keyW = bruce_i18n::utf8PixelWidth(fittedKey, FP);
    int16_t valMax = max((int16_t)24, (int16_t)(w - keyW - 8));
    String fittedVal = bruce_i18n::utf8TruncateToWidth(val, valMax, FP);

    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), bgCol);
    tft.drawString(fittedKey, x, y, 1);
    tft.setTextColor(valCol, bgCol);
    tft.drawRightString(fittedVal, x + w, y, 1);
}

void invalidateStatusBarCache() { s_statusBarCache.valid = false; }

void drawStatusBar(bool force) {
    uint8_t bat = getBattery();
    bool charging = (bat > 0) ? isCharging() : false;

    bool showSD = sdcardMounted;
    bool showGPS = gpsConnected;
#ifdef CONFIG_IDF_TARGET_ESP32P4
    bool showWifi = WiFi.isConnected() || WiFi.AP.started();
#else
    bool showWifi = (WiFi.getMode() != 0);
#endif
    bool showWeb = isWebUIActive;
    bool showBLE = BLEConnected;
    bool showWG = isConnectedWireguard;

    uint8_t iconMask = (showSD ? 0x01 : 0) | (showGPS ? 0x02 : 0) | (showWifi ? 0x04 : 0) |
                       (showWeb ? 0x08 : 0) | (showBLE ? 0x10 : 0) | (showWG ? 0x20 : 0);

    if (clock_set) {
#if defined(HAS_RTC)
        updateTimeStr(_rtc.getTimeStruct());
#else
        updateTimeStr(rtc.getTimeStruct());
#endif
    }

    uint8_t langId = static_cast<uint8_t>(bruce_i18n::getLanguage());
    uint8_t nStyle = (uint8_t)bruceConfig.nezhaThemeStyle;
    if (!force && s_statusBarCache.valid && s_statusBarCache.bat == bat &&
        s_statusBarCache.charging == charging && s_statusBarCache.iconMask == iconMask &&
        s_statusBarCache.clockSet == clock_set && s_statusBarCache.border == bruceConfig.theme.border &&
        s_statusBarCache.langId == langId && s_statusBarCache.nezhaStyle == nStyle &&
        s_statusBarCache.priColor == bruceConfig.priColor &&
        s_statusBarCache.secColor == bruceConfig.secColor &&
        s_statusBarCache.bgColor == bruceConfig.bgColor &&
        s_statusBarCache.w == tftWidth && s_statusBarCache.h == tftHeight &&
        (!clock_set || strncmp(s_statusBarCache.timeBuf, timeStr, sizeof(s_statusBarCache.timeBuf)) == 0)) {
        return;
    }

    s_statusBarCache.valid = true;
    s_statusBarCache.bat = bat;
    s_statusBarCache.charging = charging;
    s_statusBarCache.iconMask = iconMask;
    s_statusBarCache.clockSet = clock_set;
    s_statusBarCache.border = bruceConfig.theme.border;
    s_statusBarCache.langId = langId;
    s_statusBarCache.nezhaStyle = nStyle;
    s_statusBarCache.priColor = bruceConfig.priColor;
    s_statusBarCache.secColor = bruceConfig.secColor;
    s_statusBarCache.bgColor = bruceConfig.bgColor;
    s_statusBarCache.w = tftWidth;
    s_statusBarCache.h = tftHeight;
    if (clock_set) {
        strncpy(s_statusBarCache.timeBuf, timeStr, sizeof(s_statusBarCache.timeBuf) - 1);
        s_statusBarCache.timeBuf[sizeof(s_statusBarCache.timeBuf) - 1] = '\0';
    }

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t barBg = (bg == 0x0000) ? 0x0841 : uiSurfaceColor();
    uint16_t borderDim = uiBorderDimColor();
    uint16_t textBright = uiTextBrightColor();
    int hudH = STATUS_BAR_HEIGHT - 6;

    // Clean minimal hardware status bar with restrained 哪吒魔童 Hun Tian Ling accent hairline
    tft.fillRect(0, 0, tftWidth, hudH, barBg);
    drawNezhaRibbonRule(0, hudH, tftWidth);

    // Left badge: live clock or Nezha Cyber brand with 哪吒魔童 crest
    int crestR = (hudH >= 20) ? 5 : 4;
    if (clock_set) {
        int cx = 8;
        if (nStyle > 0) {
            drawNezhaCrest(10, hudH / 2, crestR, pri, sec, nStyle);
            cx = 19;
        }
        int badgeY = (hudH - LH * FP) / 2;
        setTftDisplay(cx, badgeY, textBright, FP, barBg);
        tft.print(timeStr);
    } else {
        if (nStyle > 0) {
            drawNezhaCrest(10, hudH / 2, crestR, pri, sec, nStyle);
        } else {
            tft.fillCircle(10, hudH / 2, 2, pri);
        }
        String brand = bruce_i18n::tr("Nezha Cyber");
        int badgeH = (FP == 1 && bruce_i18n::containsNonAscii(brand)) ? 12 : (LH * FP);
        int badgeY = (hudH - badgeH) / 2;
        setTftDisplay(19, badgeY, textBright, FP, barBg);
        tft.print(brand);
    }

    if (bat > 0) drawBatteryStatus(bat);

    int iconCount = 0;
    if (showSD) iconCount++;
    if (showGPS) iconCount++;
    if (showWifi) iconCount++;
    if (showWeb) iconCount++;
    if (showBLE) iconCount++;
    if (showWG) iconCount++;

    if (iconCount > 0) {
        const int IW = 16;
        const int IH = 16;
        const int GAP = 4;
        int totalW = iconCount * IW + (iconCount - 1) * GAP;
        int sx = (tftWidth - totalW) / 2;
        int iy = (hudH - IH) / 2;
        int idx = 0;

        bruceConfig.bgColor = barBg;
        bruceConfig.priColor = textBright;
        if (showSD) {
            drawSdSmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showGPS) {
            drawGpsSmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showWifi) {
            bruceConfig.priColor = pri;
            drawWifiSmall(sx + idx * (IW + GAP), iy);
            bruceConfig.priColor = textBright;
            idx++;
        }
        if (showWeb) {
            drawWebUISmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showBLE) {
            bruceConfig.priColor = pri;
            drawBLESmall(sx + idx * (IW + GAP), iy);
            bruceConfig.priColor = textBright;
            idx++;
        }
        if (showWG) {
            drawWireguardStatus(sx + idx * (IW + GAP), iy);
            idx++;
        }
        bruceConfig.priColor = pri;
        bruceConfig.bgColor = bg;
    }
}

void drawMainBorder(bool clear) {
    if (clear) {
        invalidateStatusBarCache();
        tft.drawPixel(0, 0, 0);
        tft.fillScreen(bruceConfig.bgColor);
    }
    setTftDisplay(BORDER_PAD_X, BORDER_PAD_X, bruceConfig.priColor, FP, bruceConfig.bgColor);
    tft.setTextDatum(0);

    // if(wifiConnected) {tft.print(timeStr);} else {tft.print("BRUCE 1.0b");}

    drawStatusBar(clear);

#if defined(HAS_TOUCH)
    TouchFooter();
#endif
}

void drawMainBorderWithTitle(const String &title, bool clear) {
    drawMainBorder(clear);
    printTitle(title);
}

void printTitle(const String &title) {
    String t = bruce_i18n::tr(title);
    uint16_t bg = bruceConfig.bgColor;
    int maxW = tftWidth - 2 * BORDER_PAD_X;
    bool compact = (tftHeight < 160);

    tft.fillRect(0, BORDER_PAD_Y - 1, tftWidth, 21, bg);
    if (!compact) {
        uiDrawText16(t, tftWidth / 2, BORDER_PAD_Y, maxW, uiTextBrightColor(), bg, 1);
    } else {
        int th = (FP == 1 && bruce_i18n::containsNonAscii(t)) ? 12 : (LH * FP);
        tft.setTextSize(FP);
        tft.setTextColor(uiTextBrightColor(), bg);
        tft.drawCentreString(
            bruce_i18n::utf8TruncateToWidth(t, maxW, FP), tftWidth / 2, BORDER_PAD_Y + (16 - th) / 2, 1
        );
    }
    drawNezhaRibbonRule(8, BORDER_PAD_Y + 18, tftWidth - 16);

    tft.setTextColor(bruceConfig.priColor, bg);
    tft.setTextSize(FP);
    tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y + 21);
}

void printSubtitle(const String &subtitle, bool withLine) {
    String sub = bruce_i18n::tr(subtitle);
    uint16_t bg = bruceConfig.bgColor;
    int y = BORDER_PAD_Y + 21;
    int maxW = tftWidth - 2 * BORDER_PAD_X;
    tft.setTextColor(uiTextMutedColor(), bg);
    tft.setTextSize(FP);
    tft.drawCentreString(bruce_i18n::utf8TruncateToWidth(sub, maxW, FP), tftWidth / 2, y, 1);

    int nextY = y + ((FP == 1 && bruce_i18n::containsNonAscii(sub)) ? 13 : (LH * FP + 2));
    if (withLine) {
        tft.drawFastHLine(24, nextY, tftWidth - 48, uiBorderDimColor());
        nextY += 4;
    }
    tft.setTextColor(bruceConfig.priColor, bg);
    tft.setCursor(BORDER_PAD_X, nextY);
}

void printFootnote(const String &text) {
    String trText = bruce_i18n::tr(text);
    int fh = (FP == 1 && bruce_i18n::containsNonAscii(trText)) ? 12 : (FP * LH);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    tft.drawRightString(trText, tftWidth - BORDER_PAD_X, tftHeight - BORDER_PAD_X - fh, 1);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

void printCenterFootnote(const String &text) {
    String trText = bruce_i18n::tr(text);
    int fh = (FP == 1 && bruce_i18n::containsNonAscii(trText)) ? 12 : (FP * LH);
    tft.fillRect(
        BORDER_PAD_X,
        tftHeight - BORDER_PAD_X - fh,
        tftWidth - 2 * BORDER_PAD_X,
        fh,
        bruceConfig.bgColor
    );
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    tft.drawCentreString(trText, tftWidth / 2, tftHeight - BORDER_PAD_X - fh, 1);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
}

void drawBatteryStatus(uint8_t bat) {
    if (bat == 0) return;

    bool charging = isCharging();
    uint16_t barBg = (bruceConfig.bgColor == 0x0000) ? 0x0841 : uiSurfaceColor();
    uint16_t outlineCol = (bat < 16) ? TFT_RED : uiTextMutedColor();
    uint16_t fillCol = (bat < 16) ? TFT_RED : (charging ? TFT_GREEN : uiTextBrightColor());

    const int hudH = STATUS_BAR_HEIGHT - 6;
    const int battH = 10;
    const int battW = 20;
    const int battY = (hudH - battH) / 2;
    const int battX = tftWidth - battW - 8;
    const int textGap = 5;
    const int textY = (hudH - LH * FP) / 2;

    tft.setTextSize(FP);
    tft.setTextColor(charging ? TFT_GREEN : (bat < 16 ? TFT_RED : uiTextMutedColor()), barBg);
    if (charging) {
        tft.drawRightString(bruce_i18n::tr("CHG"), battX - textGap, textY, 1);
    } else {
        tft.drawRightString(String(bat) + "%", battX - textGap, textY, 1);
    }

    tft.fillRoundRect(battX, battY, battW, battH, 2, barBg);
    tft.drawRoundRect(battX, battY, battW, battH, 2, outlineCol);
    tft.fillRect(battX + battW, battY + 3, 2, battH - 6, outlineCol);
    int innerW = battW - 4;
    int fillW = (innerW * bat + 50) / 100;
    if (fillW < 2 && bat > 0) fillW = 2;
    if (fillW > innerW) fillW = innerW;
    tft.fillRect(battX + 2, battY + 2, fillW, battH - 4, fillCol);
}

void drawWireguardStatus(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    if (isConnectedWireguard) {
        tft.drawRoundRect(x + 4, y + 2, 8, 8, 3, bruceConfig.priColor);
        tft.fillRoundRect(x + 3, y + 7, 10, 7, 1, bruceConfig.priColor);
    } else {
        tft.drawRoundRect(x + 4, y + 2, 8, 8, 3, bruceConfig.priColor);
        tft.drawRoundRect(x + 3, y + 7, 10, 7, 1, bruceConfig.priColor);
    }
}

// desenhos do menu principal, sprite "draw" com 80x80 pixels

void drawSdSmall(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    tft.drawLine(x + 3, y + 2, x + 3, y + 14, bruceConfig.priColor);
    tft.drawLine(x + 3, y + 14, x + 13, y + 14, bruceConfig.priColor);
    tft.drawLine(x + 13, y + 14, x + 13, y + 5, bruceConfig.priColor);
    tft.drawLine(x + 13, y + 5, x + 10, y + 2, bruceConfig.priColor);
    tft.drawLine(x + 10, y + 2, x + 3, y + 2, bruceConfig.priColor);

    tft.drawLine(x + 5, y + 4, x + 5, y + 6, bruceConfig.priColor);
    tft.drawLine(x + 7, y + 4, x + 7, y + 6, bruceConfig.priColor);
    tft.drawLine(x + 9, y + 4, x + 9, y + 6, bruceConfig.priColor);
    tft.drawLine(x + 11, y + 5, x + 11, y + 6, bruceConfig.priColor);
}

void drawWifiSmall(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    tft.fillCircle(x + 8, y + 13, 1, bruceConfig.priColor);
    tft.drawArc(x + 8, y + 13, 4, 6, 135, 225, bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawArc(x + 8, y + 13, 9, 11, 135, 225, bruceConfig.priColor, bruceConfig.bgColor);
}

void drawWebUISmall(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    tft.drawCircle(x + 8, y + 8, 6, bruceConfig.priColor);
    tft.drawLine(x + 3, y + 4, x + 13, y + 4, bruceConfig.priColor);
    tft.drawLine(x + 2, y + 8, x + 14, y + 8, bruceConfig.priColor);
    tft.drawLine(x + 3, y + 12, x + 13, y + 12, bruceConfig.priColor);
}

void drawBLESmall(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    tft.drawWideLine(x + 8, y + 8, x + 4, y + 4, 2, bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawWideLine(x + 8, y + 8, x + 4, y + 12, 2, bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawTriangle(x + 8, y + 8, x + 8, y + 2, x + 12, y + 5, bruceConfig.priColor);
    tft.drawTriangle(x + 8, y + 8, x + 8, y + 14, x + 12, y + 11, bruceConfig.priColor);
}

void drawBLE_beacon(int x, int y, uint16_t color) {
    tft.fillRect(x, y, 40, 80, bruceConfig.bgColor);
    tft.drawWideLine(40 + x, 53 + y, 2 + x, 26 + y, 5, color, bruceConfig.bgColor);
    tft.drawWideLine(40 + x, 26 + y, 2 + x, 53 + y, 5, color, bruceConfig.bgColor);
    tft.drawWideLine(40 + x, 53 + y, 20 + x, 68 + y, 5, color, bruceConfig.bgColor);
    tft.drawWideLine(40 + x, 26 + y, 20 + x, 12 + y, 5, color, bruceConfig.bgColor);
    tft.drawWideLine(20 + x, 12 + y, 20 + x, 68 + y, 5, color, bruceConfig.bgColor);
    tft.fillTriangle(40 + x, 26 + y, 20 + x, 40 + y, 20 + x, 12 + y, color);
    tft.fillTriangle(40 + x, 53 + y, 20 + x, 40 + y, 20 + x, 68 + y, color);
}

void drawGPS(int x, int y) {
    tft.fillRect(x, y, 80, 80, bruceConfig.bgColor);
    tft.drawEllipse(40 + x, 70 + y, 15, 8, bruceConfig.priColor);
    tft.drawArc(40 + x, 25 + y, 23, 7, 0, 340, bruceConfig.priColor, bruceConfig.bgColor);
    tft.fillTriangle(40 + x, 70 + y, 20 + x, 64 + y, 60 + x, 64 + y, bruceConfig.priColor);
}

void drawGpsSmall(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    tft.drawEllipse(x + 8, y + 13, 4, 3, bruceConfig.priColor);
    tft.drawArc(x + 8, y + 5, 5, 2, 0, 360, bruceConfig.priColor, bruceConfig.bgColor);
    tft.fillTriangle(x + 8, y + 14, x + 4, y + 8, x + 12, y + 8, bruceConfig.priColor);
}

void drawCreditCard(int x, int y) {
    tft.fillRect(x, y, 70, 50, bruceConfig.bgColor);
    tft.fillRoundRect(x + 5, y + 5, 60, 40, 5, bruceConfig.priColor);
    tft.fillRect(x + 5, y + 15, 60, 10, getColorVariation(bruceConfig.priColor, 3, -1));
    tft.fillRect(x + 10, y + 30, 12, 10, getColorVariation(bruceConfig.priColor, 3, 1));
    tft.drawRect(x + 10, y + 30, 12, 10, getColorVariation(bruceConfig.priColor, 5, -1));
    tft.drawRect(x + 10 + 4, y + 30, 4, 10, getColorVariation(bruceConfig.priColor, 5, -1));
    tft.drawRect(x + 10, y + 33, 5, 4, getColorVariation(bruceConfig.priColor, 5, -1));
    tft.drawRect(x + 17, y + 33, 5, 4, getColorVariation(bruceConfig.priColor, 5, -1));
    tft.fillRect(x + 30, y + 35, 30, 5, getColorVariation(bruceConfig.priColor, 5, 1));
}

void drawMfkey32Icon(int x, int y) {
    tft.drawRect(x + 2, y + 15, 24, 40, bruceConfig.priColor);
    tft.drawRect(x + 5, y + 18, 18, 12, bruceConfig.priColor);
    tft.drawRect(x + 5, y + 34, 18, 18, bruceConfig.priColor);
    tft.drawLine(x + 5, y + 40, x + 22, y + 40, bruceConfig.priColor);
    tft.drawLine(x + 5, y + 46, x + 22, y + 46, bruceConfig.priColor);
    tft.drawLine(x + 11, y + 34, x + 11, y + 51, bruceConfig.priColor);
    tft.drawLine(x + 17, y + 34, x + 17, y + 51, bruceConfig.priColor);
    tft.drawRect(x + 30, y + 10, 25, 35, bruceConfig.priColor);
    int startX = x + 32;
    int startY = y + 12;
    int endX = x + 52;
    int endY = y + 32;
    int step = 2;
    int turns = 0;

    while (startX <= endX && startY <= endY && turns < 3) {
        for (int i = startX; i <= endX; i++) { tft.drawPixel(i, startY, bruceConfig.priColor); }
        startY += step;
        for (int i = startY; i <= endY; i++) { tft.drawPixel(endX, i, bruceConfig.priColor); }
        endX -= step;
        for (int i = endX; i >= startX; i--) { tft.drawPixel(i, endY, bruceConfig.priColor); }
        endY -= step;
        for (int i = endY; i >= startY; i--) { tft.drawPixel(startX, i, bruceConfig.priColor); }
        startX += step;
        turns++;
    }
    tft.fillRect(x + 40, y + 36, 6, 6, getColorVariation(bruceConfig.priColor, 3, 1));
}

void drawMfkey64Icon(int x, int y) {
    drawMfkey32Icon(x, y);
    tft.fillRoundRect(x + 40, y + 6, 24, 14, 4, bruceConfig.bgColor);
    tft.drawRoundRect(x + 40, y + 6, 24, 14, 4, getColorVariation(bruceConfig.priColor, 3, -1));
    tft.drawCircle(x + 48, y + 12, 4, getColorVariation(bruceConfig.priColor, 3, -1));
}

// ####################################################################################################
//  Draw a JPEG on the TFT, images will be cropped on the right/bottom sides if they do not fit
// ####################################################################################################
//  from:
//  https://github.com/Bodmer/TFT_eSPI/blob/master/examples/Generic/ESP32_SDcard_jpeg/ESP32_SDcard_jpeg.ino
//  This function assumes xpos,ypos is a valid screen coordinate. For convenience images that do not
//  fit totally on the screen are cropped to the nearest MCU size and may leave right/bottom borders.
void jpegRender(int xpos, int ypos) {

    // jpegInfo(); // Print information from the JPEG file (could comment this line out)

    uint16_t *pImg;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t max_x = JpegDec.width;
    uint32_t max_y = JpegDec.height;

    bool swapBytes = tft.getSwapBytes();
    tft.setSwapBytes(true);

    // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
    // Typically these MCUs are 16x16 pixel blocks
    // Determine the width and height of the right and bottom edge image blocks
    uint32_t min_w = jpg_min(mcu_w, max_x % mcu_w);
    uint32_t min_h = jpg_min(mcu_h, max_y % mcu_h);

    // save the current image block size
    uint32_t win_w = mcu_w;
    uint32_t win_h = mcu_h;

    // save the coordinate of the right and bottom edges to assist image cropping
    // to the screen size
    max_x += xpos;
    max_y += ypos;

    // Fetch data from the file, decode and display
    tft.fillRect(xpos, ypos, JpegDec.width, JpegDec.height, TFT_BLACK);
    while (JpegDec.read()) {   // While there is more data in the file
        pImg = JpegDec.pImage; // Decode a MCU (Minimum Coding Unit, typically a 8x8 or 16x16 pixel block)

        // Calculate coordinates of top left corner of current MCU
        int mcu_x = JpegDec.MCUx * mcu_w + xpos;
        int mcu_y = JpegDec.MCUy * mcu_h + ypos;

        // check if the image block size needs to be changed for the right edge
        if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
        else win_w = min_w;

        // check if the image block size needs to be changed for the bottom edge
        if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
        else win_h = min_h;

        // copy pixels into a contiguous block
        if (win_w != mcu_w) {
            uint16_t *cImg;
            int p = 0;
            cImg = pImg + win_w;
            for (int h = 1; h < win_h; h++) {
                p += mcu_w;
                for (int w = 0; w < win_w; w++) {
                    *cImg = *(pImg + w + p);
                    cImg++;
                }
            }
        }

        // calculate how many pixels must be drawn
        uint32_t mcu_pixels = win_w * win_h;

        // draw image MCU block only if it will fit on the screen
        if ((mcu_x + win_w) <= tft.width() && (mcu_y + win_h) <= tft.height())
            tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
        else if ((mcu_y + win_h) > tft.height())
            JpegDec.abort(); // Image has run off bottom of screen so abort decoding
    }

    tft.setSwapBytes(swapBytes);
}

bool showJpeg(FS &fs, const String &filename, int x, int y, bool center) {
    // record the current time so we can measure how long it takes to draw an image
    uint32_t drawTime = millis();
    File picture;
    if (fs.exists(filename)) picture = fs.open(filename, FILE_READ);
    else return false;
    if (!picture) return false;

    const size_t data_size = picture.size();
    if (data_size == 0) {
        picture.close();
        return false;
    }

    // Prefer PSRAM for multi-KB JPEG payloads to preserve internal DMA-capable DRAM.
    uint8_t *data_array = nullptr;
    if (psramFound()) data_array = (uint8_t *)ps_malloc(data_size);
    if (data_array == nullptr) data_array = (uint8_t *)malloc(data_size);
    if (data_array == nullptr) {
        picture.close();
        log_e("showJpeg: failed to allocate %u bytes", (unsigned)data_size);
        return false;
    }

    const size_t bytes_read = picture.read(data_array, data_size);
    picture.close();

    if (bytes_read == 0) {
        free(data_array);
        return false;
    }

    const bool decoded = JpegDec.decodeArray(data_array, bytes_read);

    if (decoded) {
        if (center) {
            x = x + (tftWidth - JpegDec.width) / 2;
            y = y + (tftHeight - JpegDec.height) / 2;
        }
        jpegRender(x, y);
    }
    // calculate how long it took to draw the image
    drawTime = millis() - drawTime; // Calculate the time it took

    // print the results to the serial port
    Serial.print("Total render time was    : ");
    Serial.print(drawTime);
    Serial.println(" ms");
    Serial.println("=====================================");

    free(data_array); // free heap before leaving
    return decoded;
}

bool showJpeg(const uint8_t *data_array, size_t data_size, int x, int y, bool center) {
    bool decoded = false;
    if (data_array) {
        decoded = JpegDec.decodeArray(data_array, data_size);
    } else {
        return false;
    }

    if (decoded) {
        if (center) {
            x = x + (tftWidth - JpegDec.width) / 2;
            y = y + (tftHeight - JpegDec.height) / 2;
        }
        jpegRender(x, y);
    }

    return true;
}

#if !defined(LITE_VERSION)
// ####################################################################################################
//  Draw a GIF on the TFT
//  derived from
//  https://github.com/bitbank2/AnimatedGIF/blob/master/examples/TFT_eSPI_memory/TFT_eSPI_memory.ino and
//  https://github.com/bitbank2/AnimatedGIF/blob/master/examples/best_practices_example/best_practices_example.ino
// ####################################################################################################

Gif::Gif() : gif(nullptr), gifPosition(0, 0) {}

Gif::~Gif() {
    if (gif != nullptr) {
        gif->close();
        delete gif;
        gif = nullptr;
    }
}

FS *Gif::GifFs = NULL;

void *Gif::openFile(const char *fname, int32_t *pSize) {
    File GifFile;

    if (GifFs != NULL) {
        GifFile = GifFs->open(fname);
    } else {
        if (SD.exists(fname)) GifFile = SD.open(fname);
        else if (LittleFS.exists(fname)) GifFile = LittleFS.open(fname);
    }

    if (!GifFile) return NULL;

    File *FSGifFile = new (std::nothrow) File(GifFile);

    if (FSGifFile) {
        *pSize = FSGifFile->size();
        return (void *)FSGifFile;
    }
    GifFile.close();
    return NULL;
}

void Gif::closeFile(void *pHandle) {
    File *f = static_cast<File *>(pHandle);
    if (f != NULL) { f->close(); }
    delete f;
}

int32_t Gif::readFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
        iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0) return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
}

int32_t Gif::seekFile(GIFFILE *pFile, int32_t iPosition) {
    int i = micros();
    File *f = static_cast<File *>(pFile->fHandle);
    f->seek(iPosition);
    pFile->iPos = (int32_t)f->position();
    i = micros() - i;
    return pFile->iPos;
}

void Gif::GIFDraw(GIFDRAW *pDraw) {
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[tftWidth];
    int x, y, iWidth;

    GifPosition *position = (GifPosition *)(pDraw->pUser);

    iWidth = pDraw->iWidth;
    if (iWidth > tftWidth) iWidth = tftWidth;
    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line

    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) { // restore to background color
        for (x = 0; x < iWidth; x++) {
            if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
        }
        pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) { // if transparency used
        uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
        int x, iCount;
        pEnd = s + iWidth;
        x = 0;
        iCount = 0; // count non-transparent pixels
        while (x < iWidth) {
            c = ucTransparent - 1;
            d = usTemp;
            while (c != ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) { // done, stop
                    s--;                  // back up to treat it like transparent
                } else {                  // opaque
                    *d++ = usPalette[c];
                    iCount++;
                }
            } // while looking for opaque pixels
            if (iCount) { // any opaque pixels?
                tft.drawPixel(0, 0, 0);
                tft.pushImage(pDraw->iX + x + position->x, y + position->y, iCount, 1, (uint16_t *)usTemp);
                x += iCount;
                iCount = 0;
            }
            // no, look for a run of transparent pixels
            c = ucTransparent;
            while (c == ucTransparent && s < pEnd) {
                c = *s++;
                if (c == ucTransparent) iCount++;
                else s--;
            }
            if (iCount) {
                x += iCount; // skip these
                iCount = 0;
            }
        }
    } else {
        s = pDraw->pPixels;
        // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
        for (x = 0; x < iWidth; x++) usTemp[x] = usPalette[*s++];
        tft.drawPixel(0, 0, 0);
        tft.pushImage(pDraw->iX + position->x, y + position->y, iWidth, 1, (uint16_t *)usTemp);
    }
} /* GIFDraw() */

bool Gif::openGIF(FS *fs, const char *filename) {
    if (gif != nullptr) {
        gif->close();
        delete gif;
        gif = nullptr;
    }

    if (fs != NULL) {
        GifFs = fs;
        if (!fs->exists(filename)) return false;
    } else {
        GifFs = NULL;
    }

    gif = new (std::nothrow) AnimatedGIF();
    if (gif == nullptr) {
        log_e("GIF allocation error");
        return false;
    }
    gif->begin(BIG_ENDIAN_PIXELS);
    if (gif->open(filename, openFile, closeFile, readFile, seekFile, GIFDraw)) { return true; }

    log_e("GIF opening error: %d\n", gif->getLastError());
    delete gif;
    gif = nullptr;
    return false;
}

// Play a single frame
// returns:
// 2 = skipped waiting for another frame
// 1 = good result and more frames exist
// 0 = no more frames exist, a frame may or may not have been played: use getLastError() and look for
// GIF_SUCCESS to know if a frame was played -1 = error
int Gif::playFrame(int x, int y, bool bSync) {
    if (gif == nullptr) return -1;

    gifPosition.x = x;
    gifPosition.y = y;
    return gif->playFrame(bSync, nullptr, &gifPosition);
}

int Gif::getLastError() { return gif ? gif->getLastError() : -1; }

/*
 * playDurationMs:
 *  -1 : Play the GIF in an infinite loop
 *  0  : Play the GIF once
 * >0  : Play the GIF for the specified duration in milliseconds
 *       (e.g., 1000 = play for 1 second)
 */
bool showGif(
    FS *fs, const char *filename, int x, int y, bool center, int playDurationMs, bool resetButtonStatus
) {
    if (!fs->exists(filename)) return false;

    Gif gif;
    bool success = gif.openGIF(fs, filename);
    if (!success) { return false; }

    if (center) {
        x = x + (tftWidth - gif.getCanvasWidth()) / 2;
        y = y + (tftHeight - gif.getCanvasHeight()) / 2;
    }

    int result = 0;
    long timeStart = millis();
    do {
        result = gif.playFrame(x, y);
        if (result == -1) log_e("GIF playFrame error: %d\n", gif.getLastError());

        if (check(AnyKeyPress, resetButtonStatus)) break;

        if (playDurationMs > 0 && (millis() - timeStart) > playDurationMs) break;
        if (playDurationMs == 0 && result == 0) break;
    } while (result >= 0);

    return true;
}
#endif
/***************************************************************************************
** Function name: getComplementaryColor2
** Description:   Get simple complementary color in RGB565 format
***************************************************************************************/
uint16_t getComplementaryColor2(uint16_t color) {
    int r = 31 - ((color >> 11) & 0x1F);
    int g = 63 - ((color >> 5) & 0x3F);
    int b = 31 - (color & 0x1F);
    return (r << 11) | (g << 5) | b;
}
/***************************************************************************************
** Function name: getComplementaryColor
** Description:   Get complementary color in RGB565 format
***************************************************************************************/
uint16_t getComplementaryColor(uint16_t color) {
    double r = ((color >> 11) & 0x1F) / 31.0;
    double g = ((color >> 5) & 0x3F) / 63.0;
    double b = (color & 0x1F) / 31.0;

    double cmax = fmax(r, fmax(g, b));
    double cmin = fmin(r, fmin(g, b));
    double delta = cmax - cmin;

    double hue = 0.0;
    if (delta == 0) hue = 0.0;
    else if (cmax == r) hue = 60 * fmod((g - b) / delta, 6);
    else if (cmax == g) hue = 60 * ((b - r) / delta + 2);
    else hue = 60 * ((r - g) / delta + 4);

    if (hue < 0) hue += 360;

    double lightness = (cmax + cmin) / 2;
    double saturation = (delta == 0) ? 0 : delta / (1 - std::abs(2 * lightness - 1));

    double compHue = fmod(hue + 180, 360);

    double c = (1 - std::abs(2 * lightness - 1)) * saturation;
    double x = c * (1 - std::abs(fmod(compHue / 60, 2) - 1));
    double m = lightness - c / 2;

    double compR = 0, compG = 0, compB = 0;
    if (compHue >= 0 && compHue < 60) {
        compR = c;
        compG = x;
    } else if (compHue >= 60 && compHue < 120) {
        compR = x;
        compG = c;
    } else if (compHue >= 120 && compHue < 180) {
        compG = c;
        compB = x;
    } else if (compHue >= 180 && compHue < 240) {
        compG = x;
        compB = c;
    } else if (compHue >= 240 && compHue < 300) {
        compB = c;
        compR = x;
    } else {
        compB = x;
        compR = c;
    }

    uint16_t compl_color = uint8_t(compR * 31) << 11 | uint8_t(compG * 63) << 5 | uint8_t(compB * 31);

    // change black color
    if (compl_color == 0) compl_color = color - 0x1111;

    return compl_color;
}

/***************************************************************************************
** Function name: getColorVariation
** Description:   Get a variation of color in RGB565 format
***************************************************************************************/
uint16_t getColorVariation(uint16_t color, int delta, int direction) {
    uint8_t r = ((color >> 11) & 0x1F);
    uint8_t g = ((color >> 5) & 0x3F);
    uint8_t b = (color & 0x1F);

    float brightness = 0.299 * r / 31 + 0.587 * g / 63 + 0.114 * b / 31;

    if (direction < 0 || (direction == 0 && brightness >= 0.5)) {
        r = max(0, r - delta);
        g = max(0, g - 2 * delta);
        b = max(0, b - delta);
    } else {
        r = min(31, r + delta);
        g = min(63, g + 2 * delta);
        b = min(31, b + delta);
    }

    uint16_t compl_color = r << 11 | g << 5 | b;

    return compl_color;
}

uint16_t blendColors(uint16_t a, uint16_t b, uint8_t t) {
    int ar = (a >> 11) & 0x1f, ag = (a >> 5) & 0x3f, ab = a & 0x1f;
    int br = (b >> 11) & 0x1f, bg = (b >> 5) & 0x3f, bb = b & 0x1f;
    int r = ar + (br - ar) * t / 255;
    int g = ag + (bg - ag) * t / 255;
    int bl = ab + (bb - ab) * t / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static bool isLightRgb565(uint16_t c) {
    int r = (c >> 11) & 0x1F;
    int g = (c >> 5) & 0x3F;
    int b = c & 0x1F;
    return (r * 299 + (g >> 1) * 587 + b * 114) > 15500;
}

// Neutral hardware surface system: pure obsidian canvas + crisp neutral graphite elevations.
// Adapts automatically if the user selects a light or colored custom background.
uint16_t uiSurfaceColor() {
    uint16_t bg = bruceConfig.bgColor;
    if (bg == 0x0000) return 0x1082; // Neutral charcoal #101010
    return isLightRgb565(bg) ? blendColors(bg, TFT_BLACK, 20) : blendColors(bg, TFT_WHITE, 18);
}
uint16_t uiSurfaceHiColor() {
    uint16_t bg = bruceConfig.bgColor;
    if (bg == 0x0000) return 0x2104; // Neutral graphite #202020
    return isLightRgb565(bg) ? blendColors(bg, TFT_BLACK, 38) : blendColors(bg, TFT_WHITE, 34);
}
uint16_t uiBorderDimColor() {
    uint16_t bg = bruceConfig.bgColor;
    if (bg == 0x0000) return 0x2965; // Crisp subtle hairline #282C28
    return isLightRgb565(bg) ? blendColors(bg, TFT_BLACK, 64) : blendColors(bg, TFT_WHITE, 52);
}
uint16_t uiTextBrightColor() {
    return isLightRgb565(bruceConfig.bgColor) ? 0x18E3 : 0xF7BE; // Crisp off-white #F0F4F0 / dark charcoal
}
uint16_t uiTextMutedColor() {
    return isLightRgb565(bruceConfig.bgColor) ? 0x6B6D : 0x8C71; // Balanced neutral steel gray #8A8E8A
}
uint16_t uiTextOnAccentColor(uint16_t accent) {
    return isLightRgb565(accent) ? TFT_BLACK : TFT_WHITE;
}

int16_t uiMeasureText16(const String &text) {
#if defined(LOAD_FONT2)
    if (!bruce_i18n::containsNonAscii(text)) {
        uint8_t oldSize = tft.getTextSize();
        tft.setTextSize(1);
        int16_t w = tft.textWidth(text, 2);
        tft.setTextSize(oldSize);
        return w;
    }
#endif
    return bruce_i18n::utf8PixelWidth(text, FM);
}

String uiTruncateText16(const String &text, int16_t maxPx) {
    if (maxPx <= 0) return "";
#if defined(LOAD_FONT2)
    if (!bruce_i18n::containsNonAscii(text)) {
        uint8_t oldSize = tft.getTextSize();
        tft.setTextSize(1);
        if (tft.textWidth(text, 2) <= maxPx) {
            tft.setTextSize(oldSize);
            return text;
        }
        String out = text;
        int16_t ellW = tft.textWidth("..", 2);
        while (out.length() > 0 && tft.textWidth(out, 2) + ellW > maxPx) {
            out.remove(out.length() - 1);
        }
        tft.setTextSize(oldSize);
        return out.length() > 0 ? (out + "..") : "";
    }
#endif
    return bruce_i18n::utf8TruncateToWidth(text, maxPx, FM);
}

int16_t uiDrawText16(
    const String &text, int32_t x, int32_t y, int16_t maxPx, uint16_t fg, uint16_t bg, uint8_t align
) {
    String fitted = (maxPx > 0) ? uiTruncateText16(text, maxPx) : text;
    tft.setTextColor(fg, bg);
#if defined(LOAD_FONT2)
    if (!bruce_i18n::containsNonAscii(fitted)) {
        tft.setTextSize(1);
        if (align == 1) return tft.drawCentreString(fitted, x, y, 2);
        if (align == 2) return tft.drawRightString(fitted, x, y, 2);
        return tft.drawString(fitted, x, y, 2);
    }
#endif
    tft.setTextSize(FM);
    if (align == 1) return tft.drawCentreString(fitted, x, y, 1);
    if (align == 2) return tft.drawRightString(fitted, x, y, 1);
    return tft.drawString(fitted, x, y, 1);
}

void buildHeatPalette(uint16_t *lut, uint8_t n) {
    if (!lut || n < 2) return;
    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t hot = blendColors(pri, TFT_WHITE, 150);

    lut[0] = bg;
    for (uint8_t i = 1; i < n; i++) {
        int t = i * 255 / (n - 1);
        // ramp to the primary for the lower two thirds, then burn toward the
        // highlight so strong signals stay readable against a busy plot
        lut[i] = (t < 170) ? blendColors(bg, pri, 55 + t * 200 / 255)
                           : blendColors(pri, hot, (t - 170) * 255 / 85);
    }
}

// Draw BITMAP files
// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t read16(fs::File &f) {
    uint16_t result = 0;                // Initialize to prevent undefined behavior
    ((uint8_t *)&result)[0] = f.read(); // LSB
    ((uint8_t *)&result)[1] = f.read(); // MSB
    return result;
}

uint32_t read32(fs::File &f) {
    uint32_t result = 0;                // Initialize to prevent undefined behavior
    ((uint8_t *)&result)[0] = f.read(); // LSB
    ((uint8_t *)&result)[1] = f.read();
    ((uint8_t *)&result)[2] = f.read();
    ((uint8_t *)&result)[3] = f.read(); // MSB
    return result;
}
bool drawBmp(FS &fs, const String &filename, int x, int y, bool center) {
    if ((x >= tft.width()) || (y >= tft.height())) return false;
    uint32_t startTime = millis();

    File bmpFS;

    // Open requested file on SD card
    bmpFS = fs.open(filename, "r");

    if (!bmpFS) {
        Serial.print("File not found");
        goto ERROR;
    }

    uint32_t seekOffset;
    uint16_t w, h, row, col;
    uint8_t r, g, b;

    if (read16(bmpFS) == 0x4D42) {
        read32(bmpFS);
        read32(bmpFS);
        seekOffset = read32(bmpFS);
        read32(bmpFS);
        w = read32(bmpFS);
        h = read32(bmpFS);
        if (center) {
            x = x + (tftWidth - w) / 2;
            y = y + (tftHeight - h) / 2;
        }

        if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0)) {
            y += h - 1;

            bool oldSwapBytes = tft.getSwapBytes();
            tft.setSwapBytes(true);
            bmpFS.seek(seekOffset);

            uint16_t padding = (4 - ((w * 3) & 3)) & 3;
            uint8_t lineBuffer[w * 3 + padding];

            for (row = 0; row < h; row++) {

                bmpFS.read(lineBuffer, sizeof(lineBuffer));
                uint8_t *bptr = lineBuffer;
                uint16_t *tptr = (uint16_t *)lineBuffer;
                // Convert 24 to 16-bit colours
                for (uint16_t col = 0; col < w; col++) {
                    b = *bptr++;
                    g = *bptr++;
                    r = *bptr++;
                    *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                }

                // Push the pixel row to screen, pushImage will crop the line if needed
                // y is decremented as the BMP image is drawn bottom up
                tft.drawPixel(
                    0, 0, 0
                ); // shared TFT_Spi devices struggle to work, need call a line first sometimes
                tft.pushImage(x, y--, w, 1, (uint16_t *)lineBuffer);
            }
            tft.setSwapBytes(oldSwapBytes);
            Serial.print("BMP Loaded in ");
            Serial.print(millis() - startTime);
            Serial.println(" ms");
        } else {
            goto ERROR;
        }
    } else {
    ERROR:
        Serial.println("BMP format not recognized.");
        bmpFS.close();
        return false;
    }
    bmpFS.close();
    return true;
}

bool drawImg(
    FS &fs, const String &filename, int x, int y, bool center, int playDurationMs, bool resetButtonStatus
) {
    String ext = filename.substring(filename.lastIndexOf('.'));
    ext.toLowerCase();
    uint8_t fls = 2;         // 2 for Little FS
    if (&fs == &SD) fls = 0; // 0 for SD
    tft.imageToBin(fls, filename, x, y, center, playDurationMs);
    if (ext.endsWith("jpg")) return showJpeg(fs, filename, x, y, center);
    else if (ext.endsWith("bmp")) return drawBmp(fs, filename, x, y, center);
    else if (ext.endsWith("png")) return drawPNG(fs, filename, x, y, center);

#if !defined(LITE_VERSION)

    else if (ext.endsWith("gif"))
        return showGif(&fs, filename.c_str(), x, y, center, playDurationMs, resetButtonStatus);
#endif
    else log_e("Image not supported");

    return false;
}

#if !defined(LITE_VERSION)
/// Draw PNG files

#include <PNGdec.h>
#if TFT_WIDTH > TFT_HEIGHT
#define MAX_IMAGE_WIDTH TFT_WIDTH
#else
#define MAX_IMAGE_WIDTH TFT_HEIGHT
#endif
PNG *png = nullptr;
// Optional pointer to write decoded lines into a cached BIN file
static File *pngBinOut = nullptr;
static bool pngCacheOnly = false;
// Optionally use heap capabilities on ESP32 to pick the best memory region for the decoder
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif
// Functions to access a file on the SD card
File myfile;
FS *_fs;

void *myOpen(const char *filename, int32_t *size) {
    // Serial.printf("Attempting to open %s\n", filename);
    myfile = _fs->open(filename);
    *size = myfile.size();
    return &myfile;
}
void myClose(void *handle) {
    if (myfile) myfile.close();
}
int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
    if (!myfile) return 0;
    return myfile.read(buffer, length);
}
int32_t mySeek(PNGFILE *handle, int32_t position) {
    if (!myfile) return 0;
    return myfile.seek(position);
}
// Function to draw pixels to the display
int16_t xpos = 0;
int16_t ypos = 0;
int PNGDraw(PNGDRAW *pDraw) {
    uint16_t usPixels[MAX_IMAGE_WIDTH];
    // static uint16_t dmaBuffer[MAX_IMAGE_WIDTH]; // static so buffer persists after fn exit
    uint8_t r = ((uint16_t)bruceConfig.bgColor & 0xF800) >> 8;
    uint8_t g = ((uint16_t)bruceConfig.bgColor & 0x07E0) >> 3;
    uint8_t b = ((uint16_t)bruceConfig.bgColor & 0x001F) << 3;
    png->getLineAsRGB565(pDraw, usPixels, PNG_RGB565_BIG_ENDIAN, b << 16 | g << 8 | r);
    if (!pngCacheOnly) {
        tft.drawPixel(0, 0, 0);
        tft.drawPixel(0, 0, 0);
        tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, usPixels);
    }
    if (pngBinOut) { pngBinOut->write((uint8_t *)usPixels, pDraw->iWidth * sizeof(uint16_t)); }
    return 1;
}

// Build a cache path alongside the PNG: <dir>/tmp/<basename>.bin
static String buildPngBinPath(const String &pngPath) {
    int slash = pngPath.lastIndexOf('/');
    String dir = (slash >= 0) ? pngPath.substring(0, slash) : "";
    String name = pngPath.substring(slash + 1);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);

    String tmpDir = dir.length() ? dir + "/tmp" : "/tmp";
    if (!tmpDir.startsWith("/")) tmpDir = "/" + tmpDir;

    return tmpDir + "/" + name + ".bin";
}

static bool ensureTmpDir(FS &fs, const String &binPath) {
    int slash = binPath.lastIndexOf('/');
    if (slash < 0) return false;
    String dir = binPath.substring(0, slash);
    if (fs.exists(dir)) return true;
    return fs.mkdir(dir);
}

// Render a previously cached BIN (RGB565 LE with 2-byte width/height header)
static bool drawPngBin(FS &fs, const String &binPath, int x, int y, bool center) {
    File f = fs.open(binPath, FILE_READ);
    if (!f) return false;

    uint16_t w = 0, h = 0;
    if (f.read((uint8_t *)&w, sizeof(uint16_t)) != sizeof(uint16_t) ||
        f.read((uint8_t *)&h, sizeof(uint16_t)) != sizeof(uint16_t)) {
        f.close();
        return false;
    }

    if (center) {
        x = x + (tftWidth - w) / 2;
        y = y + (tftHeight - h) / 2;
    }

    if (x >= tft.width() || y >= tft.height()) {
        f.close();
        return false;
    }

    std::unique_ptr<uint16_t[]> line(new (std::nothrow) uint16_t[w]);
    if (!line) {
        f.close();
        return false;
    }

    size_t rowBytes = w * sizeof(uint16_t);
    for (uint16_t row = 0; row < h; ++row) {
        if (f.read((uint8_t *)line.get(), rowBytes) != rowBytes) {
            f.close();
            return false;
        }
        tft.pushImage(x, y + row, w, 1, line.get());
    }

    f.close();
    return true;
}

bool drawPNG(FS &fs, const String &filename, int x, int y, bool center) {
    if ((x >= tft.width()) || (y >= tft.height())) return false;
    _fs = &fs;
    uint32_t dt = millis();

    String binPath = buildPngBinPath(filename);
    if (fs.exists(binPath)) {
        if (pngCacheOnly) return true; // cache already ready
        if (drawPngBin(fs, binPath, x, y, center)) return true;
        fs.remove(binPath); // stale cache, fall back to decode
    }

    // Allocate decoder only while drawing, then release to keep RAM available for Wi-Fi/AP usage
#if defined(ESP32)
    bool usedHeapCaps = true;
    void *mem = psramFound() ? heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                             : heap_caps_malloc(sizeof(PNG), MALLOC_CAP_8BIT);
    if (!mem) {
        mem = malloc(sizeof(PNG));
        usedHeapCaps = false;
    }
#else
    void *mem = malloc(sizeof(PNG));
#endif
#if !defined(ESP32)
    bool usedHeapCaps = false;
#endif

    if (!mem) {
        Serial.println("Fail alloc PNG!");
        bruceConfig.theme.label = true;
        return false;
    }

    png = new (mem) PNG();
    int16_t rc = png->open(filename.c_str(), myOpen, myClose, myRead, mySeek, PNGDraw);
    if (rc == PNG_SUCCESS) {
        // Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\n", png->getWidth(),
        // png->getHeight(), png->getBpp(), png->getPixelType());

        File binFile;
        if (ensureTmpDir(fs, binPath)) {
            binFile = fs.open(binPath, FILE_WRITE);
            if (binFile) {
                uint16_t w = png->getWidth();
                uint16_t h = png->getHeight();
                binFile.write((uint8_t *)&w, sizeof(uint16_t));
                binFile.write((uint8_t *)&h, sizeof(uint16_t));
                pngBinOut = &binFile;
            }
        }

        if (center) {
            xpos = x + (tftWidth - png->getWidth()) / 2;
            ypos = y + (tftHeight - png->getHeight()) / 2;
        }

        if (png->getWidth() > MAX_IMAGE_WIDTH) {
            Serial.println("Image too wide for allocated line buffer size!");
        } else {
            rc = png->decode(NULL, 0);
            png->close();
        }

        if (pngBinOut) {
            pngBinOut->close();
            pngBinOut = nullptr;
        } else {
            if (fs.exists(binPath) && rc != PNG_SUCCESS) fs.remove(binPath);
        }
        if (rc != PNG_SUCCESS && fs.exists(binPath)) { fs.remove(binPath); }

        // How long did rendering take...
        Serial.print("PNG Loaded in ");
        Serial.print(millis() - dt);
        Serial.println("ms");
    } else {
        // Decode/open failed, ensure no stale cache
        if (fs.exists(binPath)) fs.remove(binPath);
    }

    // Destroy placement-new object and free memory so RAM is available after rendering
    png->~PNG();
#if defined(ESP32)
    if (usedHeapCaps) heap_caps_free(mem);
    else free(mem);
#else
    free(mem);
#endif
    png = nullptr;

    return rc == PNG_SUCCESS;
}

// Prepare (or verify) the cached BIN for a PNG without rendering it on screen
bool preparePngBin(FS &fs, const String &filename) {
    bool previous = pngCacheOnly;
    pngCacheOnly = true;
    bool ok = drawPNG(fs, filename, 0, 0, false);
    pngCacheOnly = previous;
    return ok;
}
#else
bool preparePngBin(FS &fs, const String &filename) {
    log_w("PNG: Not supported in this version");
    return true;
}
bool drawPNG(FS &fs, const String &filename, int x, int y, bool center) {
    log_w("PNG: Not supported in this version");
    return false;
}
#endif
