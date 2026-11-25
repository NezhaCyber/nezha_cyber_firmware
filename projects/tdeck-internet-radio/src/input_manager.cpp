#include "input_manager.h"
#include "pins.h"
#include <Wire.h>

namespace {

volatile uint8_t s_tbUpPulses = 0;
volatile uint8_t s_tbDownPulses = 0;
volatile uint8_t s_tbLeftPulses = 0;
volatile uint8_t s_tbRightPulses = 0;

void IRAM_ATTR isrTbUp() { s_tbUpPulses++; }
void IRAM_ATTR isrTbDown() { s_tbDownPulses++; }
void IRAM_ATTR isrTbLeft() { s_tbLeftPulses++; }
void IRAM_ATTR isrTbRight() { s_tbRightPulses++; }

} // namespace

void InputManager::begin() {
    pinMode(PIN_TB_UP, INPUT_PULLUP);
    pinMode(PIN_TB_DOWN, INPUT_PULLUP);
    pinMode(PIN_TB_LEFT, INPUT_PULLUP);
    pinMode(PIN_TB_RIGHT, INPUT_PULLUP);
    pinMode(PIN_TB_CLICK, INPUT_PULLUP);

    // T-Deck Hall-effect trackball sensors pulse on every detent transition
    attachInterrupt(digitalPinToInterrupt(PIN_TB_UP), isrTbUp, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_TB_DOWN), isrTbDown, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_TB_LEFT), isrTbLeft, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_TB_RIGHT), isrTbRight, CHANGE);

    // Initialize ESP32-C3 keyboard co-processor on I2C (SDA=18, SCL=8, addr=0x55)
    Wire.begin(PIN_KB_SDA, PIN_KB_SCL, 100000);
    Wire.beginTransmission(KB_I2C_ADDR);
    _kbPresent = (Wire.endTransmission() == 0);
    Serial.printf("[INPUT] Trackball ready, I2C Keyboard (0x%02X): %s\n",
                  KB_I2C_ADDR, _kbPresent ? "OK" : "not detected");
}

char InputManager::readKeyboardChar() {
    if (!_kbPresent) return 0;
    uint32_t now = millis();
    if (now - _lastKbPollMs < 20) return 0;
    _lastKbPollMs = now;

    Wire.requestFrom(KB_I2C_ADDR, (uint8_t)1);
    if (Wire.available()) {
        char c = static_cast<char>(Wire.read());
        if (c != 0 && c != (char)0xFF) return c;
    }
    return 0;
}

InputEvent InputManager::poll() {
    uint32_t now = millis();

    // 1. Trackball center click (GPIO 0, active LOW, debounced)
    bool clickNow = digitalRead(PIN_TB_CLICK);
    if (_lastClickState == HIGH && clickNow == LOW && (now - _lastClickMs) > 250) {
        _lastClickMs = now;
        _lastClickState = clickNow;
        return InputEvent::TOGGLE_MUTE;
    }
    _lastClickState = clickNow;

    // 2. Trackball directional rolls (require >=2 Hall pulses + cooldown to avoid accidental jumps)
    if (now - _lastRollMs >= 160) {
        noInterrupts();
        uint8_t up = s_tbUpPulses;
        uint8_t dn = s_tbDownPulses;
        uint8_t lt = s_tbLeftPulses;
        uint8_t rt = s_tbRightPulses;
        if (up >= 2 || dn >= 2 || lt >= 2 || rt >= 2) {
            s_tbUpPulses = s_tbDownPulses = s_tbLeftPulses = s_tbRightPulses = 0;
        }
        interrupts();

        if (up >= 2 && up >= dn && up >= lt && up >= rt) {
            _lastRollMs = now;
            return InputEvent::VOL_UP;
        }
        if (dn >= 2 && dn >= up && dn >= lt && dn >= rt) {
            _lastRollMs = now;
            return InputEvent::VOL_DOWN;
        }
        if (lt >= 2 && lt >= rt && lt >= up && lt >= dn) {
            _lastRollMs = now;
            return InputEvent::STATION_PREV;
        }
        if (rt >= 2 && rt >= lt && rt >= up && rt >= dn) {
            _lastRollMs = now;
            return InputEvent::STATION_NEXT;
        }
    }

    // 3. ESP32-C3 I2C Keyboard shortcuts (and optional USB Serial control)
    char key = readKeyboardChar();
    if (key == 0 && Serial.available()) {
        key = static_cast<char>(Serial.read());
    }
    if (key != 0) {
        if (key == '+' || key == '=' || key == 'u' || key == 'U') return InputEvent::VOL_UP;
        if (key == '-' || key == '_' || key == 'd' || key == 'D') return InputEvent::VOL_DOWN;
        if (key == 'n' || key == 'N' || key == ']' || key == '>') return InputEvent::STATION_NEXT;
        if (key == 'p' || key == 'P' || key == '[' || key == '<') return InputEvent::STATION_PREV;
        if (key == 'm' || key == 'M' || key == ' ') return InputEvent::TOGGLE_MUTE;
        if (key == 'w' || key == 'W') return InputEvent::WIFI_SETUP;
        if (key == 'r' || key == 'R') return InputEvent::RECONNECT;
        if (key >= '1' && key <= '9') {
            return static_cast<InputEvent>(static_cast<uint8_t>(InputEvent::PRESET_1) + (key - '1'));
        }
    }

    return InputEvent::NONE;
}

String InputManager::readLineBlocking(
    const char *prompt, bool maskInput, void (*onRedraw)(const String &val)
) {
    Serial.printf("\n[SETUP] %s (type on T-Deck keyboard or Serial, press Enter):\n> ", prompt);
    String value;
    auto redraw = [&]() {
        if (!onRedraw) return;
        if (maskInput) {
            String masked;
            for (size_t i = 0; i < value.length(); ++i) masked += '*';
            onRedraw(masked);
        } else {
            onRedraw(value);
        }
    };
    redraw();

    while (true) {
        char c = readKeyboardChar();
        if (c == 0 && Serial.available()) {
            c = static_cast<char>(Serial.read());
        }
        if (c != 0) {
            if (c == '\r' || c == '\n') {
                Serial.println();
                return value;
            }
            if (c == 0x08 || c == 0x7F) { // Backspace
                if (!value.isEmpty()) {
                    value.remove(value.length() - 1);
                    Serial.print("\b \b");
                    redraw();
                }
            } else if (c >= 32 && c <= 126 && value.length() < 63) {
                value += c;
                Serial.print(maskInput ? '*' : c);
                redraw();
            }
        }
        delay(10);
    }
}
