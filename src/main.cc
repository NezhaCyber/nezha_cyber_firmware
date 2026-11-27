#include "core/main_menu.h"
#include <globals.h>

#include "core/bus_HAL.h"
#include "core/logging/nzlog.h"
#include "core/powerSave.h"
#include "core/ram_profile.h"
#include "core/serial_commands/cli.h"
#include "core/utils.h"
#include "current_year.h"
#include "esp32-hal-psram.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <functional>
#include <string>
#include <vector>
// Native RF decoding runs beneath the menu callbacks on loopTask. The default
// 8 KiB stack overflows on the full protocol path (including RAW fallback).
// Keep the smaller framework default on Lite builds without stream codecs.
#if !defined(LITE_VERSION)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

io_expander ioExpander;
BruceConfig bruceConfig;
BruceConfigPins bruceConfigPins;

SerialCli serialCli;
USBSerial USBserial;
SerialDevice *serialDevice = &USBserial;

StartupApp startupApp;
String startupAppJSInterpreterFile = "";

MainMenu mainMenu;
#ifndef VSPI
#define VSPI FSPI
#endif
#ifdef USE_HSPI_PORT
// The display owns HSPI (SPIClass() defaults to HSPI): mounting the SD on it would re-route the
// HSPI MISO input to the SD card's pin and break every device that shares the display MISO
// (e.g. the XPT2046 touchscreen).
SPIClass sdcardSPI(VSPI);
// SPIClass sdcardSPI;
SPIClass AUX_SPI(VSPI);
#else
// sdcardSPI and AUX_SPI must live on different SPI hosts: SPIClass's default constructor and
// AUX_SPI(HSPI) both resolve to the same host, so once sdcardSPI has claimed it, calling
// AUX_SPI.begin() on other pins (e.g. a shared XPT2046 touch bus) succeeds but never actually
// reroutes that host's GPIO matrix -- every AUX_SPI consumer ends up talking over the SD card's
// pins instead. Confirmed on the T-HMI: its shared touch bus read only 0x1FFF (floating) through
// AUX_SPI while the exact same pins, driven bit-banged, answered the XPT2046 correctly.
SPIClass sdcardSPI(VSPI);
SPIClass AUX_SPI(HSPI);
#endif

// Navigation Variables
volatile bool NextPress = false;
volatile bool PrevPress = false;
volatile bool UpPress = false;
volatile bool DownPress = false;
volatile bool SelPress = false;
volatile bool EscPress = false;
volatile bool AnyKeyPress = false;
volatile bool NextPagePress = false;
volatile bool PrevPagePress = false;
volatile bool LongPress = false;
volatile bool SerialCmdPress = false;
volatile int forceMenuOption = -1;
volatile uint8_t menuOptionType = 0;
String menuOptionLabel = "";
#ifdef HAS_ENCODER_LED
volatile int EncoderLedChange = 0;
#endif

BruceTouchPoint touchPoint;
volatile bool touchZoneOutsideFooterEnabled = true;

keyStroke KeyStroke;

volatile int32_t RotaryNetSteps = 0;

TaskHandle_t xHandle;
void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        checkPowerSaveTime();
        // Sometimes this task run 2 or more times before looptask,
        // and navigation gets stuck, the idea here is run the input detection
        // if AnyKeyPress is false, or rerun if it was not renewed within 75ms (arbitrary)
        // because AnyKeyPress will be true if didn´t passed through a check(bool var)
        if (!AnyKeyPress || millis() - timer > 75) {
            // Held across the whole update so consumers never observe a half-written state
            inputLock();
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            SerialCmdPress = false;
            NextPagePress = false;
            PrevPagePress = false;
            touchPoint.pressed = false;
            touchPoint.Clear();
            checkAndRecoverSysI2CBus();
            InputHandler();
            inputUnlock();
            timer = millis();
        }
#if defined(NO_TOUCH_FOOTER)
        else if (millis() - timer >= 40) {
            // If a legacy module only listens to PrevPress/NextPress (ignoring UpPress/DownPress),
            // promote unconsumed vertical trackball ticks so up/down rolling works everywhere.
            inputLock();
            if (UpPress) {
                UpPress = false;
                PrevPress = true;
            }
            if (DownPress) {
                DownPress = false;
                NextPress = true;
            }
            inputUnlock();
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
// Public Globals Variables
unsigned long previousMillis = millis();
int prog_handler; // 0 - Flash, 1 - LittleFS, 3 - Download
String cachedPassword = "";
int8_t interpreter_state = -1;
bool sdcardMounted = false;
bool gpsConnected = false;

// wifi globals
// TODO put in a namespace
bool wifiConnected = false;
bool isWebUIActive = false;
String wifiIP;

bool BLEConnected = false;
bool returnToMenu;
bool isSleeping = false;
bool isScreenOff = false;
bool dimmer = false;
char timeStr[16];
time_t localTime;
struct tm *timeInfo;
#if defined(HAS_RTC)
#if defined(HAS_RTC_PCF85063A)
pcf85063_RTC _rtc;
#else
cplus_RTC _rtc;
#endif
RTC_TimeTypeDef _time;
RTC_DateTypeDef _date;
bool clock_set = true;
#else
ESP32Time rtc;
bool clock_set = false;
#endif

std::vector<Option> options;
// Protected global variables
tft_logger tft = tft_logger(); // Invoke custom library
tft_sprite sprite = tft_sprite(&tft);
tft_sprite draw = tft_sprite(&tft);
volatile int tftWidth = TFT_HEIGHT;
#ifdef HAS_TOUCH
volatile int tftHeight = TFT_WIDTH - TOUCH_FOOTER_HEIGHT; // reserved to draw the TouchFooter(), were the btns
                                                          // are being read in touch devices.
#else
volatile int tftHeight = TFT_WIDTH;
#endif

#include "core/bus_HAL.h"
#include "core/display.h"
#include "core/i18n.h"
#include "core/nezha_splash.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "modules/bjs_interpreter/interpreter.h" // for JavaScript interpreter
#include "modules/others/audio.h"                // for playAudioFile
#include "modules/rf/rf_utils.h"                 // for initCC1101once
#include <Wire.h>

/*********************************************************************
 **  Function: begin_storage
 **  Config LittleFS and SD storage
 *********************************************************************/
void begin_storage() {
    if (!setupLittleFS()) {
        LittleFS.format();
        setupLittleFS();
    }
    RAM_LOG("after LittleFS");
    bool checkFS = setupSdCard();
    bruceConfig.fromFile(checkFS);
    bruceConfigPins.fromFile(checkFS);
    // Boot as early as possible so a crash during the rest of setup is visible
    // in /NezhaLogs/nezha.log after the next reboot.
    nzlog::begin();
    NZLOGI(
        "BOOT", "storage ready: LittleFS=OK SD=%s", sdcardMounted ? "mounted" : "not mounted"
    );
    if (sdcardMounted) {
        NZLOGI(
            "BOOT", "SD total=%uKB used=%uKB", (unsigned)(SD.totalBytes() / 1024),
            (unsigned)(SD.usedBytes() / 1024)
        );
    }
    NZLOGMEM("BOOT");
}

/*********************************************************************
 **  Function: _setup_gpio()
 **  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
 *********************************************************************/
void _setup_gpio() __attribute__((weak));
void _setup_gpio() {}

/*********************************************************************
 **  Function: _post_setup_gpio()
 **  Sets up a weak (empty) function to be replaced by /ports/* /interface.h
 *********************************************************************/
void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
 **  Function: _pre_storage_gpio()
 **  Sets up a weak (empty) function for board fixes that must run
 **  after the first TFT access and before storage is mounted.
 *********************************************************************/
void _pre_storage_gpio() __attribute__((weak));
void _pre_storage_gpio() {}

/*********************************************************************
 **  Function: _late_setup_gpio()
 **  Sets up a weak (empty) function for board fixes that must run
 **  Runs right before animation
 *********************************************************************/
void _late_setup_gpio() __attribute__((weak));
void _late_setup_gpio() {}

/*********************************************************************
 **  Function: setup_gpio
 **  Setup GPIO pins
 *********************************************************************/
void setup_gpio() {

    // init setup from /ports/*/interface.h
    _setup_gpio();

    // Freeze the board's generic wiring so post-storage hooks can tell their own defaults
    // apart from values loaded from /brucePins.conf (see applyBoardDefault() in configPins.h).
    brucePinsSetupDefaults = bruceConfigPins;

    // IO expander (AW9523/PCA9555: Smoochiee, Reaper, T-Lora Pager, Mako, Pebble) switching GPS,
    // MIC, Vibro, CC1101 RX/TX, NFC... powerlines. It is an onboard peripheral, so it lives on the
    // sys_i2c bus each board registers in _setup_gpio() via setSysI2CBus(); &Wire only for a board
    // that never declared sys_i2c.
    bool hasSysI2C = bruceConfigPins.sys_i2c.sda >= 0 && bruceConfigPins.sys_i2c.scl >= 0;
    ioExpander.init(IO_EXPANDER_ADDRESS, hasSysI2C ? getSysI2CBus() : &Wire);

    initCC1101once(acquireSPIBus(
        bruceConfigPins.CC1101_bus.sck, bruceConfigPins.CC1101_bus.miso, bruceConfigPins.CC1101_bus.mosi
    ));
    // acquireSPIBus() returns nullptr when these pins have no hardware controller left (e.g.
    // ARDUINO_M5STICK_C_PLUS and others that don't share SPI with the display/SD/aux bus);
    // initCC1101once(NULL) lets the driver fall back to managing the default SPI object itself.
}

/*********************************************************************
 **  Function: begin_tft
 **  Config tft
 *********************************************************************/
void begin_tft() {
    tft.setRotation(bruceConfigPins.rotation); // sometimes it misses the first command
    tft.invertDisplay(bruceConfig.colorInverted);
    tft.setRotation(bruceConfigPins.rotation);
    tftWidth = tft.width();
#ifdef HAS_TOUCH
    tftHeight = tft.height() - TOUCH_FOOTER_HEIGHT;
#else
    tftHeight = tft.height();
#endif
    resetTftDisplay();
    setBrightness(bruceConfig.bright, false);
}

/*********************************************************************
 **  Function: boot_screen
 **  Draw boot screen
 *********************************************************************/
static int bootSplashBarY() {
    return (tftHeight < 180) ? (tftHeight - 14) : (tftHeight - 18);
}

void boot_screen() {
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t ink = 0xF7BE;
    uint16_t muted = 0x8C71;
    int cx = tftWidth / 2;

    // Pure jet black so the Nezha Demon Child (哪吒魔童) emblem blends seamlessly
    tft.fillScreen(TFT_BLACK);

    int titleY = 0;
#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
    if (tftHeight >= 200 && tftWidth >= NEZHA_SPLASH_W) {
        int imgX = (tftWidth - NEZHA_SPLASH_W) / 2;
        int imgY = 4;
        showJpeg(NEZHA_SPLASH_JPG, sizeof(NEZHA_SPLASH_JPG), imgX, imgY, false);
        titleY = imgY + NEZHA_SPLASH_H + 5;
    } else
#endif
    {
        // Compact crisp 哪吒魔童 vector emblem (Qiankun ring + ribbons + twin buns + chibi face + lotus flame)
        int cy = (tftHeight < 160) ? 31 : (tftHeight / 2 - 36);
        int r = (tftHeight < 160) ? 15 : 22;
        // Qiankun Ring Halo & Hun Tian Ling ribbon curves
        tft.drawCircle(cx, cy, r + 2, sec);
        tft.drawCircle(cx, cy, r + 1, sec);
        tft.drawLine(cx - r - 9, cy + 7, cx - r + 2, cy - 1, pri);
        tft.drawLine(cx + r + 9, cy + 7, cx + r - 2, cy - 1, pri);
        tft.drawLine(cx - r - 8, cy + 9, cx - r + 3, cy + 1, sec);
        tft.drawLine(cx + r + 8, cy + 9, cx + r - 3, cy + 1, sec);
        // Twin Demon-Child hair buns
        int bunR = (tftHeight < 160) ? 5 : 7;
        tft.fillCircle(cx - r + 2, cy - r + 3, bunR + 1, pri);
        tft.fillCircle(cx + r - 2, cy - r + 3, bunR + 1, pri);
        tft.fillCircle(cx - r + 2, cy - r + 3, bunR, 0x18C4);
        tft.fillCircle(cx + r - 2, cy - r + 3, bunR, 0x18C4);
        // Chibi head + bangs + smoky eyes + forehead lotus flame
        int fr = r - 4;
        tft.fillCircle(cx, cy, fr + 1, 0x18C4);
        tft.fillCircle(cx, cy + 1, fr - 1, 0xFED6);
        tft.fillRect(cx - fr + 1, cy - fr, (fr - 1) * 2, 5, 0x18C4);
        tft.fillCircle(cx - 4, cy + 2, 2, 0x18C4);
        tft.fillCircle(cx + 4, cy + 2, 2, 0x18C4);
        tft.drawPixel(cx - 4, cy + 2, pri);
        tft.drawPixel(cx + 4, cy + 2, pri);
        drawNezhaCrest(cx, cy - 4, (tftHeight < 160) ? 4 : 6, pri, sec, max(1, bruceConfig.nezhaThemeStyle));
        titleY = cy + r + 8;
    }

    uiDrawText16(bruce_i18n::tr("Nezha Cyber"), cx, titleY, tftWidth - 16, ink, TFT_BLACK, 1);

    tft.setTextSize(FP);
    tft.setTextColor(sec, TFT_BLACK);
    String sub = String(BRUCE_VERSION) + "  " + bruce_i18n::tr("My fate is mine!");
    if (bruce_i18n::containsNonAscii(sub)) {
        uiDrawText16(sub, cx, titleY + 18, tftWidth - 16, sec, TFT_BLACK, 1);
    } else {
        tft.setTextColor(muted, TFT_BLACK);
        tft.drawCentreString(sub, cx, titleY + 21, 1);
    }

    int barW = min(200, tftWidth - 48);
    int barY = bootSplashBarY();
    int barH = (tftHeight < 180) ? 4 : 5;
    if (barY + barH < tftHeight) {
        tft.fillRoundRect(cx - barW / 2, barY, barW, barH, 2, 0x18C3);
    }
}

/*********************************************************************
 **  Function: boot_screen_anim
 **  Draw boot screen
 *********************************************************************/
void boot_screen_anim() {
    boot_screen();
    int i = millis();
    // checks for boot.jpg in SD and LittleFS for customization
    int boot_img = 0;
    bool drawn = false;
    if (sdcardMounted) {
        if (SD.exists("/boot.jpg")) boot_img = 1;
        else if (SD.exists("/boot.gif")) boot_img = 3;
    }
    if (boot_img == 0 && LittleFS.exists("/boot.jpg")) boot_img = 2;
    else if (boot_img == 0 && LittleFS.exists("/boot.gif")) boot_img = 4;
    if (bruceConfig.theme.boot_img) boot_img = 5; // override others

    tft.drawPixel(0, 0, 0); // Forces back communication with TFT, to avoid ghosting
    if (boot_img > 0) {
        while (millis() < (unsigned long)i + 4500) {
            if ((millis() - i > 600) && !drawn) {
                tft.fillScreen(bruceConfig.bgColor);
                if (boot_img == 5) {
                    drawImg(
                        *bruceConfig.themeFS(),
                        bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_img),
                        0,
                        0,
                        true,
                        3600
                    );
                } else if (boot_img == 1) {
                    drawImg(SD, "/boot.jpg", 0, 0, true);
                } else if (boot_img == 2) {
                    drawImg(LittleFS, "/boot.jpg", 0, 0, true);
                } else if (boot_img == 3) {
                    drawImg(SD, "/boot.gif", 0, 0, true, 3600);
                } else if (boot_img == 4) {
                    drawImg(LittleFS, "/boot.gif", 0, 0, true, 3600);
                }
                tft.drawPixel(0, 0, 0);
                drawn = true;
            }
            if (check(AnyKeyPress)) {
                tft.fillScreen(bruceConfig.bgColor);
                delay(10);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else {
        // ====================================================================
        // 5-Second Animated 哪吒魔童 (Nezha Demon Child) Cyber Boot Sequence
        // Instantly skippable on any key / trackball / touch press!
        // ====================================================================
        const unsigned long splashDurMs = 5000;
        const bool compact = (tftHeight < 180);
        const int cx = tftWidth / 2;
        const int barW = min(200, tftWidth - 48);
        const int barX = cx - barW / 2;
        const int barY = compact ? (tftHeight - 14) : (tftHeight - 18);
        const int barH = compact ? 4 : 5;
        const int tickerY = compact ? (barY - 12) : (barY - 15);
        const uint16_t pri = bruceConfig.priColor;
        const uint16_t sec = bruceConfig.secColor;
        const uint16_t sparkCyan = 0x3EBF;

        // Flanking safe zone widths (zero overlap with center 160x144 JPEG or vector emblem)
        const int safeLeftMax = compact ? 64 : max(24, (tftWidth - 166) / 2);
        const int safeRightMin = tftWidth - safeLeftMax;
        const int sparkTop = 10;
        const int sparkBot = compact ? 88 : 154;

        struct BootSpark {
            int16_t baseX;
            int16_t lastX;
            int16_t lastY;
            float y;
            float speed;
            float phase;
            uint16_t col;
            uint8_t size;
        };

        constexpr int kSparkCount = 16;
        BootSpark sparks[kSparkCount];
        for (int s = 0; s < kSparkCount; s++) {
            bool rightSide = (s & 1) != 0;
            int span = max(8, safeLeftMax - 14);
            int bx = rightSide ? (safeRightMin + 6 + ((s * 17) % span)) : (8 + ((s * 19) % span));
            sparks[s].baseX = (int16_t)bx;
            sparks[s].lastX = -1;
            sparks[s].lastY = -1;
            sparks[s].y = (float)(sparkTop + ((s * 23) % max(12, sparkBot - sparkTop)));
            sparks[s].speed = 1.4f + (s % 4) * 0.55f;
            sparks[s].phase = s * 0.72f;
            sparks[s].col = (s % 3 == 0) ? pri : ((s % 3 == 1) ? sec : sparkCyan);
            sparks[s].size = (s % 4 == 0) ? 2 : 1;
        }

        static const char *kBootStages[] = {
            "IGNITING WIND-FIRE CORE...",
            "ARMING RF & SUB-GHZ ARRAY...",
            "SYNCING BLE & WIFI RADAR...",
            "AWAKENING NEZHA MOTONG...",
            "LOADING CYBER ARSENAL...",
            "MY FATE IS MINE! READY"
        };
        constexpr int kStageCount = 6;
        int lastStage = -1;
        int lastPct = -1;
        uint16_t frame = 0;
        bool playedStartChime = false;
        bool playedEndChime = false;

        // Clear progress bar track
        if (barY + barH < tftHeight) {
            tft.fillRoundRect(barX, barY, barW, barH, 2, 0x18C3);
        }

        while (true) {
            unsigned long elapsed = millis() - (unsigned long)i;
            if (elapsed >= splashDurMs) break;

            if (!playedStartChime) {
                playedStartChime = true;
#if !defined(LITE_VERSION)
                if (bruceConfig.soundEnabled) {
                    _tone(523, 35);
                    _tone(659, 35);
                    _tone(784, 45);
                }
#endif
            }

            int pct = (int)((elapsed * 100UL) / splashDurMs);
            if (pct > 100) pct = 100;
            frame++;

            // 1. Animated Pulsing Qiankun Corner Brackets (4 corners)
            uint16_t cornerCol = ((frame / 6) & 1) ? pri : sec;
            int cLen = compact ? 10 : 14;
            // Top-left & Top-right
            tft.drawFastHLine(4, 4, cLen, cornerCol);
            tft.drawFastVLine(4, 4, cLen, cornerCol);
            tft.drawFastHLine(tftWidth - 4 - cLen, 4, cLen, cornerCol);
            tft.drawFastVLine(tftWidth - 5, 4, cLen, cornerCol);
            // Bottom-left & Bottom-right
            tft.drawFastHLine(4, tftHeight - 5, cLen, cornerCol);
            tft.drawFastVLine(4, tftHeight - 4 - cLen, cLen, cornerCol);
            tft.drawFastHLine(tftWidth - 4 - cLen, tftHeight - 5, cLen, cornerCol);
            tft.drawFastVLine(tftWidth - 5, tftHeight - 4 - cLen, cLen, cornerCol);

            // 2. Swirling Wind-Fire Ember Sparks along both black flanks
            for (int s = 0; s < kSparkCount; s++) {
                if (sparks[s].lastX >= 0) {
                    tft.fillCircle(sparks[s].lastX, sparks[s].lastY, sparks[s].size, TFT_BLACK);
                }
                sparks[s].y -= sparks[s].speed;
                if (sparks[s].y < sparkTop) {
                    sparks[s].y = (float)sparkBot;
                }
                float sway = sinf(frame * 0.14f + sparks[s].phase) * (compact ? 3.5f : 6.0f);
                int nx = (int)roundf(sparks[s].baseX + sway);
                int ny = (int)roundf(sparks[s].y);
                // Keep strictly inside flanking lanes
                bool rightSide = (s & 1) != 0;
                if (rightSide) nx = constrain(nx, safeRightMin + 3, tftWidth - 8);
                else nx = constrain(nx, 8, safeLeftMax - 3);
                sparks[s].lastX = (int16_t)nx;
                sparks[s].lastY = (int16_t)ny;
                tft.fillCircle(nx, ny, sparks[s].size, sparks[s].col);
            }

            // 3. Dual-Side Live Cyber Reactor Equalizer Bars (non-compact T-Deck)
            if (!compact) {
                for (int b = 0; b < 5; b++) {
                    int by = 46 + b * 14;
                    int lv = 6 + (int)(fabsf(sinf(frame * 0.22f + b * 0.85f)) * 18.0f);
                    tft.fillRect(10, by, 26, 4, TFT_BLACK);
                    tft.fillRoundRect(10, by, lv, 4, 1, (b & 1) ? sec : pri);
                    tft.fillRect(tftWidth - 36, by, 26, 4, TFT_BLACK);
                    tft.fillRoundRect(tftWidth - 10 - lv, by, lv, 4, 1, (b & 1) ? sec : pri);
                }
            }

            // 4. Dynamic Boot Subsystem Ticker
            int stage = min(kStageCount - 1, (pct * kStageCount) / 100);
            if (stage != lastStage) {
                lastStage = stage;
                tft.fillRect(18, tickerY, tftWidth - 36, 11, TFT_BLACK);
                tft.setTextSize(FP);
                tft.setTextColor((stage == kStageCount - 1) ? sec : 0xAD55, TFT_BLACK);
                tft.drawCentreString(kBootStages[stage], cx, tickerY + 1, 1);
            }

            // 5. Animated Dual-Tone Wind-Fire Progress Bar + Percentage
            if (pct != lastPct && barY + barH < tftHeight) {
                lastPct = pct;
                int fillW = max(8, (barW * pct) / 100);
                if (fillW > barW) fillW = barW;
                tft.fillRoundRect(barX, barY, fillW, barH, 2, pri);
                int tipW = min(14, fillW);
                tft.fillRoundRect(barX + fillW - tipW, barY, tipW, barH, 2, sec);
                if (fillW > 4) {
                    tft.fillRect(barX + fillW - 3, barY + 1, 2, max(1, barH - 2), TFT_WHITE);
                }
                if (!compact) {
                    char pBuf[8];
                    snprintf(pBuf, sizeof(pBuf), "%d%%", pct);
                    tft.fillRect(barX + barW + 4, barY - 2, 32, 10, TFT_BLACK);
                    tft.setTextSize(FP);
                    tft.setTextColor(sec, TFT_BLACK);
                    tft.drawString(pBuf, barX + barW + 6, barY - 1, 1);
                }
            }

            if (pct >= 96 && !playedEndChime) {
                playedEndChime = true;
#if !defined(LITE_VERSION)
                if (bruceConfig.soundEnabled) {
                    _tone(1046, 55);
                }
#endif
            }

            if (check(AnyKeyPress)) {
                tft.fillScreen(bruceConfig.bgColor);
                delay(10);
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(32));
        }
    }

    // Clear splashscreen
    tft.fillScreen(bruceConfig.bgColor);
}

/*********************************************************************
 **  Function: init_clock
 **  Clock initialisation for propper display in menu
 *********************************************************************/
void init_clock() {
#if defined(HAS_RTC)
    _rtc.begin();
#if defined(HAS_RTC_BM8563)
    _rtc.GetBm8563Time();
#endif
#if defined(HAS_RTC_PCF85063A)
    _rtc.GetPcf85063Time();
#endif
    _rtc.GetTime(&_time);
    _rtc.GetDate(&_date);

    struct tm timeinfo = {};
    timeinfo.tm_sec = _time.Seconds;
    timeinfo.tm_min = _time.Minutes;
    timeinfo.tm_hour = _time.Hours;
    timeinfo.tm_mday = _date.Date;
    timeinfo.tm_mon = _date.Month > 0 ? _date.Month - 1 : 0;
    timeinfo.tm_year = _date.Year >= 1900 ? _date.Year - 1900 : 0;
    time_t epoch = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, nullptr);
#else
    struct tm timeinfo = {};
    timeinfo.tm_year = CURRENT_YEAR - 1900;
    timeinfo.tm_mon = 0x05;
    timeinfo.tm_mday = 0x14;
    time_t epoch = mktime(&timeinfo);
    rtc.setTime(epoch);
    clock_set = true;
    struct timeval tv = {.tv_sec = epoch};
    settimeofday(&tv, nullptr);
    restorePersistedClock(); // override the default with the last-saved time (NVS) + start periodic save
#endif
}

/*********************************************************************
 **  Function: init_led
 **  Led initialisation
 *********************************************************************/
void init_led() {
#ifdef HAS_RGB_LED
    beginLed();
#endif
}

/*********************************************************************
 **  Function: startup_sound
 **  Play sound or tone depending on device hardware
 *********************************************************************/
void startup_sound() {
    if (bruceConfig.soundEnabled == 0) return; // if sound is disabled, do not play sound
#if !defined(LITE_VERSION)
#if defined(HAS_SPEAKER)
    // play a boot sound
    if (bruceConfig.theme.boot_sound) {
        playAudioFile(bruceConfig.themeFS(), bruceConfig.getThemeItemImg(bruceConfig.theme.paths.boot_sound));
    } else if (SD.exists("/boot.wav")) {
        playAudioFile(&SD, "/boot.wav");
    } else if (LittleFS.exists("/boot.wav")) {
        playAudioFile(&LittleFS, "/boot.wav");
    }
#else
    // Bip M5 just because it can. Does not bip if splashscreen is bypassed.
    // _tone() is a no-op while no buzzer pin is configured.
    _tone(5000, 50);
    delay(200);
    _tone(5000, 50);
    /*  2fix: menu infinite loop */
#endif
#endif
}

/*********************************************************************
 **  Function: setup
 **  Where the devices are started and variables set
 *********************************************************************/
void setup() {
    Serial.setRxBufferSize(
        SAFE_STACK_BUFFER_SIZE / 4
    ); // Must be invoked before Serial.begin(). Default is 256 chars
    Serial.begin(115200);
    nzlog::begin(); // serial sink first; the SD file sink opens in begin_storage()
    NZLOGI("BOOT", "Nezha Cyber " BRUCE_VERSION " booting");

    log_d("Total heap: %d", ESP.getHeapSize());
    log_d("Free heap: %d", ESP.getFreeHeap());
    bool psramStarted = psramInit();
    // Printed unconditionally (boards force CORE_DEBUG_LEVEL=1, so log_d is invisible).
    // If PSRAM fails to init, a PSRAM board effectively becomes a no-PSRAM board and
    // Wi-Fi + BLE cannot coexist. This one boot line makes that failure mode observable.
    Serial.printf(
        "[PSRAM] init=%d found=%d total=%u free=%u | internal free=%u largest=%u\n",
        psramStarted,
        psramFound(),
        (unsigned)ESP.getPsramSize(),
        (unsigned)ESP.getFreePsram(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
    );
    Serial.flush();
    NZLOGI(
        "BOOT",
        "psram init=%d found=%d total=%u free=%u",
        (int)psramStarted,
        (int)psramFound(),
        (unsigned)ESP.getPsramSize(),
        (unsigned)ESP.getFreePsram()
    );

    RAM_LOG("setup-start");

    // declare variables
    prog_handler = 0;
    sdcardMounted = false;
    wifiConnected = false;
    BLEConnected = false;
    bruceConfig.bright = 100; // theres is no value yet
    bruceConfigPins.rotation = ROTATION;
    setup_gpio();

    tft.init();
    tft.setRotation(bruceConfigPins.rotation);
    tft.fillScreen(TFT_BLACK);
    // bruceConfig is not read yet.. just to show something on screen due to long boot time
    tft.setTextColor(TFT_PURPLE, TFT_BLACK);
    tft.drawCentreString("Booting", tft.width() / 2, tft.height() / 2, 1);
    RAM_LOG("first-display-elem"); // first element drawn on screen

    _pre_storage_gpio();
    begin_storage();
    RAM_LOG("after-storage"); // bruceConfig/bruceConfigPins loaded from FS
    begin_tft();
    init_clock();
    init_led();
    RAM_LOG("after-tft-clock-led");

    options.reserve(20); // preallocate some options space to avoid fragmentation

    RAM_LOG("before-wifi-init"); // largest contiguous internal block here gates Wi-Fi/BLE

    // Set WiFi country to avoid warnings and ensure max power
    const wifi_country_t country = {
        .cc = "US",
        .schan = 1,
        .nchan = 14,
#ifdef CONFIG_ESP_PHY_MAX_TX_POWER
        .max_tx_power = CONFIG_ESP_PHY_MAX_TX_POWER, // 20
#endif
        .policy = WIFI_COUNTRY_POLICY_MANUAL
    };

    esp_wifi_set_max_tx_power(80); // 80 translates to 20dBm
    esp_wifi_set_country(&country);

    // Some GPIO Settings (such as CYD's brightness control must be set after tft and sdcard)
    _post_setup_gpio();
    // Some board interfaces initialize or reset the backlight in post-setup,
    // so re-apply the stored brightness after that stage completes.
    setBrightness(bruceConfig.bright, false);
    // end of post gpio begin

    inputLockInit();
#if defined(HAS_RESISTIVE_TOUCH)
    // Runs before the input task exists; asks for the calibration when nothing is stored in the NVS
    if (!loadTouchCalibration()) calibrateTouch();
#endif
    // This task keeps running all the time, will never stop
    xTaskCreate(
        taskInputHandler,              // Task function
        "InputHandler",                // Task Name
        INPUT_HANDLER_TASK_STACK_SIZE, // Stack size
        NULL,                          // Task parameters
        2,                             // Task priority (0 to 3), loopTask has priority 2.
        &xHandle                       // Task handle (not used)
    );
    _late_setup_gpio();
#if !defined(USE_DUMMY_TFT)
    bruceConfig.openThemeFile(bruceConfig.themeFS(), bruceConfig.themePath, false);
    if (!bruceConfig.instantBoot) {
        boot_screen_anim();
        startup_sound();
    }
    if (bruceConfig.wifiAtStartup) {
        log_i("Loading Wifi at Startup");
        xTaskCreate(
            wifiConnectTask,   // Task function
            "wifiConnectTask", // Task Name
            4096,              // Stack size
            NULL,              // Task parameters
            2,                 // Task priority (0 to 3), loopTask has priority 2.
            NULL               // Task handle (not used)
        );
    }
#endif
    //  start a task to handle serial commands while the webui is running
    startSerialCommandsHandlerTask(true);

    wakeUpScreen();
    if (bruceConfig.startupApp != "" && !startupApp.startApp(bruceConfig.startupApp)) {
        bruceConfig.setStartupApp("");
    }

    RAM_LOG("setup-end");
    NZLOGI("BOOT", "setup complete");
    NZLOGMEM("BOOT");
}

/**********************************************************************
 **  Function: loop
 **  Main loop
 **********************************************************************/
#if !defined(USE_DUMMY_TFT)
void loop() {
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    if (interpreter_state > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        interpreter_state = 2;
        Serial.println("Entering interpreter...");
        while (interpreter_state > 0) { vTaskDelay(pdMS_TO_TICKS(500)); }
        if (interpreter_state == 0) {
            Serial.println("Interpreter put to background.");
        } else {
            Serial.println("Exiting interpreter...");
        }
        if (interpreter_state == -1) { interpreterTaskHandler = NULL; }
        previousMillis = millis(); // ensure that will not dim screen when get back to menu
    }
#endif
    tft.fillScreen(bruceConfig.bgColor);

#if defined(ENABLE_RAM_LOGGING)
    static bool ramLoggedFirstMenu = false;
    if (!ramLoggedFirstMenu) {
        RAM_LOG("first-mainMenu");
        ramLoggedFirstMenu = true;
    }
#endif

    mainMenu.begin();
    delay(1);
}
#else

void loop() {
    tft.setLogging();
    Serial.println(
        "\n"
        "██████  ██████  ██    ██  ██████ ███████ \n"
        "██   ██ ██   ██ ██    ██ ██      ██      \n"
        "██████  ██████  ██    ██ ██      █████   \n"
        "██   ██ ██   ██ ██    ██ ██      ██      \n"
        "██████  ██   ██  ██████   ██████ ███████ \n"
        "                                         \n"
        "         PREDATORY FIRMWARE\n\n"
        "Tips: Connect to the WebUI for better experience\n"
        "      Add your network by sending: wifi add ssid password\n\n"
        "At your command:"
    );

    // Enable navigation through webUI
    tft.fillScreen(bruceConfig.bgColor);
    mainMenu.begin();
    vTaskDelay(10 / portTICK_PERIOD_MS);
}
#endif
