#include "massStorage.h"
#if defined(SOC_USB_OTG_SUPPORTED)
#include "core/display.h"
#include "core/i18n.h"
#include <USB.h>
bool MassStorage::shouldStop = false;
int32_t MassStorage::status = -1;

MassStorage::MassStorage() { setup(); }

MassStorage::~MassStorage() {
    msc.end();
    USB.~ESPUSB();

    // Hack to make USB back to flash mode
    USB.enableDFU();
}

void MassStorage::setup() {
    displayMessage("Mounting...");

    setShouldStop(false);

    if (!setupSdCard()) {
        displayError("SD card not found.");
        delay(1000);
        return;
    }

    beginUsb();

    delay(500);
    return loop();
}

void MassStorage::loop() {
    int32_t prev_status = -1;
    while (!check(EscPress) && !shouldStop) {
        if (prev_status != status) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            switch (status) {
                case ARDUINO_USB_STARTED_EVENT: drawUSBStickIcon(true); break;
                case ARDUINO_USB_STOPPED_EVENT: drawUSBStickIcon(false); break;
                case ARDUINO_USB_SUSPEND_EVENT: MassStorage::displayMessage("USB suspend"); break;
                case ARDUINO_USB_RESUME_EVENT: MassStorage::displayMessage("USB resume"); break;
                default: break;
            }
            prev_status = status;
        } else vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void MassStorage::beginUsb() {
    setupUsbCallback();
    setupUsbEvent();
    drawUSBStickIcon(false);
    USB.begin();
}

void MassStorage::setupUsbCallback() {
    uint32_t secSize = SD.sectorSize();
    uint32_t numSectors = SD.numSectors();

    msc.vendorID("ESP32");
    msc.productID("NEZHA");
    msc.productRevision("1.0");

    msc.onRead(usbReadCallback);
    msc.onWrite(usbWriteCallback);
    msc.onStartStop(usbStartStopCallback);

    msc.mediaPresent(true);
    msc.begin(numSectors, secSize);
}

void MassStorage::setupUsbEvent() {
    USB.onEvent([](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
        if (event_base == ARDUINO_USB_EVENTS) { status = event_id; }
    });
}

void MassStorage::displayMessage(String message) {
    drawMainBorderWithTitle("Mass Storage");
    if (!message.isEmpty()) {
        bool compact = (tftHeight < 160);
        int cardX = compact ? 10 : 18;
        int cardY = BORDER_PAD_Y + (compact ? 26 : 34);
        int cardW = tftWidth - 2 * cardX;
        int cardH = compact ? 42 : 52;
        uiDrawCard(cardX, cardY, cardW, cardH, true);
        uiDrawText16(
            bruce_i18n::tr(message),
            tftWidth / 2,
            cardY + (cardH - 16) / 2,
            cardW - 16,
            uiTextBrightColor(),
            uiSurfaceColor(),
            1
        );
        printCenterFootnote("ESC: Eject & Exit");
    }
}

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    // Verify freespace
    uint64_t freeSpace = SD.totalBytes() - SD.usedBytes();
    if (bufsize > freeSpace) {
        return -1; // no space available
    }

    // Verify sector size
    const uint32_t secSize = SD.sectorSize();
    if (secSize == 0) return -1; // disk error

    // Write blocs
    for (uint32_t x = 0; x < bufsize / secSize; ++x) {
        uint8_t blkBuffer[secSize];
        memcpy(blkBuffer, buffer + secSize * x, secSize);
        if (!SD.writeRAW(blkBuffer, lba + x)) {
            return -1; // write error
        }
    }
    return bufsize;
}

int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    // Verify sector size
    const uint32_t secSize = SD.sectorSize();
    if (secSize == 0) return -1; // disk error

    // Read blocs
    for (uint32_t x = 0; x < bufsize / secSize; ++x) {
        if (!SD.readRAW(reinterpret_cast<uint8_t *>(buffer) + (x * secSize), lba + x)) {
            return -1; // read error
        }
    }
    return bufsize;
}

bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject) {
    if (!start && load_eject) {
        MassStorage::setShouldStop(true);
        return false;
    }

    return true;
}

void drawUSBStickIcon(bool plugged) {
    bool compact = (tftHeight < 160);
    int cardX = compact ? 10 : 18;
    int cardY = BORDER_PAD_Y + 24;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 18 : 22);
    uint16_t surf = uiSurfaceColor();
    uint16_t surfHi = uiSurfaceHiColor();
    uint16_t bdim = uiBorderDimColor();

    drawMainBorderWithTitle("Mass Storage");
    uiDrawCard(cardX, cardY, cardW, cardH, true);

    int iconW = compact ? 104 : 136;
    int iconH = compact ? 32 : 44;
    int bodyW = (iconW * 2) / 3;
    int bodyH = iconH;
    int bodyX = tftWidth / 2 - iconW / 2;
    int bodyY = cardY + (cardH - iconH) / 2 - (compact ? 8 : 10);

    int portW = iconW - bodyW;
    int portH = (bodyH * 75) / 100;
    int portX = bodyX + bodyW - 2;
    int portY = bodyY + (bodyH - portH) / 2;

    int holeW = max(5, portW / 3);
    int holeH = max(4, portH / 4);
    int holeX = portX + (portW - holeW) / 2 + 1;
    int holeY1 = portY + portH / 4 - holeH / 2;
    int holeY2 = portY + (portH * 3) / 4 - holeH / 2;

    // USB connector & themed body
    tft.fillRoundRect(portX, portY, portW, portH, 4, uiTextMutedColor());
    tft.fillRect(holeX, holeY1, holeW, holeH, surf);
    tft.fillRect(holeX, holeY2, holeW, holeH, surf);
    tft.fillRoundRect(bodyX, bodyY, bodyW, bodyH, 6, surfHi);
    tft.drawRoundRect(bodyX, bodyY, bodyW, bodyH, 6, plugged ? bruceConfig.priColor : bdim);

    // Status LED strip on body
    int ledW = max(4, bodyH / 8);
    int ledH = (bodyH * 55) / 100;
    int ledX = bodyX + 8;
    int ledY = bodyY + (bodyH - ledH) / 2;
    uint16_t stateCol = plugged ? TFT_DARKGREEN : bruceConfig.secColor;
    tft.fillRoundRect(ledX, ledY, ledW, ledH, 2, plugged ? TFT_GREEN : bruceConfig.priColor);

    // Label inside USB body
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextBrightColor(), surfHi);
    tft.drawCentreString("NEZHA", bodyX + bodyW / 2 + 4, bodyY + (bodyH - LH * FP) / 2, 1);

    // Status pill below icon
    String badgeTxt = plugged ? "USB Connected" : "Waiting USB";
    int16_t badgeW = bruce_i18n::utf8PixelWidth(bruce_i18n::tr(badgeTxt), FP) + 12;
    uiDrawBadge(tftWidth / 2 - badgeW / 2, bodyY + bodyH + (compact ? 6 : 12), badgeTxt, stateCol, 16);
    printCenterFootnote("ESC: Eject & Exit");
}

#endif // SOC_USB_OTG_SUPPORTED
