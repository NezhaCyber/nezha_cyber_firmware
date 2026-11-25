#pragma once

// ============================================================================
// LILYGO T-Deck (ESP32-S3FN16R8) Strict Hardware Pinout
// ============================================================================

// 1. Peripheral Power Rail Enable (MUST be pulled HIGH first at boot!)
constexpr int PIN_BOARD_POWERON = 10;

// 2. MAX98357A I2S Speaker Amplifier
constexpr int PIN_I2S_BCLK = 7;
constexpr int PIN_I2S_LRC  = 5;
constexpr int PIN_I2S_DOUT = 6;

// 3. ST7789 2.8" IPS LCD (320x240 landscape) & Shared SPI Bus
constexpr int PIN_TFT_CS   = 12;
constexpr int PIN_TFT_DC   = 11;
constexpr int PIN_TFT_SCLK = 40;
constexpr int PIN_TFT_MOSI = 41;
constexpr int PIN_TFT_MISO = 38;
constexpr int PIN_TFT_BL   = 42;
constexpr int PIN_SD_CS    = 39;
constexpr int PIN_LORA_CS  = 9; // Deselect SX1262 LoRa on shared SPI bus

// 4. Trackball Hall-Effect Sensors + Center Click
constexpr int PIN_TB_UP    = 3;
constexpr int PIN_TB_DOWN  = 15;
constexpr int PIN_TB_LEFT  = 1;
constexpr int PIN_TB_RIGHT = 2;
constexpr int PIN_TB_CLICK = 0;

// 5. ESP32-C3 Keyboard Co-Processor (I2C)
constexpr int PIN_KB_SDA       = 18;
constexpr int PIN_KB_SCL       = 8;
constexpr uint8_t KB_I2C_ADDR  = 0x55;
