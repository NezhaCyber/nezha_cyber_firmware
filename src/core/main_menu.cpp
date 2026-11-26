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

    bool gridStyle = bruceConfig.mainMenuStyle == MAIN_MENU_GRID;

    // Option::hover is a plain function pointer, so both renderers stay capture-less
    // and the grid one resolves its own position from the item pointer it receives.
    bool (*renderItem)(void *menuItem, bool shouldRender) = [](void *menuItem, bool shouldRender) {
        if (!shouldRender) return false;
        drawMainBorder(false);

        MenuItemInterface *obj = static_cast<MenuItemInterface *>(menuItem);
        float scale = float((float)tftWidth / (float)240);
        if (bruceConfigPins.rotation & 0b01) scale = float((float)tftHeight / (float)135);
        obj->draw(scale);
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
        return true;
    };

    if (gridStyle) {
        renderItem = [](void *menuItem, bool shouldRender) {
            if (!shouldRender) return false;
            mainMenu.drawGrid(mainMenu.gridIndexOf(static_cast<MenuItemInterface *>(menuItem)));
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

    // The grid needs a fresh full repaint every time we come back to the main menu,
    // since loopOptions clears the screen before the first render.
    if (gridStyle) {
        buildGridLayout(options.size());
        _gridScroll = 0;
        _gridLastIndex = -1;
        _gridRedrawAll = true;
        mainMenuGridColumns = _grid.cols;
    } else {
        mainMenuGridColumns = 0;
    }

    _currentIndex = loopOptions(options, MENU_TYPE_MAIN, "Main Menu", _currentIndex);
};

/*********************************************************************
**  Function: gridIndexOf
**  Position of a menu item inside the currently built options list.
**  The hover callback is a plain function pointer and cannot capture the
**  index, so it is resolved back from the item pointer it receives.
**********************************************************************/
int MainMenu::gridIndexOf(MenuItemInterface *item) {
    for (size_t i = 0; i < options.size(); i++) {
        if (options[i].hoverPointer == item) return (int)i;
    }
    return 0;
}

/*********************************************************************
**  Function: buildGridLayout
**  Picks the column count that makes cells (and therefore icons) as large as possible for the
**  available area, instead of just cramming in as many columns as fit at the minimum size. Only
**  falls back to the minimum size, with paging, once even one column can't make cells that big.
**********************************************************************/
void MainMenu::buildGridLayout(int itemCount) {
    // Open hardware launcher proportions: 4x3 on T-Deck (320x240), 3x2 on M5StickC Plus 1.1 (240x135)
    const bool compactScreen = (tftHeight < 160);
    const int MIN_CELL_W = 64 * FP;
    const int MIN_CELL_H = (compactScreen ? 46 : 56) * FP;
    const int PAGE_TAP_W = 10;

    if (itemCount < 1) itemCount = 1;

    int areaX = 6;
    int areaY = STATUS_BAR_HEIGHT - 2;
    int areaW = tftWidth - areaX - PAGE_TAP_W - 2;
    int areaH = tftHeight - areaY - 4;

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

    _grid.x = areaX + (areaW - _grid.cols * _grid.cellW) / 2;
    _grid.y = areaY + (areaH - _grid.visibleRows * _grid.cellH) / 2;

    _gridItemCount = itemCount;
    _gridRotation = bruceConfigPins.rotation;
}

/*********************************************************************
**  Function: drawGrid
**  Draws the module grid highlighting `index`. Only the two cells that
**  changed are repainted unless the whole view has to come back.
**********************************************************************/
void MainMenu::drawGrid(int index) {
    int itemCount = options.size();
    if (itemCount < 1) return;
    if (index < 0 || index >= itemCount) index = 0;

    if (itemCount != _gridItemCount || _gridRotation != bruceConfigPins.rotation) {
        buildGridLayout(itemCount);
        _gridScroll = 0;
        _gridLastIndex = -1;
        _gridRedrawAll = true;
    }

    // Keep the selected cell inside the visible window
    int scroll = _gridScroll;
    int row = index / _grid.cols;
    if (row < scroll) scroll = row;
    else if (row >= scroll + _grid.visibleRows) scroll = row - _grid.visibleRows + 1;

    bool redrawAll = _gridRedrawAll || scroll != _gridScroll;
    _gridScroll = scroll;

    if (redrawAll) {
        // Cells scrolled out of the visible window won't get a drawGridCell() call below to refresh
        // their tap rect, so clear everyone's first (mirrors drawOptions()'s same stale-rect concern).
        for (auto &opt : options) {
            opt.w = 0;
            opt.h = 0;
        }
        drawMainBorder(false);
        tft.fillRect(
            _grid.x, _grid.y, _grid.cols * _grid.cellW, _grid.visibleRows * _grid.cellH, bruceConfig.bgColor
        );
        int first = _gridScroll * _grid.cols;
        int last = min(itemCount, first + _grid.visibleRows * _grid.cols);
        for (int i = first; i < last; i++) drawGridCell(i, i == index);
        drawGridScrollBar();
#if defined(HAS_TOUCH)
        TouchFooter();
#endif
    } else {
        if (_gridLastIndex >= 0 && _gridLastIndex != index && _gridLastIndex < itemCount)
            drawGridCell(_gridLastIndex, false);
        drawGridCell(index, true);
    }

    _gridLastIndex = index;
    _gridRedrawAll = false;
}

/*********************************************************************
**  Function: drawGridCell
**  Paints a single module cell, inverted when it is the selected one
**********************************************************************/
void MainMenu::drawGridCell(int index, bool selected) {
    int row = index / _grid.cols - _gridScroll;
    int col = index % _grid.cols;
    if (row < 0 || row >= _grid.visibleRows) return;

    int x = _grid.x + col * _grid.cellW;
    int y = _grid.y + row * _grid.cellH;

    options[index].x = x;
    options[index].y = y;
    options[index].w = _grid.cellW;
    options[index].h = _grid.cellH;

    int cardX = x + 3;
    int cardY = y + 3;
    int cardW = _grid.cellW - 6;
    int cardH = _grid.cellH - 6;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t textBright = uiTextBrightColor();
    uint16_t textMuted = uiTextMutedColor();
    uint16_t cellBg = selected ? uiSurfaceHiColor() : bg;
    uint16_t iconCol = selected ? pri : blendColors(bg, textBright, 200);
    uint16_t textCol = selected ? textBright : textMuted;

    // Open borderless canvas for unselected apps; crisp elevated graphite tile + 2px accent ring for active focus
    tft.fillRect(x, y, _grid.cellW, _grid.cellH, bg);
    if (selected) {
        tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, cellBg);
        tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, pri);
        tft.drawRoundRect(cardX + 1, cardY + 1, cardW - 2, cardH - 2, 7, pri);
    }

    MenuItemInterface *item = static_cast<MenuItemInterface *>(options[index].hoverPointer);
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
**  Right-edge strip (see PAGE_TAP_W in buildGridLayout): tapping the top half jumps a full page
**  up, the bottom half a full page down. Needed because a tap can only select a cell that's
**  already on screen, unlike Prev/Next which auto-scroll into view one cell at a time.
**********************************************************************/
bool MainMenu::handleGridPageTap(int x, int y, int currentIndex, int &newIndex) {
    if (_grid.rows <= _grid.visibleRows) return false; // everything already fits on one page

    int zoneX = _grid.x + _grid.cols * _grid.cellW;
    int zoneY = _grid.y;
    int zoneW = tftWidth - BORDER_OFFSET_FROM_SCREEN_EDGE - zoneX;
    int zoneH = _grid.visibleRows * _grid.cellH;
    if (x < zoneX || x >= zoneX + zoneW || y < zoneY || y >= zoneY + zoneH) return false;

    int maxScroll = _grid.rows - _grid.visibleRows;
    int targetScroll = (y < zoneY + zoneH / 2) ? max(0, _gridScroll - _grid.visibleRows)
                                               : min(maxScroll, _gridScroll + _grid.visibleRows);

    // Keep the same column, jump to the first row of the new page.
    int col = currentIndex % _grid.cols;
    int itemCount = static_cast<int>(options.size());
    newIndex = min(targetScroll * _grid.cols + col, itemCount - 1);
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
