#include "unity_lite.h"
#include "core/cjk_font_data.h"
#include "core/lang_zh_cn.h"

#include <cstring>

using namespace bruce_i18n;

static const char *bsearchZh(const char *en) {
    size_t lo = 0;
    size_t hi = kZhCnTableSize;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = std::strcmp(kZhCnTable[mid].en, en);
        if (cmp == 0) return kZhCnTable[mid].zh;
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

static bool hasGlyph(uint16_t cp) {
    size_t lo = 0;
    size_t hi = kCjkGlyphCount;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (kCjkGlyphs[mid].codepoint == cp) return true;
        if (kCjkGlyphs[mid].codepoint < cp) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

static uint32_t nextUtf8Codepoint(const char *&p) {
    unsigned char c = static_cast<unsigned char>(*p++);
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && p[0]) {
        uint32_t cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && p[0] && p[1]) {
        uint32_t cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(p[0]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(p[1]) & 0x3F);
        p += 2;
        return cp;
    }
    return 0;
}

TEST_CASE(zh_table_is_strictly_ascii_sorted) {
    ASSERT_TRUE(kZhCnTableSize > 100);
    for (size_t i = 1; i < kZhCnTableSize; ++i) {
        int cmp = std::strcmp(kZhCnTable[i - 1].en, kZhCnTable[i].en);
        ASSERT_TRUE(cmp < 0);
    }
}

TEST_CASE(zh_bsearch_finds_every_table_entry) {
    for (size_t i = 0; i < kZhCnTableSize; ++i) {
        const char *found = bsearchZh(kZhCnTable[i].en);
        ASSERT_NE(found, nullptr);
        ASSERT_STREQ(found, kZhCnTable[i].zh);
    }
    ASSERT_EQ(bsearchZh("__NON_EXISTENT_NEZHA_KEY__"), nullptr);
    ASSERT_EQ(bsearchZh(""), nullptr);
}

TEST_CASE(cjk_glyph_table_is_strictly_sorted) {
    ASSERT_TRUE(kCjkGlyphCount > 100);
    for (size_t i = 1; i < kCjkGlyphCount; ++i) {
        ASSERT_TRUE(kCjkGlyphs[i - 1].codepoint < kCjkGlyphs[i].codepoint);
    }
}

TEST_CASE(every_non_ascii_char_in_zh_table_has_rasterized_glyph) {
    for (size_t i = 0; i < kZhCnTableSize; ++i) {
        const char *p = kZhCnTable[i].zh;
        while (*p != '\0') {
            uint32_t cp = nextUtf8Codepoint(p);
            if (cp >= 0x80 && cp <= 0xFFFF) {
                ASSERT_TRUE(hasGlyph(static_cast<uint16_t>(cp)));
            }
        }
    }
}

int main() { return nezha_test::runAll("i18n & CJK PROGMEM Tables"); }
