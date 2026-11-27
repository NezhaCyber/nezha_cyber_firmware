#include "core/bus_HAL.h"
#include "core/powerSave.h"
#include "core/utils.h"
#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include <Wire.h>
#include <interface.h>

#define BTN_ACT LOW

// Setup for Trackball (volatile pulse accumulators with critical-section atomic drain)
void IRAM_ATTR ISR_up();
void IRAM_ATTR ISR_down();
void IRAM_ATTR ISR_left();
void IRAM_ATTR ISR_right();

static portMUX_TYPE tbMux = portMUX_INITIALIZER_UNLOCKED;
static volatile int16_t trackball_up_count = 0;
static volatile int16_t trackball_down_count = 0;
static volatile int16_t trackball_left_count = 0;
static volatile int16_t trackball_right_count = 0;

void IRAM_ATTR ISR_up() {
    portENTER_CRITICAL_ISR(&tbMux);
    trackball_up_count++;
    portEXIT_CRITICAL_ISR(&tbMux);
}
void IRAM_ATTR ISR_down() {
    portENTER_CRITICAL_ISR(&tbMux);
    trackball_down_count++;
    portEXIT_CRITICAL_ISR(&tbMux);
}
void IRAM_ATTR ISR_left() {
    portENTER_CRITICAL_ISR(&tbMux);
    trackball_left_count++;
    portEXIT_CRITICAL_ISR(&tbMux);
}
void IRAM_ATTR ISR_right() {
    portENTER_CRITICAL_ISR(&tbMux);
    trackball_right_count++;
    portEXIT_CRITICAL_ISR(&tbMux);
}

#define LILYGO_KB_SLAVE_ADDRESS 0x55
#define KB_I2C_SDA 18 // SYS_I2C_SDA
#define KB_I2C_SCL 8  // SYS_I2C_SCL
#define SEL_BTN 0
#define UP_BTN 3
#define DW_BTN 15
#define L_BTN 1 // Physical LEFT Hall sensor on T-Deck (BOARD_TBOX_G03)
#define R_BTN 2 // Physical RIGHT Hall sensor on T-Deck (BOARD_TBOX_G01)
#define PIN_POWER_ON 10
#define BOARD_TOUCH_INT 16

// covers that variant -- same per-rotation values the old InputHandler's isPlus=false branch used.
static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = KB_I2C_SDA;
    cfg.pin_scl = KB_I2C_SCL;
    cfg.pin_irq = BOARD_TOUCH_INT;
    cfg.i2c_bus = &Wire1;
#ifdef T_DECK_PLUS
    constexpr bool isPlus = true;
#else
    constexpr bool isPlus = false;
#endif
    // rotation:          0         1        2         3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, !isPlus, true, isPlus};
    bool mirrorY[4] = {!isPlus, true, isPlus, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cc
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    bruceConfigPins.speaker_bus = {
        (gpio_num_t)7, (gpio_num_t)5, (gpio_num_t)6, (gpio_num_t)48
    }; // bclk,ws,dout,mclk
    // Wiring kept for reference; this board has no HAS_MICROPHONE gate
    bruceConfigPins.mic_bus = {(gpio_num_t)21, (gpio_num_t)14, GPIO_NUM_NC, MIC_TYPE_PDM}; // clk,data,ws,type
    bruceConfigPins.sys_i2c = {(gpio_num_t)18, (gpio_num_t)8};                             // sda, scl
    bruceConfigPins.i2c_bus = {(gpio_num_t)43, (gpio_num_t)44};                            // sda, scl (Grove)
    bruceConfigPins.rfTx = 43;
    bruceConfigPins.rfRx = 44;
    bruceConfigPins.irTx = -1;
    bruceConfigPins.irRx = 44;
    bruceConfigPins.rotation = 1;
    bruceConfigPins.uart_bus = {(gpio_num_t)44, (gpio_num_t)43};   // rx, tx
    bruceConfigPins.gps_bus = {(gpio_num_t)44, (gpio_num_t)43};    // rx, tx
    bruceConfigPins.badusb_bus = {(gpio_num_t)44, (gpio_num_t)43}; // rx, tx (Grove)
    // Board's default/generic SPI bus (used by drivers without their own bus, e.g. RC522-SPI)
    bruceConfigPins.outer_bus = {(gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)-1};
    bruceConfigPins.PN532_bus = {(gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)-1};
    // CC1101/NRF24/SDCARD/W5500/LoRa share the main SPI bus (sck=40, miso=38, mosi=41)
    bruceConfigPins.CC1101_bus = {
        (gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)43, (gpio_num_t)44, GPIO_NUM_NC
    }; // sck,miso,mosi,cs,gdo0,gdo2
    bruceConfigPins.NRF24_bus = {
        (gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)43, (gpio_num_t)44
    }; // sck,miso,mosi,cs(ss),ce
    bruceConfigPins.SDCARD_bus = {
        (gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)39
    }; // sck,miso,mosi,cs
#if !defined(LITE_VERSION)
    bruceConfigPins.W5500_bus = {
        (gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)43, (gpio_num_t)44, GPIO_NUM_NC
    }; // sck,miso,mosi,cs,int,rst
    bruceConfigPins.LoRa_bus = {
        (gpio_num_t)40, (gpio_num_t)38, (gpio_num_t)41, (gpio_num_t)9, (gpio_num_t)17, (gpio_num_t)45
    }; // sck,miso,mosi,cs,rst,dio0
#endif

    // Power on peripheral rail first so ESP32-C3 keyboard MCU and GT911 boot before I2C init
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    delay(500);           // wait for ESP32-C3 keyboard MCU to boot after PIN_POWER_ON
    setSysI2CBus(&Wire1); // Keyboard (0x55) + GT911 touch both live on Wire1
    if (!Wire1.begin(bruceConfigPins.sys_i2c.sda, bruceConfigPins.sys_i2c.scl))
        Serial.println("Fail starting ESP32-C3 keyboard");

    pinMode(SEL_BTN, INPUT);

    pinMode(9, OUTPUT); // LoRa Radio CS Pin to HIGH (Inhibit the SPI Communication for this module)
    digitalWrite(9, HIGH);

    // Setup for Trackball
    pinMode(UP_BTN, INPUT_PULLUP);
    attachInterrupt(UP_BTN, ISR_up, FALLING);
    pinMode(DW_BTN, INPUT_PULLUP);
    attachInterrupt(DW_BTN, ISR_down, FALLING);
    pinMode(L_BTN, INPUT_PULLUP);
    attachInterrupt(L_BTN, ISR_left, FALLING);
    pinMode(R_BTN, INPUT_PULLUP);
    attachInterrupt(R_BTN, ISR_right, FALLING);

#ifdef T_DECK_PLUS
    bruceConfigPins.gpsBaudrate = 38400;
#endif
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cc
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    if (!hal_touch_init(touchCfg(), 0x5D) && !hal_touch_init(touchCfg(), 0x14)) {
        Serial.println("Failed to find GT911 (0x5D/0x14) - check your wiring!");
    }
    // Brightness control must be initialized after tft in this case @Pirata
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, 100);
}
/*********************************************************************
** Function: setBrightness
** location: settings.cc
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    char keyValue = 0;
    unsigned long now = millis();
    static unsigned long tm = 0;
    static unsigned long lastMoveMs = 0;
    static unsigned long gatherStartMs = 0;
    static int16_t accX = 0;
    static int16_t accY = 0;

    // 1. Atomically drain trackball Hall-sensor pulses on every poll (no delay inside inputLock)
    int16_t du, dd, dl, dr;
    portENTER_CRITICAL(&tbMux);
    du = trackball_up_count;
    dd = trackball_down_count;
    dl = trackball_left_count;
    dr = trackball_right_count;
    trackball_up_count = 0;
    trackball_down_count = 0;
    trackball_left_count = 0;
    trackball_right_count = 0;
    portEXIT_CRITICAL(&tbMux);

    if (du || dd || dl || dr) {
        if (now - lastMoveMs < 110) {
            // Discard trailing Hall-sensor bounce from the previous flick
            accX = 0;
            accY = 0;
            gatherStartMs = 0;
        } else {
            int16_t dx = dr - dl;
            int16_t dy = dd - du;
            // Align trackball axes with current display rotation (rotation 1 = default landscape)
            uint8_t rot = bruceConfigPins.rotation & 0x03;
            int16_t rx = dx, ry = dy;
            if (rot == 3) {
                rx = -dx;
                ry = -dy;
            } else if (rot == 0) {
                rx = -dy;
                ry = dx;
            } else if (rot == 2) {
                rx = dy;
                ry = -dx;
            }
            accX += rx;
            accY += ry;
            if (gatherStartMs == 0 && (accX != 0 || accY != 0)) { gatherStartMs = now; }
        }
    }

    // Emit one clean dominant-axis step once >=2 pulses confirm the axis or after a 25ms gather window
    if (gatherStartMs != 0 &&
        (abs(accX) >= 2 || abs(accY) >= 2 || (now - gatherStartMs >= 25))) {
        int16_t fx = accX;
        int16_t fy = accY;
        accX = 0;
        accY = 0;
        gatherStartMs = 0;
        if (fx != 0 || fy != 0) {
            lastMoveMs = now;
            if (wakeUpScreen()) return;
            AnyKeyPress = true;
            if (abs(fy) > abs(fx)) {
                if (fy < 0) UpPress = true;
                else DownPress = true;
            } else {
                if (fx < 0) PrevPress = true;
                else NextPress = true;
            }
            return;
        }
    }

    static unsigned long lastKbMs = 0;

    // 2. Poll the ESP32-C3 I2C keyboard without the 160ms touch/button throttle: reading 0x55 pops
    //    the hardware key buffer, so discarding it or clearing KeyStroke on the next idle tick drops input.
    if (Wire1.requestFrom(LILYGO_KB_SLAVE_ADDRESS, 1) == 1 && Wire1.available() > 0) {
        keyValue = static_cast<char>(Wire1.read());
    }

    const uint8_t kbByte = static_cast<uint8_t>(keyValue);
    if (kbByte != 0x00 && kbByte != 0xFF) {
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;

        if (!KeyStroke.pressed || (now - lastKbMs > 400)) { KeyStroke.Clear(); }
        KeyStroke.hid_keys.push_back(keyValue);
        if (keyValue == (char)0x08) {
            if (!KeyStroke.word.empty()) {
                KeyStroke.word.pop_back();
            } else {
                KeyStroke.del = true;
            }
            EscPress = true;
        } else if (keyValue == (char)0x0D || keyValue == (char)0x0A) {
            KeyStroke.enter = true;
            SelPress = true;
        } else if (keyValue == (char)0x1B) {
            KeyStroke.exit_key = true;
            EscPress = true;
        } else {
            if (keyValue == ' ') KeyStroke.exit_key = true; // Fn+Space exit in HID/Ducky modes
            KeyStroke.word.push_back(keyValue);
        }
        if (digitalRead(SEL_BTN) == BTN_ACT) KeyStroke.fn = true;
        KeyStroke.pressed = true;
        lastKbMs = now;
    } else if (KeyStroke.pressed && (now - lastKbMs > 350)) {
        // Expire unconsumed keystrokes when on screens that do not drain _getKeyPress()
        KeyStroke.Clear();
    }

    // 3. Poll GT911 touch and trackball center click (SEL_BTN) with 160ms debounce
    BruceTouchPoint t;
    bool touched = hal_touch_read(touchCfg(), t);

    if (now - tm < 160 && !LongPress) return;

    if (digitalRead(SEL_BTN) == BTN_ACT) {
        tm = now;
        if (!wakeUpScreen()) {
            AnyKeyPress = true;
        } else return;
        SelPress = true;
    }

    if (touched && ((now - tm) >= 160 || LongPress)) { // responsive touch polling
        tm = now;
        hal_touch_apply(t);
        return;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cc
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cc
** Btn logic to turn off the device (name is odd btw)
**********************************************************************/
void checkReboot() {}
