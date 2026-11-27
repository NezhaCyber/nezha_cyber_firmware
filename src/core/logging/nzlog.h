/**
 * @file nzlog.h
 * @brief Android logcat-style logger for Nezha Cyber firmware.
 *
 * Design goals:
 *  - Logcat-compatible line format: "<time> <L>/<Tag> (<task>): <message>"
 *    with levels V/D/I/W/E/F.
 *  - Real-time, append-only file sink on the SD card. Every line is written and
 *    flushed immediately, so the log survives a panic / power loss without
 *    keeping any history in RAM.
 *  - Zero heap allocation per log line (fixed stack buffers only).
 *  - Safe to call from any task; re-entrant calls fall back to serial-only so a
 *    logger call made from inside the SD stack can never deadlock.
 *  - Also mirrors to Serial so a USB cable (or the WebUI /logs endpoint) shows
 *    the same stream live.
 *
 * Usage:
 *    NZLOGI("DEAUTH", "target=%s channel=%u", mac.c_str(), ch);
 *    NZLOGE("IR", "sendRaw failed (%u pulses)", count);
 *
 * Keep the tag short (<= 8 chars) so the line stays readable on a 240px TFT.
 */
#ifndef __NZLOG_H__
#define __NZLOG_H__

#include <Arduino.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

namespace nzlog {

enum Level : uint8_t {
    VERBOSE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
};

// Log directory / file layout on the SD card.
constexpr const char *kDir = "/NezhaLogs";
constexpr const char *kPath = "/NezhaLogs/nezha.log";
constexpr const char *kOldPath = "/NezhaLogs/nezha.log.1";
// Rotate once the live file reaches this size; the previous file is kept as .1
constexpr size_t kMaxFileBytes = 512UL * 1024UL;

/**
 * Start (or retry) the logger. Safe to call repeatedly:
 *  - first call makes the logger usable and emits the boot banner;
 *  - calls made before the SD card is mounted keep serial logging only;
 *  - calls after a successful mount open/create the SD log file.
 * @return true when the SD file sink is ready.
 */
bool begin();

/** Close the SD file sink (e.g. before unmounting the card). */
void end();

/** True when lines are currently being appended to the SD card. */
bool fileReady();

/** Full path of the live log file. */
const char *path();

/** Current size of the live log file in bytes (0 when not ready). */
size_t fileSize();

/** Minimum level written to the SD card. Default: DEBUG. */
void setLevel(Level level);
Level level();

/** Minimum level mirrored to Serial. Default: INFO. */
void setSerialLevel(Level level);
Level serialLevel();

/** @return true if a line at this level would be emitted anywhere. */
inline bool isEnabled(Level l) { return l >= level() || l >= serialLevel(); }

/** Core log entry point (printf-style). */
void write(Level l, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/** Pre-formatted variant, for callers that already built a message. */
void writeRaw(Level l, const char *tag, const char *msg);

/** Flush buffered serial/file data now. */
void flush();

/** Log heap / PSRAM statistics. */
void logMemory(const char *tag, const char *where);

/** Delete the live + rotated log files (next write recreates them). */
bool clear();

/** Human-readable level letter and name. */
char levelChar(Level l);
const char *levelName(Level l);

} // namespace nzlog

// Logcat-style convenience macros.
#define NZLOGV(tag, fmt, ...) ::nzlog::write(::nzlog::VERBOSE, tag, fmt, ##__VA_ARGS__)
#define NZLOGD(tag, fmt, ...) ::nzlog::write(::nzlog::DEBUG, tag, fmt, ##__VA_ARGS__)
#define NZLOGI(tag, fmt, ...) ::nzlog::write(::nzlog::INFO, tag, fmt, ##__VA_ARGS__)
#define NZLOGW(tag, fmt, ...) ::nzlog::write(::nzlog::WARN, tag, fmt, ##__VA_ARGS__)
#define NZLOGE(tag, fmt, ...) ::nzlog::write(::nzlog::ERROR, tag, fmt, ##__VA_ARGS__)
#define NZLOGF(tag, fmt, ...) ::nzlog::write(::nzlog::FATAL, tag, fmt, ##__VA_ARGS__)
#define NZLOGMEM(tag) ::nzlog::logMemory(tag, __func__)

#endif // __NZLOG_H__
