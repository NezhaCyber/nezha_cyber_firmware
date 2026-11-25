#pragma once

#include <Arduino.h>

enum class InputEvent : uint8_t {
    NONE = 0,
    VOL_UP,
    VOL_DOWN,
    STATION_PREV,
    STATION_NEXT,
    TOGGLE_MUTE,
    WIFI_SETUP,
    RECONNECT,
    PRESET_1,
    PRESET_2,
    PRESET_3,
    PRESET_4,
    PRESET_5,
    PRESET_6,
    PRESET_7,
    PRESET_8,
    PRESET_9,
};

class InputManager {
public:
    void begin();
    InputEvent poll();

    // Poll a single ASCII key from ESP32-C3 I2C keyboard (0 if none)
    char readKeyboardChar();

    // Blocking line input using T-Deck I2C keyboard OR USB-C Serial
    String readLineBlocking(const char *prompt, bool maskInput, void (*onRedraw)(const String &val));

private:
    bool _kbPresent = false;
    bool _lastClickState = HIGH;
    uint32_t _lastClickMs = 0;
    uint32_t _lastRollMs = 0;
    uint32_t _lastKbPollMs = 0;
};
