#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "ble_adv_tools.h"

#include "ble_common.h"
#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline const char *tr(const char *s) { return s; }
static inline String tr(const String &s) { return s; }
} // namespace bruce_i18n
#endif
#if __has_include("core/location_state.h")
#include "core/location_state.h"
#define BLE_ADV_HAS_LOC 1
#endif
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#if !defined(RADIO_MEM_TWO_TIER)
static inline bool radioSafeBleInit(const std::string &name, bool = true) {
    return NimBLEDevice::isInitialized() || NimBLEDevice::init(name);
}
#endif
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include "modules/others/audio.h"
#include <NimBLEDevice.h>
#include <algorithm>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <globals.h>
#include <vector>

namespace {

// Standard Nordic UART Service (NUS) UUIDs for universal BLE File Transfer (Bruce <-> Bruce / Phone / PC)
static const char *NUS_SVC_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *NUS_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // Write from client -> server
static const char *NUS_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // Notify from server -> client

static FS *pickBleFs() {
    if (setupSdCard()) return &SD;
    if (checkLittleFsSize()) return &LittleFS;
    return nullptr;
}

static bool initBleReady(const char *devName = "Nezha-BLE") {
    if (!NimBLEDevice::isInitialized()) {
        if (!radioHasMemForBle() || !radioSafeBleInit(devName)) {
            displayError("Low RAM for BLE", true);
            return false;
        }
    }
    NimBLEDevice::setMTU(247);
    return true;
}

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static String bytesToHex(const uint8_t *buf, size_t len, size_t maxBytes = 24) {
    String out;
    size_t n = min(len, maxBytes);
    for (size_t i = 0; i < n; ++i) {
        char h[4];
        snprintf(h, sizeof(h), "%02X%s", buf[i], (i + 1 < n) ? " " : "");
        out += h;
    }
    if (len > maxBytes) out += "..";
    return out;
}

static String bytesToAscii(const uint8_t *buf, size_t len, size_t maxBytes = 32) {
    String out;
    size_t n = min(len, maxBytes);
    for (size_t i = 0; i < n; ++i) {
        char c = (char)buf[i];
        out += (c >= 32 && c <= 126) ? c : '.';
    }
    return out;
}

static bool parseHexString(const String &hexIn, std::vector<uint8_t> &out) {
    out.clear();
    String clean;
    for (size_t i = 0; i < hexIn.length(); ++i) {
        char c = hexIn[i];
        if (isxdigit((unsigned char)c)) clean += c;
    }
    if (clean.length() < 2 || (clean.length() & 1)) return false;
    for (size_t i = 0; i < clean.length(); i += 2) {
        char pair[3] = {clean[i], clean[i + 1], 0};
        out.push_back((uint8_t)strtoul(pair, nullptr, 16));
    }
    return !out.empty();
}

static void getLocCoords(double &lat, double &lon) {
    lat = 0.0;
    lon = 0.0;
#if defined(BLE_ADV_HAS_LOC)
    bruceLocation.ensureLoaded();
    if (bruceLocation.hasLocation()) {
        lat = bruceLocation.lat();
        lon = bruceLocation.lon();
    }
#endif
}

struct ScannedBleDev {
    NimBLEAddress addr;
    String name;
    int8_t rssi = -100;
    bool connectable = false;
    std::string mfg;
    std::vector<String> svcUuids;
    int8_t txPower = 0;
    bool hasTx = false;
    uint16_t appearance = 0;
    bool hasApp = false;
};

static std::vector<ScannedBleDev> quickBleScan(uint32_t ms = 3000) {
    std::vector<ScannedBleDev> out;
    if (!initBleReady()) return out;

    displayInfo("Scanning BLE devices...");
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) return out;
    scan->setActiveScan(true);
    scan->setInterval(97);
    scan->setWindow(67);
    NimBLEScanResults res = scan->getResults(ms, false);
    int cnt = res.getCount();
    for (int i = 0; i < cnt && out.size() < 40; ++i) {
        const NimBLEAdvertisedDevice *d = res.getDevice(i);
        if (!d) continue;
        ScannedBleDev item;
        item.addr = d->getAddress();
        item.name = String(d->getName().c_str());
        item.rssi = (int8_t)d->getRSSI();
        item.connectable = d->isConnectable();
        if (d->haveManufacturerData()) item.mfg = d->getManufacturerData();
        uint8_t sc = d->getServiceUUIDCount();
        for (uint8_t k = 0; k < sc && k < 6; ++k) {
            item.svcUuids.push_back(String(d->getServiceUUID(k).toString().c_str()));
        }
        uint8_t sd = d->getServiceDataCount();
        for (uint8_t k = 0; k < sd && k < 4; ++k) {
            item.svcUuids.push_back(String(d->getServiceDataUUID(k).toString().c_str()));
        }
        if (d->haveTXPower()) {
            item.hasTx = true;
            item.txPower = d->getTXPower();
        }
        if (d->haveAppearance()) {
            item.hasApp = true;
            item.appearance = d->getAppearance();
        }
        out.push_back(item);
    }
    scan->clearResults();
    std::sort(out.begin(), out.end(), [](const ScannedBleDev &a, const ScannedBleDev &b) {
        return a.rssi > b.rssi;
    });
    return out;
}

// ============================================================================
// BLE FILE TRANSFER SERVER CALLBACKS (Receive & Phone-Pull Send)
// ============================================================================
struct BleRxSession {
    FS *fs = nullptr;
    File file;
    bool receiving = false;
    bool complete = false;
    bool failed = false;
    bool clientConnected = false;
    bool clientRequestedGet = false;
    String fileName;
    size_t expectedSize = 0;
    size_t receivedBytes = 0;
    uint32_t runningCrc = 0;
    String lineBuf;
    uint32_t lastActivityMs = 0;
};

static BleRxSession *s_rxSess = nullptr;
static SemaphoreHandle_t s_rxMutex = nullptr;

static void ensureRxMutex() {
    if (s_rxMutex == nullptr) { s_rxMutex = xSemaphoreCreateMutex(); }
}

// RAII lock so every early return in the NimBLE callbacks unlocks.
struct RxLock {
    bool held = false;
    RxLock() {
        ensureRxMutex();
        if (s_rxMutex != nullptr) held = (xSemaphoreTake(s_rxMutex, portMAX_DELAY) == pdTRUE);
    }
    ~RxLock() {
        if (held) xSemaphoreGive(s_rxMutex);
    }
};

class BleFtServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
        RxLock lock;
        if (s_rxSess) s_rxSess->clientConnected = true;
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
        RxLock lock;
        if (s_rxSess) {
            s_rxSess->clientConnected = false;
            if (s_rxSess->receiving && s_rxSess->file) {
                s_rxSess->file.close();
                s_rxSess->receiving = false;
                if (s_rxSess->expectedSize == 0 && s_rxSess->receivedBytes > 0) s_rxSess->complete = true;
            }
        }
    }
};

class BleFtRxCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pChr, NimBLEConnInfo &) override {
        RxLock lock;
        if (!s_rxSess) return;
        std::string v = pChr->getValue();
        if (v.empty()) return;
        s_rxSess->lastActivityMs = millis();

        // Check for Phone-Pull GET command in Server-Send mode
        if (!s_rxSess->receiving && v.size() >= 3 && strncmp(v.data(), "GET", 3) == 0) {
            s_rxSess->clientRequestedGet = true;
            return;
        }

        // Check for framed header: "BFT1:<filename>:<size>\n"
        if (!s_rxSess->receiving && v.size() > 6 && strncmp(v.data(), "BFT1:", 5) == 0) {
            String hdr(v.c_str());
            int nl = hdr.indexOf('\n');
            String line = (nl >= 0) ? hdr.substring(5, nl) : hdr.substring(5);
            int colon = line.lastIndexOf(':');
            String fname = (colon > 0) ? line.substring(0, colon) : line;
            size_t fsz = (colon > 0) ? (size_t)line.substring(colon + 1).toInt() : 0;
            // Sanitize the remote filename: no separators, no "..", only safe
            // characters, bounded length.
            String safe;
            safe.reserve(64);
            for (size_t i = 0; i < fname.length() && safe.length() < 64; ++i) {
                char c = fname[i];
                bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                          c == '.' || c == '_' || c == '-';
                safe += ok ? c : '_';
            }
            fname = safe;
            if (fname == ".." || fname == "." || fname.isEmpty()) {
                fname = "rx_" + String(millis()) + ".bin";
            }

            if (s_rxSess->fs) {
                if (!s_rxSess->fs->exists("/ble_received")) s_rxSess->fs->mkdir("/ble_received");
                s_rxSess->fileName = "/ble_received/" + fname;
                s_rxSess->file = s_rxSess->fs->open(s_rxSess->fileName, FILE_WRITE);
                if (s_rxSess->file) {
                    s_rxSess->receiving = true;
                    s_rxSess->expectedSize = fsz;
                    s_rxSess->receivedBytes = 0;
                    s_rxSess->runningCrc = 0;
                    // Write any payload bytes following the header newline in the same packet.
                    if (nl >= 0 && (size_t)(nl + 1) < v.size()) {
                        const uint8_t *rem = (const uint8_t *)v.data() + nl + 1;
                        size_t remLen = v.size() - (size_t)(nl + 1);
                        // Never count/write more than the declared size: the
                        // old code made receivedBytes > expectedSize, which
                        // underflowed the next writeLen subtraction.
                        if (fsz > 0 && remLen > fsz) remLen = fsz;
                        s_rxSess->file.write(rem, remLen);
                        s_rxSess->receivedBytes += remLen;
                        s_rxSess->runningCrc = crc32Update(s_rxSess->runningCrc, rem, remLen);
                        if (fsz > 0 && s_rxSess->receivedBytes >= fsz) {
                            s_rxSess->file.close();
                            s_rxSess->receiving = false;
                            s_rxSess->complete = true;
                        }
                    }
                } else {
                    s_rxSess->failed = true;
                }
            }
            return;
        }

        // Check for framed trailer: "BFTEND:<crc32hex>"
        if (s_rxSess->receiving && v.size() >= 7 && strncmp(v.data(), "BFTEND:", 7) == 0) {
            if (s_rxSess->file) s_rxSess->file.close();
            s_rxSess->receiving = false;
            s_rxSess->complete = true;
            return;
        }

        // Raw fallback if phone sends plain bytes without BFT1 header
        if (!s_rxSess->receiving && s_rxSess->fs) {
            if (!s_rxSess->fs->exists("/ble_received")) s_rxSess->fs->mkdir("/ble_received");
            s_rxSess->fileName = "/ble_received/rx_" + String(millis()) + ".bin";
            s_rxSess->file = s_rxSess->fs->open(s_rxSess->fileName, FILE_WRITE);
            if (s_rxSess->file) {
                s_rxSess->receiving = true;
                s_rxSess->expectedSize = 0;
                s_rxSess->receivedBytes = 0;
                s_rxSess->runningCrc = 0;
            }
        }

        if (s_rxSess->receiving && s_rxSess->file) {
            const uint8_t *ptr = (const uint8_t *)v.data();
            size_t writeLen = v.size();
            if (s_rxSess->expectedSize > 0) {
                if (s_rxSess->receivedBytes >= s_rxSess->expectedSize) {
                    writeLen = 0;
                } else if (writeLen > s_rxSess->expectedSize - s_rxSess->receivedBytes) {
                    writeLen = s_rxSess->expectedSize - s_rxSess->receivedBytes;
                }
            }
            if (writeLen > 0) {
                s_rxSess->file.write(ptr, writeLen);
                s_rxSess->receivedBytes += writeLen;
                s_rxSess->runningCrc = crc32Update(s_rxSess->runningCrc, ptr, writeLen);
            }
            if (s_rxSess->expectedSize > 0 && s_rxSess->receivedBytes >= s_rxSess->expectedSize) {
                s_rxSess->file.close();
                s_rxSess->receiving = false;
                s_rxSess->complete = true;
            }
        }
    }
};

struct FilePickItem {
    String path;
    String name;
    size_t sizeBytes;
};

static void collectTransferFiles(FS &fs, const String &dirPath, std::vector<FilePickItem> &out, int depth = 0) {
    if (depth > 2 || out.size() >= 48) return;
    File dir = fs.open(dirPath);
    if (!dir || !dir.isDirectory()) return;
    File e = dir.openNextFile();
    while (e && out.size() < 48) {
        String raw = String(e.name());
        int sl = raw.lastIndexOf('/');
        String base = (sl >= 0) ? raw.substring(sl + 1) : raw;
        String full = (dirPath == "/") ? ("/" + base) : (dirPath + "/" + base);
        if (base.startsWith(".")) {
            e.close();
            e = dir.openNextFile();
            continue;
        }
        if (e.isDirectory()) {
            e.close();
            collectTransferFiles(fs, full, out, depth + 1);
        } else {
            out.push_back({full, base, (size_t)e.size()});
            e.close();
        }
        e = dir.openNextFile();
    }
    dir.close();
}

} // namespace

// ============================================================================
// 1. INTERACTIVE BLE GATT EXPLORER (Read / Write / Notify / Dump)
// ============================================================================
void ble_gatt_explorer() {
    std::vector<ScannedBleDev> devs = quickBleScan(3200);
    if (devs.empty()) {
        displayWarning("No BLE devices found", true);
        return;
    }

    int chosenIdx = -1;
    std::vector<Option> devOpts;
    for (size_t i = 0; i < devs.size() && i < 28; ++i) {
        String lbl = (devs[i].connectable ? "[C] " : "[ ] ") +
                     (devs[i].name.isEmpty() ? String(devs[i].addr.toString().c_str()) : devs[i].name) +
                     " (" + String(devs[i].rssi) + ")";
        devOpts.push_back({lbl, [&, i]() { chosenIdx = (int)i; }});
    }
    devOpts.push_back({"Back", []() {}});
    loopOptions(devOpts, MENU_TYPE_SUBMENU, "GATT Explorer");
    if (chosenIdx < 0) return;

    const ScannedBleDev &target = devs[chosenIdx];
    displayInfo("Connecting to " + (target.name.isEmpty() ? String(target.addr.toString().c_str()) : target.name));

    NimBLEClient *client = NimBLEDevice::createClient();
    if (!client) {
        displayError("Client alloc failed", true);
        return;
    }
    client->setConnectTimeout(6000);
    if (!client->connect(target.addr)) {
        NimBLEDevice::deleteClient(client);
        displayError("BLE connect failed", true);
        return;
    }

    const std::vector<NimBLERemoteService *> &svcs = client->getServices(true);
    if (svcs.empty()) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        displayWarning("No GATT services found", true);
        return;
    }

    while (client->isConnected() && !returnToMenu) {
        int pickSvc = -1;
        bool dumpAll = false;
        bool exitGatt = false;
        std::vector<Option> svcOpts;
        svcOpts.push_back({"Dump All GATT to File", [&]() { dumpAll = true; }});
        for (size_t i = 0; i < svcs.size(); ++i) {
            String u = String(svcs[i]->getUUID().toString().c_str());
            svcOpts.push_back({"SVC " + u, [&, i]() { pickSvc = (int)i; }});
        }
        svcOpts.push_back({"Disconnect", [&]() { exitGatt = true; }});
        loopOptions(svcOpts, MENU_TYPE_SUBMENU, "GATT Services");

        if (exitGatt) break;
        if (dumpAll) {
            String dump = "GATT Dump: " + target.name + " [" + String(target.addr.toString().c_str()) + "]\n---\n";
            for (auto *s : svcs) {
                dump += "SVC " + String(s->getUUID().toString().c_str()) + "\n";
                const auto &chrs = s->getCharacteristics(true);
                for (auto *c : chrs) {
                    String props;
                    if (c->canRead()) props += "R";
                    if (c->canWrite() || c->canWriteNoResponse()) props += "W";
                    if (c->canNotify()) props += "N";
                    if (c->canIndicate()) props += "I";
                    dump += "  CHR " + String(c->getUUID().toString().c_str()) + " [" + props + "]";
                    if (c->canRead()) {
                        NimBLEAttValue val = c->readValue();
                        if (val.size() > 0) {
                            dump += "\n    HEX: " + bytesToHex(val.data(), val.size(), 16);
                            dump += "\n    TXT: " + bytesToAscii(val.data(), val.size(), 24);
                        }
                    }
                    dump += "\n";
                }
            }
            FS *fs = pickBleFs();
            if (fs) {
                String fn = "/gatt_" + String(target.addr.toString().c_str()).substring(9) + ".txt";
                fn.replace(":", "");
                File f = fs->open(fn, FILE_WRITE);
                if (f) {
                    f.print(dump);
                    f.close();
                    dump += "\nSaved -> " + fn + "\n";
                }
            }
            ScrollableTextArea area("GATT Map");
            area.fromString(dump);
            area.show(true);
            continue;
        }

        if (pickSvc >= 0 && pickSvc < (int)svcs.size()) {
            const auto &chrs = svcs[pickSvc]->getCharacteristics(true);
            int pickChr = -1;
            std::vector<Option> chrOpts;
            for (size_t i = 0; i < chrs.size(); ++i) {
                auto *c = chrs[i];
                String p;
                if (c->canRead()) p += "R";
                if (c->canWrite() || c->canWriteNoResponse()) p += "W";
                if (c->canNotify()) p += "N";
                if (c->canIndicate()) p += "I";
                chrOpts.push_back({"[" + p + "] " + String(c->getUUID().toString().c_str()),
                                   [&, i]() { pickChr = (int)i; }});
            }
            chrOpts.push_back({"Back", []() {}});
            loopOptions(chrOpts, MENU_TYPE_SUBMENU, "Characteristics");

            if (pickChr >= 0 && pickChr < (int)chrs.size()) {
                NimBLERemoteCharacteristic *c = chrs[pickChr];
                int act = 0;
                std::vector<Option> actOpts;
                if (c->canRead()) actOpts.push_back({"Read Value", [&]() { act = 1; }});
                if (c->canWrite() || c->canWriteNoResponse()) {
                    actOpts.push_back({"Write ASCII", [&]() { act = 2; }});
                    actOpts.push_back({"Write HEX", [&]() { act = 3; }});
                }
                if (c->canNotify() || c->canIndicate()) {
                    actOpts.push_back({"Subscribe Notify", [&]() { act = 4; }});
                }
                actOpts.push_back({"Back", []() {}});
                loopOptions(actOpts, MENU_TYPE_SUBMENU, "Characteristic");

                if (act == 1) {
                    NimBLEAttValue val = c->readValue();
                    String txt = "UUID: " + String(c->getUUID().toString().c_str()) +
                                 "\nLen: " + String((unsigned)val.size()) + " bytes\n\nHEX:\n" +
                                 bytesToHex(val.data(), val.size(), 48) + "\n\nASCII:\n" +
                                 bytesToAscii(val.data(), val.size(), 64);
                    ScrollableTextArea area("GATT Read");
                    area.fromString(txt);
                    area.show(true);
                } else if (act == 2) {
                    String payload = keyboard("", 64, "ASCII Value");
                    if (!payload.isEmpty() && payload != "\x1B") {
                        bool ok = c->writeValue(
                            (const uint8_t *)payload.c_str(), payload.length(), c->canWrite()
                        );
                        if (ok) displaySuccess("Written OK", true);
                        else displayError("Write failed", true);
                    }
                } else if (act == 3) {
                    String hexStr = keyboard("", 64, "HEX (e.g. 01 A2 FF)");
                    std::vector<uint8_t> bytes;
                    if (parseHexString(hexStr, bytes)) {
                        bool ok = c->writeValue(bytes.data(), bytes.size(), c->canWrite());
                        if (ok) displaySuccess("Written OK", true);
                        else displayError("Write failed", true);
                    } else if (!hexStr.isEmpty() && hexStr != "\x1B") {
                        displayError("Invalid HEX", true);
                    }
                } else if (act == 4) {
                    static String s_notifyLog;
                    static volatile bool s_notifyDirty = false;
                    s_notifyLog = "Listening for notifications (ESC=Stop)...\n";
                    s_notifyDirty = true;
                    c->subscribe(c->canNotify(), [](NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
                        if (s_notifyLog.length() > 1200) s_notifyLog = s_notifyLog.substring(400);
                        s_notifyLog += "[" + String((unsigned)len) + "B] " + bytesToHex(data, len, 12) +
                                       " | " + bytesToAscii(data, len, 16) + "\n";
                        s_notifyDirty = true;
                    });
                    ScrollableTextArea area("BLE Notify");
                    check(EscPress);
                    while (client->isConnected() && !check(EscPress) && !returnToMenu) {
                        if (s_notifyDirty) {
                            s_notifyDirty = false;
                            area.fromString(s_notifyLog);
                            area.scrollToLine(area.getMaxLines());
                            area.draw(true);
                        }
                        if (check(UpPress) || check(PrevPress)) {
                            area.scrollUp();
                            area.draw(true);
                        } else if (check(DownPress) || check(NextPress)) {
                            area.scrollDown();
                            area.draw(true);
                        }
                        vTaskDelay(pdMS_TO_TICKS(30));
                    }
                    c->unsubscribe();
                }
            }
        }
    }

    if (client->isConnected()) client->disconnect();
    NimBLEDevice::deleteClient(client);
}

// ============================================================================
// 2. BLE TRACKER STALKER-GUARD (AirTag / SmartTag / Tile Persistence Guard)
// ============================================================================
void ble_stalker_guard() {
    if (!initBleReady()) return;

    struct TrackedTag {
        String addr;
        String kind;
        int8_t rssi;
        int8_t maxRssi;
        uint16_t hits;
        uint32_t firstMs;
        uint32_t lastMs;
        double firstLat, firstLon;
    };
    std::vector<TrackedTag> tags;
    bool exitGuard = false;
    uint32_t lastScan = 0;

    auto drawHeader = [&]() {
        drawMainBorderWithTitle("Tracker Guard");
        tft.setTextSize(FP);
        tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
        tft.drawCentreString(
            bruce_i18n::tr("SEL=Export CSV  ESC=Back"), tftWidth / 2, tftHeight - BORDER_PAD_X - LH, 1
        );
    };
    drawHeader();

    const int top = BORDER_PAD_Y + FM * LH + 4;
    const int bot = tftHeight - BORDER_PAD_X - LH - 4;
    const int left = BORDER_PAD_X + 4;
    const int w = tftWidth - 2 * BORDER_PAD_X - 8;

    check(SelPress);
    check(EscPress);

    while (!exitGuard && !returnToMenu) {
        wakeUpScreen();
        if (lastScan == 0 || millis() - lastScan >= 3000) {
            lastScan = millis();
            NimBLEScan *scan = NimBLEDevice::getScan();
            if (scan) {
                scan->setActiveScan(true);
                scan->setInterval(97);
                scan->setWindow(67);
                NimBLEScanResults res = scan->getResults(2000, false);
                uint32_t now = millis();
                double lat = 0, lon = 0;
                getLocCoords(lat, lon);

                for (int i = 0; i < res.getCount(); ++i) {
                    const NimBLEAdvertisedDevice *d = res.getDevice(i);
                    if (!d) continue;
                    String kind = "";
                    if (d->haveManufacturerData()) {
                        std::string m = d->getManufacturerData();
                        if (m.size() >= 3 && (uint8_t)m[0] == 0x4C && (uint8_t)m[1] == 0x00) {
                            if ((uint8_t)m[2] == 0x12) kind = "AirTag";
                            else if ((uint8_t)m[2] == 0x02 && m.size() >= 25) kind = "iBeacon";
                        }
                    }
                    if (kind.isEmpty()) {
                        for (uint8_t k = 0; k < d->getServiceUUIDCount() && k < 4; ++k) {
                            String u = String(d->getServiceUUID(k).toString().c_str());
                            u.toLowerCase();
                            if (u.indexOf("fd5a") >= 0) kind = "SmartTag";
                            else if (u.indexOf("feed") >= 0) kind = "Tile";
                            else if (u.indexOf("feaa") >= 0) kind = "Eddystone";
                        }
                        for (uint8_t k = 0; k < d->getServiceDataCount() && k < 4 && kind.isEmpty(); ++k) {
                            String u = String(d->getServiceDataUUID(k).toString().c_str());
                            u.toLowerCase();
                            if (u.indexOf("fd5a") >= 0) kind = "SmartTag";
                            else if (u.indexOf("feed") >= 0) kind = "Tile";
                            else if (u.indexOf("feaa") >= 0) kind = "Eddystone";
                        }
                    }
                    if (kind.isEmpty()) continue;

                    String addr = String(d->getAddress().toString().c_str());
                    int8_t rssi = (int8_t)d->getRSSI();
                    bool found = false;
                    for (auto &t : tags) {
                        if (t.addr == addr) {
                            t.rssi = rssi;
                            if (rssi > t.maxRssi) t.maxRssi = rssi;
                            t.hits++;
                            t.lastMs = now;
                            found = true;
                            break;
                        }
                    }
                    if (!found && tags.size() < 24) {
                        tags.push_back({addr, kind, rssi, rssi, 1, now, now, lat, lon});
                    }
                }
                scan->clearResults();

                // Draw tracker persistence table
                tft.fillRect(left, top, w, bot - top, bruceConfig.bgColor);
                tft.setTextSize(1);
                tft.setTextColor(tags.empty() ? TFT_GREEN : TFT_ORANGE, bruceConfig.bgColor);
                tft.drawString("TRACKERS DETECTED: " + String(tags.size()), left, top, 1);

                int y = top + LH + 4;
                if (tags.empty()) {
                    tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
                    tft.drawCentreString("No AirTag/SmartTag/Tile nearby", tftWidth / 2, y + 18, 1);
                } else {
                    int rowH = LH + 4;
                    int maxRows = max(1, (bot - y) / rowH);
                    for (int i = 0; i < (int)tags.size() && i < maxRows; ++i) {
                        const auto &t = tags[i];
                        uint32_t dwellS = (now - t.firstMs) / 1000u;
                        // Risk score increases with dwell time & strong RSSI persistence
                        int risk = min(100, (int)(dwellS / 3) + (int)t.hits * 8 + (t.rssi > -65 ? 20 : 0));
                        uint16_t col = (risk >= 65) ? TFT_RED : ((risk >= 35) ? TFT_YELLOW : TFT_CYAN);
                        if (risk >= 65 && bruceConfig.soundEnabled) _tone(2400, 40, PLAYBACK_ASYNC);

                        tft.setTextColor(col, bruceConfig.bgColor);
                        tft.drawString(t.kind + " " + t.addr.substring(9), left, y, 1);
                        char rbuf[24];
                        snprintf(rbuf, sizeof(rbuf), "%ddB %lus R%d%%", (int)t.rssi, (unsigned long)dwellS, risk);
                        tft.drawRightString(rbuf, left + w - 2, y, 1);
                        y += rowH;
                    }
                }
            }
        }

        if (check(EscPress)) exitGuard = true;
        else if (check(SelPress)) {
            FS *fs = pickBleFs();
            if (fs) {
                File f = fs->open("/trackers.csv", FILE_WRITE);
                if (f) {
                    f.println("kind,mac,rssi,max_rssi,hits,dwell_s,lat,lon");
                    uint32_t now = millis();
                    for (const auto &t : tags) {
                        f.printf(
                            "%s,%s,%d,%d,%u,%lu,%.6f,%.6f\n",
                            t.kind.c_str(),
                            t.addr.c_str(),
                            (int)t.rssi,
                            (int)t.maxRssi,
                            (unsigned)t.hits,
                            (unsigned long)((now - t.firstMs) / 1000u),
                            t.firstLat,
                            t.firstLon
                        );
                    }
                    f.close();
                    displaySuccess("Saved /trackers.csv", true);
                }
            } else {
                displayError("No storage found", true);
            }
            drawHeader();
            lastScan = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================================
// 3. BLE ADVERTISEMENT PACKET INSPECTOR & DIAGNOSTIC BEACON
// ============================================================================
void ble_adv_inspector() {
    std::vector<ScannedBleDev> devs = quickBleScan(2800);
    if (devs.empty()) {
        displayWarning("No BLE devices found", true);
        return;
    }

    int idx = -1;
    std::vector<Option> opts;
    for (size_t i = 0; i < devs.size() && i < 28; ++i) {
        String lbl = (devs[i].name.isEmpty() ? String(devs[i].addr.toString().c_str()) : devs[i].name) +
                     " (" + String(devs[i].rssi) + ")";
        opts.push_back({lbl, [&, i]() { idx = (int)i; }});
    }
    opts.push_back({"Back", []() {}});
    loopOptions(opts, MENU_TYPE_SUBMENU, "BLE Adv Inspector");
    if (idx < 0) return;

    const ScannedBleDev &d = devs[idx];
    String info = "Name: " + (d.name.isEmpty() ? "<none>" : d.name) + "\n";
    info += "Addr: " + String(d.addr.toString().c_str()) + "\n";
    info += "RSSI: " + String(d.rssi) + " dBm  Conn: " + String(d.connectable ? "YES" : "NO") + "\n";
    if (d.hasTx) info += "TX Power: " + String(d.txPower) + " dBm\n";
    if (d.hasApp) {
        char ab[16];
        snprintf(ab, sizeof(ab), "0x%04X", d.appearance);
        info += "Appearance: " + String(ab) + "\n";
    }
    if (!d.mfg.empty()) {
        uint16_t cid = (d.mfg.size() >= 2) ? ((uint8_t)d.mfg[0] | ((uint16_t)(uint8_t)d.mfg[1] << 8)) : 0;
        char cb[20];
        snprintf(cb, sizeof(cb), "0x%04X (%uB)", cid, (unsigned)d.mfg.size());
        info += "Mfg CID: " + String(cb) + "\n";
        info += "Mfg HEX: " + bytesToHex((const uint8_t *)d.mfg.data(), d.mfg.size(), 24) + "\n";
    }
    if (!d.svcUuids.empty()) {
        info += "Services:\n";
        for (const auto &u : d.svcUuids) info += "  " + u + "\n";
    }
    ScrollableTextArea area("BLE Adv Packet");
    area.fromString(info);
    area.show(true);
}

// ============================================================================
// 4. BLE SKIMMER & ROGUE HARDWARE DETECTOR
// ============================================================================
void ble_skimmer_detector() {
    if (!initBleReady()) return;

    drawMainBorderWithTitle("Skimmer / Rogue Guard");
    tft.setTextSize(FP);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString(bruce_i18n::tr("SEL=Rescan  ESC=Back"), tftWidth / 2, tftHeight - BORDER_PAD_X - LH, 1);

    const int top = BORDER_PAD_Y + FM * LH + 4;
    const int bot = tftHeight - BORDER_PAD_X - LH - 4;
    const int left = BORDER_PAD_X + 4;
    const int w = tftWidth - 2 * BORDER_PAD_X - 8;

    auto runSweep = [&]() {
        tft.fillRect(left, top, w, bot - top, bruceConfig.bgColor);
        tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
        tft.drawCentreString("Scanning for HC-05/06, RNBT, UART...", tftWidth / 2, top + 20, 1);

        NimBLEScan *scan = NimBLEDevice::getScan();
        if (!scan) return;
        scan->setActiveScan(true);
        NimBLEScanResults res = scan->getResults(3000, false);

        struct AlertHit {
            String tag;
            String name;
            String addr;
            int8_t rssi;
        };
        std::vector<AlertHit> hits;

        static const char *kSkimPrefixes[] = {
            "HC-05", "HC-06", "HC-08", "RNBT-", "BT05", "MLT-BT05", "JDY-", "AT-09", "CC41", "HM-10"
        };

        for (int i = 0; i < res.getCount(); ++i) {
            const NimBLEAdvertisedDevice *d = res.getDevice(i);
            if (!d) continue;
            String nm = String(d->getName().c_str());
            String up = nm;
            up.toUpperCase();
            String tag = "";

            for (const char *pfx : kSkimPrefixes) {
                if (up.indexOf(pfx) >= 0) {
                    tag = "SKIMMER?";
                    break;
                }
            }
            if (tag.isEmpty() && (up.indexOf("FLIPPER") >= 0 || up.indexOf("PWN") >= 0)) {
                tag = "RF-TOOL";
            }
            if (tag.isEmpty()) {
                for (uint8_t k = 0; k < d->getServiceUUIDCount() && k < 4; ++k) {
                    String u = String(d->getServiceUUID(k).toString().c_str());
                    u.toLowerCase();
                    if (u.indexOf("ffe0") >= 0) tag = "UART-BLE";
                    else if (u.indexOf("3080") >= 0 || u.indexOf("3081") >= 0 || u.indexOf("3082") >= 0)
                        tag = "FLIPPER";
                }
            }
            if (!tag.isEmpty()) {
                hits.push_back({tag, nm.isEmpty() ? "<unnamed>" : nm, String(d->getAddress().toString().c_str()),
                                (int8_t)d->getRSSI()});
            }
        }
        scan->clearResults();

        tft.fillRect(left, top, w, bot - top, bruceConfig.bgColor);
        tft.setTextSize(1);
        if (hits.empty()) {
            tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
            tft.drawCentreString("CLEAN: No skimmer/rogue BLE found", tftWidth / 2, top + 24, 1);
            return;
        }
        if (bruceConfig.soundEnabled) _tone(2600, 90, PLAYBACK_ASYNC);
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.drawString("SUSPICIOUS BLE FOUND: " + String(hits.size()), left, top, 1);

        int y = top + LH + 5;
        for (const auto &h : hits) {
            if (y + LH > bot) break;
            tft.setTextColor(TFT_RED, bruceConfig.bgColor);
            tft.drawString("[" + h.tag + "] ", left, y, 1);
            tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
            tft.drawString(h.name.substring(0, 12), left + 62, y, 1);
            tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
            tft.drawRightString(String(h.rssi) + "dB", left + w - 2, y, 1);
            y += LH + 4;
        }

        // Log findings with GPS
        FS *fs = pickBleFs();
        if (fs) {
            double lat = 0, lon = 0;
            getLocCoords(lat, lon);
            File f = fs->open("/skimmer_log.csv", FILE_APPEND);
            if (f) {
                for (const auto &h : hits) {
                    f.printf("%s,\"%s\",%s,%d,%.6f,%.6f\n", h.tag.c_str(), h.name.c_str(), h.addr.c_str(),
                             (int)h.rssi, lat, lon);
                }
                f.close();
            }
        }
    };

    runSweep();
    check(SelPress);
    check(EscPress);
    while (!returnToMenu) {
        if (check(EscPress)) break;
        if (check(SelPress)) runSweep();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================================
// 5. BLE FILE TRANSFER (Send & Receive Pictures, Docs, or Any File via BLE!)
// ============================================================================
static void bleFileReceiveMode() {
    FS *fs = pickBleFs();
    if (!fs) {
        displayError("No storage found", true);
        return;
    }
    if (!initBleReady("Nezha-FileShare")) return;

    BleRxSession sess;
    sess.fs = fs;
    ensureRxMutex();
    if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
    s_rxSess = &sess;
    if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);

    NimBLEServer *srv = NimBLEDevice::createServer();
    if (!srv) {
        s_rxSess = nullptr;
        displayError("BLE Server failed", true);
        return;
    }
    static BleFtServerCb srvCb;
    static BleFtRxCb rxCb;
    srv->setCallbacks(&srvCb);

    NimBLEService *svc = srv->getServiceByUUID(NUS_SVC_UUID);
    if (!svc) svc = srv->createService(NUS_SVC_UUID);
    if (!svc) {
        displayError("BLE service creation failed", true);
        if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
        s_rxSess = nullptr;
        if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
        return;
    }
    NimBLECharacteristic *rxChr = svc->getCharacteristic(NUS_RX_UUID);
    if (!rxChr) {
        rxChr = svc->createCharacteristic(
            NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
    }
    if (!rxChr) {
        displayError("BLE RX characteristic failed", true);
        if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
        s_rxSess = nullptr;
        if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
        return;
    }
    rxChr->setCallbacks(&rxCb);
    NimBLECharacteristic *txChr = svc->getCharacteristic(NUS_TX_UUID);
    if (!txChr) txChr = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    if (!txChr) {
        displayError("BLE TX characteristic failed", true);
        rxChr->setCallbacks(nullptr);
        if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
        s_rxSess = nullptr;
        if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
        return;
    }
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();
    adv->addServiceUUID(NUS_SVC_UUID);
    adv->setName("Nezha-FileShare");
    adv->start();

    drawMainBorderWithTitle("BLE Receive File");
    int x = BORDER_PAD_X + 6;
    int top = BORDER_PAD_Y + FM * LH + 6;
    int w = tftWidth - 2 * BORDER_PAD_X - 12;

    uint32_t lastDraw = 0;
    check(EscPress);

    while (!returnToMenu && !check(EscPress)) {
        wakeUpScreen();
        if (millis() - lastDraw >= 250) {
            lastDraw = millis();
            tft.fillRect(x, top, w, tftHeight - top - BORDER_PAD_X - LH - 4, bruceConfig.bgColor);
            tft.setTextSize(1);
            tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
            tft.drawString("Device: Nezha-FileShare (NUS)", x, top, 1);

            int y = top + LH + 5;
            if (sess.complete) {
                tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
                tft.drawString("RECEIVED OK!", x, y, 1);
                y += LH + 4;
                tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
                tft.drawString(sess.fileName, x, y, 1);
                y += LH + 4;
                char crcBuf[40];
                snprintf(crcBuf, sizeof(crcBuf), "%u B  CRC:%08lX", (unsigned)sess.receivedBytes,
                         (unsigned long)sess.runningCrc);
                tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
                tft.drawString(crcBuf, x, y, 1);
            } else if (sess.failed) {
                tft.setTextColor(TFT_RED, bruceConfig.bgColor);
                tft.drawString("Write error! Check SD/FS", x, y, 1);
            } else if (sess.receiving) {
                tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
                tft.drawString("Receiving: " + sess.fileName, x, y, 1);
                y += LH + 5;
                int pct = (sess.expectedSize > 0)
                              ? (int)min<size_t>(100, (sess.receivedBytes * 100) / sess.expectedSize)
                              : 50;
                tft.drawRect(x, y, w - 4, 10, bruceConfig.secColor);
                int fill = ((w - 6) * pct) / 100;
                if (fill > 0) tft.fillRect(x + 1, y + 1, fill, 8, TFT_GREEN);
                y += 14;
                tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
                tft.drawString(
                    String((unsigned)sess.receivedBytes) + " / " + String((unsigned)sess.expectedSize) + " B",
                    x,
                    y,
                    1
                );
            } else {
                tft.setTextColor(sess.clientConnected ? TFT_GREEN : TFT_LIGHTGREY, bruceConfig.bgColor);
                tft.drawString(
                    sess.clientConnected ? "Peer connected - waiting file..." : "Waiting for sender (Nezha/Phone)...",
                    x,
                    y,
                    1
                );
                y += LH + 6;
                tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
                tft.drawString("Save dir: /ble_received/", x, y, 1);
            }
            tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
            tft.drawCentreString(bruce_i18n::tr("ESC=Back"), tftWidth / 2, tftHeight - BORDER_PAD_X - LH, 1);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    adv->stop();
    rxChr->setCallbacks(nullptr);
    srv->setCallbacks(nullptr);
    // Wait for any in-flight NimBLE callback, close the file under the lock,
    // then detach the stack session before it goes out of scope.
    if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
    if (sess.file) sess.file.close();
    s_rxSess = nullptr;
    if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
}

static void bleFileSendDirect(FS &fs, const FilePickItem &item) {
    if (!initBleReady("Nezha-Sender")) return;

    displayInfo("Searching for Nezha-FileShare...");
    NimBLEScan *scan = NimBLEDevice::getScan();
    if (!scan) return;
    scan->setActiveScan(true);
    NimBLEScanResults res = scan->getResults(3500, false);

    std::vector<ScannedBleDev> receivers;
    for (int i = 0; i < res.getCount(); ++i) {
        const NimBLEAdvertisedDevice *d = res.getDevice(i);
        if (!d || !d->isConnectable()) continue;
        String nm = String(d->getName().c_str());
        bool isNus = d->isAdvertisingService(NimBLEUUID(NUS_SVC_UUID)) || nm == "Nezha-FileShare";
        if (isNus) {
            ScannedBleDev s;
            s.addr = d->getAddress();
            s.name = nm.isEmpty() ? String(d->getAddress().toString().c_str()) : nm;
            s.rssi = (int8_t)d->getRSSI();
            receivers.push_back(s);
        }
    }
    scan->clearResults();

    if (receivers.empty()) {
        displayWarning("No BLE receiver found! Start Receive on target", true);
        return;
    }

    int pick = 0;
    if (receivers.size() > 1) {
        pick = -1;
        std::vector<Option> ropts;
        for (size_t i = 0; i < receivers.size(); ++i) {
            ropts.push_back({receivers[i].name + " (" + String(receivers[i].rssi) + ")",
                             [&, i]() { pick = (int)i; }});
        }
        ropts.push_back({"Cancel", []() {}});
        loopOptions(ropts, MENU_TYPE_SUBMENU, "Select Receiver");
        if (pick < 0) return;
    }

    displayInfo("Connecting to " + receivers[pick].name + "...");
    NimBLEClient *client = NimBLEDevice::createClient();
    if (!client || !client->connect(receivers[pick].addr)) {
        if (client) NimBLEDevice::deleteClient(client);
        displayError("BLE connect failed", true);
        return;
    }

    NimBLERemoteService *svc = client->getService(NUS_SVC_UUID);
    NimBLERemoteCharacteristic *rx = svc ? svc->getCharacteristic(NUS_RX_UUID) : nullptr;
    if (!rx) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        displayError("Receiver missing NUS", true);
        return;
    }

    File f = fs.open(item.path, FILE_READ);
    if (!f) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        displayError("Cannot open file", true);
        return;
    }

    drawMainBorderWithTitle("BLE Sending File");
    int x = BORDER_PAD_X + 6;
    int top = BORDER_PAD_Y + FM * LH + 8;
    int w = tftWidth - 2 * BORDER_PAD_X - 12;

    // Send header: BFT1:<filename>:<size>\n
    String hdr = "BFT1:" + item.name + ":" + String((unsigned)item.sizeBytes) + "\n";
    rx->writeValue((const uint8_t *)hdr.c_str(), hdr.length(), true);
    vTaskDelay(pdMS_TO_TICKS(40));

    uint16_t mtu = client->getMTU();
    size_t chunkSz = (mtu > 23) ? min<size_t>(mtu - 3, 180) : 20;
    uint8_t buf[184];
    size_t sent = 0;
    uint32_t crc = 0;
    uint32_t t0 = millis();
    uint32_t lastUi = 0;
    bool cancelled = false;

    check(EscPress);
    while (f.available() && client->isConnected()) {
        if (check(EscPress)) {
            cancelled = true;
            break;
        }
        size_t n = f.read(buf, chunkSz);
        if (n == 0) break;
        bool ok = rx->writeValue(buf, n, false);
        if (!ok) ok = rx->writeValue(buf, n, true);
        if (!ok) {
            cancelled = true;
            break;
        }
        sent += n;
        crc = crc32Update(crc, buf, n);
        vTaskDelay(pdMS_TO_TICKS(12)); // Pacing prevents BLE controller TX queue overflow

        if (millis() - lastUi >= 200 || sent >= item.sizeBytes) {
            lastUi = millis();
            int pct = (item.sizeBytes > 0) ? (int)((sent * 100) / item.sizeBytes) : 100;
            float secs = max(0.1f, (float)(millis() - t0) / 1000.0f);
            float kbps = ((float)sent / 1024.0f) / secs;

            tft.fillRect(x, top, w, 64, bruceConfig.bgColor);
            tft.setTextSize(1);
            tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
            tft.drawString(item.name, x, top, 1);
            tft.drawRect(x, top + LH + 6, w - 4, 10, bruceConfig.secColor);
            int fill = ((w - 6) * pct) / 100;
            if (fill > 0) tft.fillRect(x + 1, top + LH + 7, fill, 8, TFT_GREEN);

            char st[44];
            snprintf(st, sizeof(st), "%u/%u B (%d%%)  %.1f KB/s", (unsigned)sent,
                     (unsigned)item.sizeBytes, pct, kbps);
            tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
            tft.drawString(st, x, top + LH + 22, 1);
        }
    }
    f.close();

    if (!cancelled && client->isConnected()) {
        char endBuf[24];
        snprintf(endBuf, sizeof(endBuf), "BFTEND:%08lX\n", (unsigned long)crc);
        rx->writeValue((const uint8_t *)endBuf, strlen(endBuf), true);
        vTaskDelay(pdMS_TO_TICKS(100));
        displaySuccess("Sent " + item.name + " OK!", true);
    } else {
        displayWarning("Transfer cancelled/dropped", true);
    }

    if (client->isConnected()) client->disconnect();
    NimBLEDevice::deleteClient(client);
}

static void bleFileShareToPhone(FS &fs, const FilePickItem &item) {
    if (!initBleReady("Nezha-FileShare")) return;

    BleRxSession sess;
    sess.fs = &fs;
    ensureRxMutex();
    if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
    s_rxSess = &sess;
    if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);

    NimBLEServer *srv = NimBLEDevice::createServer();
    if (!srv) {
        s_rxSess = nullptr;
        displayError("BLE Server failed", true);
        return;
    }
    static BleFtServerCb srvCb;
    static BleFtRxCb rxCb;
    srv->setCallbacks(&srvCb);

    NimBLEService *svc = srv->getServiceByUUID(NUS_SVC_UUID);
    if (!svc) svc = srv->createService(NUS_SVC_UUID);
    if (!svc) {
        displayError("BLE service creation failed", true);
        if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
        s_rxSess = nullptr;
        if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
        return;
    }
    NimBLECharacteristic *rxChr = svc->getCharacteristic(NUS_RX_UUID);
    if (!rxChr) {
        rxChr = svc->createCharacteristic(
            NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
    }
    if (!rxChr) {
        displayError("BLE RX characteristic failed", true);
        if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
        s_rxSess = nullptr;
        if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
        return;
    }
    rxChr->setCallbacks(&rxCb);
    NimBLECharacteristic *txChr = svc->getCharacteristic(NUS_TX_UUID);
    if (!txChr) txChr = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    if (!txChr) {
        displayError("BLE TX characteristic failed", true);
        rxChr->setCallbacks(nullptr);
        if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
        s_rxSess = nullptr;
        if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
        return;
    }
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();
    adv->addServiceUUID(NUS_SVC_UUID);
    adv->setName("Nezha-FileShare");
    adv->start();

    drawMainBorderWithTitle("BLE Share to Phone");
    int x = BORDER_PAD_X + 6;
    int top = BORDER_PAD_Y + FM * LH + 6;
    int w = tftWidth - 2 * BORDER_PAD_X - 12;

    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, bruceConfig.bgColor);
    tft.drawString("File: " + item.name + " (" + String((unsigned)item.sizeBytes) + "B)", x, top, 1);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.drawString("1. Connect phone to Nezha-FileShare", x, top + LH + 6, 1);
    tft.drawString("2. Subscribe NUS TX (6E400003)", x, top + 2 * LH + 10, 1);
    tft.drawString("3. Press SEL or send 'GET' on RX", x, top + 3 * LH + 14, 1);
    tft.setTextColor(TFT_DARKGREY, bruceConfig.bgColor);
    tft.drawCentreString("SEL=Push Now  ESC=Back", tftWidth / 2, tftHeight - BORDER_PAD_X - LH, 1);

    check(SelPress);
    check(EscPress);
    while (!returnToMenu && !check(EscPress)) {
        wakeUpScreen();
        if (sess.clientRequestedGet || check(SelPress)) {
            sess.clientRequestedGet = false;
            File f = fs.open(item.path, FILE_READ);
            if (!f) {
                displayError("Cannot open file", true);
                break;
            }
            String hdr = "BFT1:" + item.name + ":" + String((unsigned)item.sizeBytes) + "\n";
            txChr->setValue((const uint8_t *)hdr.c_str(), hdr.length());
            txChr->notify();
            vTaskDelay(pdMS_TO_TICKS(40));

            uint8_t buf[160];
            size_t sent = 0;
            uint32_t crc = 0;
            while (f.available() && !check(EscPress)) {
                size_t n = f.read(buf, sizeof(buf));
                if (n == 0) break;
                txChr->setValue(buf, n);
                txChr->notify();
                sent += n;
                crc = crc32Update(crc, buf, n);
                vTaskDelay(pdMS_TO_TICKS(18));
            }
            f.close();
            char endBuf[24];
            snprintf(endBuf, sizeof(endBuf), "BFTEND:%08lX\n", (unsigned long)crc);
            txChr->setValue((const uint8_t *)endBuf, strlen(endBuf));
            txChr->notify();
            displaySuccess("Pushed " + String((unsigned)sent) + " bytes!", true);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    adv->stop();
    rxChr->setCallbacks(nullptr);
    srv->setCallbacks(nullptr);
    if (s_rxMutex != nullptr) xSemaphoreTake(s_rxMutex, portMAX_DELAY);
    if (sess.file) sess.file.close();
    s_rxSess = nullptr;
    if (s_rxMutex != nullptr) xSemaphoreGive(s_rxMutex);
}

void ble_file_transfer() {
    int mode = 0;
    std::vector<Option> m = {
        {"Send File (Nezha->Nezha)", [&]() { mode = 1; }},
        {"Share File to Phone (NUS)", [&]() { mode = 2; }},
        {"Receive File via BLE",     [&]() { mode = 3; }},
        {"Back",                     []() {}           },
    };
    loopOptions(m, MENU_TYPE_SUBMENU, "BLE File Transfer");
    if (mode == 0) return;

    if (mode == 3) {
        bleFileReceiveMode();
        return;
    }

    FS *fs = pickBleFs();
    if (!fs) {
        displayError("No storage found", true);
        return;
    }
    displayInfo("Listing files...");
    std::vector<FilePickItem> files;
    collectTransferFiles(*fs, "/", files, 0);
    if (files.empty()) {
        displayWarning("No files found on SD/FS", true);
        return;
    }
    std::sort(files.begin(), files.end(), [](const FilePickItem &a, const FilePickItem &b) {
        return a.name.compareTo(b.name) < 0;
    });

    int pick = -1;
    std::vector<Option> fopts;
    for (size_t i = 0; i < files.size(); ++i) {
        String lbl = files[i].name + " (" + String((unsigned)((files[i].sizeBytes + 1023) / 1024)) + "K)";
        fopts.push_back({lbl, [&, i]() { pick = (int)i; }});
    }
    fopts.push_back({"Cancel", []() {}});
    loopOptions(fopts, MENU_TYPE_SUBMENU, "Select File to Send");
    if (pick < 0) return;

    if (mode == 1) bleFileSendDirect(*fs, files[pick]);
    else bleFileShareToPhone(*fs, files[pick]);
}

void ble_adv_tools_menu() {
    std::vector<Option> localOpts = {
        {"BLE File Transfer",   ble_file_transfer   },
        {"BLE GATT Explorer",   ble_gatt_explorer   },
        {"Tracker Guard",       ble_stalker_guard   },
        {"BLE Adv Inspector",   ble_adv_inspector   },
        {"Skimmer/Rogue Guard", ble_skimmer_detector},
        {"Back",                []() {}             },
    };
    loopOptions(localOpts, MENU_TYPE_SUBMENU, "BLE Adv Tools");
}

#endif // !LITE_VERSION && !BRUCE_COMPACT_CJK_FONT
