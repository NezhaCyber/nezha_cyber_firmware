#include "display.h"
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
    uint8_t ts = tft.getTextSize();
    if (ts < 1) ts = 1;
    int16_t boxPx = (coord.size > 1) ? (coord.size - 1) * LW * ts : 0;
    if (boxPx <= 0) return;
    int16_t textPx = bruce_i18n::utf8PixelWidth(trText, ts);
    static int i = 0;
    static long _lastmillis = 0;
    static String lastText = "";
    static uint16_t lastY = 0;
    if (trText != lastText || coord.y != lastY) {
        lastText = trText;
        lastY = coord.y;
        i = 0;
        _lastmillis = millis() + 700;
    }
    uint16_t fg = highlight ? coord.bgcolor : coord.fgcolor;
    uint16_t bg = highlight ? coord.fgcolor : coord.bgcolor;
    tft.setTextColor(fg, bg);
    if (textPx <= boxPx) {
        return;
    } else if (millis() > _lastmillis + 180) {
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
        String scrollingPart = bruce_i18n::utf8TruncateToWidth(tail, boxPx, ts);
        int16_t clearH = (ts == 1 && bruce_i18n::containsNonAscii(trText)) ? 12 : (LH * ts);
        tft.fillRect(coord.x, coord.y, boxPx, clearH, bg);
        tft.setCursor(coord.x, coord.y);
        tft.print(scrollingPart);
        if (i >= cpCount - 2) i = -1;
        _lastmillis = millis();
        i++;
        if (i == 1) _lastmillis = millis() + 900;
    }
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter(uint16_t color) {
#if defined(HAS_TOUCH) && !defined(NO_TOUCH_FOOTER)
    tft.drawRoundRect(
        BORDER_OFFSET_FROM_SCREEN_EDGE,
        tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 3,
        tftWidth - 2 * BORDER_OFFSET_FROM_SCREEN_EDGE,
        TOUCH_FOOTER_HEIGHT - BORDER_OFFSET_FROM_SCREEN_EDGE + 3,
        FP + 4,
        color
    );
    tft.setTextColor(color);
    tft.setTextSize(FM);
    tft.drawCentreString(bruce_i18n::tr("PREV"), tftWidth / 6, tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 1, 1);
    tft.drawCentreString(bruce_i18n::tr("SEL"), tftWidth / 2, tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 1, 1);
    tft.drawCentreString(bruce_i18n::tr("NEXT"), 5 * tftWidth / 6, tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 1, 1);
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
    tft.drawRoundRect(
        BORDER_OFFSET_FROM_SCREEN_EDGE,
        tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 3,
        tftWidth - 2 * BORDER_OFFSET_FROM_SCREEN_EDGE,
        TOUCH_FOOTER_HEIGHT - BORDER_OFFSET_FROM_SCREEN_EDGE + 3,
        FP + 4,
        color
    );
    tft.setTextColor(color);
    tft.setTextSize(FM);
    tft.drawCentreString(bruce_i18n::tr("Exit"), tftWidth / 6, tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 1, 1);
    tft.drawCentreString(bruce_i18n::tr("UP"), tftWidth / 2, tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 1, 1);
    tft.drawCentreString(bruce_i18n::tr("DOWN"), 5 * tftWidth / 6, tftHeight + BORDER_OFFSET_FROM_SCREEN_EDGE - 1, 1);
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
    int size;
    if (fgcolor == bgcolor && fgcolor == TFT_WHITE) fgcolor = TFT_BLACK;

    String displayText = bruce_i18n::tr(text);
    int maxBoxPx = tftWidth - 2 * BORDER_PAD_X - 10;

    std::vector<String> wrappedLines;
    if (bruce_i18n::utf8PixelWidth(displayText, FM) < maxBoxPx) {
        size = FM;
        wrappedLines = bruce_i18n::utf8WrapToWidth(displayText, maxBoxPx, FM);
    } else {
        size = FP;
        wrappedLines = bruce_i18n::utf8WrapToWidth(displayText, maxBoxPx, FP);
    }
    if (wrappedLines.empty()) wrappedLines.push_back(displayText);

    int lineHeight = (size == 1 && bruce_i18n::containsNonAscii(displayText)) ? 14 : (LH * size + 2);
    int boxHeight = wrappedLines.size() * lineHeight + 12;
    int boxX = BORDER_PAD_X;
    int boxY = tftHeight / 2 - boxHeight / 2;
    int boxW = tftWidth - 2 * BORDER_PAD_X;

    // Elevated cyber toast card with bright left accent bar
    uint16_t cardBg = blendColors(bruceConfig.bgColor, bgcolor, 60);
    uint16_t textCol = (bgcolor == TFT_YELLOW) ? TFT_YELLOW : uiTextBrightColor();
    tft.drawPixel(0, 0, 0);
    tft.fillRoundRect(boxX, boxY, boxW, boxHeight, 6, cardBg);
    tft.drawRoundRect(boxX, boxY, boxW, boxHeight, 6, bgcolor);
    tft.fillRect(boxX + 2, boxY + 4, 4, boxHeight - 8, bgcolor);

    tft.setTextColor(textCol, cardBg);
    tft.setTextSize(size);

    int startY = boxY + (boxHeight - (int)wrappedLines.size() * lineHeight) / 2 + 1;
    for (size_t i = 0; i < wrappedLines.size(); i++) {
        tft.drawCentreString(wrappedLines[i], boxX + 4 + (boxW - 4) / 2, startY + i * lineHeight);
    }
}

void drawButton(
    int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, const char *text, bool inverted = false
) {
    if (inverted) {
        tft.fillRoundRect(x, y, w, h, 5, color);
    } else {
        tft.fillRoundRect(x, y, w, h, 5, TFT_BLACK);
        tft.drawRoundRect(x, y, w, h, 5, color);
    }
    tft.setTextColor(inverted ? TFT_BLACK : color);
    tft.drawString(bruce_i18n::tr(text), x + w / 2, y + h);
}

int8_t displayMessage(
    const char *message, const char *leftButton, const char *centerButton, const char *rightButton,
    uint16_t color
) {

    uint8_t oldTextDatum = tft.getTextDatum();

    tft.setTextColor(color);
    tft.setTextSize(FM);
    tft.setTextDatum(TC_DATUM);

    // Handle newline characters in message
    String msg = bruce_i18n::tr(String(message));
    int y = tftHeight / 2 - 20;
    int start = 0;
    int end = msg.indexOf('\n');

    while (end != -1) {
        tft.drawString(msg.substring(start, end), tftWidth / 2, y);
        y += FM * LH;
        start = end + 1;
        end = msg.indexOf('\n', start);
    }
    tft.drawString(msg.substring(start), tftWidth / 2, y);

    tft.setTextDatum(BC_DATUM);
    int16_t buttonHeight = LH * FM + 4;
    int16_t buttonY = tftHeight - buttonHeight - BORDER_OFFSET_FROM_SCREEN_EDGE;
    int16_t buttonWidth = tftWidth / 3 - BORDER_PAD_X;

    int8_t totalButtons = (leftButton ? 1 : 0) + (centerButton ? 1 : 0) + (rightButton ? 1 : 0);
    int8_t selected = 0; // Start at first available button
    bool redraw = true;

    while (true) {
        if (check(PrevPress) || check(UpPress) || check(EscPress)) {
            selected = (selected - 1 + totalButtons) % totalButtons; // Cycle backward
            redraw = true;
        }
        if (check(NextPress) || check(DownPress)) {
            selected = (selected + 1) % totalButtons; // Cycle forward
            redraw = true;
        }
        if (check(SelPress)) { break; }

        // Draw buttons with selection highlighting
        int8_t index = 0;

        if (redraw) {
            if (leftButton) {
                drawButton(
                    BORDER_OFFSET_FROM_SCREEN_EDGE,
                    buttonY,
                    buttonWidth,
                    buttonHeight,
                    color,
                    leftButton,
                    selected == index
                );
                index++;
            }

            if (centerButton) {
                drawButton(
                    tftWidth / 3 + BORDER_OFFSET_FROM_SCREEN_EDGE,
                    buttonY,
                    buttonWidth,
                    buttonHeight,
                    color,
                    centerButton,
                    selected == index
                );
                index++;
            }

            if (rightButton) {
                drawButton(
                    tftWidth * 2 / 3 + BORDER_OFFSET_FROM_SCREEN_EDGE,
                    buttonY,
                    buttonWidth,
                    buttonHeight,
                    color,
                    rightButton,
                    selected == index
                );
            }
            redraw = false;
        }

        delay(10);
    }

    tft.setTextDatum(oldTextDatum);

    return selected;
}

void displayError(const String &txt, bool waitKeyPress) {
    displayRedStripe(txt);
    Serial.println("ERR: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displayWarning(const String &txt, bool waitKeyPress) {
    displayRedStripe(txt, TFT_BLACK, TFT_YELLOW);
    Serial.println("WARN: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displayInfo(const String &txt, bool waitKeyPress) {
    displayRedStripe(txt, TFT_WHITE, TFT_BLUE);
    Serial.println("INFO: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displaySuccess(const String &txt, bool waitKeyPress) {
    displayRedStripe(txt, TFT_WHITE, TFT_DARKGREEN);
    Serial.println("SUCCESS: " + txt);
#ifdef USE_DUMMY_TFT
    return;
#endif
    delay(200);
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(10 / portTICK_PERIOD_MS);
}

void displayTextLine(const String &txt, bool waitKeyPress) {
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

    s_submenuForceFull = true;

    bool useTapToSelect = menuType == MENU_TYPE_REGULAR || menuType == MENU_TYPE_SUBMENU ||
                          (bruceConfig.mainMenuStyle == MAIN_MENU_GRID && menuType == MENU_TYPE_MAIN);

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
    bool showEscButton = useTapToSelect && (menuType == MENU_TYPE_REGULAR || menuType == MENU_TYPE_SUBMENU);
    const int escW = 28;
    const int escH = 16;
    const int escX = tftWidth - escW - 6;
    const int escY = STATUS_BAR_HEIGHT - 2;
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
        // Grid page-up/page-down tap zone (right edge strip) — needed because tap-to-select can
        // only ever reach cells that are actually on screen, unlike Prev/Next which auto-scroll.
        if (useTapToSelect && touchPoint.pressed && menuType == MENU_TYPE_MAIN && mainMenuGridColumns > 1 &&
            gridPageTapHandler) {
            int newIndex;
            if (gridPageTapHandler(touchPoint.x, touchPoint.y, index, newIndex)) {
                index = newIndex;
                redraw = true;
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
            Serial.println("Selected: " + String(options[chosen].label));
            options[chosen].operation();
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
void progressHandler(int progress, size_t total, const String &message) {
    int barHeight = LH * FP + 6;
    int barY = tftHeight - barHeight - BORDER_PAD_Y;
    int trackW = tftWidth - 4 * BORDER_PAD_X;
    int barWidth = (total > 0) ? map(progress, 0, total, 0, trackW - 4) : 0;
    if (barWidth < 3) {
        tft.fillRect(
            0,
            STATUS_BAR_HEIGHT - 4,
            tftWidth,
            tftHeight - STATUS_BAR_HEIGHT + 4,
            bruceConfig.bgColor
        );
        tft.fillRoundRect(2 * BORDER_PAD_X, barY - 2, trackW, barHeight + 4, 4, uiSurfaceColor());
        tft.drawRoundRect(2 * BORDER_PAD_X, barY - 2, trackW, barHeight + 4, 4, uiBorderDimColor());
        displayRedStripe(message, TFT_WHITE, bruceConfig.priColor);
    }
    if (barWidth > 0) {
        tft.fillRoundRect(2 * BORDER_PAD_X + 2, barY, barWidth, barHeight, 3, bruceConfig.priColor);
    }
}

/***************************************************************************************
** Function name: drawOptions
** Description:   Modern tactical context/list menu with partial-row dirty redraws
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

    int rowHeightPx = (tftHeight < 160) ? (FM * LH + 5) : (FM * LH + 8);
#if defined(HAS_TOUCH)
    int32_t topMargin = border ? (STATUS_BAR_HEIGHT + 16) : (STATUS_BAR_HEIGHT + 16);
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
    int32_t boxX = border ? (tftWidth >= 280 ? 18 : 8) : 4;
    int32_t boxW = tftWidth - 2 * boxX;
    int32_t contentTopY = border ? max(topMargin, (tftHeight + topMargin - totalRows * rowHeightPx) / 2) : topMargin;
    int32_t boxTopY = border ? (contentTopY - 6) : (topMargin - 2);
    int32_t boxH = border ? (rowHeightPx * totalRows + 12) : (tftHeight - boxTopY - 4);
    int32_t scrollPad = needsPaging ? 8 : 2;
    int32_t innerW = boxW - 8 - scrollPad;

    bool fullPageRepaint = firstRender || (init != last_init);
    tft.drawPixel(0, 0, bgcolor);

    if (firstRender) {
        tft.fillRoundRect(boxX, boxTopY, boxW, boxH, 6, surface);
        tft.drawRoundRect(boxX, boxTopY, boxW, boxH, 6, borderDim);
        if (border) {
            tft.fillRect(boxX + 8, boxTopY, boxW - 16, 2, fgcolor);
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
    tft.setTextSize(FM);

#if defined(HAS_TOUCH)
    if (showPageUp) {
        if (fullPageRepaint) {
            tft.fillRoundRect(boxX + 4, curY + 1, innerW, rowHeightPx - 2, 3, blendColors(surface, fgcolor, 18));
            tft.setTextColor(fgcolor, blendColors(surface, fgcolor, 18));
            tft.setTextSize(FP);
            int ty = curY + (rowHeightPx - LH * FP) / 2;
            tft.drawCentreString(bruce_i18n::tr("-- Page Up --"), boxX + 4 + innerW / 2, ty, 1);
            tft.setTextSize(FM);
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
        uint16_t rowBg = isSel ? blendColors(bgcolor, accentCol, 52) : surface;
        int32_t textX = boxX + 12;
        int32_t textY = rowY + (rowHeightPx - FM * LH) / 2;
        int16_t rightReserve = options[i].selected ? 16 : 10;
        int16_t maxLinePx = innerW - 14 - rightReserve;

        tft.fillRect(boxX + 3, rowY, innerW + 2, rowHeightPx, surface);
        if (isSel) {
            tft.fillRoundRect(boxX + 4, rowY + 1, innerW, rowHeightPx - 2, 4, rowBg);
            tft.drawRoundRect(boxX + 4, rowY + 1, innerW, rowHeightPx - 2, 4, accentCol);
            tft.fillRect(boxX + 5, rowY + 3, 3, rowHeightPx - 6, accentCol);

            coord.x = textX;
            coord.y = textY;
            coord.size = max(2, maxLinePx / (LW * FM) + 1);
            coord.fgcolor = textBright;
            coord.bgcolor = rowBg;
        } else {
            tft.fillRect(boxX + 6, rowY + rowHeightPx / 2 - 1, 2, 2, borderDim);
        }

        uint16_t textCol;
        if (!options[i].enabled) textCol = TFT_DARKGREY;
        else if (isSel) textCol = textBright;
        else if (options[i].selected) textCol = selcolor;
        else if (options[i].hasColor) textCol = options[i].color;
        else textCol = bruceConfig.secColor;

        String labelText = bruce_i18n::tr(options[i].label);
        String fitted = bruce_i18n::utf8TruncateToWidth(labelText, maxLinePx, FM);
        tft.setTextSize(FM);
        tft.setTextColor(textCol, rowBg);
        tft.setCursor(textX, textY);
        tft.print(fitted);

        if (options[i].selected) {
            tft.fillCircle(boxX + 4 + innerW - 8, rowY + rowHeightPx / 2, 3, isSel ? accentCol : selcolor);
        } else if (isSel && innerW > 80) {
            tft.setTextSize(FP);
            tft.setTextColor(accentCol, rowBg);
            tft.drawRightString(">", boxX + 4 + innerW - 5, rowY + (rowHeightPx - LH * FP) / 2, 1);
            tft.setTextSize(FM);
        }
    }

#if defined(HAS_TOUCH)
    if (showPageDown) {
        if (fullPageRepaint) {
            tft.fillRoundRect(boxX + 4, curY + 1, innerW, rowHeightPx - 2, 3, blendColors(surface, fgcolor, 18));
            tft.setTextColor(fgcolor, blendColors(surface, fgcolor, 18));
            tft.setTextSize(FP);
            int ty = curY + (rowHeightPx - LH * FP) / 2;
            tft.drawCentreString(bruce_i18n::tr("-- Page Down --"), boxX + 4 + innerW / 2, ty, 1);
            tft.setTextSize(FM);
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
        int barX = boxX + boxW - 5;
        int trackY = contentTopY + 2;
        int trackH = totalRows * rowHeightPx - 4;
        tft.fillRoundRect(barX, trackY, 3, trackH, 1, blendColors(bgcolor, fgcolor, 25));
        int thumbH = max(8, trackH * menuSize / arraySize);
        int denom = max(1, arraySize - 1);
        int thumbY = trackY + (trackH - thumbH) * index / denom;
        tft.fillRoundRect(barX, thumbY, 3, thumbH, 1, fgcolor);
    }

    last_init = init;
    last_index = index;
    tft.setTextColor(fgcolor, bgcolor);
#if defined(HAS_TOUCH)
    if (firstRender) {
        const int escW = 28;
        const int escH = 16;
        const int escX = tftWidth - escW - 6;
        const int escY = STATUS_BAR_HEIGHT - 2;
        tft.fillRoundRect(escX, escY, escW, escH, 3, uiSurfaceColor());
        tft.drawRoundRect(escX, escY, escW, escH, 3, fgcolor);
        tft.setTextColor(textBright, uiSurfaceColor());
        tft.setTextSize(FP);
        tft.drawCentreString("X", escX + escW / 2, escY + (escH - LH * FP) / 2, 1);
    }
    TouchFooter();
#endif
    return coord;
}

/***************************************************************************************
** Function name: drawSubmenu
** Description:   Modern hacker tactical submenu with header, cards & dirty-row redraws
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
    uint16_t surface = uiSurfaceColor();
    uint16_t surfaceHi = uiSurfaceHiColor();
    uint16_t borderDim = uiBorderDimColor();
    uint16_t textBright = uiTextBrightColor();

    bool compactScreen = (tftHeight < 160);
    int headerY = STATUS_BAR_HEIGHT - 2;
    int headerH = compactScreen ? 14 : 17;
    int listTop = headerY + headerH + 4;
    int listBottom = tftHeight - 5;
    int rowH = compactScreen ? 21 : 26;
    int visibleRows = max(1, (listBottom - listTop) / rowH);
    if (visibleRows > menuSize) visibleRows = menuSize;

    // Keep scroll window stable across same-submenu navigation
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
    int rowW = tftWidth - 18;
    int barX = tftWidth - 8;

    // Header strip: "// TITLE" + counter pill + touch [X]
    if (fullRedraw) {
        tft.fillRect(0, headerY, tftWidth, headerH + 2, bg);
        String trTitle = bruce_i18n::tr(String(title));
        int titleH = (FP == 1 && bruce_i18n::containsNonAscii(trTitle)) ? 12 : (LH * FP);
        int titleY = headerY + (headerH - titleH) / 2;

        tft.setTextSize(FP);
        tft.setTextColor(pri, bg);
        tft.setCursor(8, titleY);
        tft.print("// ");
        int prefixW = 3 * LW * FP;
#if defined(HAS_TOUCH)
        int rightBlock = 74;
#else
        int rightBlock = 44;
#endif
        String fittedTitle = bruce_i18n::utf8TruncateToWidth(trTitle, tftWidth - 16 - prefixW - rightBlock, FP);
        tft.setTextColor(textBright, bg);
        tft.print(fittedTitle);
        tft.drawFastHLine(6, headerY + headerH + 1, tftWidth - 12, borderDim);

#if defined(HAS_TOUCH)
        const int escW = 28;
        const int escH = 16;
        const int escX = tftWidth - escW - 6;
        const int escY = headerY;
        tft.fillRoundRect(escX, escY, escW, escH, 3, surface);
        tft.drawRoundRect(escX, escY, escW, escH, 3, pri);
        tft.setTextColor(textBright, surface);
        tft.setTextSize(FP);
        tft.drawCentreString("X", escX + escW / 2, escY + (escH - LH * FP) / 2, 1);
#endif
    }

    // Update counter pill (e.g. 03/12)
    {
        char cntBuf[12];
        snprintf(cntBuf, sizeof(cntBuf), "%02d/%02d", index + 1, menuSize);
#if defined(HAS_TOUCH)
        int cntRight = tftWidth - 38;
#else
        int cntRight = tftWidth - 8;
#endif
        int cntW = 5 * LW * FP + 6;
        int cntY = headerY + (headerH - LH * FP) / 2;
        tft.fillRect(cntRight - cntW, headerY + 1, cntW, headerH - 2, bg);
        tft.setTextSize(FP);
        tft.setTextColor(bruceConfig.secColor, bg);
        tft.drawRightString(cntBuf, cntRight, cntY, 1);
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
        s_pageUpX = barX - 8;
        s_pageUpY = listTop;
        s_pageUpW = 14;
        s_pageUpH = (listBottom - listTop) / 2;
        s_pageUpTargetIndex = max(0, scroll - visibleRows);
    }
    if (s_pageDownVisible) {
        s_pageDownX = barX - 8;
        s_pageDownY = listTop + (listBottom - listTop) / 2;
        s_pageDownW = 14;
        s_pageDownH = (listBottom - listTop) / 2;
        s_pageDownTargetIndex = min(menuSize - 1, scroll + visibleRows);
    }
#endif

    // Draw visible rows (only changed rows when same scroll window!)
    tft.setTextSize(FM);
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

        uint16_t rowBg = isSel ? surfaceHi : bg;
        tft.fillRect(rowX, rowY, rowW, rowH, bg);
        if (isSel) {
            tft.fillRoundRect(rowX, rowY + 1, rowW, rowH - 2, 4, surfaceHi);
            tft.drawRoundRect(rowX, rowY + 1, rowW, rowH - 2, 4, pri);
            tft.fillRect(rowX + 1, rowY + 3, 3, rowH - 6, pri);
        } else {
            tft.fillRect(rowX + 4, rowY + rowH / 2 - 1, 2, 2, borderDim);
        }

        uint16_t labelCol = !options[i].enabled ? TFT_DARKGREY : (isSel ? textBright : bruceConfig.secColor);
        String label = bruce_i18n::tr(options[i].label);
        int maxTextW = rowW - 24;
        String fitted = bruce_i18n::utf8TruncateToWidth(label, maxTextW, FM);
        int textY = rowY + (rowH - FM * LH) / 2;
        tft.setTextSize(FM);
        tft.setTextColor(labelCol, rowBg);
        tft.setCursor(rowX + 10, textY);
        tft.print(fitted);

        if (isSel) {
            tft.setTextSize(FP);
            tft.setTextColor(pri, surfaceHi);
            tft.drawRightString(">", rowX + rowW - 5, rowY + (rowH - LH * FP) / 2, 1);
            tft.setTextSize(FM);
        }
    }

    // Sleek right-edge scrollbar
    int trackH = visibleRows * rowH - 2;
    tft.fillRoundRect(barX, listTop + 1, 3, trackH, 1, surface);
    if (menuSize > 1) {
        int thumbH = max(8, trackH * visibleRows / menuSize);
        int thumbY = listTop + 1 + (trackH - thumbH) * index / (menuSize - 1);
        tft.fillRoundRect(barX, thumbY, 3, thumbH, 1, pri);
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
    bool isZh = false;
    uint16_t priColor = 0;
    uint16_t bgColor = 0;
    int16_t w = 0;
    int16_t h = 0;
    char timeBuf[16] = {0};
};
StatusBarCache s_statusBarCache;
} // namespace

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

    bool isZh = bruce_i18n::isChinese();
    if (!force && s_statusBarCache.valid && s_statusBarCache.bat == bat &&
        s_statusBarCache.charging == charging && s_statusBarCache.iconMask == iconMask &&
        s_statusBarCache.clockSet == clock_set && s_statusBarCache.border == bruceConfig.theme.border &&
        s_statusBarCache.isZh == isZh &&
        s_statusBarCache.priColor == bruceConfig.priColor && s_statusBarCache.bgColor == bruceConfig.bgColor &&
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
    s_statusBarCache.isZh = isZh;
    s_statusBarCache.priColor = bruceConfig.priColor;
    s_statusBarCache.bgColor = bruceConfig.bgColor;
    s_statusBarCache.w = tftWidth;
    s_statusBarCache.h = tftHeight;
    if (clock_set) {
        strncpy(s_statusBarCache.timeBuf, timeStr, sizeof(s_statusBarCache.timeBuf) - 1);
        s_statusBarCache.timeBuf[sizeof(s_statusBarCache.timeBuf) - 1] = '\0';
    }

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t hudBg = uiSurfaceColor();
    uint16_t borderDim = uiBorderDimColor();
    uint16_t textBright = uiTextBrightColor();
    int hudH = STATUS_BAR_HEIGHT - 5;

    // Elevated cyber HUD top bar with left neon accent notch + bottom hairline
    tft.fillRect(0, 0, tftWidth, hudH, hudBg);
    tft.fillRect(0, 0, 3, hudH, pri);
    tft.drawFastHLine(0, hudH, tftWidth, borderDim);
    tft.drawFastHLine(0, hudH, 36, pri);

    // Subtle cyber corner brackets along the bottom edge (replaces dated Nokia outer box)
    if (bruceConfig.theme.border) {
        const int br = 9;
        int by = tftHeight - 2;
        tft.drawFastHLine(3, by, tftWidth - 6, borderDim);
        // Bottom-left bracket
        tft.drawFastHLine(2, by, br, pri);
        tft.drawFastVLine(2, by - br + 1, br, pri);
        // Bottom-right bracket
        tft.drawFastHLine(tftWidth - 2 - br, by, br, pri);
        tft.drawFastVLine(tftWidth - 3, by - br + 1, br, pri);
    }

    // Left HUD badge (live clock or Nezha Cyber / 哪吒网络安全 identity)
    if (clock_set) {
        int badgeY = (hudH - LH * FP) / 2;
        setTftDisplay(8, badgeY, textBright, FP, hudBg);
        tft.print(timeStr);
    } else {
        String brand = bruce_i18n::tr("Nezha Cyber");
        int badgeH = (FP == 1 && bruce_i18n::containsNonAscii(brand)) ? 12 : (LH * FP);
        int badgeY = (hudH - badgeH) / 2;
        setTftDisplay(8, badgeY, textBright, FP, hudBg);
        tft.print(brand);
        if (tftWidth >= 260 && !bruce_i18n::containsNonAscii(brand)) {
            tft.setTextColor(pri, hudBg);
            tft.print(" // ");
            tft.setTextColor(bruceConfig.secColor, hudBg);
            tft.print(BRUCE_VERSION);
        }
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

        // Temporarily match bgColor to hudBg so small status icons blend cleanly into the HUD bar
        bruceConfig.bgColor = hudBg;
        if (showSD) {
            drawSdSmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showGPS) {
            drawGpsSmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showWifi) {
            drawWifiSmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showWeb) {
            drawWebUISmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showBLE) {
            drawBLESmall(sx + idx * (IW + GAP), iy);
            idx++;
        }
        if (showWG) {
            drawWireguardStatus(sx + idx * (IW + GAP), iy);
            idx++;
        }
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
    if (!bruce_i18n::containsNonAscii(t)) t.toUpperCase();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    // Scale down title font if it doesn't fit the screen width
    int titleSize = FM;
    while (titleSize > FP && bruce_i18n::utf8PixelWidth(t, titleSize) > tftWidth - 2 * BORDER_PAD_X) {
        titleSize--;
    }

    int16_t tw = bruce_i18n::utf8PixelWidth(t, titleSize);
    tft.setTextSize(titleSize);
    tft.setCursor((tftWidth - tw) / 2, BORDER_PAD_Y);
    tft.println(t);

    tft.setTextSize(FP);
}

void printSubtitle(const String &subtitle, bool withLine) {
    String sub = bruce_i18n::tr(subtitle);
    int16_t subW = bruce_i18n::utf8PixelWidth(sub, FP);
    int16_t cursorX = (tftWidth - subW) / 2;
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);

    tft.setCursor(cursorX, BORDER_PAD_Y + FM * LH);
    tft.println(sub);

    if (withLine) {
        int dashCount = subW / (FP * LW);
        if (dashCount < 1) dashCount = 1;
        String line = "";
        for (int i = 0; i < dashCount; i++) line += "-";

        tft.setCursor(cursorX, tft.getCursorY());
        tft.println(line);
    }
}

void printFootnote(const String &text) {
    String trText = bruce_i18n::tr(text);
    int fh = (FP == 1 && bruce_i18n::containsNonAscii(trText)) ? 12 : (FP * LH);
    tft.setTextSize(FP);
    tft.drawRightString(trText, tftWidth - BORDER_PAD_X, tftHeight - BORDER_PAD_X - fh, SMOOTH_FONT);
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
    tft.drawCentreString(trText, tftWidth / 2, tftHeight - BORDER_PAD_X - fh, SMOOTH_FONT);
}

void drawBatteryStatus(uint8_t bat) {
    if (bat == 0) return;

    bool charging = isCharging();
    uint16_t hudBg = uiSurfaceColor();
    uint16_t color = bruceConfig.priColor;
    uint16_t barcolor = bruceConfig.priColor;
    if (bat < 16) barcolor = color = TFT_RED;
    else if (charging) barcolor = TFT_GREEN;

    const int hudH = STATUS_BAR_HEIGHT - 5;
    const int battH = 11;
    const int battW = 22;
    const int battY = (hudH - battH) / 2;
    const int battX = tftWidth - battW - 8;
    const int textGap = 5;
    const int textY = (hudH - LH * FP) / 2;

    tft.setTextSize(FP);
    tft.setTextColor(charging ? TFT_GREEN : (bat < 16 ? TFT_RED : uiTextBrightColor()), hudBg);
    if (charging) {
        tft.drawRightString(bruce_i18n::tr("CHG"), battX - textGap, textY, 1);
    } else {
        tft.drawRightString(String(bat) + "%", battX - textGap, textY, 1);
    }

    // Modern capsule battery with positive terminal nub
    tft.fillRoundRect(battX, battY, battW, battH, 2, bruceConfig.bgColor);
    tft.drawRoundRect(battX, battY, battW, battH, 2, color);
    tft.fillRect(battX + battW, battY + 3, 2, battH - 6, color);
    int innerW = battW - 4;
    int fillW = (innerW * bat + 50) / 100;
    if (fillW < 2 && bat > 0) fillW = 2;
    if (fillW > innerW) fillW = innerW;
    tft.fillRect(battX + 2, battY + 2, fillW, battH - 4, barcolor);
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

    const size_t data_size = picture.size();

    // Alloc memory into heap
    uint8_t *data_array = new uint8_t[data_size];
    if (data_array == nullptr) {
        // Fail allocating memory
        picture.close();
        delete[] data_array;
        return false;
    }

    uint8_t data;
    int i = 0;
    byte line_len = 0;

    while (picture.available()) {
        data = picture.read();
        data_array[i] = data;
        i++;

        // print array on Serial
        /*
        Serial.print("0x");
        if (abs(data) < 16) {
          Serial.print("0");
        }

        Serial.print(data, HEX);
        Serial.print(","); // Add value and comma
        line_len++;
        if (line_len >= 32) {
          line_len = 0;
          Serial.println();
        }
        */
    }

    picture.close();

    bool decoded = false;
    if (data_array) {
        decoded = JpegDec.decodeArray(data_array, data_size);
    } else {
        displayError(filename + " Fail");
        delay(2500);
        delete[] data_array; // free heap before leaving
        return false;
    }

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

    delete[] data_array; // free heap before leaving
    return true;
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

Gif::Gif() : gifPosition(0, 0) {}

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

    File *FSGifFile = new File(GifFile);

    if (FSGifFile) {
        *pSize = FSGifFile->size();
        return (void *)FSGifFile;
    }
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
    if (fs != NULL) {
        GifFs = fs;
        if (!fs->exists(filename)) return false;
    } else {
        GifFs = NULL;
    }

    gif = new AnimatedGIF();
    gif->begin(BIG_ENDIAN_PIXELS);
    if (gif->open(filename, openFile, closeFile, readFile, seekFile, GIFDraw)) { return true; }

    log_e("GIF opening error: %d\n", gif->getLastError());
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

int Gif::getLastError() { return gif->getLastError(); }

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
    return (r * 299 + (g >> 1) * 587 + b * 114) > 16000;
}

uint16_t uiSurfaceColor() { return blendColors(bruceConfig.bgColor, bruceConfig.priColor, 24); }
uint16_t uiSurfaceHiColor() { return blendColors(bruceConfig.bgColor, bruceConfig.priColor, 54); }
uint16_t uiBorderDimColor() { return blendColors(bruceConfig.bgColor, bruceConfig.priColor, 82); }
uint16_t uiTextBrightColor() {
    uint16_t target = isLightRgb565(bruceConfig.bgColor) ? TFT_BLACK : TFT_WHITE;
    return blendColors(bruceConfig.priColor, target, 195);
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
