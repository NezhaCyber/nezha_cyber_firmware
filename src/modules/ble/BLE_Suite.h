#ifndef BLE_SUITE_H
#define BLE_SUITE_H
#if !defined(LITE_VERSION)

// Include display header FIRST so TFT color definitions are available
#include "core/display.h"

#include "HFP_Exploit.h"
#include "fastpair_crypto.h"
#include <NimBLEDevice.h>
#include "ble_p4_compat.h"
#include <WString.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>

extern volatile int tftWidth;
extern volatile int tftHeight;
class tft_logger;
extern tft_logger tft;
class BruceConfig;
extern BruceConfig bruceConfig;

bool check(int key);

/*
 * NimBLE scan timing constants used by the BLE suite scanner.
 * ACTIVE_SCAN_TIME/PASSIVE_SCAN_TIME set the scan duration in seconds for
 * active and passive discovery; SCAN_INT/SCAN_WINDOW set the controller
 * interval and window (0.625 ms units) — window <= interval controls the duty
 * cycle and how quickly nearby advertisers are detected.
 */

#define ACTIVE_SCAN_TIME 8
#define PASSIVE_SCAN_TIME 8
#define SCAN_INT 100
#define SCAN_WINDOW 99

/*
 * Shared BLE enums. BLE_*_PRESS map the four navigation keys to dispatch
 * indices; FastPairPopupType selects the pairing-popup flavor and
 * FastPairExploitType selects the exploit primitive exercised against a target.
 */

enum { BLE_ESC_PRESS = 0, BLE_SEL_PRESS = 1, BLE_PREV_PRESS = 2, BLE_NEXT_PRESS = 3 };

enum FastPairPopupType { FP_POPUP_REGULAR = 0, FP_POPUP_FUN, FP_POPUP_PRANK, FP_POPUP_CUSTOM };

enum FastPairExploitType {
    FP_EXPLOIT_MEMORY_CORRUPTION = 0,
    FP_EXPLOIT_STATE_CONFUSION,
    FP_EXPLOIT_CRYPTO_OVERFLOW,
    FP_EXPLOIT_HANDSHAKE_FAULT,
    FP_EXPLOIT_RAPID_CONNECTION,
    FP_EXPLOIT_ALL
};

/*
 * Scanner result PODs. DeviceInfo describes one discovered device (address,
 * name, RSSI, and FastPair/HFP/type flags). DeviceSnapshot captures a
 * version-stamped copy of the scanner vectors under the scanner mutex so the
 * UI/render path can read a consistent view without holding the lock.
 */

struct DeviceInfo {
    String address;
    String name;
    int rssi;
    bool hasFastPair;
    bool hasHFP;
    uint8_t deviceType;
};

struct DeviceSnapshot {
    uint32_t version;
    uint32_t count;
    uint32_t timestamp;
    std::vector<String> names;
    std::vector<String> addresses;
    std::vector<int> rssi;
    std::vector<bool> fastPair;
    std::vector<bool> hfp;
    std::vector<uint8_t> types;

    DeviceSnapshot() : version(0), count(0), timestamp(0) {}
};

/*
 * Flat carrier passed from the scan UI into attack routines, duplicating the
 * fields an attack needs (address, name, RSSI, capability flags, type) so it
 * does not depend on live scanner state.
 */

struct SelectedDevice {
    String address;
    String name;
    int rssi;
    bool hasFastPair;
    bool hasHFP;
    uint8_t deviceType;
};

/*
 * Thread-safe scanner result table. Parallel vectors hold device fields; a
 * FreeRTOS mutex guards mutation, and getSnapshot() returns a cached
 * DeviceSnapshot stamped with dataVersion so consumers can cheaply detect
 * changes and avoid rebuilding the view when nothing moved.
 */

struct ScannerData {
    std::vector<String> deviceNames;
    std::vector<String> deviceAddresses;
    std::vector<int> deviceRssi;
    std::vector<bool> deviceFastPair;
    std::vector<bool> deviceHasHFP;
    std::vector<uint8_t> deviceTypes;
    SemaphoreHandle_t mutex;
    int foundCount;

    uint32_t dataVersion;
    DeviceSnapshot *snapshotCache;
    uint32_t cacheTimestamp;

    ScannerData();
    ~ScannerData();
    void
    addDevice(const String &name, const String &address, int rssi, bool fastPair, bool hasHFP, uint8_t type);
    void clear();
    size_t size();
    DeviceSnapshot *getSnapshot();
    bool getDeviceInfo(int index, DeviceInfo &info);
};

struct CharacteristicInfo {
    String uuid;
    bool canRead;
    bool canWrite;
    bool canNotify;
};

struct DeviceProfile {
    String address;
    bool connected;
    bool hasFastPair;
    bool hasAVRCP;
    bool hasHID;
    bool hasBattery;
    bool hasDeviceInfo;
    std::vector<String> services;
    std::vector<CharacteristicInfo> characteristics;
};

struct HIDDeviceProfile {
    String osType;
    bool supportsBootProtocol;
    bool supportsReportProtocol;
    bool requiresAuthentication;
    bool hasExistingBond;
    uint16_t vendorId;
    uint16_t productId;
    std::vector<String> servicePatterns;
    int connectionBehavior;
    String deviceName;
    int rssi;
    bool isAppleDevice;
    bool isWindowsDevice;
    bool isAndroidDevice;
    bool isLinuxDevice;
    bool isIoTDevice;
    String suggestedAttack;
};

struct HIDConnectionResult {
    bool success;
    String method;
    NimBLEClient *client;
    uint32_t attemptTime;
    int attemptCount;
};

struct DuckyCommand {
    String command;
    String parameter;
    int delay_ms;
};

/*
 * RAII guard that runs a stored std::function on scope exit unless disable()
 * is called first. Used to guarantee BLE stack/client teardown runs on every
 * return path, including early exits and error paths.
 */

class AutoCleanup {
private:
    std::function<void()> cleanupFunc;
    bool enabled;

public:
    AutoCleanup(std::function<void()> func, bool enable = true);
    ~AutoCleanup();
    void disable();
    void enable();
};

/*
 * Centralizes NimBLE controller lifecycle. initBLE()/deinitBLE() bring the
 * stack up and down (gated by the radio-memory guard); registerClient/
 * unregisterClient/cleanupAllClients() track live NimBLEClient handles so they
 * are deterministically torn down before re-init.
 */

class BLEStateManager {
private:
    static bool bleInitialized;
    static std::vector<NimBLEClient *> activeClients;
    static String currentDeviceName;

public:
    static bool initBLE(const String &name, int powerLevel = ESP_PWR_LVL_P9);
    static void deinitBLE(bool immediate = false);
    static void registerClient(NimBLEClient *client);
    static void unregisterClient(NimBLEClient *client);
    static void cleanupAllClients();
    static bool isBLEActive();
    static String getCurrentDeviceName();
    static size_t getActiveClientCount();
};

/*
 * Orchestrates per-attack BLE connection setup: prepares the stack, connects
 * to a target with an optional exploit handshake, and profiles a device's
 * services/characteristics for downstream attack selection.
 */

class BLEAttackManager {
public:
    void prepareForConnection();
    void cleanupAfterAttack();
    bool connectToDevice(NimBLEAddress target, NimBLEClient **outClient, bool useExploitHandshake = false);
    DeviceProfile profileDevice(NimBLEAddress target);
};

/*
 * FastPair data types and helpers. FastPairDeviceInfo/ModelInfo describe a
 * discovered device and its model metadata; Samsung OUI lookup and
 * detectFastPairVersion() classify targets before exploit selection.
 */

struct FastPairDeviceInfo {
    NimBLEAddress address;
    String name;
    int rssi;
    bool supportsFastPair;
    bool connected;
    uint32_t modelId;
    String deviceType;
};

struct FastPairModelInfo {
    uint32_t modelId;
    const char *name;
    const char *deviceType;
};

// v3.1: Samsung MAC OUI detection
extern const char *SAMSUNG_MAC_OUIS[];
extern const int SAMSUNG_MAC_OUIS_COUNT;
bool isSamsungDevice(const NimBLEAddress &address);
bool isSamsungDevice(const String &mac);

// v3.1: FastPair version detection
enum FastPairVersion { FP_VERSION_UNKNOWN = 0, FP_VERSION_1, FP_VERSION_2, FP_VERSION_3 };

FastPairVersion detectFastPairVersion(NimBLEAddress target);

/*
 * FastPair attack surface: scans for FastPair-capable devices, exploits the
 * Key-Based Pairing characteristic (memory corruption, state confusion, crypto
 * overflow, handshake fault, rapid connection), and spams pairing popups using
 * model IDs drawn from curated per-flavor tables.
 */

class FastPairExploitEngine {
public:
    std::vector<FastPairDeviceInfo> scanForFastPairDevices(int duration);
    bool exploitFastPairConnection(NimBLEAddress target, FastPairExploitType exploitType);
    void spamFastPairPopups(FastPairPopupType popupType, int count);
    bool testVulnerability(NimBLEAddress target);

    bool smartExploit(NimBLEAddress target);
    bool exploitSamsungFastPair(NimBLEAddress target);
    bool exploitGoogleFastPair(NimBLEAddress target);

    bool executeMemoryCorruption(NimBLERemoteCharacteristic *pChar);
    bool executeStateConfusion(NimBLERemoteCharacteristic *pChar);
    bool executeCryptoOverflow(NimBLERemoteCharacteristic *pChar);
    bool executeHandshakeFault(NimBLERemoteCharacteristic *pChar);
    bool executeRapidConnection(NimBLEAddress target, NimBLERemoteCharacteristic *pChar);
    bool executeAllExploits(NimBLERemoteCharacteristic *pChar, NimBLEAddress target);

private:
    std::vector<FastPairDeviceInfo> discoveredDevices;
    NimBLERemoteCharacteristic *findKBPCharacteristic(NimBLERemoteService *service);
    uint32_t selectModelForPopup(FastPairPopupType type);
    uint32_t randomRegularModel();
    uint32_t randomFunModel();
    uint32_t randomPrankModel();
    uint32_t selectCustomModel();
    void createFastPairAdvertisement(uint8_t *buffer, uint32_t modelId);
    String getDeviceTypeFromModelId(uint32_t modelId);
    bool testServiceDiscovery(NimBLEAddress target);
    bool testCharacteristicAccess(NimBLEAddress target);
    bool testBufferOverflow(NimBLEAddress target);
    bool testStateConfusion(NimBLEAddress target);
    void logExploitResult(NimBLEAddress target, FastPairExploitType type, bool success);
    void generateRandomMac(uint8_t *mac);
};

/*
 * HID injection engine. Profiles a target's HID capabilities (OS, protocol,
 * auth, bond state) then attempts platform-specific pairing bypasses (Apple
 * Magic spoof, Windows/Android pairing, boot-protocol injection) to force a
 * HID connection and execute a DuckyScript payload.
 */

class HIDExploitEngine {
public:
    HIDDeviceProfile analyzeHIDDevice(NimBLEAddress target, const String &name, int rssi);
    bool tryAppleMagicSpoof(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryWindowsHIDBypass(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryAndroidJustWorks(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryBootProtocolInjection(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryRapidStateConfusion(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryHIDReportPreconnection(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryConnectionParameterAttack(NimBLEAddress target, HIDDeviceProfile profile);
    bool trySecurityModeBypass(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryAddressSpoofingAttack(NimBLEAddress target, HIDDeviceProfile profile);
    bool tryServiceDiscoveryHijack(NimBLEAddress target, HIDDeviceProfile profile);
    HIDConnectionResult forceHIDConnection(NimBLEAddress target, const String &deviceName, int rssi);
    bool executeHIDInjection(NimBLEAddress target, const String &duckyScript);
    bool testHIDVulnerability(NimBLEAddress target);
};

/*
 * Silent FastPair "WhisperPair" attack. Performs the real KBP handshake and
 * then sends malformed protocol/state/crypto frames through the Key-Based
 * Pairing characteristic; executeSilent() suppresses the visible popup while
 * executeAdvanced() selects a specific attack vector.
 */

class WhisperPairExploit {
public:
    WhisperPairExploit();
    BLEAttackManager bleManager;
    FastPairCrypto crypto;

    NimBLERemoteCharacteristic *findKBPCharacteristic(NimBLERemoteService *fastpairService);
    bool performRealHandshake(NimBLERemoteCharacteristic *kbpChar, uint8_t *devicePubKey);
    bool sendProtocolAttack(NimBLERemoteCharacteristic *kbpChar, const uint8_t *devicePubKey);
    bool sendStateConfusionAttack(NimBLERemoteCharacteristic *kbpChar);
    bool sendCryptoOverflowAttack(NimBLERemoteCharacteristic *kbpChar);
    bool testForVulnerability(NimBLERemoteCharacteristic *kbpChar);
    bool execute(NimBLEAddress target);
    bool executeSilent(NimBLEAddress target);
    bool executeAdvanced(NimBLEAddress target, int attackType);
};

/*
 * Bluetooth audio attack surface. Discovers and attacks AVRCP, media, and
 * telephony services on a connected client to inject media commands or crash
 * the target's audio stack.
 */

class AudioAttackService {
public:
    bool findAndAttackAudioServices(NimBLEClient *pClient);
    bool attackAVRCP(NimBLERemoteService *avrcpService);
    bool attackAudioMedia(NimBLERemoteService *mediaService);
    bool attackTelephony(NimBLERemoteService *teleService);
    bool executeAudioAttack(NimBLEAddress target);
    bool injectMediaCommands(NimBLEAddress target);
    bool crashAudioStack(NimBLEAddress target);
};

/*
 * Parser for DuckyScript payloads. Maps characters to HID keycodes, parses
 * script lines into DuckyCommand entries (command/parameter/delay), and loads
 * payloads from SD or a string; getCommandCount()/clear() manage the buffer.
 */

class DuckyScriptEngine {
public:
    struct HIDKeycode {
        uint8_t modifier;
        uint8_t keycode;
    };

    DuckyScriptEngine();
    HIDKeycode charToKeycode(char c);
    bool parseLine(String line);
    bool loadFromSD(const String &filename);
    bool loadFromString(const String &script);
    std::vector<DuckyCommand> getCommands();
    bool isLoaded();
    void clear();
    size_t getCommandCount();

private:
    std::vector<DuckyCommand> commands;
    bool scriptLoaded;
};

/*
 * Executes DuckyScript over a forced HID connection. Combines the parser with
 * the HID exploit engine to connect, then dispatches key presses via
 * sendHIDReport/sendString/sendSpecialKey/sendComboKey/sendGUIKey; supports
 * script injection from a string or an SD file.
 */

class HIDDuckyService {
public:
    HIDDuckyService();
    bool injectDuckyScript(NimBLEAddress target, const String &script);
    bool injectDuckyScriptFromSD(NimBLEAddress target, const String &filename);
    bool executeDuckyScript(NimBLEAddress target);
    bool
    forceInjectDuckyScript(NimBLEAddress target, const String &script, const String &deviceName, int rssi);
    void setDefaultDelay(int delay_ms);
    size_t getScriptSize();

private:
    DuckyScriptEngine duckyEngine;
    int defaultDelay;
    HIDExploitEngine hidExploit;
    bool sendHIDReport(NimBLERemoteCharacteristic *pChar, uint8_t modifier, uint8_t keycode);
    bool sendString(NimBLERemoteCharacteristic *pChar, const String &str);
    bool sendSpecialKey(NimBLERemoteCharacteristic *pChar, const String &key);
    bool sendComboKey(NimBLERemoteCharacteristic *pChar, const String &combo);
    bool sendGUIKey(NimBLERemoteCharacteristic *pChar, char key);
};

/*
 * Authentication-bypass primitives. Stores previously paired devices (name,
 * address, link key) to spoof a trusted peer, and attempts spoofed
 * connections / forced re-pairing to exploit target auth handling.
 */

class AuthBypassEngine {
private:
    struct PairedDevice {
        String name;
        String address;
        uint8_t linkKey[16];
        unsigned long bondedAt;
    };
    std::vector<PairedDevice> knownDevices;

public:
    AuthBypassEngine();
    void addKnownDevice(const String &name, const String &address, uint8_t linkKey[16]);
    String getSpoofAddress(const String &targetName);
    bool attemptSpoofConnection(NimBLEAddress target, const String &targetName);
    bool forceRepairing(NimBLEAddress target);
    bool exploitAuthBypass(NimBLEAddress target);
};

/*
 * Multi-target denial-of-service and relay primitives. Floods a target with
 * connections or advertisements, runs MITM and nRF24 jamming across a target
 * vector, and tracks active NimBLEClient connections for cleanup().
 */

class MultiConnectionAttack {
public:
    MultiConnectionAttack();
    ~MultiConnectionAttack();
    bool connectionFloodSingle(NimBLEAddress target, int timeout);
    bool connectionFlood(std::vector<NimBLEAddress> targets, int attemptsPerTarget = 3);
    bool advertisingSpamSingle(NimBLEAddress target);
    bool advertisingSpam(std::vector<NimBLEAddress> targets);
    bool mitmAttackSingle(NimBLEAddress target);
    bool mitmAttack(std::vector<NimBLEAddress> targets);
    bool nrf24JamAttack(int jamMode = 0);
    bool jamAndConnect(NimBLEAddress target);
    void cleanup();

private:
    std::vector<NimBLEClient *> activeConnections;
};

/*
 * Extensible BLE vulnerability scanner. Holds a registry of named checks (each
 * a bool(*)(NimBLEAddress)) with descriptions; scanDevice/runAllChecks execute
 * them against a target and collect results via getVulnerabilities().
 */

class VulnerabilityScanner {
private:
    struct VulnCheck {
        String name;
        bool (*checkFunction)(NimBLEAddress);
        String description;
    };
    std::vector<VulnCheck> vulnerabilityChecks;

public:
    VulnerabilityScanner();
    void scanDevice(NimBLEAddress target);
    void addCustomCheck(const String &name, bool (*checkFunc)(NimBLEAddress), const String &desc);
    void runAllChecks(NimBLEAddress target);
    std::vector<String> getVulnerabilities();
};

/*
 * Thin service wrappers exposing individual attack capabilities (HID keystroke
 * injection, PIN brute-force, connection flood / advertising spam) to the menu
 * layer.
 */

class HIDAttackServiceClass {
public:
    bool injectKeystrokes(NimBLEAddress target);
    bool forceHIDKeystrokes(NimBLEAddress target, const String &deviceName, int rssi);
};

class PairingAttackServiceClass {
public:
    bool bruteForcePIN(NimBLEAddress target);
};

class DoSAttackServiceClass {
public:
    bool connectionFlood(NimBLEAddress target);
    bool advertisingSpam(NimBLEAddress target);
};

/*
 * Optional heap instrumentation compiled in only under DEBUG_MEMORY.
 * HeapMonitor snapshots/prints free-heap state, and MEM_SNAPSHOT/REPORT/CHECK
 * expand to no-ops in release builds so they add zero footprint to production
 * firmware.
 */

#ifdef DEBUG_MEMORY
class HeapMonitor {
public:
    static void takeSnapshot(const String &label);
    static void printReport();
    static size_t getCurrentFree();
    static size_t getLargestFree();
};
#endif

#ifdef DEBUG_MEMORY
#define MEM_SNAPSHOT(label) HeapMonitor::takeSnapshot(label)
#define MEM_REPORT() HeapMonitor::printReport()
#define MEM_CHECK()                                                                                          \
    {                                                                                                        \
        size_t current = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);                                        \
        static size_t lastCheck = 0;                                                                         \
        if (lastCheck > 0 && current < lastCheck - 512) {                                                    \
            Serial.printf("[MEM] Warning: Lost %d bytes\n", lastCheck - current);                            \
        }                                                                                                    \
        lastCheck = current;                                                                                 \
    }
#else
#define MEM_SNAPSHOT(label)
#define MEM_REPORT()
#define MEM_CHECK()
#endif

/*
 * BLE suite top-level function declarations: stack cleanup, connection-strategy
 * attempts, and the menu/attack entry points the UI dispatches to.
 */

void cleanupBLEStack();

NimBLEClient *attemptConnectionWithStrategies(NimBLEAddress target, String &connectionMethod);
void BleSuiteMenu();
void showAttackMenuWithTarget(NimBLEAddress target);
void executeSelectedAttack(int attackIndex, NimBLEAddress target);
void runWhisperPairAttack(NimBLEAddress target);
void runAdvancedExploit(NimBLEAddress target);
void runAudioStackCrash(NimBLEAddress target);
void runMediaCommandHijack(NimBLEAddress target);
void runHIDInjection(NimBLEAddress target);
void runDuckyScriptAttack(NimBLEAddress target);
void runPINBruteForce(NimBLEAddress target);
void runConnectionFlood(NimBLEAddress target);
void runAdvertisingSpam(NimBLEAddress target);

/*
 * Attack entry points that take a SelectedDevice, carrying device context
 * (name/RSSI/capabilities) in addition to the target address.
 */

void runQuickTest(NimBLEAddress target, SelectedDevice deviceInfo);
void runDeviceProfiling(NimBLEAddress target, SelectedDevice deviceInfo);
void runUniversalAttack(NimBLEAddress target, SelectedDevice deviceInfo);

/*
 * Submenu renderers that take a SelectedDevice, so each attack-category menu
 * (FastPair/HFP/Audio/HID/Memory/DoS/Payload/Testing) can show target-aware
 * options.
 */

void showFastPairSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showHFPSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showAudioSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showHIDSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showMemorySubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showDoSSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showPayloadSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);
void showTestingSubMenu(NimBLEAddress target, SelectedDevice deviceInfo);

/*
 * Additional BLE suite function declarations: write/protocol-fuzz/jam/HID/audio
 * tests, progress/result/confirmation UI helpers, address parsing, target
 * selection from scan, and script loading from SD.
 */

void runWriteAccessTest(NimBLEAddress target);
void runProtocolFuzzer(NimBLEAddress target);
void runJamConnectAttack(NimBLEAddress target);
void runHIDTest(NimBLEAddress target);
void runAudioControlTest(NimBLEAddress target);
void runVulnerabilityScan(NimBLEAddress target);
void runForceHIDInjection(NimBLEAddress target);
void runHIDConnectionExploit(NimBLEAddress target);
void runAdvancedDuckyInjection(NimBLEAddress target);
void runHIDVulnerabilityTest(NimBLEAddress target);
void runMultiTargetAttack();
void executeAudioTest(int testIndex, NimBLEAddress target);
void showAttackProgress(const char *message, uint16_t color = bruceConfig.priColor);
void showAttackResult(bool success, const char *message = nullptr);
bool confirmAttack(const char *targetName);
String selectTargetFromScan(const char *title);
String selectMultipleTargetsFromScan(const char *title, std::vector<NimBLEAddress> &targets);
String getScriptFromUser();
NimBLEAddress parseAddress(const String &addressInfo);
bool requireSimpleConfirmation(const char *message);
int8_t showAdaptiveMessage(
    const char *line1, const char *btn1, const char *btn2, const char *btn3, uint16_t color,
    bool showEscHint = true, bool autoProgress = false
);
void showWarningMessage(const char *message);
void showErrorMessage(const char *message);
void showSuccessMessage(const char *message);
void showDeviceInfoScreen(
    const char *title, const std::vector<String> &lines, uint16_t bgColor, uint16_t textColor
);
bool isBLEInitialized();
void runHFPVulnerabilityTest(NimBLEAddress target);
void runHFPAttackChain(NimBLEAddress target);
void runHFPHIDPivotAttack(NimBLEAddress target);
void runSmartHFPPivot(NimBLEAddress target, const String &deviceName, int rssi);
void runFastPairScan(NimBLEAddress target);
void runFastPairVulnerabilityTest(NimBLEAddress target);
void runFastPairMemoryCorruption(NimBLEAddress target);
void runFastPairStateConfusion(NimBLEAddress target);
void runFastPairCryptoOverflow(NimBLEAddress target);
void runFastPairPopupSpam(NimBLEAddress target, FastPairPopupType type);
void runFastPairAllExploits(NimBLEAddress target);
void runFastPairHIDChain(NimBLEAddress target);
String selectFileFromSD();
bool loadScriptFromSD(const String &filename);

// BLE Sniffer
void BLE_Sniffer();

// Forward declarations for old submenu functions (for compatibility)
void showFastPairSubMenu(NimBLEAddress target);
void showHFPSubMenu(NimBLEAddress target);
void showAudioSubMenu(NimBLEAddress target);
void showHIDSubMenu(NimBLEAddress target);
void showMemorySubMenu(NimBLEAddress target);
void showDoSSubMenu(NimBLEAddress target);
void showPayloadSubMenu(NimBLEAddress target);
void showTestingSubMenu(NimBLEAddress target);
void executeAttackWithTargetScan(int attackIndex);

#endif
#endif
