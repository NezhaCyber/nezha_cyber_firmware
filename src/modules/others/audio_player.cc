// Senape3000 (Really Ugly) Audio Player UI

#include "audio_player.h"
#if defined(HAS_SPEAKER)
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "core/settings.h"
#include "modules/others/audio.h"
#include <Arduino.h>

// ===== ENUMS & STRUCTS =====

enum IconType { ICON_PREV, ICON_PLAY, ICON_PAUSE, ICON_NEXT, ICON_VOL, ICON_LOOP_ON, ICON_LOOP_OFF };

struct PlayerState {
    bool isPlaying;
    bool isPaused;
    bool loopEnabled;
    String filename;
    unsigned long lastUpdate;
    int scrollOffset;
    unsigned long lastPauseAction;
};

struct UILayout {
    int MARGIN_X;
    int MARGIN_Y;
    int HEADER_HEIGHT;
    int DISPLAY_HEIGHT;
    int PROGRESS_HEIGHT;
    int CONTROLS_HEIGHT;
    int BUTTON_SIZE;
    int BUTTON_SPACING;
    int TEXT_SIZE_LARGE;
    int TEXT_SIZE_SMALL;

    void calculate() {
        bool isLarge = (tftWidth > 200 && tftHeight > 200);
        MARGIN_X = isLarge ? 10 : 5;
        MARGIN_Y = isLarge ? 10 : 5;
        HEADER_HEIGHT = isLarge ? 30 : 22;
        DISPLAY_HEIGHT = isLarge ? 40 : 30;
        PROGRESS_HEIGHT = isLarge ? 12 : 8;
        CONTROLS_HEIGHT = isLarge ? 70 : 50;
        BUTTON_SIZE = isLarge ? 50 : 36;
        TEXT_SIZE_LARGE = isLarge ? 2 : 1;
        TEXT_SIZE_SMALL = 1;
    }
};

// ===== HELPER FUNCTIONS =====

String formatTime(unsigned long ms) {
    unsigned long totalSeconds = ms / 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
    return String(buffer);
}

// Delete file extension in filename display
String extractFilename(const String &filepath) {
    int lastSlash = filepath.lastIndexOf('/');
    int lastDot = filepath.lastIndexOf('.');
    String name = filepath.substring(lastSlash + 1);
    // Remove extension
    if (lastDot > lastSlash) { name = name.substring(0, lastDot - lastSlash - 1); }
    return name;
}

// ===== GRAPHICS ENGINE =====

// Draw vector icons with better visibility
void drawVectorIcon(int x, int y, int size, IconType icon, uint16_t color) {
    int cx = x + size / 2;
    int cy = y + size / 2;
    int r = size / 2 - 2; // Larger icon radius for better visibility

    switch (icon) {
        case ICON_PLAY: {
            // Triangle play icon - shifted right for better centering
            int offset = -r / 4; // 3 More left
            tft.fillTriangle(
                cx - r / 2 + offset, cy - r, cx - r / 2 + offset, cy + r, cx + r + offset, cy, color
            );
            break;
        }

        case ICON_PAUSE: {
            // Two vertical bars - centered
            int barWidth = r / 1.5;
            int gap = r / 4;
            tft.fillRoundRect(cx - barWidth - gap / 2, cy - r, barWidth, r * 2, 2, color);
            tft.fillRoundRect(cx + gap / 2, cy - r, barWidth, r * 2, 2, color);
            break;
        }

        case ICON_PREV: {
            // Two triangles pointing left - shifted
            int offset = -r / 6;
            tft.fillTriangle(cx + offset, cy - r, cx + offset, cy + r, cx - r + offset, cy, color);
            tft.fillTriangle(cx + r + offset, cy - r, cx + r + offset, cy + r, cx + offset, cy, color);
            break;
        }

        case ICON_VOL: {
            // Speaker cone - better centered
            int offset = -r / 4;
            tft.fillTriangle(cx - r + offset, cy, cx + offset, cy - r, cx + offset, cy + r, color);
            tft.fillRect(cx - r + offset, cy - r / 2, r / 2, r, color);
            // Sound waves
            tft.drawCircle(cx + r / 3, cy, r / 2, color);
            tft.drawCircle(cx + r / 3, cy, r, color);
            // Redraw cone on top
            tft.fillTriangle(cx - r + offset, cy, cx + offset, cy - r, cx + offset, cy + r, color);
            break;
        }

        case ICON_LOOP_ON: {
            // Main circle
            tft.drawCircle(cx, cy, r, color);
            // Arrow tip to indicate loop
            tft.fillTriangle(cx + r - 3, cy - 3, cx + r + 3, cy - 3, cx + r, cy + 3, color);
            tft.setTextColor(color);
            tft.setTextSize(FP);
            tft.setCursor(cx - 2, cy - 3);
            tft.print("1");
            break;
        }

        case ICON_LOOP_OFF: {
            uint16_t muted = uiTextMutedColor();
            tft.drawCircle(cx, cy, r, muted);
            tft.fillTriangle(cx + r - 3, cy - 3, cx + r + 3, cy - 3, cx + r, cy + 3, muted);
            tft.setTextColor(muted);
            tft.setTextSize(FP);
            tft.setCursor(cx - 2, cy - 3);
            tft.print("1");
            break;
        }

        default: break;
    }
}

// Draw theme-aware transport button container + centered vector icon
void drawButton(int x, int y, int size, IconType icon, bool selected, bool active) {
    uint16_t pri = bruceConfig.priColor;
    uint16_t fillCol = selected ? pri : uiSurfaceHiColor();
    uint16_t iconCol = selected ? uiTextOnAccentColor(pri) : (active ? uiTextBrightColor() : uiTextMutedColor());
    uint16_t borderCol = selected ? bruceConfig.secColor : uiBorderDimColor();

    tft.fillRoundRect(x, y, size, size, 8, fillCol);
    tft.drawRoundRect(x, y, size, size, 8, borderCol);

    int iconPadding = max(5, size / 6);
    drawVectorIcon(x + iconPadding, y + iconPadding, size - 2 * iconPadding, icon, iconCol);
}

// Draw smooth rounded Nezha progress bar
void drawProgressBar(int x, int y, int width, int height, unsigned long position, unsigned long duration) {
    uint16_t surf = uiSurfaceColor();
    int barH = max(5, min(height, 8));
    int barY = y + (height - barH) / 2;

    tft.fillRect(x - 4, y - 2, width + 8, height + 4, surf);
    tft.fillRoundRect(x, barY, width, barH, barH / 2, uiSurfaceHiColor());

    if (duration > 0) {
        int fillWidth = (int)((uint64_t)position * width / duration);
        if (fillWidth > width) fillWidth = width;
        if (fillWidth > 0) {
            tft.fillRoundRect(x, barY, fillWidth, barH, barH / 2, bruceConfig.priColor);
            if (fillWidth > 4) {
                tft.fillCircle(x + fillWidth, y + height / 2, 4, bruceConfig.secColor);
            }
        }
    }
}

// Theme-aware volume control popup card
bool showVolumeControl(uint8_t &currentVolume) {
    bool compact = (tftHeight < 160);
    const int BAR_WIDTH = compact ? 54 : 68;
    const int BAR_HEIGHT = compact ? 96 : 136;
    const int BAR_X = (tftWidth - BAR_WIDTH) / 2;
    const int BAR_Y = max((int)STATUS_BAR_HEIGHT + 2, (tftHeight - BAR_HEIGHT) / 2);

    uint8_t tempVolume = currentVolume;
    bool changed = false;

    auto drawVolumeBar = [&]() {
        uint16_t surf = uiSurfaceColor();
        uiDrawCard(BAR_X, BAR_Y, BAR_WIDTH, BAR_HEIGHT, true);

        drawVectorIcon(BAR_X + (BAR_WIDTH - 18) / 2, BAR_Y + 9, 18, ICON_VOL, bruceConfig.priColor);

        int innerBarH = BAR_HEIGHT - 54;
        int innerBarW = 10;
        int innerBarX = BAR_X + (BAR_WIDTH - innerBarW) / 2;
        int innerBarY = BAR_Y + 32;

        tft.fillRoundRect(innerBarX, innerBarY, innerBarW, innerBarH, 4, uiSurfaceHiColor());

        int fillH = (tempVolume * innerBarH) / 100;
        uint16_t volColor = (tempVolume > 85) ? bruceConfig.secColor : bruceConfig.priColor;
        if (fillH > 0) {
            tft.fillRoundRect(innerBarX, innerBarY + (innerBarH - fillH), innerBarW, fillH, 4, volColor);
        }

        char vbuf[8];
        snprintf(vbuf, sizeof(vbuf), "%d%%", tempVolume);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(uiTextBrightColor(), surf);
        tft.setTextSize(FP);
        tft.fillRect(BAR_X + 4, BAR_Y + BAR_HEIGHT - 16, BAR_WIDTH - 8, 12, surf);
        tft.drawCentreString(vbuf, BAR_X + BAR_WIDTH / 2, BAR_Y + BAR_HEIGHT - 15, 1);
    };

    drawVolumeBar();

    while (true) {
        InputHandler();
        wakeUpScreen();

        bool needsRedraw = false;

        if (check(NextPress) || check(UpPress)) {
            if (tempVolume < 100) {
                tempVolume += 10;
                if (tempVolume > 100) tempVolume = 100;
                needsRedraw = true;
                changed = true;
            }
        }

        if (check(PrevPress) || check(DownPress)) {
            if (tempVolume >= 5) {
                tempVolume -= 5;
            } else {
                tempVolume = 0;
            }
            needsRedraw = true;
            changed = true;
        }

        if (needsRedraw) {
            setAudioPlaybackVolume(tempVolume);
            drawVolumeBar();
            delay(50);
        }

        if (check(SelPress)) {
            currentVolume = tempVolume;
            delay(200);
            return changed;
        }

        if (check(EscPress)) {
            if (changed) setAudioPlaybackVolume(currentVolume);
            delay(200);
            return false;
        }
    }
}
// ===== MAIN PLAYER UI =====

void musicPlayerUI(FS *fs, const String &filepath) {
    if (!fs || filepath.isEmpty() || !fs->exists(filepath)) {
        displayError("File Invalid", true);
        return;
    }

    UILayout ui;
    ui.calculate();

    PlayerState player;
    player.isPlaying = false;
    player.isPaused = false;
    player.loopEnabled = false;
    player.filename = extractFilename(filepath);
    player.lastUpdate = 0;
    player.scrollOffset = 0;
    player.lastPauseAction = 0;

    enum ControlButton { BTN_PREV = 0, BTN_PLAY = 1, BTN_VOLUME = 2, BTN_LOOP = 3, BTN_COUNT = 4 };
    int selectedButton = BTN_PLAY;

    unsigned long currentPosition = 0;
    unsigned long duration = 0;
    uint8_t currentVolume = bruceConfig.soundVolume;

    // Extract uppercase file format badge (e.g. MP3, WAV, FLAC)
    String formatBadge = "AUDIO";
    int extDot = filepath.lastIndexOf('.');
    if (extDot > 0 && extDot + 1 < (int)filepath.length()) {
        formatBadge = filepath.substring(extDot + 1);
        formatBadge.toUpperCase();
        if (formatBadge.length() > 5) formatBadge = formatBadge.substring(0, 5);
    }

    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 12;
    int cardY = BORDER_PAD_Y + 23;
    int cardW = tftWidth - 2 * cardX;
    int cardH = compact ? 42 : 64;
    int barX = cardX + 10;
    int barW = cardW - 20;
    int barY = cardY + cardH - (compact ? 14 : 20);
    int btnSize = compact ? 32 : 44;

    auto drawHeader = [&]() {
        drawMainBorderWithTitle("Audio Player", true);
        uiDrawCard(cardX, cardY, cardW, cardH, true);
    };

    auto drawTrackInfo = [&]() {
        uint16_t surf = uiSurfaceColor();
        int rowY = cardY + (compact ? 7 : 11);
        tft.fillRect(cardX + 6, rowY - 1, cardW - 12, compact ? 18 : 22, surf);

        int16_t badgeW = uiDrawBadge(cardX + 10, rowY, formatBadge, bruceConfig.priColor, 16);
        int textX = cardX + 16 + badgeW;
        int availPx = (cardX + cardW - 10) - textX;

        String displayText = player.filename;
        if (!compact) {
            uiDrawText16(displayText, textX, rowY, availPx, uiTextBrightColor(), surf, 0);
        } else {
            tft.setTextDatum(TL_DATUM);
            tft.setTextSize(FP);
            tft.setTextColor(uiTextBrightColor(), surf);
            tft.drawString(bruce_i18n::utf8TruncateToWidth(displayText, availPx, FP), textX, rowY + 4, 1);
        }
    };

    auto drawTimers = [&]() {
        uint16_t surf = uiSurfaceColor();
        int ty = barY - (compact ? 10 : 12);
        tft.fillRect(barX, ty, barW, LH * FP + 2, surf);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(FP);
        tft.setTextColor(uiTextMutedColor(), surf);

        String curr = formatTime(currentPosition);
        String tot = duration > 0 ? formatTime(duration) : "--:--";
        tft.drawString(curr, barX, ty, 1);
        tft.drawRightString(tot, barX + barW, ty, 1);
    };

    auto drawControls = [&]() {
        int startY = cardY + cardH + (compact ? 6 : 16);
        if (startY + btnSize > tftHeight - 6) startY = tftHeight - btnSize - 6;
        int totalWidth = cardW;
        int gap = max(6, (totalWidth - (BTN_COUNT * btnSize)) / (BTN_COUNT - 1));
        int x = cardX + max(0, (totalWidth - (BTN_COUNT * btnSize + (BTN_COUNT - 1) * gap)) / 2);

        drawButton(x, startY, btnSize, ICON_PREV, selectedButton == BTN_PREV, true);
        x += btnSize + gap;

        IconType ppIcon = (player.isPlaying && !player.isPaused) ? ICON_PAUSE : ICON_PLAY;
        drawButton(x, startY, btnSize, ppIcon, selectedButton == BTN_PLAY, true);
        x += btnSize + gap;

        drawButton(x, startY, btnSize, ICON_VOL, selectedButton == BTN_VOLUME, true);
        x += btnSize + gap;

        IconType loopIcon = player.loopEnabled ? ICON_LOOP_ON : ICON_LOOP_OFF;
        drawButton(x, startY, btnSize, loopIcon, selectedButton == BTN_LOOP, player.loopEnabled);
    };

    // Initial Draw
    drawHeader();
    drawTrackInfo();
    drawTimers();
    drawProgressBar(barX, barY, barW, ui.PROGRESS_HEIGHT, 0, 1);
    drawControls();

    unsigned long lastPosUpdate = 0;
    // MAIN LOOP
    while (true) {
        InputHandler();
        wakeUpScreen();

        bool controlsNeedRedraw = false;
        bool progressNeedsRedraw = false;
        bool infoNeedsRedraw = false;

        AudioPlaybackInfo info = getAudioPlaybackInfo();

        // Playback state check
        bool actualPlaying = (info.state == PLAYBACK_PLAYING);
        if (player.isPlaying != actualPlaying && !player.isPaused) {
            // State change detected externally (e.g. end of track)
        }

        // Update position more smoothly (every 200ms)
        if (millis() - lastPosUpdate > 200) {
            if (currentPosition != info.position || info.duration != duration) {
                currentPosition = info.position;
                duration = info.duration;
                progressNeedsRedraw = true;
            }

            // Scroll text logic
            if (player.filename.length() > 15) infoNeedsRedraw = true;
            lastPosUpdate = millis();
        }

        // Loop check
        if (player.loopEnabled && info.state == PLAYBACK_IDLE && player.isPlaying && !player.isPaused) {
            playAudioFile(fs, filepath, PLAYBACK_ASYNC);
            controlsNeedRedraw = true;
        }

        // INPUT HANDLING
        if (check(PrevPress)) {
            selectedButton = (selectedButton - 1 + BTN_COUNT) % BTN_COUNT;
            controlsNeedRedraw = true;
        }

        if (check(NextPress)) {
            selectedButton = (selectedButton + 1) % BTN_COUNT;
            controlsNeedRedraw = true;
        }
        if (check(SelPress)) {
            // Anti-bounce for play button
            if (selectedButton == BTN_PLAY && millis() - player.lastPauseAction < 300) {
                // Ignore rapid presses
            } else {
                switch (selectedButton) {
                    case BTN_PREV:
                        stopAudioPlayback();
                        delay(50);
                        playAudioFile(fs, filepath, PLAYBACK_ASYNC);
                        player.isPlaying = true;
                        player.isPaused = false;
                        controlsNeedRedraw = true;
                        break;

                    case BTN_PLAY:
                        if (player.isPlaying && !player.isPaused) {
                            // Pause
                            pauseAudioPlayback();
                            player.isPaused = true;
                        } else if (player.isPaused) {
                            // Restart from pause pauseAudioPlayback (toggle)
                            pauseAudioPlayback();
                            player.isPaused = false;
                        } else {
                            // Play New File
                            playAudioFile(fs, filepath, PLAYBACK_ASYNC);
                            player.isPlaying = true;
                            player.isPaused = false;
                        }
                        player.lastPauseAction = millis();
                        controlsNeedRedraw = true;
                        break;

                    case BTN_VOLUME:
                        showVolumeControl(currentVolume);
                        tft.fillScreen(bruceConfig.bgColor); // Full redraw after popup
                        drawHeader();
                        infoNeedsRedraw = true;
                        progressNeedsRedraw = true;
                        controlsNeedRedraw = true;
                        break;

                    case BTN_LOOP:
                        player.loopEnabled = !player.loopEnabled;
                        controlsNeedRedraw = true;
                        break;
                }
            }
        }
        if (check(EscPress)) {
            stopAudioPlayback();
            break;
        }

        // REDRAWS
        if (infoNeedsRedraw) drawTrackInfo();

        if (progressNeedsRedraw) {
            drawTimers();
            drawProgressBar(barX, barY, barW, ui.PROGRESS_HEIGHT, currentPosition, duration);
        }

        if (controlsNeedRedraw) drawControls();

        delay(30); // Loop for responsive input
    }

    tft.fillScreen(bruceConfig.bgColor);
}
#endif
