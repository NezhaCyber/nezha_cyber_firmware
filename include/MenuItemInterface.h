#ifndef __MENU_ITEM_INTERFACE_H__
#define __MENU_ITEM_INTERFACE_H__

#include "core/display.h"
#include "core/i18n.h"
#include <globals.h>

// Pixel span a menu icon covers when drawn at scale 1. Lower it to render the icons
// bigger inside the same box, raise it if the widest ones start touching their label.
#define ICON_SCALE_REFERENCE 80.0f

class MenuItemInterface {
public:
    virtual ~MenuItemInterface() = default;
    virtual void optionsMenu(void) = 0;
    virtual void drawIcon(float scale = 1) = 0;
    virtual void drawIconImg() {
        drawImg(
            *bruceConfig.themeFS(),
            bruceConfig.getThemeItemImg(themePath()),  // const String& - no heap alloc
            0,
            imgCenterY,
            true,
            bruceConfig.theme.gifDuration,
            false
        );
    }
    virtual bool hasTheme() = 0;
    virtual const String& themePath() = 0;

    bool checkTheme() { return hasTheme() && themePath().length() > 0; }
    String getName() const { return String(_name); }

    void draw(float scale = 1) {
        if (rotation != bruceConfigPins.rotation) resetCoordinates();
        if (!checkTheme()) {
            uint16_t bg = bruceConfig.bgColor;
            uint16_t pri = bruceConfig.priColor;
            uint16_t surface = uiSurfaceColor();
            uint16_t borderDim = uiBorderDimColor();
            uint16_t textBright = uiTextBrightColor();

            int contentTop = STATUS_BAR_HEIGHT - 3;
            int contentBottom = tftHeight - 5;
            tft.fillRect(0, contentTop, tftWidth, contentBottom - contentTop, bg);

            bool compactScreen = (tftHeight < 160);
            int pillH = compactScreen ? 20 : 26;
            int pillY = contentBottom - pillH - (compactScreen ? 2 : 6);
            int cardTop = contentTop + (compactScreen ? 3 : 8);
            int cardH = pillY - cardTop - (compactScreen ? 4 : 8);
            if (cardH < 36) cardH = 36;
            int cardW = min(tftWidth - (compactScreen ? 64 : 84), cardH + (compactScreen ? 34 : 44));
            int cardX = (tftWidth - cardW) / 2;

            // Elevated Cyber Spotlight Card framing the module icon
            tft.fillRoundRect(cardX, cardTop, cardW, cardH, 8, surface);
            tft.drawRoundRect(cardX, cardTop, cardW, cardH, 8, borderDim);
            tft.fillRect(cardX + 12, cardTop, cardW - 24, 2, pri);
            tft.fillRect(cardX + cardW / 2 - 12, cardTop + cardH - 2, 24, 2, pri);

            int box = min(cardW - 18, cardH - 14);
            if (box < 20) box = 20;
            drawIconInBox(tftWidth / 2, cardTop + cardH / 2, box, textBright, surface);

            drawArrows(scale);
            drawTitle(scale);
        } else {
            if (bruceConfig.theme.label)
                drawTitle(scale); // If using .GIF, labels are draw after complete, which takes some time
            drawIconImg();
            if (bruceConfig.theme.label) drawTitle(scale); // Makes sure to draw over the image
        }
        drawStatusBar();
    }

    // Renders drawIcon() inside an arbitrary box instead of the full screen icon area,
    // used by the grid main menu. The icons paint straight to `tft` using the member
    // coordinates and read their colors from bruceConfig, so both are swapped for the
    // call and put back afterwards.
    void drawIconInBox(int centerX, int centerY, int box, uint16_t fgColor, uint16_t bgColor) {
        int oldCenterX = iconCenterX, oldCenterY = iconCenterY;
        int oldAreaX = iconAreaX, oldAreaY = iconAreaY;
        int oldAreaW = iconAreaW, oldAreaH = iconAreaH;
        uint16_t oldPriColor = bruceConfig.priColor;
        uint16_t oldBgColor = bruceConfig.bgColor;

        iconCenterX = centerX;
        iconCenterY = centerY;
        iconAreaW = box;
        iconAreaH = box;
        iconAreaX = centerX - box / 2;
        iconAreaY = centerY - box / 2;
        bruceConfig.priColor = fgColor;
        bruceConfig.bgColor = bgColor;

        // The widest icon spans about ICON_SCALE_REFERENCE px at scale 1, so dividing by it
        // keeps every icon inside `box` and stops it bleeding into the neighbouring cells.
        drawIcon((float)box / ICON_SCALE_REFERENCE);

        bruceConfig.priColor = oldPriColor;
        bruceConfig.bgColor = oldBgColor;
        iconCenterX = oldCenterX;
        iconCenterY = oldCenterY;
        iconAreaX = oldAreaX;
        iconAreaY = oldAreaY;
        iconAreaW = oldAreaW;
        iconAreaH = oldAreaH;
    }

    void drawArrows(float scale = 1) {
        (void)scale;
        bool compactScreen = (tftHeight < 160);
        int contentTop = STATUS_BAR_HEIGHT - 3;
        int pillH = compactScreen ? 20 : 26;
        int pillY = (tftHeight - 5) - pillH - (compactScreen ? 2 : 6);
        int cardTop = contentTop + (compactScreen ? 3 : 8);
        int cardH = max(36, pillY - cardTop - (compactScreen ? 4 : 8));
        int arrowY = cardTop + cardH / 2;

        int arrowSize = compactScreen ? 7 : 10;
        int lineWidth = compactScreen ? 2 : 3;
        int arrowX = compactScreen ? 10 : 16;

        uint16_t pri = bruceConfig.priColor;
        uint16_t bg = bruceConfig.bgColor;

        // Left sleek chevron
        tft.drawWideLine(arrowX, arrowY, arrowX + arrowSize, arrowY + arrowSize, lineWidth, pri, bg);
        tft.drawWideLine(arrowX, arrowY, arrowX + arrowSize, arrowY - arrowSize, lineWidth, pri, bg);

        // Right sleek chevron
        tft.drawWideLine(
            tftWidth - arrowX, arrowY, tftWidth - arrowX - arrowSize, arrowY + arrowSize, lineWidth, pri, bg
        );
        tft.drawWideLine(
            tftWidth - arrowX, arrowY, tftWidth - arrowX - arrowSize, arrowY - arrowSize, lineWidth, pri, bg
        );
    }

    void drawTitle(float scale = 1) {
        (void)scale;
        bool compactScreen = (tftHeight < 160);
        int pillH = compactScreen ? 20 : 26;
        int pillY = (tftHeight - 5) - pillH - (compactScreen ? 2 : 6);
        int pillW = tftWidth - (compactScreen ? 48 : 68);
        int pillX = (tftWidth - pillW) / 2;

        uint16_t pri = bruceConfig.priColor;
        uint16_t surfaceHi = uiSurfaceHiColor();
        uint16_t textBright = uiTextBrightColor();

        tft.fillRoundRect(pillX, pillY, pillW, pillH, 5, surfaceHi);
        tft.drawRoundRect(pillX, pillY, pillW, pillH, 5, pri);
        tft.fillRect(pillX + 2, pillY + 4, 3, pillH - 8, pri);

        tft.setTextSize(FM);
        tft.setTextColor(textBright, surfaceHi);
        String label = bruce_i18n::tr(getName());
        String fitted = bruce_i18n::utf8TruncateToWidth(label, pillW - 18, FM);
        int textY = pillY + (pillH - FM * LH) / 2;
        tft.drawCentreString(fitted, tftWidth / 2, textY, 1);
    }

protected:
    const char *_name = "";
    uint8_t rotation = ROTATION;

    int iconAreaH =
        ((tftHeight - 2 * BORDER_PAD_Y) % 2 == 0 ? tftHeight - 2 * BORDER_PAD_Y
                                                 : tftHeight - 2 * BORDER_PAD_Y + 1);
    int iconAreaW = iconAreaH;

    int iconCenterX = tftWidth / 2;
    int iconCenterY = tftHeight / 2;
    int imgCenterY = 13;

    int iconAreaX = iconCenterX - iconAreaW / 2;
    int iconAreaY = iconCenterY - iconAreaH / 2;

    int arrowAreaX = BORDER_PAD_X;
    int arrowAreaW = iconAreaX - arrowAreaX;

    MenuItemInterface(const char *name) : _name(name) {}

    void clearIconArea(void) {
        tft.fillRect(iconAreaX, iconAreaY, iconAreaW, iconAreaH, bruceConfig.bgColor);
    }
    void clearImgArea(void) { tft.fillRect(7, 27, tftWidth - 14, tftHeight - 34, bruceConfig.bgColor); }
    void resetCoordinates(void) {
        // Recalculate Center and ared due to portrait/landscape changings
        if (tftWidth > tftHeight) {
            iconAreaH =
                ((tftHeight - 2 * BORDER_PAD_Y) % 2 == 0 ? tftHeight - 2 * BORDER_PAD_Y
                                                         : tftHeight - 2 * BORDER_PAD_Y + 1);
        } else {
            iconAreaH =
                ((tftWidth - 2 * BORDER_PAD_Y) % 2 == 0 ? tftWidth - 2 * BORDER_PAD_Y
                                                        : tftWidth - 2 * BORDER_PAD_Y + 1);
        }

        iconAreaW = iconAreaH;

        iconCenterX = tftWidth / 2;
        iconCenterY = tftHeight / 2;

        iconAreaX = iconCenterX - iconAreaW / 2;
        iconAreaY = iconCenterY - iconAreaH / 2;

        arrowAreaX = BORDER_PAD_X;
        arrowAreaW = iconAreaX - arrowAreaX;

        rotation = bruceConfigPins.rotation;
    }

private:
};

#endif // __MENU_ITEM_INTERFACE_H__
