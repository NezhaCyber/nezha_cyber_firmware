#include "system_monitor.h"

#include "core/display.h"
#include "core/i18n.h"
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

static void drawSysMonPage(uint8_t page) {
    const int x = BORDER_PAD_X;
    const int w = tftWidth - 2 * BORDER_PAD_X;
    const int topY = uiContentTop();
    const int botY = uiContentBottom(true);
    const int cardH = max(48, botY - topY);
    const bool compact = (tftHeight < 160);
    const uint16_t surf = uiSurfaceColor();
    const int ix = x + 8;
    const int iw = w - 16;
    const int rowStep = (FP == 1 && bruce_i18n::isChinese()) ? 14 : (LH + 4);

    uiDrawCard(x, topY, w, cardH, true);
    int y = topY + (compact ? 6 : 9);
    tft.setTextSize(FP);

    if (page == 0) {
        const uint32_t heapFree = ESP.getFreeHeap();
        const uint32_t heapTotal = ESP.getHeapSize();
        const uint32_t heapUsed = (heapTotal > heapFree) ? (heapTotal - heapFree) : 0;
        const int heapPct = (heapTotal > 0) ? (int)(((uint64_t)heapUsed * 100ULL) / heapTotal) : 0;
        const uint32_t dmaFree = (uint32_t)radioFreeDmaBytes();
        const uint32_t dmaLargest = (uint32_t)radioLargestDmaBlock();

        size_t minBleBlk = 0, minBleFree = 0;
        radioBleMinThresholds(minBleBlk, minBleFree);
        const bool wifiReady = dmaLargest >= RADIO_WIFI_MIN_DMA_BLOCK;
        const bool bleReady = radioIsBleInitialized() || radioDmaAtLeast(minBleBlk, minBleFree);

        char val[48];
        snprintf(val, sizeof(val), "%lu/%lu KB", (unsigned long)(heapFree / 1024), (unsigned long)(heapTotal / 1024));
        uiDrawKeyValueRow(ix, y, iw, "DRAM Free", val, uiTextBrightColor(), surf);
        y += rowStep - 1;
        uiDrawProgressBar(ix, y, iw, 6, heapPct, (heapFree < 32768) ? TFT_RED : bruceConfig.priColor, false);
        y += 9;

        snprintf(val, sizeof(val), "%luK / %luK", (unsigned long)(dmaLargest / 1024), (unsigned long)(dmaFree / 1024));
        uiDrawKeyValueRow(ix, y, iw, "DMA Max/Free", val, uiTextBrightColor(), surf);
        y += rowStep;

        int16_t wW = uiDrawBadge(ix, y, wifiReady ? "WiFi OK" : "WiFi LOW", wifiReady ? TFT_DARKGREEN : TFT_RED, 15);
        uiDrawBadge(ix + wW + 6, y, bleReady ? "BLE OK" : "BLE LOW", bleReady ? TFT_DARKGREEN : TFT_RED, 15);
        y += 18;

        if (y + rowStep <= topY + cardH - 4) {
            if (psramFound() && ESP.getPsramSize() > 0) {
                uint32_t pf = ESP.getFreePsram();
                uint32_t pt = ESP.getPsramSize();
                uint32_t pu = (pt > pf) ? (pt - pf) : 0;
                int psPct = (int)(((uint64_t)pu * 100ULL) / pt);
                snprintf(val, sizeof(val), "%lu/%lu KB", (unsigned long)(pf / 1024), (unsigned long)(pt / 1024));
                uiDrawKeyValueRow(ix, y, iw, "PSRAM Free", val, uiTextBrightColor(), surf);
                y += rowStep - 1;
                if (y + 7 <= topY + cardH - 4) {
                    uiDrawProgressBar(ix, y, iw, 6, psPct, bruceConfig.secColor, false);
                    y += 9;
                }
            } else {
                uiDrawKeyValueRow(ix, y, iw, "PSRAM", "N/A", uiTextMutedColor(), surf);
                y += rowStep;
            }
        }

        if (y + rowStep <= topY + cardH - 2) {
            int bat = (getBattery != nullptr) ? getBattery() : -1;
            snprintf(
                val,
                sizeof(val),
                "%d%%  %luMHz  %lus",
                bat,
                (unsigned long)ESP.getCpuFreqMHz(),
                (unsigned long)(millis() / 1000UL)
            );
            uiDrawKeyValueRow(ix, y, iw, "System", val, uiTextMutedColor(), surf);
        }
    } else {
        char val[48];
        snprintf(val, sizeof(val), "%lu KB", (unsigned long)(esp_get_minimum_free_heap_size() / 1024));
        uiDrawKeyValueRow(ix, y, iw, "Min Heap", val, uiTextBrightColor(), surf);
        y += rowStep;

        snprintf(
            val,
            sizeof(val),
            "%lu (%lu B)",
            (unsigned long)uxTaskGetNumberOfTasks(),
            (unsigned long)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t))
        );
        uiDrawKeyValueRow(ix, y, iw, "Tasks / Stack", val, uiTextBrightColor(), surf);
        y += rowStep;

        snprintf(
            val,
            sizeof(val),
            "%lu MB / %s",
            (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
            sdcardMounted ? bruce_i18n::tr("ON") : bruce_i18n::tr("OFF")
        );
        uiDrawKeyValueRow(ix, y, iw, "Flash / SD", val, uiTextBrightColor(), surf);
        y += rowStep;

        size_t lfsUsed = LittleFS.usedBytes();
        size_t lfsTotal = LittleFS.totalBytes();
        snprintf(val, sizeof(val), "%lu / %lu KB", (unsigned long)(lfsUsed / 1024), (unsigned long)(lfsTotal / 1024));
        uiDrawKeyValueRow(ix, y, iw, "LittleFS", val, uiTextBrightColor(), surf);
        y += rowStep;

        if (y + rowStep <= topY + cardH - 2) {
            snprintf(
                val,
                sizeof(val),
                "W:%d B:%s Web:%s",
                (int)WiFi.getMode(),
                radioIsBleInitialized() ? "1" : "0",
                isWebUIActive ? "1" : "0"
            );
            uiDrawKeyValueRow(ix, y, iw, "Radios", val, bruceConfig.priColor, surf);
        }
    }

    printCenterFootnote(page == 0 ? "OK=Reclaim  LR=Page 2  ESC=Back" : "OK=Reclaim  LR=Page 1  ESC=Back");
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
