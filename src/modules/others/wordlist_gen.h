#ifndef __WORDLIST_GEN_H__
#define __WORDLIST_GEN_H__

/*
 * Wordlist Generator — builds WiFi passphrase dictionaries on the device filesystem for the
 * existing WiFi Password Recover cracker (src/modules/wifi/wifi_recover.cc).
 *
 * Output contract (consumed by WordlistReader in wifi_recover.cc):
 *   - one ASCII word per line, newline terminated;
 *   - only words with 8..63 characters are written (WPA passphrase limits);
 *   - file lives in /wordlists/<sanitized-name>.txt on SD when mounted, else LittleFS.
 *
 * The engine never touches the radio (no radioHasMemForWifi()/wifiConnectMenu(): they can
 * unmount SD) and has no display dependency, so the serial CLI can share it with the TFT UI.
 *
 * Engine / UI split (AGENTS 3.1): wordlist_gen_generate*() is non-interactive, while
 * wordlist_gen_run() owns the menus, keyboard prompts and progress screen.
 *
 * mode / spec grammar:
 *   "charset"  "<charset>[:<min>:<max>]"   min/max default to 8; lengths are clipped to 8..63
 *   "pattern"  "<mask>"                    a=lower A=upper #=digit *=printable ASCII, \ escapes
 *   "leet"     "<word>[:<flags>]"          flags subset of "aeiostbg", default all transforms
 *   "number"   "<start>-<end>[:<width>]"   width 0/absent means 8 and is clamped to 8..16
 *
 * Every mode is bounded: at most 100000 words, at most 24 KiB on LittleFS, and at least 8 KiB
 * of free space is always kept. Files outside these limits are refused or capped, never
 * allowed to fill the filesystem.
 */
#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

#include <stdint.h>

// Progress callback: done/total are candidate lines processed/planned.
typedef void (*WordlistProgressCb)(uint32_t done, uint32_t total, void *ctx);

struct WordlistGenStats {
    uint32_t planned = 0;        // valid words the plan will write
    uint32_t candidates = 0;     // candidate lines iterated (progress total)
    uint32_t written = 0;        // lines actually written
    uint32_t skipped = 0;        // candidates rejected by the 8..63 length contract
    uint32_t bytes = 0;          // bytes written, or estimated on a dry run
    uint64_t freeBytes = 0;      // free bytes on the target filesystem
    bool onSd = false;           // true: SD card, false: LittleFS
    bool capped = false;         // plan clamped to the word/LittleFS budget
    bool existed = false;        // target file already existed
    bool aborted = false;        // caller abort flag (or UI Esc) stopped generation
    char path[64] = {};          // resolved "/wordlists/<name>.txt"
    const char *error = nullptr; // static English message, nullptr on success
};

struct WordlistGenOptions {
    const char *name = nullptr;         // requested file name, sanitized by the engine
    const char *mode = nullptr;         // "charset", "pattern", "leet" or "number"
    const char *spec = nullptr;         // mode-specific parameters, see grammar above
    WordlistProgressCb cb = nullptr;    // optional progress callback
    void *ctx = nullptr;                // opaque callback context
    WordlistGenStats *stats = nullptr;  // optional result details
    volatile bool *abortFlag = nullptr; // optional cooperative abort (polled per word)
    bool dryRun = false;                // validate/plan only; no directory or file writes
};

// Full engine entry used by the UI and (later) by the serial CLI.
bool wordlist_gen_generate_ex(const WordlistGenOptions &opts);

// Simple engine entry: generate <name> from <mode>/<spec>, no abort flag, no stats.
bool wordlist_gen_generate(
    const char *name, const char *mode, const char *spec, WordlistProgressCb cb, void *ctx
);

// Menu / TFT entry point (mode picker, prompts, preview, progress, result screen).
void wordlist_gen_run();

#endif
#endif
