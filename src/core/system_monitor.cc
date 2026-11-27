#include "system_monitor.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#include "core/sd_functions.h"
#include "core/settings.h"
#include "core/utils.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <globals.h>

String systemMonitorJson() {
    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t heapMin = esp_get_minimum_free_heap_size();
    const uint32_t dmaFree = (uint32_t)radioFreeDmaBytes();
    const uint32_t dmaLargest = (uint32_t)radioLargestDmaBlock();
    const uint32_t psramFree = psramFound() ? ESP.getFreePsram() : 0;
    const uint32_t psramTotal = psramFound() ? ESP.getPsramSize() : 0;

    size_t minBleBlk = 0, minBleFree = 0;
    radioBleMinThresholds(minBleBlk, minBleFree);
    const bool wifiReady = dmaLargest >= RADIO_WIFI_MIN_DMA_BLOCK;
    const bool bleReady = radioIsBleInitialized() || radioDmaAtLeast(minBleBlk, minBleFree);

    size_t lfsUsed = 0, lfsTotal = 0;
    if (LittleFS.totalBytes() > 0) {
        lfsUsed = LittleFS.usedBytes();
        lfsTotal = LittleFS.totalBytes();
    }

    int bat = (getBattery != nullptr) ? getBattery() : -1;

    char buf[384];
    snprintf(
        buf,
        sizeof(buf),
        "{\"board\":\"%s\",\"heap_free\":%lu,\"heap_total\":%lu,\"heap_min\":%lu,"
        "\"dma_free\":%lu,\"dma_largest\":%lu,\"psram_free\":%lu,\"psram_total\":%lu,"
        "\"wifi_ok\":%s,\"ble_ok\":%s,\"wifi_mode\":%d,\"ble_up\":%s,\"webui\":%s,"
        "\"sd\":%s,\"lfs_used\":%lu,\"lfs_total\":%lu,\"bat\":%d,\"cpu_mhz\":%lu,"
        "\"uptime_s\":%lu,\"tasks\":%lu}",
        DEVICE_NAME,
        (unsigned long)heapFree,
        (unsigned long)heapTotal,
        (unsigned long)heapMin,
        (unsigned long)dmaFree,
        (unsigned long)dmaLargest,
        (unsigned long)psramFree,
        (unsigned long)psramTotal,
        wifiReady ? "true" : "false",
        bleReady ? "true" : "false",
        (int)WiFi.getMode(),
        radioIsBleInitialized() ? "true" : "false",
        isWebUIActive ? "true" : "false",
        sdcardMounted ? "true" : "false",
        (unsigned long)lfsUsed,
        (unsigned long)lfsTotal,
        bat,
        (unsigned long)ESP.getCpuFreqMHz(),
        (unsigned long)(millis() / 1000UL),
        (unsigned long)uxTaskGetNumberOfTasks()
    );
    return String(buf);
}

static void drawMeterBar(int x, int y, int w, int h, uint32_t used, uint32_t total, uint16_t fillCol) {
    tft.fillRect(x, y, w, h, uiSurfaceColor());
    tft.drawRect(x, y, w, h, uiBorderDimColor());
    if (total == 0) return;
    uint32_t pct = (used >= total) ? 100 : (uint32_t)(((uint64_t)used * 100ULL) / total);
    int fillW = (int)(((int32_t)(w - 2) * (int32_t)pct) / 100);
    if (fillW > 0) tft.fillRect(x + 1, y + 1, fillW, h - 2, fillCol);
}

static void drawSysMonPage(uint8_t page) {
    const int x = BORDER_PAD_X;
    const int w = tftWidth - 2 * BORDER_PAD_X;
    int y = BORDER_PAD_Y + LH + 4;
    const int bodyH = tftHeight - BORDER_PAD_X - LH - 4 - y;

    tft.fillRect(x, y, w, max(0, bodyH), bruceConfig.bgColor);
    tft.setTextSize(FP);

    if (page == 0) {
        const uint32_t heapFree = ESP.getFreeHeap();
        const uint32_t heapTotal = ESP.getHeapSize();
        const uint32_t heapUsed = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
        const uint32_t dmaFree = (uint32_t)radioFreeDmaBytes();
        const uint32_t dmaLargest = (uint32_t)radioLargestDmaBlock();

        size_t minBleBlk = 0, minBleFree = 0;
        radioBleMinThresholds(minBleBlk, minBleFree);
        const bool wifiReady = dmaLargest >= RADIO_WIFI_MIN_DMA_BLOCK;
        const bool bleReady = radioIsBleInitialized() || radioDmaAtLeast(minBleBlk, minBleFree);

        char line[64];
        // Internal DRAM
        tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
        snprintf(line, sizeof(line), "DRAM %lu/%luK free", (unsigned long)(heapFree / 1024), (unsigned long)(heapTotal / 1024));
        tft.drawString(line, x, y);
        y += LH + 1;
        drawMeterBar(x, y, w, 6, heapUsed, heapTotal, (heapFree < 32768) ? TFT_RED : bruceConfig.priColor);
        y += 9;

        // Contiguous DMA Block
        tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
        snprintf(line, sizeof(line), "DMA Max:%luK Free:%luK", (unsigned long)(dmaLargest / 1024), (unsigned long)(dmaFree / 1024));
        tft.drawString(line, x, y);
        y += LH + 2;

        // Radio readiness pills
        tft.setTextColor(wifiReady ? TFT_GREEN : TFT_RED, bruceConfig.bgColor);
        tft.drawString(wifiReady ? "[WIFI OK]" : "[WIFI LOW]", x, y);
        tft.setTextColor(bleReady ? TFT_GREEN : TFT_RED, bruceConfig.bgColor);
        tft.drawRightString(bleReady ? "[BLE OK]" : "[BLE LOW]", x + w, y, 1);
        y += LH + 4;

        // PSRAM
        tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
        if (psramFound() && ESP.getPsramSize() > 0) {
            uint32_t pf = ESP.getFreePsram();
            uint32_t pt = ESP.getPsramSize();
            uint32_t pu = (pt > pf) ? (pt - pf) : 0;
            snprintf(line, sizeof(line), "PSRAM %lu/%luK free", (unsigned long)(pf / 1024), (unsigned long)(pt / 1024));
            tft.drawString(line, x, y);
            y += LH + 1;
            drawMeterBar(x, y, w, 6, pu, pt, bruceConfig.secColor);
            y += 9;
        } else {
            tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
            tft.drawString("PSRAM: N/A (Internal DRAM)", x, y);
            y += LH + 4;
        }

        // Battery + CPU + Uptime
        int bat = (getBattery != nullptr) ? getBattery() : -1;
        tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
        snprintf(
            line,
            sizeof(line),
            "Bat:%d%%  %luMHz  Up:%lus",
            bat,
            (unsigned long)ESP.getCpuFreqMHz(),
            (unsigned long)(millis() / 1000UL)
        );
        tft.drawString(line, x, y);
    } else {
        // Page 2: Tasks, Flash, Storage, Radios
        char line[64];
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.drawString("System & Tasks", x, y);
        y += LH + 4;

        tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
        snprintf(
            line, sizeof(line), "Min Heap Ever: %lu KB", (unsigned long)(esp_get_minimum_free_heap_size() / 1024)
        );
        tft.drawString(line, x, y);
        y += LH + 3;

        snprintf(
            line,
            sizeof(line),
            "Tasks: %lu  StackFree: %lu B",
            (unsigned long)uxTaskGetNumberOfTasks(),
            (unsigned long)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t))
        );
        tft.drawString(line, x, y);
        y += LH + 3;

        snprintf(line, sizeof(line), "Flash: %lu MB  SD: %s", (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)), sdcardMounted ? "ON" : "OFF");
        tft.drawString(line, x, y);
        y += LH + 3;

        size_t lfsUsed = LittleFS.usedBytes();
        size_t lfsTotal = LittleFS.totalBytes();
        snprintf(line, sizeof(line), "LittleFS: %lu / %lu KB", (unsigned long)(lfsUsed / 1024), (unsigned long)(lfsTotal / 1024));
        tft.drawString(line, x, y);
        y += LH + 3;

        snprintf(
            line,
            sizeof(line),
            "WiFi:%d BLE:%s WebUI:%s",
            (int)WiFi.getMode(),
            radioIsBleInitialized() ? "ON" : "OFF",
            isWebUIActive ? "ON" : "OFF"
        );
        tft.drawString(line, x, y);
    }

    // Footer bar
    const int footY = tftHeight - BORDER_PAD_X - LH;
    tft.fillRect(x, footY, w, LH, bruceConfig.bgColor);
    tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
    tft.drawString(page == 0 ? "OK:Reclaim  <>:P2  ESC" : "OK:Reclaim  <>:P1  ESC", x, footY);
}

void showSystemMonitor() {
    drawMainBorderWithTitle("System Monitor");
    uint8_t page = 0;
    uint32_t lastDrawMs = 0;
    bool forceDraw = true;

    check(SelPress);
    check(EscPress);

    while (!check(EscPress) && !returnToMenu) {
        wakeUpScreen();

        if (check(NextPress) || check(PrevPress) || check(UpPress) || check(DownPress)) {
            page = (page + 1) % 2;
            forceDraw = true;
        }

        if (check(SelPress)) {
            // One-tap memory hygiene: clear scan caches and reclaim idle Wi-Fi/WebUI DMA buffers
            WiFi.scanDelete();
            if (!wifiConnected && !isWebUIActive) {
                radioReclaimWifiMemory(RADIO_BLE_TARGET_DMA_BLOCK, RADIO_BLE_TARGET_DMA_FREE);
            }
            displaySuccess("RAM Cache Purged", false);
            vTaskDelay(pdMS_TO_TICKS(350));
            drawMainBorderWithTitle("System Monitor");
            forceDraw = true;
        }

        uint32_t now = millis();
        if (forceDraw || (now - lastDrawMs >= 1000)) {
            lastDrawMs = now;
            forceDraw = false;
            drawSysMonPage(page);
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
    check(EscPress);
}
