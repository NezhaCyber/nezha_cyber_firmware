#if !defined(LITE_VERSION)
#include "wire_tools.h"

#include "core/bus_HAL.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/utils.h"
#include <SPI.h>
#include <globals.h>

// Session state for the console UI (the serial commands pass everything explicitly)
static SPIClass *spiBus = nullptr;
static int8_t spiCsPin = -1;
static uint32_t spiFreq = 1000000;

/*********************************************************************
**  Helpers
**********************************************************************/
static String bytesToHex(const uint8_t *data, size_t len) {
    String out;
    for (size_t i = 0; i < len; i++) {
        if (i) out += " ";
        if (data[i] < 0x10) out += "0";
        out += String(data[i], HEX);
    }
    return out;
}

static int parseHexBytePairs(String hex, uint8_t *out, size_t maxLen) {
    hex.trim();
    hex.replace("0x", "");
    hex.replace("0X", "");
    hex.replace(" ", "");
    if (hex.isEmpty() || hex.length() % 2 != 0) return -1;
    size_t len = hex.length() / 2;
    if (len > maxLen) return -1;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)strtol(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
    return (int)len;
}

static bool spi_resolve(int8_t &sck, int8_t &miso, int8_t &mosi, int8_t &cs) {
    BruceConfigPins::SPIPins pins = bruceConfigPins.outer_bus;
    if (sck < 0) sck = (int8_t)pins.sck;
    if (miso < 0) miso = (int8_t)pins.miso;
    if (mosi < 0) mosi = (int8_t)pins.mosi;
    if (cs < 0) cs = (int8_t)pins.cs;
    return sck >= 0 && miso >= 0 && mosi >= 0 && cs >= 0;
}

/*********************************************************************
**  Serial-command primitives (wire spi ...), see wire_tools.h
**********************************************************************/
bool wire_spi_begin(int8_t sck, int8_t miso, int8_t mosi, int8_t cs, uint32_t freqHz) {
    if (!spi_resolve(sck, miso, mosi, cs)) return false;
    if (freqHz < 1000 || freqHz > 40000000) return false;

    SPIClass *bus = acquireSPIBus((gpio_num_t)sck, (gpio_num_t)miso, (gpio_num_t)mosi);
    if (bus == nullptr) return false;

    spiBus = bus;
    spiCsPin = cs;
    spiFreq = freqHz;
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    return true;
}

void wire_spi_end() {
    // The auxiliary SPI bus is shared (HAL tracks it); never end() it, just drop our handle
    spiBus = nullptr;
    spiCsPin = -1;
}

String wire_spi_transfer(const String &hexBytes) {
    if (spiBus == nullptr) return "ERR: bus not started";
    uint8_t tx[256];
    uint8_t rx[256];
    int len = parseHexBytePairs(hexBytes, tx, sizeof(tx));
    if (len <= 0) return "ERR: need hex byte pairs";

    spiBus->beginTransaction(SPISettings(spiFreq, MSBFIRST, SPI_MODE0));
    digitalWrite(spiCsPin, LOW);
    for (int i = 0; i < len; i++) rx[i] = spiBus->transfer(tx[i]);
    digitalWrite(spiCsPin, HIGH);
    spiBus->endTransaction();
    return bytesToHex(rx, len);
}

String wire_spi_flash_detect() {
    if (spiBus == nullptr) return "ERR: bus not started";
    uint8_t tx[4] = {0x9F, 0x00, 0x00, 0x00}; // JEDEC READ ID
    uint8_t rx[4];

    spiBus->beginTransaction(SPISettings(spiFreq, MSBFIRST, SPI_MODE0));
    digitalWrite(spiCsPin, LOW);
    for (int i = 0; i < 4; i++) rx[i] = spiBus->transfer(tx[i]);
    digitalWrite(spiCsPin, HIGH);
    spiBus->endTransaction();

    uint32_t jedec = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    if (jedec == 0 || jedec == 0xFFFFFF || (jedec & 0xFF) == 0) return "ERR: no flash detected";

    String manufacturer;
    switch (rx[1]) {
        case 0xEF: manufacturer = "Winbond";       break;
        case 0xC8: manufacturer = "GigaDevice";    break;
        case 0xC2: manufacturer = "Macronix";      break;
        case 0x20: manufacturer = "Micron";        break;
        case 0x01: manufacturer = "Spansion/Cyp."; break;
        case 0xBF: manufacturer = "SST";           break;
        case 0x1C: manufacturer = "EON";           break;
        case 0x85: manufacturer = "Puya";          break;
        case 0x9D: manufacturer = "ISSI";          break;
        case 0x5E: manufacturer = "Zbit";          break;
        case 0xA1: manufacturer = "Fudan";         break;
        default:   manufacturer = "Unknown";       break;
    }

    uint32_t capacityKb = (1UL << rx[3]) / 1024; // third JEDEC byte is log2(size in bytes)
    return "OK " + manufacturer + " ID:0x" + String(jedec, HEX) + " size:" + String(capacityKb) + "KB";
}

static uint32_t spi_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) { crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1))); }
    }
    return ~crc;
}

String wire_spi_dump_flash(uint32_t sizeBytes, String &outPath) {
    if (spiBus == nullptr) return "ERR: bus not started";
    if (sizeBytes == 0 || sizeBytes > (16UL * 1024UL * 1024UL)) return "ERR: invalid dump size";

    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) return "ERR: no storage mounted";

    if (!fs->exists("/NezhaDumps")) fs->mkdir("/NezhaDumps");
    char pathBuf[48] = {};
    for (uint16_t idx = 0; idx < 1000; ++idx) {
        snprintf(pathBuf, sizeof(pathBuf), "/NezhaDumps/spi_flash_%03u.bin", (unsigned)idx);
        if (!fs->exists(pathBuf)) break;
    }
    outPath = pathBuf;
    File outFile = fs->open(pathBuf, FILE_WRITE);
    if (!outFile) return "ERR: failed creating dump file";

    constexpr size_t CHUNK_SZ = 4096;
    uint8_t *buf = (uint8_t *)malloc(CHUNK_SZ);
    if (!buf) {
        outFile.close();
        return "ERR: low RAM";
    }

    uint32_t crc = 0;
    uint32_t offset = 0;
    bool aborted = false;
    uint32_t lastUiMs = 0;

    while (offset < sizeBytes) {
        if (check(EscPress)) {
            aborted = true;
            break;
        }
        size_t toRead = (sizeBytes - offset > CHUNK_SZ) ? CHUNK_SZ : (size_t)(sizeBytes - offset);

        spiBus->beginTransaction(SPISettings(spiFreq, MSBFIRST, SPI_MODE0));
        digitalWrite(spiCsPin, LOW);
        spiBus->transfer(0x03); // Standard READ (24-bit address)
        spiBus->transfer((uint8_t)((offset >> 16) & 0xFF));
        spiBus->transfer((uint8_t)((offset >> 8) & 0xFF));
        spiBus->transfer((uint8_t)(offset & 0xFF));
        for (size_t i = 0; i < toRead; ++i) buf[i] = spiBus->transfer(0x00);
        digitalWrite(spiCsPin, HIGH);
        spiBus->endTransaction();

        if (outFile.write(buf, toRead) != toRead) {
            free(buf);
            outFile.close();
            return "ERR: SD write failed";
        }
        crc = spi_crc32_update(crc, buf, toRead);
        offset += toRead;

        uint32_t now = millis();
        if (now - lastUiMs >= 150) {
            lastUiMs = now;
            progressHandler((int)(offset / 1024), (size_t)(sizeBytes / 1024), "Dumping SPI Flash");
        }
        vTaskDelay(1);
    }

    free(buf);
    outFile.flush();
    outFile.close();

    if (aborted) return "ERR: aborted at " + String(offset / 1024) + "KB";

    char summary[64];
    snprintf(
        summary,
        sizeof(summary),
        "OK %luKB CRC32:%08lX",
        (unsigned long)(offset / 1024),
        (unsigned long)crc
    );
    return String(summary);
}

static void spi_dump_flash_menu() {
    if (!wire_spi_begin(-1, -1, -1, -1, spiFreq)) {
        displayError("Set SPI pins first", true);
        return;
    }

    // Attempt JEDEC auto-size first
    uint32_t autoBytes = 0;
    uint8_t rx[4] = {};
    spiBus->beginTransaction(SPISettings(spiFreq, MSBFIRST, SPI_MODE0));
    digitalWrite(spiCsPin, LOW);
    rx[0] = spiBus->transfer(0x9F);
    rx[1] = spiBus->transfer(0x00);
    rx[2] = spiBus->transfer(0x00);
    rx[3] = spiBus->transfer(0x00);
    digitalWrite(spiCsPin, HIGH);
    spiBus->endTransaction();
    if (rx[3] >= 16 && rx[3] <= 24) autoBytes = (1UL << rx[3]);

    uint32_t chosenBytes = 0;
    std::vector<Option> sizeOpts;
    if (autoBytes > 0) {
        sizeOpts.push_back(
            {"Auto JEDEC (" + String(autoBytes / 1024) + " KB)", [&]() { chosenBytes = autoBytes; }}
        );
    }
    sizeOpts.push_back({"64 KB", [&]() { chosenBytes = 64UL * 1024; }});
    sizeOpts.push_back({"512 KB", [&]() { chosenBytes = 512UL * 1024; }});
    sizeOpts.push_back({"1 MB", [&]() { chosenBytes = 1024UL * 1024; }});
    sizeOpts.push_back({"2 MB", [&]() { chosenBytes = 2048UL * 1024; }});
    sizeOpts.push_back({"4 MB", [&]() { chosenBytes = 4096UL * 1024; }});
    sizeOpts.push_back({"8 MB", [&]() { chosenBytes = 8192UL * 1024; }});
    sizeOpts.push_back({"Cancel", []() {}});

    loopOptions(sizeOpts, MENU_TYPE_SUBMENU, "SPI Flash Size");
    if (chosenBytes == 0) return;

    drawMainBorderWithTitle("SPI Flash Dump");
    String path;
    String res = wire_spi_dump_flash(chosenBytes, path);
    if (res.startsWith("OK")) {
        displaySuccess(res + "\n" + path, true);
    } else {
        displayError(res, true);
    }
}

/*********************************************************************
**  Function: spi_pick_clock
**********************************************************************/
static void spi_pick_clock() {
    static const uint32_t clocks[] = {400000, 1000000, 4000000, 8000000, 16000000, 26000000};
    constexpr size_t N = sizeof(clocks) / sizeof(clocks[0]);
    int selected = 0;
    options.clear();
    for (size_t i = 0; i < N; i++) {
        if (clocks[i] == spiFreq) selected = i;
        options.push_back({String(clocks[i] / 1000000.0, 1) + " MHz",
                           [i]() { spiFreq = clocks[i]; }, clocks[i] == spiFreq});
    }
    loopOptions(options, MENU_TYPE_SUBMENU, "SPI Clock", selected);
}

/*********************************************************************
**  Function: spi_console
**  CS-framed HEX exchange loop: OK opens the HEX keyboard, response is
**  appended to a scrolling log. ESC leaves.
**********************************************************************/
static void spi_console() {
    drawMainBorderWithTitle("SPI Console");
    BruceConfigPins::SPIPins pins = bruceConfigPins.outer_bus;
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString(
        String(spiFreq / 1000000.0, 1) + "MHz  CS:" + int(spiCsPin) + "  SCK:" + int(pins.sck),
        BORDER_PAD_X, tft.getCursorY()
    );

    const int16_t areaY = tft.getCursorY() + LH + 2;
    ScrollableTextArea area(FP, BORDER_PAD_X, areaY, tftWidth - 2 * BORDER_PAD_X,
                            tftHeight - BORDER_PAD_X - areaY, false, false);
    area.addLine("OK = send HEX bytes");
    area.draw(true);

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();
        if (check(SelPress)) {
            String input = hex_keyboard("", 64, "Send HEX (cs framed):");
            if (input != "\x1B" && !input.isEmpty()) {
                area.addLine("TX " + input);
                String response = wire_spi_transfer(input);
                area.addLine(response.startsWith("ERR") ? response : "RX " + response);
                area.scrollToLine(area.getMaxLines());
                area.draw();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    check(SelPress);
}

/*********************************************************************
**  Function: spi_bus_setup
**********************************************************************/
void spi_bus_setup() {
    returnToMenu = false;
    while (!returnToMenu) {
        BruceConfigPins::SPIPins pins = bruceConfigPins.outer_bus;
        options = {
            {String("Pins: ") + int(pins.sck) + "/" + int(pins.miso) + "/" + int(pins.mosi) + "/CS" +
                 int(pins.cs),
             []() { setSPIPinsMenu(bruceConfigPins.outer_bus); }              },
            {String("Clock: ") + spiFreq / 1000000.0 + " MHz", spi_pick_clock   },
            {String("Detect Flash (JEDEC)"), []() {
                 if (!wire_spi_begin(-1, -1, -1, -1, spiFreq)) {
                     displayError("Set SPI pins first", true);
                     return;
                 }
                 String result = wire_spi_flash_detect();
                 if (result.startsWith("OK")) displayInfo(result, true);
                 else displayError(result.substring(5), true);
             }},
            {String("Dump Flash to SD"),     spi_dump_flash_menu},
            {String("Console"),              []() {
                 if (!wire_spi_begin(-1, -1, -1, -1, spiFreq)) {
                     displayError("Set SPI pins first", true);
                     return;
                 }
                 spi_console();
             }},
        };
        addOptionToMainMenu();
        loopOptions(options, MENU_TYPE_SUBMENU, "SPI Bus");
    }
}
#endif
