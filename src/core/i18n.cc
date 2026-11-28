#include "i18n.h"
#include "lang_zh_cn.h"
#include <cstring>
#include <strings.h>

namespace bruce_i18n {

// Simplified Chinese is the default firmware language.
static Language s_currentLanguage = Language::ZH_CN;

Language getLanguage() { return s_currentLanguage; }

bool isChinese() { return s_currentLanguage == Language::ZH_CN; }

void setLanguage(Language lang) { s_currentLanguage = lang; }

String normalizeLanguageCode(const String &code) {
    String lower = code;
    lower.trim();
    lower.toLowerCase();
    if (lower == "en" || lower == "en-us" || lower == "en-gb" || lower == "english") {
        return "en";
    }
    return "zh-CN";
}

void setLanguage(const String &code) {
    s_currentLanguage = (normalizeLanguageCode(code) == "en") ? Language::EN : Language::ZH_CN;
}

const char *languageCode(Language lang) {
    return (lang == Language::EN) ? "en" : "zh-CN";
}

static const char *lookupExactZh(const char *en) {
    if (!en || !*en) return nullptr;
    size_t lo = 0;
    size_t hi = kZhCnTableSize;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(kZhCnTable[mid].en, en);
        if (cmp == 0) return kZhCnTable[mid].zh;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    for (size_t i = 0; i < kZhCnTableSize; ++i) {
        if (strcasecmp(kZhCnTable[i].en, en) == 0) return kZhCnTable[i].zh;
    }
    return nullptr;
}

const char *tr(const char *en) {
    if (!en || s_currentLanguage == Language::EN) return en;
    const char *hit = lookupExactZh(en);
    return hit ? hit : en;
}

String tr(const String &en) {
    if (en.isEmpty() || s_currentLanguage == Language::EN) return en;

    const char *exact = lookupExactZh(en.c_str());
    if (exact) return String(exact);

    String trimmed = en;
    trimmed.trim();
    if (trimmed.length() != en.length() && !trimmed.isEmpty()) {
        String trTrimmed = tr(trimmed);
        if (trTrimmed != trimmed) {
            size_t lead = 0;
            while (lead < en.length() && (en[lead] == ' ' || en[lead] == '\t')) ++lead;
            size_t trail = en.length();
            while (trail > lead && (en[trail - 1] == ' ' || en[trail - 1] == '\t')) --trail;
            return en.substring(0, lead) + trTrimmed + en.substring(trail);
        }
    }

    int arrow = en.indexOf(" > ");
    if (arrow > 0) {
        String left = en.substring(0, arrow);
        String right = en.substring(arrow + 3);
        String trLeft = tr(left);
        String trRight = tr(right);
        if (trLeft != left || trRight != right) {
            return trLeft + " > " + trRight;
        }
    }

    int sep = en.indexOf(": ");
    if (sep > 0) {
        String key = en.substring(0, sep);
        const char *trKey = lookupExactZh(key.c_str());
        if (trKey) {
            String val = en.substring(sep + 2);
            const char *trVal = lookupExactZh(val.c_str());
            if (trVal) return String(trKey) + ": " + String(trVal);
            int paren = val.indexOf(" (");
            if (paren > 0) {
                String valHead = val.substring(0, paren);
                const char *trHead = lookupExactZh(valHead.c_str());
                if (trHead) return String(trKey) + ": " + String(trHead) + val.substring(paren);
            }
            return String(trKey) + ": " + val;
        }
    }

    if (en.endsWith(":") && en.length() > 1) {
        String key = en.substring(0, en.length() - 1);
        const char *trKey = lookupExactZh(key.c_str());
        if (trKey) {
            return String(trKey) + ":";
        }
    }

    // Handle parenthesized suffix pattern: "Head (Sub)" -> "中文 (中文/Sub)"
    int openParen = en.indexOf(" (");
    if (openParen > 0 && en.endsWith(")")) {
        String head = en.substring(0, openParen);
        String inner = en.substring(openParen + 2, en.length() - 1);
        const char *trHead = lookupExactZh(head.c_str());
        const char *trInner = lookupExactZh(inner.c_str());
        if (trHead || trInner) {
            return String(trHead ? trHead : head.c_str()) + " (" + String(trInner ? trInner : inner.c_str()) + ")";
        }
    }

    // Handle "A / B" composite labels when both sides have translations
    int slash = en.indexOf(" / ");
    if (slash > 0) {
        String left = en.substring(0, slash);
        String right = en.substring(slash + 3);
        const char *trL = lookupExactZh(left.c_str());
        const char *trR = lookupExactZh(right.c_str());
        if (trL && trR) {
            return String(trL) + " / " + String(trR);
        }
    }

    return en;
}

bool containsNonAscii(const char *s) {
    if (!s) return false;
    while (*s) {
        if (static_cast<unsigned char>(*s) >= 0x80) return true;
        ++s;
    }
    return false;
}

bool containsNonAscii(const String &s) { return containsNonAscii(s.c_str()); }

uint32_t utf8NextCodepoint(const char *&p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (c == 0) return 0;
    if (c < 0x80) {
        ++p;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && p[1] != 0) {
        uint32_t cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(p[1]) & 0x3F);
        p += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
        uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(p[1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(p[2]) & 0x3F);
        p += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
        uint32_t cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(p[1]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(p[2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(p[3]) & 0x3F);
        p += 4;
        return cp;
    }
    ++p;
    return '?';
}

bool isWideCodepoint(uint32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x3000 && cp <= 0x303F);
}

int16_t glyphAdvancePx(uint32_t cp, uint8_t textSize) {
    if (textSize < 1) textSize = 1;
    if (cp < 0x80) {
        return static_cast<int16_t>(6 * textSize);
    }
    if (textSize == 1) return 12;
    if (textSize == 2) return 16;
    return static_cast<int16_t>(8 * textSize);
}

int16_t utf8PixelWidth(const char *s, uint8_t textSize) {
    if (!s || !*s) return 0;
    if (textSize < 1) textSize = 1;
    int16_t total = 0;
    const char *p = s;
    while (*p) {
        uint32_t cp = utf8NextCodepoint(p);
        if (cp == '\n' || cp == '\r') continue;
        total += glyphAdvancePx(cp, textSize);
    }
    return total;
}

int16_t utf8PixelWidth(const String &s, uint8_t textSize) { return utf8PixelWidth(s.c_str(), textSize); }

int16_t utf8LineHeight(uint8_t textSize) {
    if (textSize < 1) textSize = 1;
    return static_cast<int16_t>(8 * textSize);
}

String utf8TruncateToWidth(const String &s, int16_t maxPx, uint8_t textSize) {
    if (maxPx <= 0 || s.isEmpty()) return "";
    const char *start = s.c_str();
    const char *p = start;
    int16_t width = 0;
    while (*p) {
        const char *prev = p;
        uint32_t cp = utf8NextCodepoint(p);
        int16_t adv = glyphAdvancePx(cp, textSize);
        if (width + adv > maxPx) {
            return s.substring(0, static_cast<unsigned int>(prev - start));
        }
        width += adv;
    }
    return s;
}

std::vector<String> utf8WrapToWidth(const String &s, int16_t maxPx, uint8_t textSize) {
    std::vector<String> lines;
    if (maxPx <= 0 || s.isEmpty()) return lines;

    const char *base = s.c_str();
    const char *lineStart = base;
    const char *p = base;
    const char *lastBreakPtr = nullptr;
    bool lastBreakSkipChar = false;
    int16_t curWidth = 0;

    while (*p) {
        if (*p == '\n') {
            lines.push_back(s.substring(lineStart - base, p - base));
            ++p;
            lineStart = p;
            lastBreakPtr = nullptr;
            curWidth = 0;
            continue;
        }
        if (*p == '\r') {
            ++p;
            continue;
        }

        const char *charStart = p;
        uint32_t cp = utf8NextCodepoint(p);
        int16_t adv = glyphAdvancePx(cp, textSize);

        if (cp == ' ' || cp == '-' || cp == '_') {
            lastBreakPtr = charStart;
            lastBreakSkipChar = (cp == ' ');
        }

        if (curWidth + adv > maxPx && charStart > lineStart) {
            if (lastBreakPtr && lastBreakPtr > lineStart) {
                lines.push_back(s.substring(lineStart - base, lastBreakPtr - base));
                lineStart = lastBreakSkipChar ? (lastBreakPtr + 1) : lastBreakPtr;
                p = lineStart;
                lastBreakPtr = nullptr;
                curWidth = 0;
                continue;
            }
            lines.push_back(s.substring(lineStart - base, charStart - base));
            lineStart = charStart;
            lastBreakPtr = nullptr;
            curWidth = adv;
            continue;
        }

        curWidth += adv;
    }

    if (lineStart < p || lines.empty()) {
        lines.push_back(s.substring(lineStart - base, p - base));
    }
    return lines;
}

size_t utf8SafeCutBytes(const char *s, size_t len, size_t maxBytes) {
    if (!s || len <= maxBytes) return len;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return cut;
}

} // namespace bruce_i18n
