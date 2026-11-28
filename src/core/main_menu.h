#ifndef __MAIN_MENU_H__
#define __MAIN_MENU_H__

#include <MenuItemInterface.h>

#include "menu_items/BleMenu.h"
#include "menu_items/ClockMenu.h"
#include "menu_items/ConfigMenu.h"
#include "menu_items/EthernetMenu.h"
#include "menu_items/FMMenu.h"
#include "menu_items/FileMenu.h"
#include "menu_items/GpsMenu.h"
#include "menu_items/IRMenu.h"
#include "menu_items/LoRaMenu.h"
#include "menu_items/NRF24.h"
#include "menu_items/OthersMenu.h"
#include "menu_items/RadarMenu.h"
#include "menu_items/RFIDMenu.h"
#include "menu_items/RFMenu.h"
#include "menu_items/ScriptsMenu.h"
#include "menu_items/WifiMenu.h"
#include "menu_items/WireToolsMenu.h"
#include "menu_items/PhoneMenu.h"
class MainMenu {
public:
    FileMenu fileMenu;
    BleMenu bleMenu;
    ClockMenu clockMenu;
    ConfigMenu configMenu;
    FMMenu fmMenu;
    GpsMenu gpsMenu;
    IRMenu irMenu;
    NRF24Menu nrf24Menu;
    OthersMenu othersMenu;
    RFIDMenu rfidMenu;
    RFMenu rfMenu;
    ScriptsMenu scriptsMenu;
    WifiMenu wifiMenu;
#if !defined(LITE_VERSION)
    RadarMenu radarMenu;
    LoRaMenu loraMenu;
    EthernetMenu ethernetMenu;
    WireToolsMenu wireToolsMenu;
    PhoneMenu phoneMenu;
#endif

    MainMenu();
    ~MainMenu();

    void begin(void);
    std::vector<MenuItemInterface *> getItems(void) { return _menuItems; }
    void hideAppsMenu();

    // Launcher layout rendering for all MainMenuStyle variants
    void drawActiveStyle(int index);
    void drawGrid(int index);
    void drawList(int index);
    void drawCards(int index);
    void drawDock(int index);
    void drawSplit(int index);
    void drawCompactGrid(int index);
    int gridIndexOf(MenuItemInterface *item);

    // Registered as gridPageTapHandler (see display.h) so loopOptions() can delegate
    // page-strip taps (Grid/List/Split/Compact) and hero/preview launch taps (Dock/Split).
    bool handleGridPageTap(int x, int y, int currentIndex, int &newIndex);

private:
    struct GridLayout {
        int x = 0;
        int y = 0;
        int cellW = 0;
        int cellH = 0;
        int cols = 1;
        int rows = 1;
        int visibleRows = 1;
        int iconBox = 0;
        int labelSize = 1;
    };

    void buildGridLayout(int itemCount, bool compactTiles = false);
    void drawGridCommon(int index, bool compactTiles);
    void drawGridCell(int index, bool selected, bool compactTiles = false);
    void drawGridScrollBar();
    bool updateLinearScroll(int style, int index, int itemCount, int visibleRows);
    void drawLinearScrollBar(int barX, int trackY, int trackH, int visibleRows, int itemCount, int index);
    void drawCompactFooterBar(int index, int itemCount);
    void drawListRow(int index, int visibleSlot, int rowY, int rowH, bool selected);
    void drawSplitRailRow(int index, int visibleSlot, int rowY, int rowH, int railX, int railW, bool selected);
    void drawSplitPreview(int index, int itemCount, int paneX, int paneY, int paneW, int paneH);
    void clearOptionRects();

    int _currentIndex = 0;
    int _totalItems = 0;
    std::vector<MenuItemInterface *> _menuItems;

    GridLayout _grid;
    int _gridItemCount = 0;
    int _gridScroll = 0;
    int _gridLastIndex = -1;
    int _lastStyle = -1;
    uint8_t _gridRotation = ROTATION;
    bool _gridRedrawAll = true;
    int _launchRectX = 0;
    int _launchRectY = 0;
    int _launchRectW = 0;
    int _launchRectH = 0;
    int _pageStripX = 0;
    int _pageStripY = 0;
    int _pageStripW = 0;
    int _pageStripH = 0;
    int _pageStepRows = 1;
    int _pageTotalRows = 1;
    int _pageVisibleRows = 1;
};
extern MainMenu mainMenu;

#endif
