#include "main_menu.h"
#include "display.h"
#include "i18n.h"
#include "utils.h"
#include <globals.h>

MainMenu::MainMenu() {
    _menuItems = {
        &wifiMenu,
        &bleMenu,
#if !defined(LITE_VERSION)
        &radarMenu,
#endif
        &rfMenu,
        &nrf24Menu,
#if !defined(LITE_VERSION)
        &loraMenu,
#endif
#if !defined(LITE_VERSION)
        &fmMenu,
#endif
        &irMenu,
#if !defined(LITE_VERSION)
        &ethernetMenu,
#endif
        &gpsMenu,
        &rfidMenu,
        &fileMenu,
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
        &scriptsMenu,
#endif
        &clockMenu,
        &othersMenu,
#if !defined(LITE_VERSION)
        &wireToolsMenu,
        &phoneMenu,
#endif
        &configMenu,
    };

    _totalItems = _menuItems.size();

#if defined(HAS_TOUCH)
    gridPageTapHandler = [](int x, int y, int currentIndex, int &newIndex) -> bool {
        return mainMenu.handleGridPageTap(x, y, currentIndex, newIndex);
    };
#endif
}

MainMenu::~MainMenu() {}

void MainMenu::begin(void) {
    returnToMenu = false;
    options = {};

    int style = bruceConfig.mainMenuStyle;
    bool carouselStyle = (style == MAIN_MENU_CAROUSEL);

    // Option::hover is a plain function pointer, so renderers stay capture-less
    // and resolve the active index from the MenuItemInterface pointer they receive.
    bool (*renderItem)(void *menuItem, bool shouldRender) = [](void *menuItem, bool shouldRender) {
        if (!shouldRender) return false;
        drawMainBorder(false);

        MenuItemInterface *obj = static_cast<MenuItemInterface *>(menuItem);
        float scale = float((float)tftWidth / (float)240);
        if (bruceConfigPins.rotation & 0b01) scale = float((float)tftHeight / (float)135);
        obj->draw(scale);

        // Subtle muted position counter in top-right below status bar
        int idx = mainMenu.gridIndexOf(obj);
        int total = (int)options.size();
        if (total > 0) {
            char buf[12];
            snprintf(buf, sizeof(buf), "%d/%d", idx + 1, total);
            tft.setTextSize(FP);
            tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
            tft.drawRightString(buf, tftWidth - 8, STATUS_BAR_HEIGHT - 1, 1);
        }
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
        return true;
    };

    if (!carouselStyle) {
        renderItem = [](void *menuItem, bool shouldRender) {
            if (!shouldRender) return false;
            mainMenu.drawActiveStyle(mainMenu.gridIndexOf(static_cast<MenuItemInterface *>(menuItem)));
            return true;
        };
    }

    std::vector<String> l = bruceConfig.disabledMenus;
    for (int i = 0; i < _totalItems; i++) {
        String itemName = _menuItems[i]->getName();
        if (find(l.begin(), l.end(), itemName) == l.end()) { // If menu item is not disabled
            options.push_back(
                {// selected lambda
                 itemName,
                 [this, i]() { _menuItems[i]->optionsMenu(); },
                 false, // selected = false
                 renderItem,
                 _menuItems[i]
                }
            );
        }
    }

    _gridScroll = 0;
    _gridLastIndex = -1;
    _gridRedrawAll = true;
    _lastStyle = style;
    _launchRectW = 0;
    _pageStripW = 0;

    if (style == MAIN_MENU_GRID) {
        buildGridLayout(options.size(), false);
        mainMenuGridColumns = _grid.cols;
    } else if (style == MAIN_MENU_COMPACT) {
        buildGridLayout(options.size(), true);
        mainMenuGridColumns = _grid.cols;
    } else {
        mainMenuGridColumns = 0;
    }

    _currentIndex = loopOptions(options, MENU_TYPE_MAIN, "Main Menu", _currentIndex);
};

void MainMenu::clearOptionRects() {
    for (auto &opt : options) {
        opt.w = 0;
        opt.h = 0;
    }
}

void MainMenu::drawActiveStyle(int index) {
    switch (bruceConfig.mainMenuStyle) {
        case MAIN_MENU_GRID: drawGrid(index); break;
        case MAIN_MENU_LIST: drawList(index); break;
        case MAIN_MENU_CARDS: drawCards(index); break;
        case MAIN_MENU_DOCK: drawDock(index); break;
        case MAIN_MENU_SPLIT: drawSplit(index); break;
        case MAIN_MENU_COMPACT: drawCompactGrid(index); break;
        default: drawGrid(index); break;
    }
}

/*********************************************************************
**  Function: gridIndexOf
**  Position of a menu item inside the currently built options list.
**********************************************************************/
int MainMenu::gridIndexOf(MenuItemInterface *item) {
    for (size_t i = 0; i < options.size(); i++) {
        if (options[i].hoverPointer == item) return (int)i;
    }
    return 0;
}

/*********************************************************************
**  Function: buildGridLayout
**  Computes cell geometry for both standard Grid (4x3 / 3x2) and
**  high-density Compact Grid (5x3 / 4x2 + bottom title bar).
**********************************************************************/
void MainMenu::buildGridLayout(int itemCount, bool compactTiles) {
    const bool compactScreen = (tftHeight < 160);
    const int PAGE_TAP_W = 10;

    if (itemCount < 1) itemCount = 1;

    int areaX = 6;
    int areaY = STATUS_BAR_HEIGHT - 2;
    int areaW = tftWidth - areaX - PAGE_TAP_W - 2;
    int bottomReserve = compactTiles ? (compactScreen ? 23 : 30) : 4;
    int areaH = tftHeight - areaY - bottomReserve;

    if (compactTiles) {
        _grid.cols = compactScreen ? 4 : 5;
        if (_grid.cols > itemCount) _grid.cols = itemCount;
        _grid.visibleRows = compactScreen ? 2 : 3;
        _grid.rows = (itemCount + _grid.cols - 1) / _grid.cols;
        if (_grid.visibleRows > _grid.rows) _grid.visibleRows = _grid.rows;
        _grid.cellW = areaW / _grid.cols;
        _grid.cellH = areaH / _grid.visibleRows;
        _grid.labelSize = FP;
        _grid.iconBox = min(_grid.cellW - 14, _grid.cellH - 14);
        if (_grid.iconBox < 18) _grid.iconBox = 18;
    } else {
        const int MIN_CELL_W = 64 * FP;
        const int MIN_CELL_H = (compactScreen ? 46 : 56) * FP;
        int maxCols = max(1, min(itemCount, areaW / MIN_CELL_W));
        int bestCols = 1;
        int bestScore = -1;
        for (int cols = 1; cols <= maxCols; cols++) {
            int rows = (itemCount + cols - 1) / cols;
            int score = min(areaW / cols, areaH / rows);
            if (score > bestScore) {
                bestScore = score;
                bestCols = cols;
            }
        }
        int maxVisibleRows = max(1, areaH / MIN_CELL_H);
        if (bestCols * maxVisibleRows < itemCount && maxCols > bestCols) {
            bestCols = maxCols;
        }
        _grid.cols = bestCols;
        _grid.cellW = areaW / _grid.cols;
        _grid.rows = (itemCount + _grid.cols - 1) / _grid.cols;
        _grid.visibleRows = maxVisibleRows;
        if (_grid.visibleRows > _grid.rows) _grid.visibleRows = _grid.rows;
        _grid.cellH = areaH / _grid.visibleRows;
        _grid.labelSize = FP;
        int labelReserve = 14;
        _grid.iconBox = _grid.cellH - labelReserve - 14;
        if (_grid.iconBox > _grid.cellW - 18) _grid.iconBox = _grid.cellW - 18;
        if (_grid.iconBox < 18) _grid.iconBox = 18;
    }

    _grid.x = areaX + (areaW - _grid.cols * _grid.cellW) / 2;
    _grid.y = areaY + (areaH - _grid.visibleRows * _grid.cellH) / 2;

    _gridItemCount = itemCount;
    _gridRotation = bruceConfigPins.rotation;
}

/*********************************************************************
**  Style 1: Standard Open Grid (4x3 T-Deck, 3x2 M5StickC Plus 1.1)
**********************************************************************/
void MainMenu::drawGrid(int index) {
    int itemCount = options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    if (itemCount != _gridItemCount || _gridRotation != bruceConfigPins.rotation || _lastStyle != MAIN_MENU_GRID) {
        buildGridLayout(itemCount, false);
        _gridScroll = 0;
        _gridLastIndex = -1;
        _gridRedrawAll = true;
        _lastStyle = MAIN_MENU_GRID;
    }

    int scroll = _gridScroll;
    int row = index / _grid.cols;
    if (row < scroll) scroll = row;
    else if (row >= scroll + _grid.visibleRows) scroll = row - _grid.visibleRows + 1;

    bool redrawAll = _gridRedrawAll || scroll != _gridScroll;
    _gridScroll = scroll;
    _launchRectW = 0;

    if (_grid.rows > _grid.visibleRows) {
        _pageStripX = _grid.x + _grid.cols * _grid.cellW;
        _pageStripY = _grid.y;
        _pageStripW = tftWidth - _pageStripX;
        _pageStripH = _grid.visibleRows * _grid.cellH;
        _pageStepRows = _grid.visibleRows;
        _pageTotalRows = _grid.rows;
        _pageVisibleRows = _grid.visibleRows;
    } else {
        _pageStripW = 0;
    }

    if (redrawAll) {
        clearOptionRects();
        drawMainBorder(false);
        tft.fillRect(
            _grid.x, _grid.y, _grid.cols * _grid.cellW, _grid.visibleRows * _grid.cellH, bruceConfig.bgColor
        );
        int first = _gridScroll * _grid.cols;
        int last = min(itemCount, first + _grid.visibleRows * _grid.cols);
        for (int i = first; i < last; i++) drawGridCell(i, i == index, false);
        drawGridScrollBar();
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
    } else {
        if (_gridLastIndex >= 0 && _gridLastIndex != index && _gridLastIndex < itemCount)
            drawGridCell(_gridLastIndex, false, false);
        drawGridCell(index, true, false);
    }

    _gridLastIndex = index;
    _gridRedrawAll = false;
}

/*********************************************************************
**  Style 6: Compact Icon-First Bento Grid (5x3 / 4x2 + Bottom Bar)
**********************************************************************/
void MainMenu::drawCompactGrid(int index) {
    int itemCount = options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    if (itemCount != _gridItemCount || _gridRotation != bruceConfigPins.rotation || _lastStyle != MAIN_MENU_COMPACT) {
        buildGridLayout(itemCount, true);
        _gridScroll = 0;
        _gridLastIndex = -1;
        _gridRedrawAll = true;
        _lastStyle = MAIN_MENU_COMPACT;
    }

    int scroll = _gridScroll;
    int row = index / _grid.cols;
    if (row < scroll) scroll = row;
    else if (row >= scroll + _grid.visibleRows) scroll = row - _grid.visibleRows + 1;

    bool redrawAll = _gridRedrawAll || scroll != _gridScroll;
    _gridScroll = scroll;
    _launchRectW = 0;

    if (_grid.rows > _grid.visibleRows) {
        _pageStripX = _grid.x + _grid.cols * _grid.cellW;
        _pageStripY = _grid.y;
        _pageStripW = tftWidth - _pageStripX;
        _pageStripH = _grid.visibleRows * _grid.cellH;
        _pageStepRows = _grid.visibleRows;
        _pageTotalRows = _grid.rows;
        _pageVisibleRows = _grid.visibleRows;
    } else {
        _pageStripW = 0;
    }

    if (redrawAll) {
        clearOptionRects();
        drawMainBorder(false);
        tft.fillRect(
            0, STATUS_BAR_HEIGHT - 3, tftWidth, tftHeight - (STATUS_BAR_HEIGHT - 3), bruceConfig.bgColor
        );
        int first = _gridScroll * _grid.cols;
        int last = min(itemCount, first + _grid.visibleRows * _grid.cols);
        for (int i = first; i < last; i++) drawGridCell(i, i == index, true);
        drawGridScrollBar();
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
    } else {
        if (_gridLastIndex >= 0 && _gridLastIndex != index && _gridLastIndex < itemCount)
            drawGridCell(_gridLastIndex, false, true);
        drawGridCell(index, true, true);
    }

    drawCompactFooterBar(index, itemCount);
    _gridLastIndex = index;
    _gridRedrawAll = false;
}

void MainMenu::drawCompactFooterBar(int index, int itemCount) {
    bool compactScreen = (tftHeight < 160);
    int barH = compactScreen ? 19 : 25;
    int barY = tftHeight - barH - 3;
    int barX = 6;
    int barW = tftWidth - 12;

    uint16_t surf = uiSurfaceColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();

    tft.fillRoundRect(barX, barY, barW, barH, 6, surf);
    tft.drawRoundRect(barX, barY, barW, barH, 6, bdim);
    tft.fillCircle(barX + 10, barY + barH / 2, 3, pri);

    String label = bruce_i18n::tr(options[index].label);
    if (!compactScreen) {
        uiDrawText16(label, barX + 20, barY + (barH - 16) / 2, barW - 72, bright, surf, 0);
    } else {
        int lh = (FP == 1 && bruce_i18n::containsNonAscii(label)) ? 12 : (LH * FP);
        tft.setTextSize(FP);
        tft.setTextColor(bright, surf);
        tft.drawString(bruce_i18n::utf8TruncateToWidth(label, barW - 62, FP), barX + 18, barY + (barH - lh) / 2, 1);
    }

    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%d/%d", index + 1, itemCount);
    tft.setTextSize(FP);
    tft.setTextColor(muted, surf);
    tft.drawRightString(cnt, barX + barW - 8, barY + (barH - LH * FP) / 2, 1);
}

/*********************************************************************
**  Function: drawGridCell
**  Paints a single module cell for Grid or Compact Grid
**********************************************************************/
void MainMenu::drawGridCell(int index, bool selected, bool compactTiles) {
    int row = index / _grid.cols - _gridScroll;
    int col = index % _grid.cols;
    if (row < 0 || row >= _grid.visibleRows) return;

    int x = _grid.x + col * _grid.cellW;
    int y = _grid.y + row * _grid.cellH;

    options[index].x = x;
    options[index].y = y;
    options[index].w = _grid.cellW;
    options[index].h = _grid.cellH;

    int pad = compactTiles ? 3 : 3;
    int cardX = x + pad;
    int cardY = y + pad;
    int cardW = _grid.cellW - pad * 2;
    int cardH = _grid.cellH - pad * 2;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t textBright = uiTextBrightColor();
    uint16_t textMuted = uiTextMutedColor();
    MenuItemInterface *item = static_cast<MenuItemInterface *>(options[index].hoverPointer);

    tft.fillRect(x, y, _grid.cellW, _grid.cellH, bg);

    if (compactTiles) {
        int r = (tftHeight < 160) ? 6 : 8;
        uint16_t tileBg = selected ? pri : uiSurfaceColor();
        uint16_t iconCol = selected ? uiTextOnAccentColor(pri) : blendColors(bg, textBright, 200);
        tft.fillRoundRect(cardX, cardY, cardW, cardH, r, tileBg);
        if (!selected) {
            tft.drawRoundRect(cardX, cardY, cardW, cardH, r, uiBorderDimColor());
        }
        if (item) {
            int box = selected ? min(cardW - 8, cardH - 8) : _grid.iconBox;
            item->drawIconInBox(cardX + cardW / 2, cardY + cardH / 2, box, iconCol, tileBg);
        }
        return;
    }

    uint16_t cellBg = selected ? uiSurfaceHiColor() : bg;
    uint16_t iconCol = selected ? pri : blendColors(bg, textBright, 200);
    uint16_t textCol = selected ? textBright : textMuted;

    if (selected) {
        tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, cellBg);
        tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, pri);
        tft.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 7, pri);
    }

    if (item) {
        int iconCenterY = cardY + 5 + _grid.iconBox / 2;
        item->drawIconInBox(cardX + cardW / 2, iconCenterY, _grid.iconBox, iconCol, cellBg);
    }

    tft.setTextSize(_grid.labelSize);
    tft.setTextColor(textCol, cellBg);
    String label = bruce_i18n::tr(options[index].label);
    String fitted = bruce_i18n::utf8TruncateToWidth(label, cardW - 6, _grid.labelSize);
    int labelH = (_grid.labelSize == 1 && bruce_i18n::containsNonAscii(fitted)) ? 12 : (LH * _grid.labelSize);
    tft.drawCentreString(fitted, cardX + cardW / 2, cardY + cardH - labelH - 4, 1);
}

/*********************************************************************
**  Style 2: Icon + Title Vertical Hardware List (Partial Redraw)
**********************************************************************/
void MainMenu::drawList(int index) {
    int itemCount = (int)options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    bool compactScreen = (tftHeight < 160);
    int listTop = STATUS_BAR_HEIGHT - 1;
    int listBottom = tftHeight - 4;
    int rowH = compactScreen ? 25 : 33;
    int visibleRows = max(1, (listBottom - listTop) / rowH);
    if (visibleRows > itemCount) visibleRows = itemCount;
    int startY = listTop + ((listBottom - listTop) - visibleRows * rowH) / 2;

    if (_lastStyle != MAIN_MENU_LIST || _gridItemCount != itemCount || _gridRotation != bruceConfigPins.rotation) {
        _gridScroll = 0;
        _gridLastIndex = -1;
        _gridRedrawAll = true;
        _lastStyle = MAIN_MENU_LIST;
        _gridItemCount = itemCount;
        _gridRotation = bruceConfigPins.rotation;
    }

    int scroll = _gridScroll;
    if (index < scroll) scroll = index;
    else if (index >= scroll + visibleRows) scroll = index - visibleRows + 1;
    int maxScroll = max(0, itemCount - visibleRows);
    if (scroll > maxScroll) scroll = maxScroll;

    bool redrawAll = _gridRedrawAll || scroll != _gridScroll;
    _gridScroll = scroll;
    _launchRectW = 0;

    if (itemCount > visibleRows) {
        _pageStripX = tftWidth - 16;
        _pageStripY = startY;
        _pageStripW = 16;
        _pageStripH = visibleRows * rowH;
        _pageStepRows = visibleRows;
        _pageTotalRows = itemCount;
        _pageVisibleRows = visibleRows;
    } else {
        _pageStripW = 0;
    }

    if (redrawAll) {
        clearOptionRects();
        drawMainBorder(false);
        tft.fillRect(0, STATUS_BAR_HEIGHT - 3, tftWidth, tftHeight - (STATUS_BAR_HEIGHT - 3), bruceConfig.bgColor);
        for (int r = 0; r < visibleRows; r++) {
            int i = _gridScroll + r;
            drawListRow(i, r, startY + r * rowH, rowH, i == index);
        }
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
    } else {
        if (_gridLastIndex >= _gridScroll && _gridLastIndex < _gridScroll + visibleRows && _gridLastIndex != index) {
            int r = _gridLastIndex - _gridScroll;
            drawListRow(_gridLastIndex, r, startY + r * rowH, rowH, false);
        }
        int r = index - _gridScroll;
        drawListRow(index, r, startY + r * rowH, rowH, true);
    }

    // Slim 2px right-edge scrollbar
    if (itemCount > visibleRows) {
        int barX = tftWidth - 5;
        int trackY = startY + 2;
        int trackH = visibleRows * rowH - 4;
        tft.fillRect(barX, trackY, 2, trackH, uiBorderDimColor());
        int thumbH = max(10, trackH * visibleRows / itemCount);
        int thumbY = trackY + (trackH - thumbH) * index / max(1, itemCount - 1);
        tft.fillRect(barX, thumbY, 2, thumbH, uiTextBrightColor());
    }

    _gridLastIndex = index;
    _gridRedrawAll = false;
}

void MainMenu::drawListRow(int index, int visibleSlot, int rowY, int rowH, bool selected) {
    (void)visibleSlot;
    bool compactScreen = (tftHeight < 160);
    int rowX = 7;
    int rowW = tftWidth - 17;

    options[index].x = rowX;
    options[index].y = rowY;
    options[index].w = rowW;
    options[index].h = rowH;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();
    uint16_t onAccent = uiTextOnAccentColor(pri);
    uint16_t textNorm = blendColors(bg, bright, 205);

    uint16_t rowBg = selected ? pri : bg;
    tft.fillRect(rowX, rowY, rowW, rowH, bg);
    if (selected) {
        tft.fillRoundRect(rowX, rowY + 1, rowW, rowH - 2, compactScreen ? 5 : 7, pri);
    }

    int iconZone = compactScreen ? 20 : 26;
    int iconCx = rowX + (compactScreen ? 14 : 18);
    int iconCy = rowY + rowH / 2;
    uint16_t iconBg = selected ? pri : surf;
    uint16_t iconFg = selected ? onAccent : blendColors(bg, bright, 210);
    if (!selected) {
        tft.fillRoundRect(iconCx - iconZone / 2, iconCy - iconZone / 2, iconZone, iconZone, 5, surf);
    }
    MenuItemInterface *item = static_cast<MenuItemInterface *>(options[index].hoverPointer);
    if (item) {
        item->drawIconInBox(iconCx, iconCy, compactScreen ? 15 : 19, iconFg, iconBg);
    }

    int textX = rowX + (compactScreen ? 30 : 38);
    String label = bruce_i18n::tr(options[index].label);
    uiDrawText16(label, textX, rowY + (rowH - 16) / 2, rowW - (textX - rowX) - 24, selected ? onAccent : textNorm, rowBg, 0);

    if (selected) {
        int cx = rowX + rowW - 10;
        int cy = rowY + rowH / 2;
        tft.drawLine(cx - 2, cy - 3, cx + 1, cy, onAccent);
        tft.drawLine(cx - 2, cy + 3, cx + 1, cy, onAccent);
    } else {
        char num[6];
        snprintf(num, sizeof(num), "%02d", index + 1);
        tft.setTextSize(FP);
        tft.setTextColor(muted, bg);
        tft.drawRightString(num, rowX + rowW - 8, rowY + (rowH - LH * FP) / 2, 1);
    }
}

/*********************************************************************
**  Style 3: 3-Card CoverFlow / Peek Deck
**********************************************************************/
void MainMenu::drawCards(int index) {
    int itemCount = (int)options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    bool compactScreen = (tftHeight < 160);
    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();

    clearOptionRects();
    drawMainBorder(false);
    int topY = STATUS_BAR_HEIGHT - 3;
    tft.fillRect(0, topY, tftWidth, tftHeight - topY, bg);

    _launchRectW = 0;
    _pageStripW = 0;
    _lastStyle = MAIN_MENU_CARDS;
    _gridLastIndex = index;
    _gridRedrawAll = false;

    int prevIdx = (index + itemCount - 1) % itemCount;
    int nextIdx = (index + 1) % itemCount;

    int heroW = compactScreen ? 118 : 164;
    int heroH = compactScreen ? 86 : 166;
    int heroX = (tftWidth - heroW) / 2;
    int heroY = topY + (compactScreen ? 4 : 11);

    int sideW = compactScreen ? 50 : 66;
    int sideH = compactScreen ? 64 : 122;
    int sideY = heroY + (heroH - sideH) / 2;
    int leftX = max(4, heroX - sideW - (compactScreen ? 5 : 6));
    int rightX = min(tftWidth - sideW - 4, heroX + heroW + (compactScreen ? 5 : 6));

    auto drawSideCard = [&](int idx, int sx) {
        if (itemCount < 2) return;
        options[idx].x = sx;
        options[idx].y = sideY;
        options[idx].w = sideW;
        options[idx].h = sideH;

        int r = compactScreen ? 6 : 9;
        tft.fillRoundRect(sx, sideY, sideW, sideH, r, surf);
        tft.drawRoundRect(sx, sideY, sideW, sideH, r, bdim);

        MenuItemInterface *it = static_cast<MenuItemInterface *>(options[idx].hoverPointer);
        if (it) {
            int ibox = compactScreen ? 24 : 34;
            it->drawIconInBox(sx + sideW / 2, sideY + sideH / 2 - (compactScreen ? 7 : 10), ibox, muted, surf);
        }
        String lbl = bruce_i18n::tr(options[idx].label);
        String fit = bruce_i18n::utf8TruncateToWidth(lbl, sideW - 8, FP);
        int lh = (FP == 1 && bruce_i18n::containsNonAscii(fit)) ? 12 : (LH * FP);
        tft.setTextSize(FP);
        tft.setTextColor(muted, surf);
        tft.drawCentreString(fit, sx + sideW / 2, sideY + sideH - lh - (compactScreen ? 4 : 8), 1);
    };

    if (itemCount >= 2) drawSideCard(prevIdx, leftX);
    if (itemCount >= 3) drawSideCard(nextIdx, rightX);

    // Center Hero Card
    options[index].x = heroX;
    options[index].y = heroY;
    options[index].w = heroW;
    options[index].h = heroH;

    int hr = compactScreen ? 8 : 12;
    tft.fillRoundRect(heroX, heroY, heroW, heroH, hr, surf);
    tft.drawRoundRect(heroX, heroY, heroW, heroH, hr, pri);
    tft.drawRoundRect(heroX + 1, heroY + 1, heroW - 2, heroH - 2, hr - 1, pri);

    MenuItemInterface *heroItem = static_cast<MenuItemInterface *>(options[index].hoverPointer);
    if (heroItem) {
        int ibox = compactScreen ? 42 : 74;
        heroItem->drawIconInBox(heroX + heroW / 2, heroY + (compactScreen ? 28 : 60), ibox, pri, surf);
    }

    String heroLbl = bruce_i18n::tr(options[index].label);
    if (!compactScreen) {
        uiDrawText16(heroLbl, heroX + heroW / 2, heroY + heroH - 42, heroW - 18, bright, surf, 1);
    } else {
        String fit = bruce_i18n::utf8TruncateToWidth(heroLbl, heroW - 12, FP);
        int lh = (FP == 1 && bruce_i18n::containsNonAscii(fit)) ? 12 : (LH * FP);
        tft.setTextSize(FP);
        tft.setTextColor(bright, surf);
        tft.drawCentreString(fit, heroX + heroW / 2, heroY + heroH - lh - 14, 1);
    }

    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%d / %d", index + 1, itemCount);
    tft.setTextSize(FP);
    tft.setTextColor(muted, surf);
    tft.drawCentreString(cnt, heroX + heroW / 2, heroY + heroH - (compactScreen ? 11 : 17), 1);

    // Slim bottom pagination progress bar
    int barW = compactScreen ? 96 : 128;
    int barH = compactScreen ? 2 : 3;
    int barX = (tftWidth - barW) / 2;
    int barY = tftHeight - (compactScreen ? 7 : 11);
    tft.fillRoundRect(barX, barY, barW, barH, 1, bdim);
    int segW = max(10, barW / max(1, itemCount));
    int segX = barX + (barW - segW) * index / max(1, itemCount - 1);
    tft.fillRoundRect(segX, barY, segW, barH, 1, pri);

#if defined(HAS_TOUCH)
    TouchFooter();
#endif
}

/*********************************************************************
**  Style 4: Hero Showcase + 5-Slot Floating Bottom Dock
**********************************************************************/
void MainMenu::drawDock(int index) {
    int itemCount = (int)options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    bool compactScreen = (tftHeight < 160);
    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();
    uint16_t onAccent = uiTextOnAccentColor(pri);

    clearOptionRects();
    drawMainBorder(false);
    int topY = STATUS_BAR_HEIGHT - 3;
    tft.fillRect(0, topY, tftWidth, tftHeight - topY, bg);

    _pageStripW = 0;
    _lastStyle = MAIN_MENU_DOCK;
    _gridLastIndex = index;
    _gridRedrawAll = false;

    int padX = compactScreen ? 8 : 12;
    int stageW = tftWidth - padX * 2;
    int stageY = topY + (compactScreen ? 2 : 6);
    int dockH = compactScreen ? 40 : 62;
    int gap = compactScreen ? 4 : 8;
    int stageH = tftHeight - stageY - dockH - gap - (compactScreen ? 3 : 7);

    // Top Hero Stage (tap anywhere on stage to launch active app!)
    _launchRectX = padX;
    _launchRectY = stageY;
    _launchRectW = stageW;
    _launchRectH = stageH;

    tft.fillRoundRect(padX, stageY, stageW, stageH, compactScreen ? 7 : 10, surf);
    tft.drawRoundRect(padX, stageY, stageW, stageH, compactScreen ? 7 : 10, bdim);

    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%02d/%02d", index + 1, itemCount);
    MenuItemInterface *heroItem = static_cast<MenuItemInterface *>(options[index].hoverPointer);
    String heroLbl = bruce_i18n::tr(options[index].label);

    if (compactScreen) {
        if (heroItem) {
            heroItem->drawIconInBox(padX + 28, stageY + stageH / 2, 34, pri, surf);
        }
        uiDrawText16(heroLbl, padX + 54, stageY + (stageH - 28) / 2, stageW - 64, bright, surf, 0);
        tft.setTextSize(FP);
        tft.setTextColor(muted, surf);
        tft.drawString(cnt, padX + 54, stageY + stageH - 13, 1);
    } else {
        tft.setTextSize(FP);
        tft.setTextColor(muted, surf);
        tft.drawRightString(cnt, padX + stageW - 10, stageY + 8, 1);
        if (heroItem) {
            heroItem->drawIconInBox(padX + stageW / 2, stageY + (stageH - 26) / 2, 68, pri, surf);
        }
        uiDrawText16(heroLbl, padX + stageW / 2, stageY + stageH - 27, stageW - 24, bright, surf, 1);
    }

    // Bottom Floating 5-Slot Dock
    int dockY = stageY + stageH + gap;
    tft.fillRoundRect(padX, dockY, stageW, dockH, compactScreen ? 8 : 12, surf);
    tft.drawRoundRect(padX, dockY, stageW, dockH, compactScreen ? 8 : 12, bdim);

    int slots = min(5, itemCount);
    int slotW = stageW / slots;
    int half = slots / 2;

    for (int s = 0; s < slots; s++) {
        int offset = s - half;
        int idx = (index + offset + itemCount * 4) % itemCount;
        int sx = padX + s * slotW;
        bool active = (offset == 0);

        options[idx].x = sx;
        options[idx].y = dockY;
        options[idx].w = slotW;
        options[idx].h = dockH;

        int cx = sx + slotW / 2;
        int cy = dockY + dockH / 2 - (active ? (compactScreen ? 1 : 2) : 0);
        uint16_t slotBg = active ? pri : surf;
        uint16_t iconCol = active ? onAccent : (abs(offset) == 1 ? blendColors(bg, bright, 195) : muted);

        if (active) {
            int pw = min(slotW - 6, compactScreen ? 34 : 46);
            int ph = dockH - (compactScreen ? 8 : 14);
            tft.fillRoundRect(cx - pw / 2, dockY + (compactScreen ? 3 : 5), pw, ph, compactScreen ? 6 : 9, pri);
            tft.fillRoundRect(cx - 6, dockY + dockH - 4, 12, 2, 1, bright);
        }

        MenuItemInterface *it = static_cast<MenuItemInterface *>(options[idx].hoverPointer);
        if (it) {
            int ibox = active ? (compactScreen ? 21 : 29)
                              : (abs(offset) == 1 ? (compactScreen ? 18 : 24) : (compactScreen ? 15 : 20));
            it->drawIconInBox(cx, cy, ibox, iconCol, slotBg);
        }
    }

#if defined(HAS_TOUCH)
    TouchFooter();
#endif
}

/*********************************************************************
**  Style 5: Dual-Pane Cyberdeck Split (Left Rail + Right Preview)
**********************************************************************/
void MainMenu::drawSplit(int index) {
    int itemCount = (int)options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    bool compactScreen = (tftHeight < 160);
    int topY = STATUS_BAR_HEIGHT - 1;
    int availH = tftHeight - topY - 4;
    int railX = compactScreen ? 4 : 6;
    int railW = compactScreen ? 104 : 134;
    int barX = railX + railW + 2;
    int paneX = barX + (compactScreen ? 5 : 7);
    int paneW = tftWidth - paneX - (compactScreen ? 4 : 6);

    int rowH = compactScreen ? 20 : 25;
    int visibleRows = max(1, availH / rowH);
    if (visibleRows > itemCount) visibleRows = itemCount;
    int railY = topY + (availH - visibleRows * rowH) / 2;

    if (_lastStyle != MAIN_MENU_SPLIT || _gridItemCount != itemCount || _gridRotation != bruceConfigPins.rotation) {
        _gridScroll = 0;
        _gridLastIndex = -1;
        _gridRedrawAll = true;
        _lastStyle = MAIN_MENU_SPLIT;
        _gridItemCount = itemCount;
        _gridRotation = bruceConfigPins.rotation;
    }

    int scroll = _gridScroll;
    if (index < scroll) scroll = index;
    else if (index >= scroll + visibleRows) scroll = index - visibleRows + 1;
    int maxScroll = max(0, itemCount - visibleRows);
    if (scroll > maxScroll) scroll = maxScroll;

    bool redrawAll = _gridRedrawAll || scroll != _gridScroll;
    _gridScroll = scroll;

    // Tapping the right preview card launches the currently focused app!
    _launchRectX = paneX;
    _launchRectY = topY;
    _launchRectW = paneW;
    _launchRectH = availH;

    if (itemCount > visibleRows) {
        _pageStripX = barX - 4;
        _pageStripY = railY;
        _pageStripW = 10;
        _pageStripH = visibleRows * rowH;
        _pageStepRows = visibleRows;
        _pageTotalRows = itemCount;
        _pageVisibleRows = visibleRows;
    } else {
        _pageStripW = 0;
    }

    if (redrawAll) {
        clearOptionRects();
        drawMainBorder(false);
        tft.fillRect(0, STATUS_BAR_HEIGHT - 3, tftWidth, tftHeight - (STATUS_BAR_HEIGHT - 3), bruceConfig.bgColor);
        for (int r = 0; r < visibleRows; r++) {
            int i = _gridScroll + r;
            drawSplitRailRow(i, r, railY + r * rowH, rowH, railX, railW, i == index);
        }
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
    } else {
        if (_gridLastIndex >= _gridScroll && _gridLastIndex < _gridScroll + visibleRows && _gridLastIndex != index) {
            int r = _gridLastIndex - _gridScroll;
            drawSplitRailRow(_gridLastIndex, r, railY + r * rowH, rowH, railX, railW, false);
        }
        int r = index - _gridScroll;
        drawSplitRailRow(index, r, railY + r * rowH, rowH, railX, railW, true);
    }

    // Slim 2px rail scrollbar
    if (itemCount > visibleRows) {
        int trackY = railY + 2;
        int trackH = visibleRows * rowH - 4;
        tft.fillRect(barX, trackY, 2, trackH, uiBorderDimColor());
        int thumbH = max(10, trackH * visibleRows / itemCount);
        int thumbY = trackY + (trackH - thumbH) * index / max(1, itemCount - 1);
        tft.fillRect(barX, thumbY, 2, thumbH, uiTextBrightColor());
    }

    drawSplitPreview(index, itemCount, paneX, topY, paneW, availH);
    _gridLastIndex = index;
    _gridRedrawAll = false;
}

void MainMenu::drawSplitRailRow(int index, int visibleSlot, int rowY, int rowH, int railX, int railW, bool selected) {
    (void)visibleSlot;
    bool compactScreen = (tftHeight < 160);
    options[index].x = railX;
    options[index].y = rowY;
    options[index].w = railW;
    options[index].h = rowH;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t bright = uiTextBrightColor();
    uint16_t onAccent = uiTextOnAccentColor(pri);
    uint16_t textNorm = blendColors(bg, bright, 200);
    uint16_t rowBg = selected ? pri : bg;

    tft.fillRect(railX, rowY, railW, rowH, bg);
    if (selected) {
        tft.fillRoundRect(railX, rowY + 1, railW, rowH - 2, compactScreen ? 4 : 5, pri);
    }

    String lbl = bruce_i18n::tr(options[index].label);
    if (!compactScreen) {
        uiDrawText16(lbl, railX + 8, rowY + (rowH - 16) / 2, railW - 14, selected ? onAccent : textNorm, rowBg, 0);
    } else {
        String fit = bruce_i18n::utf8TruncateToWidth(lbl, railW - 10, FP);
        int lh = (FP == 1 && bruce_i18n::containsNonAscii(fit)) ? 12 : (LH * FP);
        tft.setTextSize(FP);
        tft.setTextColor(selected ? onAccent : textNorm, rowBg);
        tft.drawString(fit, railX + 6, rowY + (rowH - lh) / 2, 1);
    }
}

void MainMenu::drawSplitPreview(int index, int itemCount, int paneX, int paneY, int paneW, int paneH) {
    bool compactScreen = (tftHeight < 160);
    uint16_t pri = bruceConfig.priColor;
    uint16_t surf = uiSurfaceColor();
    uint16_t surfHi = uiSurfaceHiColor();
    uint16_t bdim = uiBorderDimColor();
    uint16_t bright = uiTextBrightColor();
    uint16_t muted = uiTextMutedColor();

    tft.fillRoundRect(paneX, paneY, paneW, paneH, compactScreen ? 7 : 10, surf);
    tft.drawRoundRect(paneX, paneY, paneW, paneH, compactScreen ? 7 : 10, bdim);

    char cnt[12];
    snprintf(cnt, sizeof(cnt), "%d/%d", index + 1, itemCount);
    tft.setTextSize(FP);
    tft.setTextColor(muted, surf);
    tft.drawRightString(cnt, paneX + paneW - 8, paneY + (compactScreen ? 5 : 8), 1);

    MenuItemInterface *it = static_cast<MenuItemInterface *>(options[index].hoverPointer);
    if (it) {
        int ibox = compactScreen ? 42 : 74;
        int cy = paneY + (compactScreen ? 38 : 74);
        it->drawIconInBox(paneX + paneW / 2, cy, ibox, pri, surf);
    }

    String lbl = bruce_i18n::tr(options[index].label);
    if (!compactScreen) {
        uiDrawText16(lbl, paneX + paneW / 2, paneY + paneH - 64, paneW - 16, bright, surf, 1);
        int btnW = min(paneW - 36, 96);
        int btnH = 24;
        int btnX = paneX + (paneW - btnW) / 2;
        int btnY = paneY + paneH - btnH - 12;
        tft.fillRoundRect(btnX, btnY, btnW, btnH, 6, surfHi);
        tft.drawRoundRect(btnX, btnY, btnW, btnH, 6, pri);
        uiDrawText16(bruce_i18n::tr("Open"), paneX + paneW / 2, btnY + (btnH - 16) / 2, btnW - 10, pri, surfHi, 1);
    } else {
        String fit = bruce_i18n::utf8TruncateToWidth(lbl, paneW - 10, FP);
        int lh = (FP == 1 && bruce_i18n::containsNonAscii(fit)) ? 12 : (LH * FP);
        tft.setTextSize(FP);
        tft.setTextColor(bright, surf);
        tft.drawCentreString(fit, paneX + paneW / 2, paneY + paneH - lh - 8, 1);
    }
}

/*********************************************************************
**  Function: drawGridScrollBar
**  Minimal 2px scrollbar on the right indicating off-screen rows
**********************************************************************/
void MainMenu::drawGridScrollBar() {
    int barX = tftWidth - 5;
    int trackY = _grid.y + 4;
    int trackH = _grid.visibleRows * _grid.cellH - 8;

    tft.fillRect(barX - 1, trackY, 4, trackH, bruceConfig.bgColor);
    if (_grid.rows <= _grid.visibleRows) return;

    tft.fillRect(barX, trackY, 2, trackH, uiBorderDimColor());
    int thumbH = max(10, trackH * _grid.visibleRows / _grid.rows);
    int thumbY = trackY + (trackH - thumbH) * _gridScroll / (_grid.rows - _grid.visibleRows);
    tft.fillRect(barX, thumbY, 2, thumbH, uiTextBrightColor());
}

/*********************************************************************
**  Function: handleGridPageTap
**  Handles hero/preview launch taps (Dock/Split) and right-edge page
**  strip taps (Grid/Compact/List/Split).
**********************************************************************/
bool MainMenu::handleGridPageTap(int x, int y, int currentIndex, int &newIndex) {
    if (_launchRectW > 0 && x >= _launchRectX && x < _launchRectX + _launchRectW && y >= _launchRectY &&
        y < _launchRectY + _launchRectH) {
        newIndex = currentIndex;
        return true;
    }

    if (_pageStripW <= 0 || _pageTotalRows <= _pageVisibleRows) return false;
    if (x < _pageStripX || x >= _pageStripX + _pageStripW || y < _pageStripY || y >= _pageStripY + _pageStripH)
        return false;

    int maxScroll = _pageTotalRows - _pageVisibleRows;
    int targetScroll = (y < _pageStripY + _pageStripH / 2) ? max(0, _gridScroll - _pageStepRows)
                                                           : min(maxScroll, _gridScroll + _pageStepRows);

    int itemCount = static_cast<int>(options.size());
    int cols = (mainMenuGridColumns > 1) ? mainMenuGridColumns : 1;
    int col = currentIndex % cols;
    newIndex = min(targetScroll * cols + col, itemCount - 1);
    return true;
}

/*********************************************************************
**  Function: hideAppsMenu
**  Menu to Hide or show menus
**********************************************************************/

void MainMenu::hideAppsMenu() {
    auto items = this->getItems();
    int index = 0;
RESTART: // using gotos to avoid stackoverflow after many choices
    options.clear();
    for (auto item : items) {
        String label = item->getName();
        std::vector<String> l = bruceConfig.disabledMenus;
        bool enabled = find(l.begin(), l.end(), label) == l.end();
        options.push_back(
            {label,
             [this, label, enabled]() {
                 if (enabled) bruceConfig.addDisabledMenu(label);
                 else bruceConfig.removeDisabledMenu(label);
             },
             enabled}
        );
    }
    options.push_back({"Show All", [=]() { bruceConfig.disabledMenus.clear(); }, true});
    addOptionToMainMenu();
    index = loopOptions(options, index);
    bruceConfig.saveFile();
    if (!returnToMenu) goto RESTART;
}
