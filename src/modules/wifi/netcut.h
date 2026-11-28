#ifndef __NETCUT_H__
#define __NETCUT_H__

/**
 * @file netcut.h
 * @brief NetCut ARP Module for Bruce Firmware
 * @description ARP Poisoning / Restore / Troll module integrated into Bruce WiFi menu.
 *              Ported from standalone NetCut ESP32 firmware.
 *
 * Key design decisions:
 *   - Uses LwIP netif->linkoutput() for ARP packet injection (NOT esp_wifi_internal_tx)
 *   - Reuses Bruce's ARPScanner infrastructure for device discovery
 *   - All UI via Bruce's loopOptions() — no custom display drawing
 *   - Troll Mode is BLOCKING — stops when user presses EscPress/Back
 *   - No WebUI — control exclusively via T-Embed screen + encoder
 *   - VIP persistence via LittleFS (Bruce's standard config system)
 *
 * @note Requires active WiFi STA connection before use.
 */

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

// LwIP headers for ARP packet construction
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

// Bruce core headers
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/net_utils.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include <globals.h>

/*
 * Configuration constants for the NetCut ARP module: scan-list cap, poison
 * cadence and burst sizes, restore repetition rounds, troll timing bounds,
 * and the LittleFS path used for VIP persistence.
 */
#define NETCUT_MAX_DEVICES 64      // Max devices in scan list
#define NETCUT_ARP_INTERVAL_MS 150 // ARP poison packet repeat interval (ms)
#define NETCUT_POISON_BURST 5      // Packets per poison burst
#define NETCUT_RESTORE_BURST 5     // Packets per restore burst
#define NETCUT_RESTORE_ROUNDS 3    // Restore repetition rounds

#define NETCUT_TROLL_DEFAULT_OFFLINE_MS 10000 // Default troll offline duration
#define NETCUT_TROLL_DEFAULT_ONLINE_MS 5000   // Default troll online duration
#define NETCUT_TROLL_MIN_MS 1000              // Minimum troll interval
#define NETCUT_TROLL_MAX_MS 60000             // Maximum troll interval

#define NETCUT_VIP_FILE "/netcut_vip.json" // LittleFS VIP persistence file

/*
 * Per-device record shared by the scan, poison, restore, and troll paths.
 * The MAC is kept both as raw bytes (for direct ARP packet construction on
 * the hot path) and as a string (for display and VIP lookup).
 */

/**
 * @brief Represents a network device discovered by ARP scan.
 *
 * Adapted from standalone NetCut's NetworkDevice struct.
 * Stores MAC as raw bytes (uint8_t[6]) for direct use in ARP packets,
 * avoiding repeated string-to-bytes conversion in hot path.
 */
struct NetCutDevice {
    IPAddress ip;        // Device IP address
    uint8_t macBytes[6]; // MAC address as raw bytes (for ARP packets)
    String macStr;       // MAC address as string (for display/VIP lookup)

    bool isCut;    // Currently being ARP poisoned
    bool isTroll;  // Currently in troll mode
    bool isVip;    // VIP-protected

    // Troll mode individual timers
    unsigned long lastTrollToggle; // Last time troll state toggled (millis)
    bool isTrollOffline;           // Current troll phase: true=poisoning, false=restoring

    // Restore continuation timer
    unsigned long restoreUntil; // Keep sending restore packets until this time (millis)

    NetCutDevice()
        : isCut(false), isTroll(false), isVip(false), lastTrollToggle(0),
          isTrollOffline(false), restoreUntil(0) {
        memset(macBytes, 0, 6);
    }
};

/*
 * Module state is file-scoped inside netcut.cc to prevent global symbol
 * collisions; this header exposes only the public function declarations.
 */

/*
 * Top-level entry point launched from the Wi-Fi menu. Requires an active STA
 * connection; the flow is check WiFi -> scan devices -> list -> per-device or
 * global actions.
 */

/**
 * @brief Main entry point for NetCut module.
 *        Called from WifiMenu as a menu option.
 *        Requires active WiFi STA connection.
 *
 * Flow: Check WiFi → Scan Devices → Show Device List → Per-device/Global actions
 */
void netcutMenu();

/*
 * Core ARP operations: device discovery via the LwIP ARP table, bidirectional
 * poisoning of a target and the gateway, and reliable restore using broadcast
 * plus unicast ARP reply/request combinations per RFC 826.
 */

/**
 * @brief Scan the local network for devices via ARP requests.
 *        Uses LwIP etharp_request() + etharp_get_entry() — same method as Bruce's ARPScanner.
 *        Results are stored in internal device list.
 * @return Number of devices found.
 */
int netcutScanDevices();

/**
 * @brief Send ARP poison packets to a single device.
 *        Poisons both the target AND the gateway (bidirectional).
 *        Uses netif->linkoutput() via LwIP pbuf — NOT esp_wifi_internal_tx().
 * @param deviceIndex  Index in internal device list
 * @param burstCount   Number of packets to send (default 1 for periodic, 20 for instant)
 */
void netcutPoisonDevice(int deviceIndex, int burstCount = 1);

/**
 * @brief Send ARP restore packets to a single device.
 *        Sends correct gateway MAC to target, and correct target MAC to gateway.
 *        Uses broadcast + unicast ARP reply/request combo for reliability (RFC 826).
 * @param deviceIndex  Index in internal device list
 */
void netcutRestoreDevice(int deviceIndex);

/**
 * @brief Cut all non-VIP devices (bulk ARP poison).
 */
void netcutCutAll();

/**
 * @brief Resume all devices (bulk ARP restore).
 *        Also clears isCut and isTroll flags.
 */
void netcutResumeAll();

/*
 * Troll mode alternates a device between poison (offline) and restore (online)
 * phases on per-device timers. Both single-device and bulk variants are
 * blocking loops that run until the user presses Esc.
 */

/**
 * @brief Start Troll Mode for a single device.
 *        BLOCKING — loops until user presses EscPress.
 *        Alternates between poison (offline) and restore (online) phases
 *        using individual per-device timers.
 * @param deviceIndex  Index in internal device list
 */
void netcutTrollDevice(int deviceIndex);

/**
 * @brief Start Troll Mode for ALL non-VIP devices.
 *        BLOCKING — loops until user presses EscPress.
 */
void netcutTrollAll();

/*
 * VIP devices are immune to cut and troll operations. Toggling VIP on an
 * active target restores it immediately; the VIP MAC list is persisted to
 * LittleFS so it survives reboots and is re-applied during each scan.
 */

/**
 * @brief Toggle VIP status for a device.
 *        VIP devices are immune to Cut/Troll operations.
 *        If device is currently Cut/Trolled, it will be restored immediately.
 * @param deviceIndex  Index in internal device list
 */
void netcutToggleVip(int deviceIndex);

/**
 * @brief Save VIP list to LittleFS.
 *        Persists MAC addresses of all VIP-flagged devices.
 */
void netcutSaveVipList();

/**
 * @brief Load VIP list from LittleFS.
 *        Called during scan to auto-flag previously-saved VIP devices.
 */
void netcutLoadVipList();

/*
 * Interactive menu to adjust the troll offline and online durations through
 * Bruce's loopOptions() numeric input. Values are clamped to [1, 300] seconds
 * and stored as milliseconds for the active loop.
 */

/**
 * @brief Interactive menu to adjust troll offline/online durations.
 *        Uses Bruce's loopOptions() for value input.
 */
void netcutTrollTimingMenu();

#endif // __NETCUT_H__
