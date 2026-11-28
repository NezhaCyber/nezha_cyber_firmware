#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "wordlist_gen.h"

#include "core/display.h"
#include "core/i18n.h"
#include "core/logging/nzlog.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <FS.h>
#include <ctype.h>
#include <globals.h>
#include <stdio.h>
#include <string.h>

namespace {

// ── Output contract / budgets ───────────────────────────────────────────────
constexpr const char *kDir = "/wordlists";
constexpr uint32_t kMaxWords = 100000;            // hard cap for one generated dictionary
constexpr uint8_t kMinWordLen = 8;                // WPA passphrase minimum
constexpr uint8_t kMaxWordLen = 63;               // WPA passphrase maximum
constexpr uint64_t kReserveBytes = 8192;          // keep headroom on the target filesystem
constexpr uint32_t kLittleFsBudget = 24u * 1024u; // LittleFS dictionaries stay under ~24 KiB
constexpr uint16_t kFlushEvery = 128;             // progress / write-error granularity
constexpr size_t kMaxNameBytes = 32;              // sanitized base name cap

// ── Static lookup tables (read-only, flash resident) ────────────────────────
static const char kLowerSet[] PROGMEM = "abcdefghijklmnopqrstuvwxyz";
static const char kUpperSet[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char kDigitSet[] PROGMEM = "0123456789";
// Every printable ASCII byte (0x20..0x7E), used by the '*' mask symbol.
static const char kPrintableSet[] PROGMEM =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
static const char kLeetOrder[] PROGMEM = "aeiostbg";

enum : uint8_t { SET_LOWER = 0, SET_UPPER, SET_DIGIT, SET_PRINT, SET_LITERAL = 0xFF };

const char *setChars(uint8_t id) {
    switch (id) {
        case SET_LOWER: return kLowerSet;
        case SET_UPPER: return kUpperSet;
        case SET_DIGIT: return kDigitSet;
        case SET_PRINT: return kPrintableSet;
        default: return "";
    }
}

uint8_t setSize(uint8_t id) {
    switch (id) {
        case SET_LOWER:
        case SET_UPPER: return 26;
        case SET_DIGIT: return 10;
        case SET_PRINT: return 95;
        default: return 1;
    }
}

char leetDigit(char c) {
    switch (c) {
        case 'a': return '4';
        case 'e': return '3';
        case 'i': return '1';
        case 'o': return '0';
        case 's': return '5';
        case 't': return '7';
        case 'b': return '8';
        case 'g': return '9';
        default: return c;
    }
}

// ── Planning ────────────────────────────────────────────────────────────────
enum class Mode : uint8_t { Charset, Pattern, Leet, Number };

struct Plan {
    Mode mode = Mode::Charset;
    char charset[64] = {};
    uint8_t charsetLen = 0;
    uint8_t minLen = kMinWordLen;
    uint8_t maxLen = kMinWordLen;
    char mask[64] = {};
    uint8_t maskSet[64] = {};
    uint8_t maskLen = 0;
    char word[64] = {};
    uint8_t wordLen = 0;
    uint8_t leetBits = 0;
    uint8_t leetCount = 0;
    char leetLetters[8] = {};
    uint32_t numStart = 0;
    uint32_t numEnd = 0;
    uint8_t numWidth = kMinWordLen;
    uint32_t candidates = 0; // progress total
    uint32_t planned = 0;    // words that satisfy the 8..63 contract
    uint32_t skipped = 0;    // candidates rejected by the contract
    uint8_t maxLineLen = 0;
    bool capped = false;
    const char *error = nullptr;
};

struct Prepared {
    Plan plan;
    FS *fs = nullptr;
    char path[64] = {};
    uint32_t candidates = 0;
    uint32_t planned = 0;
    uint32_t skipped = 0;
    uint64_t freeBytes = 0;
    uint64_t estBytes = 0;
    bool onSd = false;
    bool existed = false;
    bool capped = false;
    const char *error = nullptr;
};

bool parseMode(const char *mode, Mode &out) {
    if (mode == nullptr) return false;
    if (strcmp(mode, "charset") == 0) {
        out = Mode::Charset;
        return true;
    }
    if (strcmp(mode, "pattern") == 0) {
        out = Mode::Pattern;
        return true;
    }
    if (strcmp(mode, "leet") == 0) {
        out = Mode::Leet;
        return true;
    }
    if (strcmp(mode, "number") == 0) {
        out = Mode::Number;
        return true;
    }
    return false;
}

bool parseDecimal(const char *s, size_t len, uint32_t limit, uint32_t &out) {
    if (s == nullptr || len == 0) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10u + (uint32_t)(s[i] - '0');
        if (value > limit) return false;
    }
    out = (uint32_t)value;
    return true;
}

// "<charset>[:<min>:<max>]", min/max default to 8 (single-length dictionary).
bool buildCharsetPlan(const char *spec, Plan &p) {
    p.mode = Mode::Charset;
    if (spec == nullptr || spec[0] == '\0') {
        p.error = "Charset cannot be empty";
        return false;
    }
    const char *sep1 = strchr(spec, ':');
    const size_t csLen = sep1 != nullptr ? (size_t)(sep1 - spec) : strlen(spec);
    if (csLen == 0) {
        p.error = "Charset cannot be empty";
        return false;
    }
    if (csLen > sizeof(p.charset) - 1) {
        p.error = "Charset is too long";
        return false;
    }
    for (size_t i = 0; i < csLen; ++i) {
        const unsigned char c = (unsigned char)spec[i];
        if (c < 0x21 || c > 0x7E) {
            p.error = "Charset has invalid characters";
            return false;
        }
    }
    memcpy(p.charset, spec, csLen);
    p.charset[csLen] = '\0';
    p.charsetLen = (uint8_t)csLen;

    uint32_t minLen = kMinWordLen;
    uint32_t maxLen = kMinWordLen;
    if (sep1 != nullptr) {
        const char *sep2 = strchr(sep1 + 1, ':');
        if (sep2 == nullptr) {
            p.error = "Invalid charset spec";
            return false;
        }
        if (!parseDecimal(sep1 + 1, (size_t)(sep2 - sep1 - 1), kMaxWordLen, minLen) ||
            !parseDecimal(sep2 + 1, strlen(sep2 + 1), kMaxWordLen, maxLen)) {
            p.error = "Length must be 8-63";
            return false;
        }
        if (minLen > maxLen) {
            p.error = "Min length cannot exceed max length";
            return false;
        }
    }
    if (maxLen < kMinWordLen) {
        p.error = "Length must be 8-63";
        return false;
    }
    if (minLen < kMinWordLen) minLen = kMinWordLen; // documented clamp: never emit <8 chars

    p.minLen = (uint8_t)minLen;
    p.maxLen = (uint8_t)maxLen;
    p.maxLineLen = (uint8_t)maxLen;

    uint64_t total = 0;
    bool overflow = false;
    for (uint32_t len = p.minLen; len <= p.maxLen; ++len) {
        uint64_t pow = 1;
        for (uint32_t i = 0; i < len; ++i) {
            if (pow > UINT64_MAX / p.charsetLen) {
                overflow = true;
                break;
            }
            pow *= p.charsetLen;
        }
        if (overflow) break;
        if (total > UINT64_MAX - pow) {
            overflow = true;
            break;
        }
        total += pow;
    }
    p.capped = overflow || total > (uint64_t)kMaxWords;
    p.candidates = p.capped ? kMaxWords : (uint32_t)total;
    p.planned = p.candidates;
    return true;
}

// Mask symbols: a=lower, A=upper, #=digit, *=printable; every other byte is literal, \ escapes.
bool buildPatternPlan(const char *spec, Plan &p) {
    p.mode = Mode::Pattern;
    if (spec == nullptr || spec[0] == '\0') {
        p.error = "Mask cannot be empty";
        return false;
    }
    size_t i = 0;
    uint8_t k = 0;
    while (spec[i] != '\0') {
        uint8_t setId = SET_LITERAL;
        char literal = 0;
        if (spec[i] == '\\') {
            if (spec[i + 1] == '\0') {
                p.error = "Invalid mask escape";
                return false;
            }
            literal = spec[i + 1];
            i += 2;
        } else if (spec[i] == 'a') {
            setId = SET_LOWER;
            ++i;
        } else if (spec[i] == 'A') {
            setId = SET_UPPER;
            ++i;
        } else if (spec[i] == '#') {
            setId = SET_DIGIT;
            ++i;
        } else if (spec[i] == '*') {
            setId = SET_PRINT;
            ++i;
        } else {
            literal = spec[i];
            ++i;
        }
        if (setId == SET_LITERAL) {
            const unsigned char c = (unsigned char)literal;
            if (c < 0x20 || c > 0x7E) {
                p.error = "Mask has invalid characters";
                return false;
            }
        }
        if (k >= kMaxWordLen) {
            p.error = "Mask is too long";
            return false;
        }
        p.mask[k] = literal;
        p.maskSet[k] = setId;
        ++k;
    }
    if (k < kMinWordLen) {
        p.error = "Mask length must be 8-63";
        return false;
    }
    p.maskLen = k;
    p.maxLineLen = k;

    uint64_t total = 1;
    bool overflow = false;
    for (uint8_t j = 0; j < k; ++j) {
        const uint32_t sz = p.maskSet[j] == SET_LITERAL ? 1 : setSize(p.maskSet[j]);
        if (total > UINT64_MAX / sz) {
            overflow = true;
            break;
        }
        total *= sz;
    }
    p.capped = overflow || total > (uint64_t)kMaxWords;
    p.candidates = p.capped ? kMaxWords : (uint32_t)total;
    p.planned = p.candidates;
    return true;
}

// "<word>[:<flags>]", flags is a subset of "aeiostbg"; no colon means every transform is on.
bool buildLeetPlan(const char *spec, Plan &p) {
    p.mode = Mode::Leet;
    if (spec == nullptr || spec[0] == '\0') {
        p.error = "Word cannot be empty";
        return false;
    }
    const char *colon = strchr(spec, ':');
    const size_t len = colon != nullptr ? (size_t)(colon - spec) : strlen(spec);
    if (len == 0) {
        p.error = "Word cannot be empty";
        return false;
    }
    if (len >= sizeof(p.word)) {
        p.error = "Word is too long";
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)spec[i];
        if (c < 0x20 || c > 0x7E) {
            p.error = "Word has invalid characters";
            return false;
        }
    }
    memcpy(p.word, spec, len);
    p.word[len] = '\0';
    p.wordLen = (uint8_t)len;

    uint8_t bits = 0xFF;
    if (colon != nullptr) {
        bits = 0;
        for (const char *f = colon + 1; *f != '\0'; ++f) {
            const char *pos = strchr(kLeetOrder, *f);
            if (pos == nullptr) {
                p.error = "Invalid leet flags";
                return false;
            }
            bits |= (uint8_t)(1u << (pos - kLeetOrder));
        }
    }
    p.leetBits = bits;

    uint8_t count = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if ((bits & (1u << i)) != 0 && memchr(p.word, kLeetOrder[i], len) != nullptr) {
            p.leetLetters[count++] = kLeetOrder[i];
        }
    }
    p.leetCount = count;
    p.maxLineLen = (uint8_t)len;

    const uint32_t combos = 1u << count; // <= 256
    p.candidates = combos;
    if (len < kMinWordLen) {
        // Substitution preserves length, so a short base word can never yield a valid WPA
        // passphrase. Refuse with a clear error instead of writing an empty dictionary.
        p.skipped = combos;
        p.planned = 0;
        p.error = "Word must be at least 8 characters";
        return false;
    }
    p.planned = combos;
    return true;
}

// "<start>-<end>[:<width>]", end <= 99999999, width 0/absent -> 8 and clamped up to 8.
bool buildNumberPlan(const char *spec, Plan &p) {
    p.mode = Mode::Number;
    if (spec == nullptr || spec[0] == '\0') {
        p.error = "Invalid number range";
        return false;
    }
    const char *dash = strchr(spec, '-');
    if (dash == nullptr || dash == spec) {
        p.error = "Invalid number range";
        return false;
    }
    uint32_t start = 0;
    uint32_t end = 0;
    if (!parseDecimal(spec, (size_t)(dash - spec), 99999999u, start)) {
        p.error = "Number must be 0-99999999";
        return false;
    }
    const char *endStr = dash + 1;
    const char *colon = strchr(endStr, ':');
    const size_t endLen = colon != nullptr ? (size_t)(colon - endStr) : strlen(endStr);
    if (!parseDecimal(endStr, endLen, 99999999u, end)) {
        p.error = "Number must be 0-99999999";
        return false;
    }
    if (start > end) {
        p.error = "Start must not exceed End";
        return false;
    }

    uint32_t width = 0;
    if (colon != nullptr) {
        if (!parseDecimal(colon + 1, strlen(colon + 1), 16u, width)) {
            p.error = "Pad width must be 1-16";
            return false;
        }
    }
    if (width < kMinWordLen) width = kMinWordLen; // documented clamp: always emit >= 8 chars
    p.numStart = start;
    p.numEnd = end;
    p.numWidth = (uint8_t)width;

    const uint64_t count = (uint64_t)end - start + 1ULL;
    p.capped = count > (uint64_t)kMaxWords;
    p.candidates = p.capped ? kMaxWords : (uint32_t)count;
    p.planned = p.candidates;
    p.maxLineLen = (uint8_t)width;
    return true;
}

bool buildPlan(const char *mode, const char *spec, Plan &plan) {
    Mode parsed = Mode::Charset;
    if (!parseMode(mode, parsed)) {
        plan.error = "Invalid mode";
        return false;
    }
    switch (parsed) {
        case Mode::Charset: return buildCharsetPlan(spec, plan);
        case Mode::Pattern: return buildPatternPlan(spec, plan);
        case Mode::Leet: return buildLeetPlan(spec, plan);
        case Mode::Number: return buildNumberPlan(spec, plan);
    }
    plan.error = "Invalid mode";
    return false;
}

void sanitizeName(const char *in, char *out, size_t cap) {
    size_t o = 0;
    if (in != nullptr) {
        for (const char *p = in; *p != '\0' && o < kMaxNameBytes && o + 1 < cap; ++p) {
            const char c = (char)tolower((unsigned char)*p);
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out[o++] = c;
        }
    }
    if (o == 0) {
        static const char kDefaultName[] = "wordlist";
        const size_t n = min(sizeof(kDefaultName) - 1, cap - 1);
        memcpy(out, kDefaultName, n);
        o = n;
    }
    out[o] = '\0';
}

bool buildPath(const char *name, char *out, size_t cap) {
    char base[kMaxNameBytes + 1];
    sanitizeName(name, base, sizeof(base));
    const int n = snprintf(out, cap, "%s/%s.txt", kDir, base);
    return n > 0 && (size_t)n < cap;
}

// FS itself does not expose totalBytes()/usedBytes() on this core; dispatch to the concrete fs.
uint64_t fsTotalBytes(FS *fs) {
    if (fs == &SD) return (uint64_t)SD.totalBytes();
    if (fs == &LittleFS) return (uint64_t)LittleFS.totalBytes();
    return 0;
}

uint64_t fsUsedBytes(FS *fs) {
    if (fs == &SD) return (uint64_t)SD.usedBytes();
    if (fs == &LittleFS) return (uint64_t)LittleFS.usedBytes();
    return 0;
}

bool prepare(const char *name, const char *mode, const char *spec, Prepared &out) {
    Plan plan;
    if (!buildPlan(mode, spec, plan)) {
        out.error = plan.error != nullptr ? plan.error : "Invalid spec";
        return false;
    }

    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) {
        out.error = "No filesystem available";
        return false;
    }

    uint32_t candidates = plan.candidates;
    uint32_t planned = plan.planned;
    bool capped = plan.capped;
    if (fs == &LittleFS) {
        // Keep LittleFS dictionaries under ~24 KiB regardless of the free-space value.
        const uint32_t perLine = (uint32_t)plan.maxLineLen + 1u;
        const uint32_t maxLines = (uint32_t)(kLittleFsBudget / perLine);
        if (maxLines == 0) {
            out.error = "Not enough space on storage";
            return false;
        }
        if (candidates > maxLines) {
            candidates = maxLines;
            capped = true;
        }
        if (planned > candidates) planned = candidates;
    }

    const uint64_t totalBytes = fsTotalBytes(fs);
    const uint64_t usedBytes = fsUsedBytes(fs);
    const uint64_t freeBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;
    const uint64_t estBytes = (uint64_t)candidates * ((uint64_t)plan.maxLineLen + 1ULL);
    if (freeBytes <= kReserveBytes || estBytes + kReserveBytes > freeBytes) {
        out.error = "Not enough space on storage";
        return false;
    }

    char path[64];
    if (!buildPath(name, path, sizeof(path))) {
        out.error = "Name is too long";
        return false;
    }

    out.plan = plan;
    out.fs = fs;
    memcpy(out.path, path, sizeof(path));
    out.candidates = candidates;
    out.planned = planned;
    out.skipped = plan.skipped;
    out.freeBytes = freeBytes;
    out.estBytes = estBytes;
    out.onSd = (fs == &SD);
    out.existed = fs->exists(path);
    out.capped = capped;
    return true;
}

// ── Generation ──────────────────────────────────────────────────────────────
struct GenerateState {
    File *file = nullptr;
    uint32_t done = 0;
    uint32_t candidates = 0;
    uint32_t written = 0;
    uint32_t skipped = 0;
    uint32_t bytes = 0;
    uint32_t nextCb = kFlushEvery;
    bool ioError = false;
    bool aborted = false;
    WordlistProgressCb cb = nullptr;
    void *cbCtx = nullptr;
    volatile bool *abortFlag = nullptr;
};

bool genStep(GenerateState &s) {
    ++s.done;
    if (s.abortFlag != nullptr && *s.abortFlag) {
        s.aborted = true;
        return false;
    }
    if (s.done >= s.nextCb) {
        s.nextCb = s.done + kFlushEvery;
        if (s.file != nullptr) {
            s.file->flush();
            if (s.file->getWriteError() != 0) {
                s.ioError = true;
                return false;
            }
        }
        if (s.cb != nullptr) s.cb(s.done, s.candidates, s.cbCtx);
        // The serial CLI path runs with cb == nullptr: yield here so a 100k-word run never
        // starves the UI loop or the task watchdog (same rule as the storage commands).
        vTaskDelay(1);
    }
    return true;
}

void genEmit(GenerateState &s, const char *word, size_t len) {
    if (len < kMinWordLen || len > kMaxWordLen) {
        ++s.skipped;
        return;
    }
    if (s.file != nullptr) {
        if (s.file->write((const uint8_t *)word, len) != len || s.file->write('\n') != 1) {
            s.ioError = true;
            return;
        }
    }
    ++s.written;
    s.bytes += (uint32_t)(len + 1);
}

void genCharset(GenerateState &s, const Plan &p) {
    char buf[64];
    uint8_t indexes[64];
    uint32_t remaining = s.candidates;

    for (uint32_t len = p.minLen; len <= p.maxLen && remaining > 0; ++len) {
        uint64_t combos = 1;
        bool overflow = false;
        for (uint32_t i = 0; i < len; ++i) {
            if (combos > UINT64_MAX / p.charsetLen) {
                overflow = true;
                break;
            }
            combos *= p.charsetLen;
        }
        const uint64_t limit = combos < (uint64_t)remaining ? combos : (uint64_t)remaining;
        const uint32_t emitCount = overflow ? remaining : (uint32_t)limit;

        memset(indexes, 0, sizeof(indexes));
        for (uint32_t n = 0; n < emitCount; ++n) {
            for (uint32_t i = 0; i < len; ++i) buf[i] = p.charset[indexes[i]];
            buf[len] = '\0';
            genEmit(s, buf, len);
            if (s.ioError) return;
            if (!genStep(s)) return;

            int pos = (int)len - 1;
            while (pos >= 0) {
                if (++indexes[pos] < p.charsetLen) break;
                indexes[pos] = 0;
                --pos;
            }
            if (pos < 0) break; // odometer exhausted
        }
        remaining -= emitCount;
    }
}

void genPattern(GenerateState &s, const Plan &p) {
    char buf[64];
    uint8_t positions[64] = {};

    for (uint32_t n = 0; n < s.candidates; ++n) {
        for (uint8_t i = 0; i < p.maskLen; ++i) {
            const uint8_t id = p.maskSet[i];
            buf[i] = (id == SET_LITERAL) ? p.mask[i] : setChars(id)[positions[i]];
        }
        buf[p.maskLen] = '\0';
        genEmit(s, buf, p.maskLen);
        if (s.ioError) return;
        if (!genStep(s)) return;

        int k = (int)p.maskLen - 1;
        while (k >= 0) {
            const uint8_t id = p.maskSet[k];
            const uint8_t sz = (id == SET_LITERAL) ? 1 : setSize(id);
            if (++positions[k] < sz) break;
            positions[k] = 0;
            --k;
        }
        if (k < 0) break; // odometer exhausted
    }
}

void genLeet(GenerateState &s, const Plan &p) {
    char buf[64];
    const uint32_t combos = 1u << p.leetCount;

    for (uint32_t m = 0; m < combos; ++m) {
        for (uint8_t i = 0; i < p.wordLen; ++i) {
            char c = p.word[i];
            for (uint8_t b = 0; b < p.leetCount; ++b) {
                if (c == p.leetLetters[b] && (m & (1u << b)) != 0) {
                    c = leetDigit(c);
                    break;
                }
            }
            buf[i] = c;
        }
        buf[p.wordLen] = '\0';
        genEmit(s, buf, p.wordLen);
        if (s.ioError) return;
        if (!genStep(s)) return;
    }
}

void genNumber(GenerateState &s, const Plan &p) {
    char buf[32];
    for (uint32_t n = 0; n < s.candidates; ++n) {
        const unsigned long value = (unsigned long)(p.numStart + n);
        const int len = snprintf(buf, sizeof(buf), "%0*lu", (int)p.numWidth, value);
        if (len <= 0 || len >= (int)sizeof(buf)) {
            s.ioError = true;
            return;
        }
        genEmit(s, buf, (size_t)len);
        if (s.ioError) return;
        if (!genStep(s)) return;
    }
}

void fillStats(const Prepared &prep, WordlistGenStats &st) {
    st = WordlistGenStats();
    st.planned = prep.planned;
    st.candidates = prep.candidates;
    st.skipped = prep.skipped;
    st.bytes = (uint32_t)prep.estBytes;
    st.freeBytes = prep.freeBytes;
    st.onSd = prep.onSd;
    st.capped = prep.capped;
    st.existed = prep.existed;
    st.error = prep.error;
    snprintf(st.path, sizeof(st.path), "%s", prep.path);
}

bool runPlan(const Prepared &prep, const WordlistGenOptions &opts, WordlistGenStats &st) {
    FS *fs = prep.fs;
    if (!fs->exists(kDir)) {
        if (!fs->mkdir(kDir)) {
            st.error = "Cannot create /wordlists folder";
            return false;
        }
    }

    // Write to a hidden temp file first and rename on success, so an abort or write error can
    // never destroy (or truncate) a previous /wordlists/<name>.txt the caller wanted to keep.
    char tmpPath[96];
    {
        const char *slash = strrchr(prep.path, '/');
        if (slash == nullptr || sizeof(tmpPath) <= (size_t)(slash - prep.path) + strlen(slash) + 2) {
            st.error = "Cannot create wordlist file";
            return false;
        }
        const size_t pre = (size_t)(slash - prep.path) + 1;
        memcpy(tmpPath, prep.path, pre);
        tmpPath[pre] = '.';
        strncpy(tmpPath + pre + 1, slash + 1, sizeof(tmpPath) - pre - 1);
        tmpPath[sizeof(tmpPath) - 1] = '\0';
        char *dot = strrchr(tmpPath, '.');
        if (dot == nullptr || strcmp(dot, ".txt") != 0) {
            st.error = "Cannot create wordlist file";
            return false;
        }
        strcpy(dot, ".tmp");
    }

    File file = fs->open(tmpPath, FILE_WRITE);
    if (!file) {
        st.error = "Cannot create wordlist file";
        return false;
    }

    GenerateState s;
    s.file = &file;
    s.candidates = prep.candidates;
    s.cb = opts.cb;
    s.cbCtx = opts.ctx;
    s.abortFlag = opts.abortFlag;
    if (s.cb != nullptr) s.cb(0, s.candidates, s.cbCtx);

    switch (prep.plan.mode) {
        case Mode::Charset: genCharset(s, prep.plan); break;
        case Mode::Pattern: genPattern(s, prep.plan); break;
        case Mode::Leet: genLeet(s, prep.plan); break;
        case Mode::Number: genNumber(s, prep.plan); break;
    }

    bool ioError = s.ioError;
    if (!ioError) {
        file.flush();
        if (file.getWriteError() != 0) ioError = true;
    }
    file.close();

    if (s.cb != nullptr) s.cb(s.done, s.candidates, s.cbCtx);

    const uint64_t totalBytes = fsTotalBytes(fs);
    const uint64_t usedBytes = fsUsedBytes(fs);
    st.written = s.written;
    st.skipped = s.skipped;
    st.bytes = s.bytes;
    st.freeBytes = totalBytes > usedBytes ? totalBytes - usedBytes : 0;

    if (s.aborted) {
        fs->remove(tmpPath);
        st.aborted = true;
        st.error = nullptr;
        NZLOGI("WLGEN", "aborted after %lu words, temp file removed", (unsigned long)s.written);
        return false;
    }
    if (ioError) {
        fs->remove(tmpPath);
        st.error = "Write error";
        NZLOGE("WLGEN", "write failed after %lu words", (unsigned long)s.written);
        return false;
    }

    if (!fs->rename(tmpPath, prep.path)) {
        fs->remove(tmpPath);
        st.error = "Write error";
        NZLOGE("WLGEN", "rename to %s failed", prep.path);
        return false;
    }

    NZLOGI(
        "WLGEN",
        "wrote %lu words (%lu bytes) to %s",
        (unsigned long)s.written,
        (unsigned long)s.bytes,
        prep.path
    );
    return true;
}

// ── TFT UI helpers (centralized Nezha Cyber UI kit) ──────────────────────────
const char *const kModeLabels[4] = {"Charset Brute", "Pattern Mask", "Leet Speak", "Number Range"};
const char *const kModeIds[4] = {"charset", "pattern", "leet", "number"};

// Per-letter transform picker. Returns false on cancel; bits keeps the selected transforms.
bool wgLeetPicker(const String &word, uint8_t &bits) {
    uint8_t rows[8];
    uint8_t count = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (word.indexOf(kLeetOrder[i]) >= 0) rows[count++] = i;
    }
    if (count == 0) {
        bits = 0;
        return true;
    }

    int cursor = 0;
    bool redraw = true;
    while (true) {
        if (returnToMenu) return false;
        if (check(EscPress)) return false;
        if (redraw) {
            drawMainBorderWithTitle("Leet Transforms");
            const int top = uiContentTop();
            const int availH = max(40, uiContentBottom(true) - top);
            const int rowH = min(22, max(15, availH / max((int)count, 1)));
            for (uint8_t r = 0; r < count; ++r) {
                const int ry = top + r * rowH;
                const bool selected = ((int)r == cursor);
                const bool on = (bits & (1u << rows[r])) != 0;
                const uint16_t rowBg = selected ? bruceConfig.priColor : uiSurfaceColor();
                const uint16_t fgCol =
                    selected ? uiTextOnAccentColor(bruceConfig.priColor) : uiTextBrightColor();
                tft.fillRoundRect(BORDER_PAD_X, ry, tftWidth - 2 * BORDER_PAD_X, rowH - 3, 5, rowBg);
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "%c = %c", kLeetOrder[rows[r]], leetDigit(kLeetOrder[rows[r]]));
                tft.setTextSize(FP);
                tft.setTextColor(fgCol, rowBg);
                const String onLbl = bruce_i18n::tr(on ? "ON" : "OFF");
                const int th = (FP == 1 && bruce_i18n::containsNonAscii(onLbl)) ? 12 : (LH * FP);
                tft.drawString(lbl, BORDER_PAD_X + 8, ry + (rowH - 3 - LH * FP) / 2, 1);
                tft.drawRightString(onLbl, tftWidth - BORDER_PAD_X - 8, ry + (rowH - 3 - th) / 2, 1);
            }
            printCenterFootnote("UP/DN Move  SEL Toggle  NEXT Start");
            redraw = false;
        }

        if (check(UpPress)) {
            cursor = (cursor + count - 1) % count;
            redraw = true;
        } else if (check(DownPress)) {
            cursor = (cursor + 1) % count;
            redraw = true;
        } else if (check(SelPress)) {
            bits ^= (uint8_t)(1u << rows[cursor]);
            redraw = true;
        } else if (check(NextPress)) {
            return true;
        } else if (check(PrevPress)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

struct WgProgress {
    volatile bool abort = false;
    uint32_t lastMs = 0;
    bool first = true;
};

void wgDrawProgressBase(const WordlistGenStats &st) {
    drawMainBorderWithTitle("Generating...");
    const uint16_t surf = uiSurfaceColor();
    const int x = BORDER_PAD_X;
    const int y = uiContentTop();
    const int w = tftWidth - 2 * BORDER_PAD_X;
    const int h = max(64, uiContentBottom(true) - y);
    uiDrawCard(x, y, w, h, true);
    uiDrawText16(st.path, x + 8, y + 8, w - 16, uiTextBrightColor(), surf, 0);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)st.planned);
    uiDrawKeyValueRow(x + 8, y + 28, w - 16, "Words", buf, bruceConfig.priColor, surf);
    uiDrawKeyValueRow(
        x + 8, y + 44, w - 16, "Storage", st.onSd ? bruce_i18n::tr("SD Card") : bruce_i18n::tr("LittleFS"),
        uiTextMutedColor(), surf
    );
    printCenterFootnote("ESC=Abort");
}

void wgDrawProgress(uint32_t done, uint32_t total) {
    const uint16_t surf = uiSurfaceColor();
    const int x = BORDER_PAD_X + 8;
    const int w = tftWidth - 2 * BORDER_PAD_X - 16;
    const int barY = uiContentTop() + 76;
    const int barH = 14;
    const int pct = total > 0 ? (int)(((uint64_t)done * 100ULL) / total) : 100;

    tft.fillRect(x, barY - 16, w, 15, surf);
    char buf[48];
    snprintf(buf, sizeof(buf), "%lu/%lu (%d%%)", (unsigned long)done, (unsigned long)total, pct);
    uiDrawKeyValueRow(x, barY - 15, w, "Progress", buf, bruceConfig.priColor, surf);
    uiDrawProgressBar(x, barY, w, barH, pct, bruceConfig.priColor, false);
}

void wgProgressCb(uint32_t done, uint32_t total, void *ctx) {
    WgProgress *p = static_cast<WgProgress *>(ctx);
    const uint32_t now = millis();
    if (p->first || done >= total || now - p->lastMs >= 120) {
        p->first = false;
        p->lastMs = now;
        wgDrawProgress(done, total);
    }
    if (check(EscPress) || returnToMenu) p->abort = true;
}

void wgDrawFinish(const WordlistGenStats &st) {
    drawMainBorderWithTitle("Wordlist Ready");
    const uint16_t surf = uiSurfaceColor();
    const uint16_t bright = uiTextBrightColor();
    const int x = BORDER_PAD_X;
    const int y = uiContentTop();
    const int w = tftWidth - 2 * BORDER_PAD_X;
    const int h = max(68, uiContentBottom(true) - y);
    uiDrawCard(x, y, w, h, true);

    uiDrawText16(st.path, x + 8, y + 8, w - 16, bright, surf, 0);

    const int rowStep = (FP == 1 && bruce_i18n::isChinese()) ? 15 : (LH * FP + 6);
    int ly = y + 30;
    char buf[64];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)st.written);
    uiDrawKeyValueRow(x + 8, ly, w - 16, "Words", buf, bruceConfig.priColor, surf);
    ly += rowStep;
    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)((st.bytes + 1023u) / 1024u));
    uiDrawKeyValueRow(x + 8, ly, w - 16, "File size", buf, bright, surf);
    ly += rowStep;
    snprintf(buf, sizeof(buf), "%lu KB", (unsigned long)(st.freeBytes / 1024ULL));
    uiDrawKeyValueRow(x + 8, ly, w - 16, "Free space", buf, bright, surf);
    ly += rowStep;
    uiDrawKeyValueRow(
        x + 8, ly, w - 16, "Storage", st.onSd ? bruce_i18n::tr("SD Card") : bruce_i18n::tr("LittleFS"),
        bright, surf
    );

    printCenterFootnote("Press any key");
}

} // namespace

// ── Public engine API ───────────────────────────────────────────────────────
bool wordlist_gen_generate_ex(const WordlistGenOptions &opts) {
    WordlistGenStats scratch;
    WordlistGenStats &st = opts.stats != nullptr ? *opts.stats : scratch;
    st = WordlistGenStats();

    Prepared prep;
    if (!prepare(opts.name, opts.mode, opts.spec, prep)) {
        st.error = prep.error != nullptr ? prep.error : "Invalid spec";
        return false;
    }
    fillStats(prep, st);
    if (opts.dryRun) return true;
    return runPlan(prep, opts, st);
}

bool wordlist_gen_generate(
    const char *name, const char *mode, const char *spec, WordlistProgressCb cb, void *ctx
) {
    WordlistGenOptions opts;
    opts.name = name;
    opts.mode = mode;
    opts.spec = spec;
    opts.cb = cb;
    opts.ctx = ctx;
    return wordlist_gen_generate_ex(opts);
}

// ── Menu / TFT entry point ──────────────────────────────────────────────────
void wordlist_gen_run() {
    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) {
        displayError("No filesystem available", true);
        return;
    }

    check(EscPress);
    check(SelPress);
    check(NextPress);
    check(PrevPress);
    check(UpPress);
    check(DownPress);
    if (returnToMenu) return;

    int modeIdx = -1;
    std::vector<Option> modeOpts;
    modeOpts.reserve(5);
    for (int i = 0; i < 4; i++) {
        modeOpts.push_back({kModeLabels[i], [&modeIdx, i]() { modeIdx = i; }});
    }
    modeOpts.push_back({"Back", [&modeIdx]() { modeIdx = -1; }});
    loopOptions(modeOpts, MENU_TYPE_SUBMENU, "Wordlist Generator");
    if (modeIdx < 0 || returnToMenu) return;

    String spec;
    if (modeIdx == 0) {
        String charset = keyboard("abc123", 32, "Charset (e.g. abc123)");
        if (charset == "\x1B" || charset.length() == 0) return;
        charset.replace(":", ""); // ':' is the spec separator
        if (charset.length() == 0) {
            displayError("Charset cannot be empty", true);
            return;
        }
        String minS = num_keyboard("8", 2, "Min Length");
        if (minS == "\x1B" || minS.length() == 0) return;
        String maxS = num_keyboard("12", 2, "Max Length");
        if (maxS == "\x1B" || maxS.length() == 0) return;
        spec = charset + ":" + minS + ":" + maxS;
    } else if (modeIdx == 1) {
        String mask = keyboard("Aa#12345", 63, "Pattern (a=lower A=upper #=digit *=any)");
        if (mask == "\x1B" || mask.length() == 0) return;
        spec = mask;
    } else if (modeIdx == 2) {
        String word = keyboard("password", 32, "Base Word");
        if (word == "\x1B" || word.length() == 0) return;
        word.replace(":", ""); // ':' is the spec separator
        if (word.length() == 0) return;
        uint8_t bits = 0xFF;
        if (!wgLeetPicker(word, bits)) return;
        spec = word;
        spec += ':';
        for (uint8_t i = 0; i < 8; ++i) {
            if ((bits & (1u << i)) != 0) spec += kLeetOrder[i];
        }
    } else {
        String startS = num_keyboard("1", 8, "Start Number");
        if (startS == "\x1B" || startS.length() == 0) return;
        String endS = num_keyboard("99999999", 8, "End Number");
        if (endS == "\x1B" || endS.length() == 0) return;
        String widthS = num_keyboard("8", 2, "Pad Width (0=auto)");
        if (widthS == "\x1B") return;
        spec = startS + "-" + endS;
        if (widthS.length() > 0 && widthS != "0") spec += ":" + widthS;
    }

    String name = keyboard("wordlist", 32, "File Name");
    if (name == "\x1B") return;
    if (name.length() == 0) name = "wordlist";

    WordlistGenStats plan;
    WordlistGenOptions dry;
    dry.name = name.c_str();
    dry.mode = kModeIds[modeIdx];
    dry.spec = spec.c_str();
    dry.stats = &plan;
    dry.dryRun = true;
    if (!wordlist_gen_generate_ex(dry)) {
        displayError(String(plan.error != nullptr ? plan.error : "Invalid parameters"), true);
        return;
    }

    if (plan.existed) {
        if (displayMessage("File exists. Overwrite?", "Overwrite", nullptr, "Cancel", bruceConfig.priColor) !=
            0)
            return;
    }

    String summary = String(bruce_i18n::tr("Generate wordlist?")) + "\n";
    summary += String(bruce_i18n::tr("Words")) + ": " + String(plan.planned) + "\n";
    summary +=
        String(bruce_i18n::tr("Estimated size")) + ": " + String((plan.bytes + 1023u) / 1024u) + " KB\n";
    summary += plan.path;
    if (plan.capped) summary += String("\n") + bruce_i18n::tr("Capped at the maximum word count");
    if (displayMessage(summary.c_str(), "Generate", nullptr, "Cancel", bruceConfig.priColor) != 0) return;

    WgProgress progress;
    wgDrawProgressBase(plan);

    WordlistGenStats result;
    WordlistGenOptions run;
    run.name = name.c_str();
    run.mode = kModeIds[modeIdx];
    run.spec = spec.c_str();
    run.cb = wgProgressCb;
    run.ctx = &progress;
    run.stats = &result;
    run.abortFlag = &progress.abort;
    const bool ok = wordlist_gen_generate_ex(run);

    if (result.aborted) {
        displayInfo("Aborted. Partial file deleted.", true);
        return;
    }
    if (!ok) {
        displayError(String(result.error != nullptr ? result.error : "Generation failed"), true);
        return;
    }

    wgDrawFinish(result);
    while (!check(AnyKeyPress) && !returnToMenu) vTaskDelay(pdMS_TO_TICKS(20));
}
#endif
