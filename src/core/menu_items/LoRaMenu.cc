#if !defined(LITE_VERSION)
#include "LoRaMenu.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/lora/LoRaRF.h"

void LoRaMenu::optionsMenu() {
    options = {
        {"Chat",             []() { lorachat(); }      },
        {"Change username",  []() { changeusername(); }},
        {"Change Frequency", []() { chfreq(); }        },
    };
    addOptionToMainMenu();
    String txt = "LoRa";
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void LoRaMenu::drawIcon(float scale) {
    clearIconArea();
    scale *= 0.75;
    int cx = iconCenterX;
    int cy = iconCenterY + (scale * 8);

#define CALC_X(val) (cx + ((val - 50) * scale))
#define CALC_Y(val) (cy + ((val - 50) * scale))

    int lineWidth = scale * 4.5;
    if (lineWidth < 2) lineWidth = 2;
    int ballRad = scale * 6;

    /*
     * Tower legs, in design space (0..100) mapped through CALC_X/CALC_Y: they
     * splay from the 44..56 apex span at Y 35 outward to 26..74 at Y 85.
     */

    // Left Leg
    tft.drawWideLine(
        CALC_X(44), CALC_Y(35), CALC_X(26), CALC_Y(85), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );
    // Right Leg
    tft.drawWideLine(
        CALC_X(56), CALC_Y(35), CALC_X(74), CALC_Y(85), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );

    /*
     * Rails: the 44..56 bar at Y 35 closes the apex, the 35..65 bar at Y 65 is
     * the waist that separates the upper and lower lattice sections.
     */

    // Top Cross Bar
    tft.drawWideLine(
        CALC_X(44), CALC_Y(35), CALC_X(56), CALC_Y(35), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );

    // Middle Cross Bar
    tft.drawWideLine(
        CALC_X(35), CALC_Y(65), CALC_X(65), CALC_Y(65), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );

    /*
     * Upper X-brace: both diagonals span Y 35..65, swapping X between the apex
     * ends (44/56) and the waist rail ends (35/65).
     */

    // X-Bracing (Top Section)
    tft.drawWideLine(
        CALC_X(44), CALC_Y(35), CALC_X(65), CALC_Y(65), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );
    tft.drawWideLine(
        CALC_X(56), CALC_Y(35), CALC_X(35), CALC_Y(65), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );

    /*
     * Lower X-brace: diagonals run from the waist rail ends (35/65 at Y 65)
     * down to the opposite leg base (74/26 at Y 85), closing the lattice.
     */

    // X-Bracing (Bottom Section)
    tft.drawWideLine(
        CALC_X(35), CALC_Y(65), CALC_X(74), CALC_Y(85), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );
    tft.drawWideLine(
        CALC_X(65), CALC_Y(65), CALC_X(26), CALC_Y(85), lineWidth, bruceConfig.priColor, bruceConfig.priColor
    );

    /*
     * Emitter: the ball is the only filled shape and sits above the apex at
     * Y 25; two concentric arcs per side (90 / 270 deg +/- 30) are the waves.
     */

    // ball
    int ballY = CALC_Y(25);
    tft.fillCircle(cx, ballY, ballRad, bruceConfig.priColor);

    // waves
    int r1 = scale * 20;
    int r2 = scale * 32;

    // Right Side (90 degrees +/-)
    tft.drawArc(cx, ballY, r1 + lineWidth, r1, 60, 120, bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawArc(cx, ballY, r2 + lineWidth, r2, 60, 120, bruceConfig.priColor, bruceConfig.bgColor);

    // Left Side (270 degrees +/-)
    tft.drawArc(cx, ballY, r1 + lineWidth, r1, 240, 300, bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawArc(cx, ballY, r2 + lineWidth, r2, 240, 300, bruceConfig.priColor, bruceConfig.bgColor);
}
#endif
