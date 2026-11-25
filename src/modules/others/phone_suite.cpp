#if !defined(LITE_VERSION)
#include "phone_suite.h"

#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline bool isChinese() { return false; }
static inline const char *tr(const char *s) { return s; }
static inline String tr(const String &s) { return s; }
} // namespace bruce_i18n
#endif
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "modules/others/audio.h"
#include <WiFi.h>
#include <algorithm>
#include <cmath>
#include <globals.h>
#include <vector>

namespace {

struct MediaFileItem {
    String path;
    String name;
    size_t sizeBytes = 0;
};

static bool hasExtCI(const String &name, const char *ext) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(ext);
}

static bool isImageFileName(const String &name) {
    return hasExtCI(name, ".jpg") || hasExtCI(name, ".jpeg") || hasExtCI(name, ".png") ||
           hasExtCI(name, ".bmp") || hasExtCI(name, ".gif");
}

static bool isMusicFileName(const String &name) {
    return hasExtCI(name, ".mp3") || hasExtCI(name, ".wav") || hasExtCI(name, ".aac") ||
           hasExtCI(name, ".flac") || hasExtCI(name, ".mid") || hasExtCI(name, ".midi");
}

static String stripExt(const String &name) {
    int dot = name.lastIndexOf('.');
    return (dot > 0) ? name.substring(0, dot) : name;
}

static void scanMediaDir(
    FS &fs, const String &dirPath, bool (*matcher)(const String &), std::vector<MediaFileItem> &out,
    int depth = 0
) {
    if (depth > 2 || out.size() >= 64) return;
    File dir = fs.open(dirPath);
    if (!dir || !dir.isDirectory()) return;

    File entry = dir.openNextFile();
    while (entry && out.size() < 64) {
        String rawName = String(entry.name());
        int slash = rawName.lastIndexOf('/');
        String base = (slash >= 0) ? rawName.substring(slash + 1) : rawName;
        String full = (dirPath == "/") ? ("/" + base) : (dirPath + "/" + base);

        if (base.startsWith(".")) {
            entry.close();
            entry = dir.openNextFile();
            continue;
        }
        if (entry.isDirectory()) {
            entry.close();
            scanMediaDir(fs, full, matcher, out, depth + 1);
        } else {
            if (matcher(base)) {
                out.push_back({full, base, (size_t)entry.size()});
            }
            entry.close();
        }
        entry = dir.openNextFile();
    }
    dir.close();
}

static FS *getPreferredMediaFs(bool &isSd) {
    if (setupSdCard()) {
        isSd = true;
        return &SD;
    }
    if (checkLittleFsSize()) {
        isSd = false;
        return &LittleFS;
    }
    isSd = false;
    return nullptr;
}

static String formatMmSs(uint32_t ms) {
    uint32_t s = ms / 1000u;
    char buf[12];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(s / 60u), (unsigned long)(s % 60u));
    return String(buf);
}

static void getLocalTimeNow(struct tm &t) {
#if defined(HAS_RTC)
    t = _rtc.getTimeStruct();
#else
    t = rtc.getTimeStruct();
#endif
}

} // namespace

// ============================================================================
// 1. MUSIC PLAYER (Phone-style Now Playing UI using Bruce async audio engine)
// ============================================================================
void media_music_player() {
#if !defined(HAS_SPEAKER)
    displayWarning("No I2S speaker on this board", true);
    return;
#else
    bool isSd = false;
    FS *fs = getPreferredMediaFs(isSd);
    if (!fs) {
        displayError("No storage found", true);
        return;
    }

    displayInfo("Scanning music files...");
    std::vector<MediaFileItem> tracks;
    scanMediaDir(*fs, "/", isMusicFileName, tracks, 0);
    std::sort(tracks.begin(), tracks.end(), [](const MediaFileItem &a, const MediaFileItem &b) {
        return a.name.compareTo(b.name) < 0;
    });

    if (tracks.empty()) {
        displayWarning("No MP3/WAV files found", true);
        return;
    }

    int idx = 0;
    bool repeatOne = false;
    bool shuffle = false;
    uint8_t volume = (uint8_t)constrain(bruceConfig.soundVolume, 0, 100);
    int animPhase = 0;
    bool exitPlayer = false;

    auto startTrack = [&](int newIdx) {
        stopAudioPlayback();
        vTaskDelay(pdMS_TO_TICKS(40));
        idx = (newIdx + (int)tracks.size()) % (int)tracks.size();
        playAudioFile(fs, tracks[idx].path, PLAYBACK_ASYNC);
        setAudioPlaybackVolume(volume);
    };

    auto drawPlayer = [&](bool fullRedraw) {
        AudioPlaybackInfo info = getAudioPlaybackInfo();
        const uint16_t bg = bruceConfig.bgColor;
        const uint16_t pri = bruceConfig.priColor;
        const uint16_t sec = bruceConfig.secColor;

        if (fullRedraw) {
            drawMainBorderWithTitle("Music Player");
            tft.setTextSize(FP);
            tft.setTextColor(TFT_DARKGREY, bg);
            tft.drawCentreString(
                bruce_i18n::tr("LR=Track  UP/DN=Vol  SEL=Play/Menu"),
                tftWidth / 2,
                tftHeight - BORDER_PAD_X - LH,
                1
            );
        }

        const int top = BORDER_PAD_Y + FM * LH + 4;
        const int bot = tftHeight - BORDER_PAD_X - LH - 4;
        const int h = max(44, bot - top);
        const int left = BORDER_PAD_X + 4;
        const int w = tftWidth - 2 * BORDER_PAD_X - 8;

        const bool showArt = (w >= 210 && h >= 88);
        const int artSize = showArt ? min(82, h - 8) : 0;
        const int infoX = showArt ? (left + artSize + 10) : left;
        const int infoW = (left + w) - infoX;

        // Animated vinyl disc + mini EQ bars
        if (showArt) {
            int cx = left + artSize / 2;
            int cy = top + h / 2;
            int r = artSize / 2 - 2;
            tft.fillRect(left, top, artSize, h, bg);
            tft.drawCircle(cx, cy, r, pri);
            tft.drawCircle(cx, cy, (r * 2) / 3, sec);
            tft.fillCircle(cx, cy, r / 3, pri);
            tft.fillCircle(cx, cy, max(3, r / 8), bg);

            float ang = (float)animPhase * 0.35f;
            int gx = cx + (int)(cosf(ang) * (r - 5));
            int gy = cy + (int)(sinf(ang) * (r - 5));
            tft.fillCircle(gx, gy, 2, TFT_YELLOW);

            // 5 mini live equalizer bars below disc center
            for (int b = 0; b < 5; ++b) {
                int bh = (info.state == PLAYBACK_PLAYING) ? (4 + ((animPhase * 3 + b * 7) % 14)) : 3;
                int bx = left + 8 + b * ((artSize - 16) / 5);
                tft.fillRect(bx, top + h - bh - 2, 5, bh, (b % 2 == 0) ? TFT_CYAN : pri);
            }
        }

        // Info panel
        tft.fillRect(infoX, top, infoW, h, bg);
        int y = top + 2;
        int maxChars = max(10, infoW / LW);

        // Track index & storage / mode badges
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, bg);
        String badge = String(isSd ? "SD " : "FS ") + String(idx + 1) + "/" + String(tracks.size());
        if (shuffle) badge += " SHF";
        if (repeatOne) badge += " RPT";
        tft.drawString(badge, infoX, y, 1);

        const char *stStr = (info.state == PLAYBACK_PLAYING)
                                ? "PLAY"
                                : ((info.state == PLAYBACK_PAUSED) ? "PAUSE" : "STOP");
        tft.setTextColor(info.state == PLAYBACK_PLAYING ? TFT_GREEN : TFT_YELLOW, bg);
        tft.drawRightString(stStr, infoX + infoW - 2, y, 1);
        y += LH + 5;

        // Track title (large)
        String title = stripExt(tracks[idx].name);
        int titleChars = max(6, infoW / (FM * LW));
        if ((int)title.length() > titleChars) title = title.substring(0, titleChars - 1) + "~";
        tft.setTextSize(FM);
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(title, infoX, y, 1);
        y += FM * LH + 5;

        // Elapsed time & progress visualizer
        tft.setTextSize(1);
        tft.setTextColor(TFT_LIGHTGREY, bg);
        tft.drawString(formatMmSs(info.position), infoX, y, 1);
        tft.drawRightString(String(tracks[idx].sizeBytes / 1024) + " KB", infoX + infoW - 2, y, 1);
        y += LH + 3;

        int barW = infoW - 4;
        tft.drawRect(infoX, y, barW, 8, sec);
        int pulsePos = (info.state == PLAYBACK_PLAYING) ? ((animPhase * 6) % max(1, barW - 18)) : 0;
        tft.fillRect(infoX + 1 + pulsePos, y + 1, min(16, barW - 2), 6, pri);
        y += 14;

        // Volume bar
        if (y + 12 <= top + h) {
            tft.setTextColor(TFT_YELLOW, bg);
            String vLbl = "VOL " + String(volume) + "%";
            tft.drawString(vLbl, infoX, y, 1);
            int vx = infoX + 54;
            int vw = max(20, infoX + infoW - vx - 2);
            tft.drawRect(vx, y, vw, 8, sec);
            int vFill = ((vw - 2) * volume) / 100;
            if (vFill > 0) tft.fillRect(vx + 1, y + 1, vFill, 6, TFT_GREEN);
        }
    };

    startTrack(0);
    drawPlayer(true);

    uint32_t lastDrawMs = 0;
    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(UpPress);
    check(DownPress);

    while (!exitPlayer && !returnToMenu) {
        wakeUpScreen();
        uint32_t now = millis();

        AudioPlaybackInfo info = getAudioPlaybackInfo();
        // Auto-advance when current track finishes
        if (info.state == PLAYBACK_IDLE) {
            if (repeatOne) startTrack(idx);
            else if (shuffle && tracks.size() > 1) startTrack(random(0, (long)tracks.size()));
            else startTrack(idx + 1);
            lastDrawMs = 0;
        }

        if (now - lastDrawMs >= 260) {
            if (info.state == PLAYBACK_PLAYING) ++animPhase;
            drawPlayer(false);
            lastDrawMs = now;
        }

        if (check(EscPress)) {
            exitPlayer = true;
        } else if (check(PrevPress)) {
            startTrack(idx - 1);
            lastDrawMs = 0;
        } else if (check(NextPress)) {
            startTrack(shuffle ? random(0, (long)tracks.size()) : (idx + 1));
            lastDrawMs = 0;
        } else if (check(UpPress)) {
            volume = (uint8_t)min(100, (int)volume + 10);
            bruceConfig.soundVolume = volume;
            setAudioPlaybackVolume(volume);
            lastDrawMs = 0;
        } else if (check(DownPress)) {
            volume = (uint8_t)max(0, (int)volume - 10);
            bruceConfig.soundVolume = volume;
            setAudioPlaybackVolume(volume);
            lastDrawMs = 0;
        } else if (check(SelPress)) {
            options = {
                {"Play / Pause", [&]() { pauseAudioPlayback(); }},
                {"Track List",
                 [&]() {
                     options.clear();
                     for (size_t i = 0; i < tracks.size(); ++i) {
                         options.push_back({stripExt(tracks[i].name), [&, i]() { startTrack((int)i); }});
                     }
                     options.push_back({"Back", []() {}});
                     loopOptions(options, MENU_TYPE_SUBMENU, "Track List");
                 }},
                {shuffle ? "Shuffle: ON" : "Shuffle: OFF", [&]() { shuffle = !shuffle; }},
                {repeatOne ? "Repeat One: ON" : "Repeat One: OFF", [&]() { repeatOne = !repeatOne; }},
                {"Exit", [&]() { exitPlayer = true; }},
            };
            loopOptions(options, MENU_TYPE_SUBMENU, "Music Player");
            if (!exitPlayer) {
                drawPlayer(true);
                lastDrawMs = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    stopAudioPlayback();
    check(SelPress);
    check(EscPress);
#endif
}

// ============================================================================
// 2. PHOTO VIEWER / GALLERY (Fullscreen image browser + Slideshow)
// ============================================================================
void media_photo_viewer() {
    bool isSd = false;
    FS *fs = getPreferredMediaFs(isSd);
    if (!fs) {
        displayError("No storage found", true);
        return;
    }

    displayInfo("Scanning photos...");
    std::vector<MediaFileItem> photos;
    scanMediaDir(*fs, "/", isImageFileName, photos, 0);
    std::sort(photos.begin(), photos.end(), [](const MediaFileItem &a, const MediaFileItem &b) {
        return a.name.compareTo(b.name) < 0;
    });

    if (photos.empty()) {
        displayWarning("No JPG/PNG/BMP/GIF found", true);
        return;
    }

    int idx = 0;
    bool showOverlay = true;
    bool slideshow = false;
    uint32_t lastSlideMs = millis();
    bool exitViewer = false;

    auto renderPhoto = [&]() {
        if (photos.empty()) return;
        idx = (idx + (int)photos.size()) % (int)photos.size();
        tft.fillScreen(TFT_BLACK);
        drawImg(*fs, photos[idx].path, 0, 0, true, 0, false);

        if (showOverlay) {
            int barH = LH + 6;
            tft.fillRect(0, tftHeight - barH, tftWidth, barH, TFT_BLACK);
            tft.drawFastHLine(0, tftHeight - barH, tftWidth, bruceConfig.secColor);
            tft.setTextSize(1);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            String leftTxt = String(idx + 1) + "/" + String(photos.size()) + " " + photos[idx].name;
            int maxC = max(10, (tftWidth - 60) / LW);
            if ((int)leftTxt.length() > maxC) leftTxt = leftTxt.substring(0, maxC - 1) + "~";
            tft.drawString(leftTxt, 4, tftHeight - barH + 3, 1);
            tft.setTextColor(slideshow ? TFT_GREEN : TFT_CYAN, TFT_BLACK);
            String rightTxt = slideshow ? "AUTO" : (String(photos[idx].sizeBytes / 1024) + "KB");
            tft.drawRightString(rightTxt, tftWidth - 4, tftHeight - barH + 3, 1);
        }
        lastSlideMs = millis();
    };

    renderPhoto();
    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(UpPress);
    check(DownPress);

    while (!exitViewer && !returnToMenu && !photos.empty()) {
        wakeUpScreen();
        if (slideshow && (millis() - lastSlideMs >= 4000)) {
            ++idx;
            renderPhoto();
        }
        if (check(EscPress)) {
            exitViewer = true;
        } else if (check(PrevPress)) {
            --idx;
            renderPhoto();
        } else if (check(NextPress)) {
            ++idx;
            renderPhoto();
        } else if (check(UpPress) || check(DownPress)) {
            showOverlay = !showOverlay;
            renderPhoto();
        } else if (check(SelPress)) {
            options = {
                {slideshow ? "Slideshow: ON" : "Slideshow: OFF", [&]() { slideshow = !slideshow; }},
                {showOverlay ? "Info Bar: ON" : "Info Bar: OFF", [&]() { showOverlay = !showOverlay; }},
                {"Photo Gallery",
                 [&]() {
                     options.clear();
                     for (size_t i = 0; i < photos.size(); ++i) {
                         String lbl = photos[i].name + " (" + String(photos[i].sizeBytes / 1024) + "K)";
                         options.push_back({lbl, [&, i]() { idx = (int)i; }});
                     }
                     options.push_back({"Back", []() {}});
                     loopOptions(options, MENU_TYPE_SUBMENU, "Photo Gallery");
                 }},
                {"Delete Photo",
                 [&]() {
                     if (fs->remove(photos[idx].path)) {
                         photos.erase(photos.begin() + idx);
                         if (idx >= (int)photos.size() && idx > 0) --idx;
                         displaySuccess("Photo deleted", true);
                     } else {
                         displayError("Delete failed", true);
                     }
                 }},
                {"Exit", [&]() { exitViewer = true; }},
            };
            loopOptions(options, MENU_TYPE_SUBMENU, "Photo Viewer");
            if (!exitViewer && !photos.empty()) renderPhoto();
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    check(SelPress);
    check(EscPress);
}

// ============================================================================
// 3. PHONE NOTES / QUICK MEMO
// ============================================================================
void phone_notes_app() {
    bool isSd = false;
    FS *fs = getPreferredMediaFs(isSd);
    if (!fs) {
        displayError("No storage found", true);
        return;
    }
    if (!fs->exists("/notes")) fs->mkdir("/notes");

    while (!returnToMenu) {
        std::vector<MediaFileItem> notes;
        scanMediaDir(*fs, "/notes", [](const String &n) { return hasExtCI(n, ".txt"); }, notes, 0);

        bool exitNotes = false;
        options.clear();
        options.push_back({"+ New Note", [&]() {
            String title = keyboard("", 20, "Note Title");
            if (title.isEmpty() || title == "\x1B") return;
            title.replace("/", "_");
            String body = keyboard("", 160, "Note Content");
            if (body == "\x1B") return;
            File f = fs->open("/notes/" + title + ".txt", FILE_WRITE);
            if (f) {
                f.println(body);
                f.close();
                displaySuccess("Note saved", true);
            } else {
                displayError("Cannot write file", true);
            }
        }});

        for (const auto &n : notes) {
            options.push_back({stripExt(n.name), [&, n]() {
                File f = fs->open(n.path, FILE_READ);
                if (!f) return;
                ScrollableTextArea area(stripExt(n.name));
                area.fromFile(f);
                f.close();
                area.show(true);

                std::vector<Option> act = {
                    {"Append Line",
                     [&]() {
                         String line = keyboard("", 120, "Append Text");
                         if (line.isEmpty() || line == "\x1B") return;
                         File wf = fs->open(n.path, FILE_APPEND);
                         if (wf) {
                             wf.println(line);
                             wf.close();
                             displaySuccess("Note updated", true);
                         }
                     }},
                    {"Delete Note",
                     [&]() {
                         if (fs->remove(n.path)) displaySuccess("Note deleted", true);
                     }},
                    {"Back", []() {}},
                };
                loopOptions(act, MENU_TYPE_SUBMENU, stripExt(n.name).c_str());
            }});
        }
        options.push_back({"Back", [&]() { exitNotes = true; }});
        int sel = loopOptions(options, MENU_TYPE_SUBMENU, "Notes");
        if (exitNotes || sel == -1) break;
    }
}

// ============================================================================
// 4. POCKET CALCULATOR
// ============================================================================
void phone_calculator_app() {
    static const char *kKeys[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"},
    };

    String entry = "0";
    double acc = 0.0;
    char pendingOp = 0;
    bool freshEntry = true;
    int curRow = 0, curCol = 0;

    auto applyOp = [&](double b) {
        if (pendingOp == '+') acc += b;
        else if (pendingOp == '-') acc -= b;
        else if (pendingOp == '*') acc *= b;
        else if (pendingOp == '/') acc = (fabs(b) > 1e-9) ? (acc / b) : 0.0;
        else acc = b;
    };

    auto fmtNum = [](double v) -> String {
        char buf[24];
        if (fabs(v - round(v)) < 1e-6 && fabs(v) < 1e12) snprintf(buf, sizeof(buf), "%.0f", round(v));
        else snprintf(buf, sizeof(buf), "%.4g", v);
        return String(buf);
    };

    auto drawCalc = [&]() {
        drawMainBorderWithTitle("Calculator");
        int top = BORDER_PAD_Y + FM * LH + 4;
        int left = BORDER_PAD_X + 6;
        int w = tftWidth - 2 * BORDER_PAD_X - 12;
        int dispH = FM * LH + 8;

        // LCD Display box
        tft.fillRoundRect(left, top, w, dispH, 4, TFT_BLACK);
        tft.drawRoundRect(left, top, w, dispH, 4, bruceConfig.secColor);
        if (pendingOp) {
            tft.setTextSize(1);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawString(fmtNum(acc) + " " + String(pendingOp), left + 6, top + 2, 1);
        }
        tft.setTextSize(FM);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawRightString(entry, left + w - 6, top + 4, 1);

        // 4x4 Key grid
        int gridTop = top + dispH + 6;
        int gridH = tftHeight - BORDER_PAD_X - gridTop - 4;
        int cellW = w / 4;
        int cellH = max(14, gridH / 4);

        tft.setTextSize(FM);
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                int bx = left + c * cellW + 2;
                int by = gridTop + r * cellH + 2;
                int bw = cellW - 4;
                int bh = cellH - 4;
                bool sel = (r == curRow && c == curCol);
                uint16_t fill = sel ? bruceConfig.priColor : bruceConfig.bgColor;
                uint16_t fg = sel ? bruceConfig.bgColor
                                  : ((c == 3 || (r == 3 && c != 1)) ? TFT_YELLOW : TFT_WHITE);
                tft.fillRoundRect(bx, by, bw, bh, 3, fill);
                tft.drawRoundRect(bx, by, bw, bh, 3, bruceConfig.secColor);
                tft.setTextColor(fg, fill);
                tft.drawCentreString(kKeys[r][c], bx + bw / 2, by + (bh - FM * LH) / 2, 1);
            }
        }
    };

    drawCalc();
    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(UpPress);
    check(DownPress);

    while (!returnToMenu) {
        bool dirty = false;
        if (check(EscPress)) break;
        if (check(PrevPress)) {
            curCol = (curCol + 3) % 4;
            dirty = true;
        } else if (check(NextPress)) {
            curCol = (curCol + 1) % 4;
            dirty = true;
        } else if (check(UpPress)) {
            curRow = (curRow + 3) % 4;
            dirty = true;
        } else if (check(DownPress)) {
            curRow = (curRow + 1) % 4;
            dirty = true;
        } else if (check(SelPress)) {
            char k = kKeys[curRow][curCol][0];
            if (k >= '0' && k <= '9') {
                if (freshEntry || entry == "0") entry = String(k);
                else if (entry.length() < 12) entry += k;
                freshEntry = false;
            } else if (k == 'C') {
                entry = "0";
                acc = 0.0;
                pendingOp = 0;
                freshEntry = true;
            } else if (k == '=') {
                applyOp(entry.toDouble());
                entry = fmtNum(acc);
                pendingOp = 0;
                freshEntry = true;
            } else {
                if (!freshEntry) applyOp(entry.toDouble());
                else if (pendingOp == 0) acc = entry.toDouble();
                pendingOp = k;
                entry = fmtNum(acc);
                freshEntry = true;
            }
            dirty = true;
        }
        if (dirty) drawCalc();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    check(SelPress);
    check(EscPress);
}

// ============================================================================
// 5. STOPWATCH & LAP TIMER
// ============================================================================
void phone_stopwatch_app() {
    bool running = false;
    uint32_t startMs = 0;
    uint32_t elapsedMs = 0;
    uint32_t laps[4] = {0};
    int lapCnt = 0;

    auto fmtWatch = [](uint32_t ms) -> String {
        uint32_t tenths = (ms / 100u) % 10u;
        uint32_t s = (ms / 1000u) % 60u;
        uint32_t m = ms / 60000u;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02lu:%02lu.%lu", (unsigned long)m, (unsigned long)s, (unsigned long)tenths);
        return String(buf);
    };

    drawMainBorderWithTitle("Stopwatch");
    tft.setTextSize(FP);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString(
        bruce_i18n::tr("SEL=Start/Stop  NEXT=Lap  PREV=Reset"),
        tftWidth / 2,
        tftHeight - BORDER_PAD_X - LH,
        1
    );

    uint32_t lastDraw = 0;
    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);

    while (!returnToMenu) {
        wakeUpScreen();
        uint32_t now = millis();
        uint32_t total = running ? (elapsedMs + (now - startMs)) : elapsedMs;

        if (now - lastDraw >= 80) {
            int y = BORDER_PAD_Y + FM * LH + 10;
            tft.setTextSize(FG);
            tft.setTextColor(running ? TFT_GREEN : TFT_YELLOW, bruceConfig.bgColor);
            tft.drawCentreString(fmtWatch(total), tftWidth / 2, y, 1);

            y += FG * LH + 8;
            tft.setTextSize(FP);
            int clearH = max(0, (tftHeight - BORDER_PAD_X - LH - 4) - y);
            tft.fillRect(BORDER_PAD_X + 6, y, tftWidth - 2 * BORDER_PAD_X - 12, clearH, bruceConfig.bgColor);
            tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
            for (int i = 0; i < lapCnt && (y + LH <= tftHeight - BORDER_PAD_X - LH - 4); ++i) {
                tft.drawCentreString(
                    "LAP " + String(i + 1) + "  " + fmtWatch(laps[i]), tftWidth / 2, y, 1
                );
                y += LH + 3;
            }
            lastDraw = now;
        }

        if (check(EscPress)) break;
        if (check(SelPress)) {
            if (running) {
                elapsedMs += (now - startMs);
                running = false;
            } else {
                startMs = now;
                running = true;
            }
            lastDraw = 0;
        } else if (check(NextPress) || check(UpPress)) {
            if (running) {
                if (lapCnt < 4) laps[lapCnt++] = total;
                else {
                    memmove(laps, laps + 1, sizeof(uint32_t) * 3);
                    laps[3] = total;
                }
                lastDraw = 0;
            }
        } else if (check(PrevPress) || check(DownPress)) {
            if (!running) {
                elapsedMs = 0;
                lapCnt = 0;
                lastDraw = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    check(SelPress);
    check(EscPress);
}

// ============================================================================
// 6. FLASHLIGHT / TORCH & SOS STROBE
// ============================================================================
void phone_flashlight_app() {
    int mode = 0; // 0=White, 1=Warm, 2=Red Night, 3=SOS Strobe
    static const uint16_t kCols[3] = {TFT_WHITE, 0xFFE8, TFT_RED};
    static const char *kNames[4] = {"WHITE", "WARM", "RED", "SOS"};
    bool strobeOn = true;
    uint32_t lastStrobe = 0;

    auto paint = [&]() {
        uint16_t col = (mode < 3) ? kCols[mode] : (strobeOn ? TFT_WHITE : TFT_BLACK);
        tft.fillScreen(col);
        uint16_t txt = (col == TFT_BLACK || col == TFT_RED) ? TFT_WHITE : TFT_BLACK;
        tft.setTextSize(1);
        tft.setTextColor(txt, col);
        tft.drawCentreString(String(kNames[mode]) + "  (LR=Mode  ESC=Exit)", tftWidth / 2, tftHeight - 12, 1);
    };

    paint();
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(SelPress);

    while (!returnToMenu) {
        wakeUpScreen();
        if (mode == 3 && millis() - lastStrobe >= 220) {
            strobeOn = !strobeOn;
            lastStrobe = millis();
            paint();
        }
        if (check(EscPress)) break;
        if (check(NextPress) || check(SelPress)) {
            mode = (mode + 1) % 4;
            strobeOn = true;
            paint();
        } else if (check(PrevPress)) {
            mode = (mode + 3) % 4;
            strobeOn = true;
            paint();
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    tft.fillScreen(bruceConfig.bgColor);
    check(EscPress);
}

// ============================================================================
// 7. PHONE HOME DASHBOARD (Lock-screen clock + 2x3 interactive app tiles)
// ============================================================================
void phone_home_setup() {
    struct AppTile {
        const char *label;
        uint16_t col;
        void (*fn)();
    };
    static const AppTile kApps[6] = {
        {"Music",  TFT_CYAN,    media_music_player  },
        {"Photos", TFT_MAGENTA, media_photo_viewer  },
        {"Notes",  TFT_YELLOW,  phone_notes_app     },
        {"Calc",   TFT_GREEN,   phone_calculator_app},
        {"Timer",  TFT_ORANGE,  phone_stopwatch_app },
        {"Torch",  TFT_WHITE,   phone_flashlight_app},
    };

    int sel = 0;
    uint32_t lastClockMs = 0;
    bool needFull = true;

    auto drawHome = [&]() {
        drawMainBorder(true);
        const uint16_t bg = bruceConfig.bgColor;

        // Large phone-style clock + date
        struct tm t;
        getLocalTimeNow(t);
        char timeBuf[12], dateBuf[20];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        int y = BORDER_PAD_Y + 2;
        tft.setTextSize(FM);
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawCentreString(timeBuf, tftWidth / 2, y, 1);
        y += FM * LH + 2;

        tft.setTextSize(1);
        tft.setTextColor(TFT_LIGHTGREY, bg);
        String statusLine = String(dateBuf) + (WiFi.status() == WL_CONNECTED ? "  WiFi" : "  NoWiFi") +
                            (sdcardMounted ? "  SD" : "  FS");
        tft.drawCentreString(statusLine, tftWidth / 2, y, 1);
        y += LH + 6;

        // 2 rows x 3 cols app launcher tiles
        int gridL = BORDER_PAD_X + 6;
        int gridW = tftWidth - 2 * BORDER_PAD_X - 12;
        int gridH = (tftHeight - BORDER_PAD_X - 6) - y;
        int cellW = gridW / 3;
        int cellH = max(24, gridH / 2);

        for (int i = 0; i < 6; ++i) {
            int r = i / 3;
            int c = i % 3;
            int bx = gridL + c * cellW + 3;
            int by = y + r * cellH + 3;
            int bw = cellW - 6;
            int bh = cellH - 6;
            bool active = (i == sel);
            uint16_t fill = active ? kApps[i].col : bg;
            uint16_t fg = active ? TFT_BLACK : kApps[i].col;

            tft.fillRoundRect(bx, by, bw, bh, 5, fill);
            tft.drawRoundRect(bx, by, bw, bh, 5, kApps[i].col);

            tft.setTextSize(FP);
            tft.setTextColor(fg, fill);
            String lbl = bruce_i18n::tr(kApps[i].label);
            int lh = (FP == 1 && bruce_i18n::isChinese()) ? 12 : LH;
            tft.drawCentreString(lbl, bx + bw / 2, by + (bh - lh) / 2, 1);
        }
        needFull = false;
        lastClockMs = millis();
    };

    check(SelPress);
    check(EscPress);
    check(PrevPress);
    check(NextPress);
    check(UpPress);
    check(DownPress);

    while (!returnToMenu) {
        wakeUpScreen();
        if (needFull || (millis() - lastClockMs >= 1000)) drawHome();

        if (check(EscPress)) break;
        if (check(PrevPress)) {
            sel = (sel + 5) % 6;
            needFull = true;
        } else if (check(NextPress)) {
            sel = (sel + 1) % 6;
            needFull = true;
        } else if (check(UpPress) || check(DownPress)) {
            sel = (sel + 3) % 6;
            needFull = true;
        } else if (check(SelPress)) {
            kApps[sel].fn();
            needFull = true;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    check(SelPress);
    check(EscPress);
}

#endif // !LITE_VERSION
