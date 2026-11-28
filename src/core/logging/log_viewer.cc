#include "core/logging/log_viewer.h"

#include "core/display.h"
#include "core/logging/nzlog.h"
#include "core/utils.h"
#include <SD.h>
#include <cstring>
#include <esp32-hal-psram.h>
#include <globals.h>

namespace {

constexpr size_t kReadChunk = 8192;
constexpr int kMaxParsedLines = 256;
constexpr uint32_t kRefreshMs = 300;

uint16_t colorForLevel(char level) {
    switch (level) {
        case 'V': return 0x8410; // grey
        case 'D': return 0xFFFF; // white
        case 'I': return 0x07E0; // green
        case 'W': return 0xFFE0; // yellow
        case 'E': return 0xF800; // red
        case 'F': return 0xF81F; // magenta
        default: return bruceConfig.priColor;
    }
}

// Logcat line: "<time> L/TAG (task): message". Timestamp may itself contain
// spaces (uptime fallback), so search for the " X/" marker near the front.
char parseLevelChar(const char *line, size_t len) {
    size_t limit = len < 40 ? len : 40;
    for (size_t i = 6; i + 2 < limit; ++i) {
        if (line[i] == ' ' && line[i + 2] == '/') {
            char c = line[i + 1];
            if (c == 'V' || c == 'D' || c == 'I' || c == 'W' || c == 'E' || c == 'F') return c;
        }
    }
    return 'I';
}

void drawFooter(bool follow, int scrollBack, int lines, size_t bytes) {
    int footerH = 14;
    int y = tftHeight - footerH;
    uint16_t bg = uiSurfaceColor();
    tft.fillRect(0, y, tftWidth, footerH, bg);

    tft.setTextSize(FP);
    tft.setTextColor(follow ? TFT_GREEN : TFT_YELLOW, bg);
    tft.drawString(follow ? "FOLLOW" : "PAUSED", 4, y + 3, 1);

    char info[64];
    snprintf(
        info,
        sizeof(info),
        "-%d %dB %uK",
        scrollBack,
        lines,
        (unsigned)(bytes / 1024)
    );
    tft.setTextColor(uiTextMutedColor(), bg);
    tft.drawRightString(info, tftWidth - 4, y + 3, 1);
}

/**
 * Draw the last screenful of the log.
 * @param scrollBack number of lines to skip from the very end (0 = live tail)
 * @return total lines parsed from the read window
 */
int drawTail(int scrollBack, bool follow) {
    File f = SD.open(nzlog::path(), FILE_READ);
    if (!f) {
        drawMainBorderWithTitle("System Log");
        tft.setTextSize(FP);
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.setCursor(BORDER_PAD_X, BORDER_PAD_Y + 24);
        tft.println("No log file.");
        tft.setCursor(BORDER_PAD_X, tft.getCursorY() + 4);
        tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
        tft.println("SD card required for logging.");
        return 0;
    }

    size_t size = f.size();
    size_t want = size > kReadChunk ? kReadChunk : size;
    size_t start = size - want;

    char *buf = nullptr;
    if (psramFound()) buf = static_cast<char *>(ps_malloc(kReadChunk + 1));
    if (buf == nullptr) buf = static_cast<char *>(malloc(kReadChunk + 1));
    if (buf == nullptr) {
        f.close();
        drawMainBorderWithTitle("System Log");
        displayError("Out of memory for log view", true);
        return 0;
    }

    if (want > 0) {
        f.seek(start);
        size_t got = f.readBytes(buf, want);
        buf[got] = '\0';
        want = got;
    } else {
        buf[0] = '\0';
    }
    f.close();

    // If we started mid-file the first line is partial: skip it.
    char *cursor = buf;
    if (start > 0) {
        char *nl = static_cast<char *>(memchr(buf, '\n', want));
        if (nl == nullptr) {
            free(buf);
            return 0; // no complete line in the window
        }
        cursor = nl + 1;
        want -= static_cast<size_t>(cursor - buf);
    }

    const char *linePtr[kMaxParsedLines];
    uint16_t lineLen[kMaxParsedLines];
    int count = 0;
    char *p = cursor;
    char *end = cursor + want;
    while (p < end && count < kMaxParsedLines) {
        char *nl = static_cast<char *>(memchr(p, '\n', static_cast<size_t>(end - p)));
        if (nl == nullptr) break;
        *nl = '\0';
        size_t len = static_cast<size_t>(nl - p);
        if (len > 0 && p[len - 1] == '\r') {
            p[len - 1] = '\0';
            --len;
        }
        linePtr[count] = p;
        lineLen[count] = static_cast<uint16_t>(len);
        ++count;
        p = nl + 1;
    }

    int topY = BORDER_PAD_Y + 21;
    int lineH = LH * FP;
    int footerH = 16;
    int maxLines = (tftHeight - topY - footerH) / lineH;
    if (maxLines < 1) maxLines = 1;

    int maxScroll = count > maxLines ? count - maxLines : 0;
    if (scrollBack > maxScroll) scrollBack = maxScroll;
    int first = count - maxLines - scrollBack;
    if (first < 0) first = 0;
    int last = first + maxLines;
    if (last > count) last = count;

    tft.fillRect(0, topY - 2, tftWidth, tftHeight - topY - footerH + 2, bruceConfig.bgColor);

    int maxCols = (tftWidth - 2 * BORDER_PAD_X) / (LW * FP);
    if (maxCols < 8) maxCols = 8;
    if (maxCols > 120) maxCols = 120;
    char tmp[128];

    int y = topY;
    for (int i = first; i < last; ++i) {
        const char *line = linePtr[i];
        size_t len = lineLen[i];
        if (len > static_cast<size_t>(maxCols)) len = static_cast<size_t>(maxCols);
        if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
        memcpy(tmp, line, len);
        tmp[len] = '\0';

        char lvl = parseLevelChar(line, lineLen[i]);
        tft.setTextSize(FP);
        tft.setTextColor(colorForLevel(lvl), bruceConfig.bgColor);
        tft.drawString(String(tmp), BORDER_PAD_X, y, 1);
        y += lineH;
    }

    drawFooter(follow, scrollBack, count, size);
    free(buf);
    return count;
}

void refreshFrame(bool follow, int scrollBack, bool clear) {
    if (clear) {
        drawMainBorderWithTitle("System Log");
    }
    drawTail(scrollBack, follow);
}

} // namespace

void systemLogViewer() {
    // Make sure the sink is open; may be called before any other log activity.
    nzlog::begin();

    bool follow = true;
    int scrollBack = 0;
    size_t lastKnownSize = (size_t)-1;
    bool dirty = true;

    drawMainBorderWithTitle("System Log");
    printCenterFootnote("UP/DN=Scroll  SEL=Follow  ESC=Back");

    while (true) {
        if (check(EscPress)) break;

        if (check(SelPress)) {
            follow = !follow;
            scrollBack = 0;
            dirty = true;
        }
        if (check(UpPress) || check(PrevPress)) {
            follow = false;
            scrollBack++;
            dirty = true;
        }
        if (check(DownPress) || check(NextPress)) {
            if (scrollBack > 0) {
                scrollBack--;
                dirty = true;
            } else if (!follow) {
                follow = true;
                dirty = true;
            }
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            if (touchPoint.y > tftHeight / 2) {
                if (scrollBack > 0) scrollBack--;
                else follow = true;
            } else {
                follow = false;
                scrollBack++;
            }
            touchPoint.pressed = false;
            dirty = true;
        }
#endif

        size_t currentSize = nzlog::fileSize();
        if (currentSize != lastKnownSize) {
            lastKnownSize = currentSize;
            if (follow) dirty = true;
        }

        if (dirty) {
            refreshFrame(follow, scrollBack, false);
            dirty = false;
        }

        vTaskDelay(pdMS_TO_TICKS(kRefreshMs));
    }

    // Returning to a menu: leave the screen clean for the next draw.
    returnToMenu = false;
}

void systemLogMenu() {
    while (true) {
        nzlog::begin();
        nzlog::Level lvl = nzlog::level();
        String levelLabel = String("Log Level: ") + nzlog::levelName(lvl) + String(" (") +
                            String(nzlog::fileReady() ? "SD" : "SERIAL") + String(")");

        options = {
            {"View live log",  systemLogViewer},
            {levelLabel.c_str(),
             []() {
                 uint8_t next = (static_cast<uint8_t>(nzlog::level()) + 1) % 6;
                 nzlog::setLevel(static_cast<nzlog::Level>(next));
             }},
            {"Clear log files",
             []() {
                 if (nzlog::clear()) displayInfo("Log files cleared", true);
                 else displayError("SD card required", true);
             }},
            {"Back",           []() { returnToMenu = true; }},
        };

        int selected = loopOptions(options, MENU_TYPE_SUBMENU, "System Log");
        if (selected < 0 || returnToMenu) {
            returnToMenu = false;
            break;
        }
    }
}
