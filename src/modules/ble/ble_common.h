#ifndef __BLE_COMMON_H__
#define __BLE_COMMON_H__

#include <NimBLEAdvertisedDevice.h>
#include <NimBLEBeacon.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>

#include "core/display.h"
#include <globals.h>

/*
 * BLE scan timing and display-limit constants shared across BLE modules.
 * SCANTIME/SCAN_INT/SCAN_WINDOW configure the NimBLE active scan window and
 * duty cycle; MAX_DISPLAY_DEVICES caps the rendered device list to avoid heap
 * exhaustion on no-PSRAM 4 MB boards; SCAN_TIME_REDUCED shortens scans when
 * the radio-memory guard reports low contiguous DMA headroom.
 */

#define SCANTIME 5
#define SCANTYPE ACTIVE
#define SCAN_INT 100
#define SCAN_WINDOW 99

// Maximum number of BLE devices to display to prevent memory issues
#define MAX_DISPLAY_DEVICES 100

// Memory protection: Reduce scan time in low-memory situations
#define SCAN_TIME_REDUCED 3

extern BLEScan *pBLEScan;
extern int scanTime;

void ble_test();
#if 0
#ifdef BOARD_HAS_PSRAM
constexpr bool FORCE_RADIO_TEARDOWN_ON_SWITCH = false;
#else
constexpr bool FORCE_RADIO_TEARDOWN_ON_SWITCH = true;
#endif
#else
constexpr bool FORCE_RADIO_TEARDOWN_ON_SWITCH = false;
#endif

bool ble_scan_setup();
void ble_scan();
void stopBLEStack();

bool bleNotifyRetry(NimBLECharacteristic *chr, const uint8_t *value, size_t length, uint8_t retries = 8);
bool bleNotifyRetry(NimBLECharacteristic *chr, uint8_t retries = 8);

void disPlayBLESend();

#endif
