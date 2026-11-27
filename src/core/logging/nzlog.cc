#include "core/logging/nzlog.h"

#include <FS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp32-hal-psram.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <globals.h>
#include <sys/time.h>
#include <time.h>
#include <cstring>

#ifndef BRUCE_VERSION
#define BRUCE_VERSION "dev"
#endif

namespace nzlog {
namespace {

constexpr size_t kLineMax = 320;
constexpr uint32_t kReopenIntervalMs = 3000;

SemaphoreHandle_t s_mutex = nullptr;
File s_file;
TaskHandle_t s_owner = nullptr;
bool s_fileReady = false;
bool s_bootSerialDone = false;
bool s_bootFileDone = false;
bool s_serialReady = false;
uint32_t s_lastOpenTry = 0;

Level s_level = DEBUG;        // SD keeps detailed diagnostics
Level s_serialLevel = INFO;   // Serial stays readable

const char *resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "POWER_ON";
        case ESP_RST_EXT: return "EXTERNAL_PIN";
        case ESP_RST_SW: return "SOFTWARE_RESTART";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default: return "UNKNOWN";
    }
}

bool resetReasonIsCrash(esp_reset_reason_t reason) {
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
           reason == ESP_RST_WDT || reason == ESP_RST_BROWNOUT;
}

void ensureMutex() {
    if (s_mutex == nullptr) { s_mutex = xSemaphoreCreateRecursiveMutex(); }
}

bool openFileLocked() {
    if (!sdcardMounted) return false;
    if (!SD.exists(kDir)) {
        if (!SD.mkdir(kDir)) return false;
    }

    // Rotate before opening so one runaway session cannot fill the card.
    if (SD.exists(kPath)) {
        File probe = SD.open(kPath, FILE_READ);
        size_t size = probe ? probe.size() : 0;
        if (probe) probe.close();
        if (size >= kMaxFileBytes) {
            SD.remove(kOldPath);
            SD.rename(kPath, kOldPath);
        }
    }

    s_file = SD.open(kPath, FILE_APPEND);
    if (!s_file) {
        s_fileReady = false;
        return false;
    }
    s_fileReady = true;
    s_bootFileDone = false; // new file/session: write the boot header into it
    s_lastOpenTry = millis();
    return true;
}

void closeFileLocked() {
    if (s_file) {
        s_file.flush();
        s_file.close();
    }
    s_fileReady = false;
}

void rotateLocked() {
    closeFileLocked();
    SD.remove(kOldPath);
    SD.rename(kPath, kOldPath);
    if (openFileLocked()) {
        static const char marker[] = "----- log rotated, previous file kept as nezha.log.1 -----\n";
        s_file.write((const uint8_t *)marker, sizeof(marker) - 1);
        s_file.flush();
    }
}

size_t formatLine(char *out, size_t cap, Level lvl, const char *tag, const char *msg) {
    char ts[24];
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    struct tm tmInfo = {};
    localtime_r(&tv.tv_sec, &tmInfo);
    if (tmInfo.tm_year + 1900 >= 2024) {
        snprintf(
            ts,
            sizeof(ts),
            "%02d-%02d %02d:%02d:%02d.%03d",
            tmInfo.tm_mon + 1,
            tmInfo.tm_mday,
            tmInfo.tm_hour,
            tmInfo.tm_min,
            tmInfo.tm_sec,
            (int)(tv.tv_usec / 1000)
        );
    } else {
        uint32_t ms = millis();
        snprintf(ts, sizeof(ts), "T+%lu.%03lu", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    }

    char taskName[16] = "isr";
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (self != nullptr) {
        const char *n = pcTaskGetName(self);
        if (n != nullptr) {
            strncpy(taskName, n, sizeof(taskName) - 1);
            taskName[sizeof(taskName) - 1] = '\0';
        }
    }

    const char *safeTag = (tag != nullptr && tag[0] != '\0') ? tag : "APP";
    const char *safeMsg = (msg != nullptr) ? msg : "";
    size_t msgLen = strlen(safeMsg);
    bool needsNewline = (msgLen == 0) || (safeMsg[msgLen - 1] != '\n');

    int n = snprintf(
        out,
        cap,
        "%s %c/%.12s (%.15s): %s%s",
        ts,
        levelChar(lvl),
        safeTag,
        taskName,
        safeMsg,
        needsNewline ? "\n" : ""
    );
    if (n < 0) return 0;
    if ((size_t)n >= cap) return cap - 1; // snprintf truncated; keep the NUL
    return (size_t)n;
}

void writeToSerial(const char *line, size_t len, Level lvl) {
    if (!s_serialReady) return;
    if (lvl < s_serialLevel) return;
    Serial.write(reinterpret_cast<const uint8_t *>(line), len);
    // Flush errors immediately so a panic shortly after still leaves them in
    // the USB FIFO; normal lines are allowed to drain asynchronously.
    if (lvl >= ERROR) Serial.flush();
}

void writeToFile(const char *line, size_t len) {
    if (!s_fileReady) return;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    // Never touch the filesystem from an ISR.
    if (self == nullptr) return;
    // A log call made from inside the SD/FAT stack must not recurse.
    if (s_owner == self) return;
    ensureMutex();
    if (s_mutex == nullptr) return;
    if (xSemaphoreTakeRecursive(s_mutex, pdMS_TO_TICKS(120)) != pdTRUE) return;
    s_owner = self;
    if (s_fileReady) {
        size_t written = s_file.write(reinterpret_cast<const uint8_t *>(line), len);
        s_file.flush(); // real-time: make the line durable now, not on close
        if (written != len) {
            closeFileLocked();
        } else if (s_file.size() >= kMaxFileBytes) {
            rotateLocked();
        }
    }
    s_owner = nullptr;
    xSemaphoreGiveRecursive(s_mutex);
}

void directWriteLocked(const char *line, size_t len, Level lvl) {
    writeToSerial(line, len, lvl);
    if (s_fileReady && s_file) {
        s_file.write((const uint8_t *)line, len);
        s_file.flush();
    }
}

void emitBootBannerLocked() {
    esp_reset_reason_t reason = esp_reset_reason();
    char line[kLineMax];

    if (!s_bootSerialDone || (s_fileReady && !s_bootFileDone)) {
        size_t len = formatLine(
            line, sizeof(line), INFO, "BOOT", "===== Nezha Cyber " BRUCE_VERSION " starting ====="
        );
        if (!s_bootSerialDone) writeToSerial(line, len, INFO);
        if (s_fileReady && !s_bootFileDone) {
            s_file.write((const uint8_t *)line, len);
            s_file.flush();
        }
        s_bootSerialDone = true;
    }

    if (!s_bootFileDone && s_fileReady) {
        if (s_file.size() > 0) {
            static const char separator[] = "\n";
            s_file.write((const uint8_t *)separator, 1);
        }
        snprintf(
            line,
            sizeof(line),
            "T+0.000 I/BOOT (boot): reset reason: %s%s\n",
            resetReasonName(reason),
            resetReasonIsCrash(reason) ? " (previous run crashed)" : ""
        );
        directWriteLocked(line, strlen(line), INFO);

        if (resetReasonIsCrash(reason)) {
            snprintf(
                line,
                sizeof(line),
                "T+0.000 E/BOOT (boot): ABNORMAL PREVIOUS SHUTDOWN (%s). "
                "Check the last lines before this marker.\n",
                resetReasonName(reason)
            );
            directWriteLocked(line, strlen(line), ERROR);
        }

        size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (psramFound()) {
            snprintf(
                line,
                sizeof(line),
                "T+0.000 I/BOOT (boot): mem heap=%u internal_free=%u largest=%u psram_free=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)internalFree,
                (unsigned)internalLargest,
                (unsigned)ESP.getFreePsram()
            );
        } else {
            snprintf(
                line,
                sizeof(line),
                "T+0.000 I/BOOT (boot): mem heap=%u internal_free=%u largest=%u\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)internalFree,
                (unsigned)internalLargest
            );
        }
        directWriteLocked(line, strlen(line), INFO);
        s_bootFileDone = true;
    }
}

} // namespace

char levelChar(Level l) {
    switch (l) {
        case VERBOSE: return 'V';
        case DEBUG: return 'D';
        case INFO: return 'I';
        case WARN: return 'W';
        case ERROR: return 'E';
        case FATAL: return 'F';
        default: return '?';
    }
}

const char *levelName(Level l) {
    switch (l) {
        case VERBOSE: return "VERBOSE";
        case DEBUG: return "DEBUG";
        case INFO: return "INFO";
        case WARN: return "WARN";
        case ERROR: return "ERROR";
        case FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

void setLevel(Level l) { s_level = l; }
Level level() { return s_level; }
void setSerialLevel(Level l) { s_serialLevel = l; }
Level serialLevel() { return s_serialLevel; }

bool begin() {
    ensureMutex();
    s_serialReady = true;

    if (s_mutex != nullptr && xSemaphoreTakeRecursive(s_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
        s_owner = xTaskGetCurrentTaskHandle();
        if (!s_fileReady) { openFileLocked(); }
        emitBootBannerLocked();
        s_owner = nullptr;
        xSemaphoreGiveRecursive(s_mutex);
    }
    return s_fileReady;
}

void end() {
    if (s_mutex != nullptr && xSemaphoreTakeRecursive(s_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
        closeFileLocked();
        xSemaphoreGiveRecursive(s_mutex);
    } else {
        s_fileReady = false;
    }
}

bool fileReady() { return s_fileReady; }

const char *path() { return kPath; }

size_t fileSize() {
    if (!s_fileReady) return 0;
    return s_file.size();
}

void writeRaw(Level l, const char *tag, const char *msg) {
    if (!isEnabled(l)) return;

    char line[kLineMax];
    size_t len = formatLine(line, sizeof(line), l, tag, msg);
    if (len == 0) return;

    writeToSerial(line, len, l);

    if (!s_fileReady && s_serialReady && (millis() - s_lastOpenTry) > kReopenIntervalMs) {
        // SD may have been mounted late (or replaced): retry the sink.
        s_lastOpenTry = millis();
        begin();
    }
    writeToFile(line, len);
}

void write(Level l, const char *tag, const char *fmt, ...) {
    if (!isEnabled(l)) return;

    char msg[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    msg[sizeof(msg) - 1] = '\0';

    // One entry == one line: fold embedded CR/LF so the parser and the SD file
    // stay line-oriented.
    for (char *p = msg; *p != '\0'; ++p) {
        if (*p == '\n' || *p == '\r') { *p = ' '; }
    }

    writeRaw(l, tag, msg);
}

void flush() {
    if (s_serialReady) Serial.flush();
    if (s_mutex != nullptr && xSemaphoreTakeRecursive(s_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
        if (s_fileReady) s_file.flush();
        xSemaphoreGiveRecursive(s_mutex);
    }
}

void logMemory(const char *tag, const char *where) {
    char msg[160];
    size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (psramFound()) {
        snprintf(
            msg,
            sizeof(msg),
            "mem @%s: heap=%u internal_free=%u largest=%u psram_free=%u",
            where,
            (unsigned)ESP.getFreeHeap(),
            (unsigned)internalFree,
            (unsigned)internalLargest,
            (unsigned)ESP.getFreePsram()
        );
    } else {
        snprintf(
            msg,
            sizeof(msg),
            "mem @%s: heap=%u internal_free=%u largest=%u",
            where,
            (unsigned)ESP.getFreeHeap(),
            (unsigned)internalFree,
            (unsigned)internalLargest
        );
    }
    writeRaw(INFO, tag, msg);
}

bool clear() {
    end();
    bool ok = true;
    if (sdcardMounted) {
        if (SD.exists(kPath)) ok = SD.remove(kPath) && ok;
        if (SD.exists(kOldPath)) ok = SD.remove(kOldPath) && ok;
    }
    begin();
    NZLOGI("LOG", "log files cleared");
    return ok;
}

} // namespace nzlog
