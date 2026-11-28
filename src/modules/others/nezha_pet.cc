#if !defined(LITE_VERSION)
#include "nezha_pet.h"

#include "audio.h"
#include "core/display.h"
#include "core/i18n.h"
#include "core/mykeyboard.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include <Preferences.h>
#include <WiFi.h>
#include <cmath>
#include <globals.h>

namespace {

/*
 * Persistent pet state stored in the NVS namespace "nezha_pet".
 * Holds level/xp plus the spirit, mood, and bond meters, plus counters that
 * survive reboots and are written back through Preferences.
 */
struct NezhaPetState {
    uint16_t level = 1;
    uint16_t xp = 0;
    uint8_t spirit = 85; // Lotus Spirit / Energy (0..100)
    uint8_t mood = 85;   // Happiness (0..100)
    uint8_t bond = 20;   // Bond with operator (0..100)
    uint16_t patrols = 0;
    uint16_t rfNodes = 0;
};

enum PetAction : uint8_t {
    ACT_FEED = 0,
    ACT_TRAIN = 1,
    ACT_PET = 2,
    ACT_PATROL = 3,
    ACT_REST = 4,
    ACT_COUNT = 5
};

enum PetAnimMode : uint8_t {
    ANIM_IDLE = 0,
    ANIM_FEED,
    ANIM_TRAIN,
    ANIM_PET,
    ANIM_PATROL,
    ANIM_REST,
    ANIM_LEVELUP
};

// Palette tailored to 哪吒魔童 (Nezha Demon Child)
constexpr uint16_t COL_CRIMSON = 0xF105;    // Vivid Nezha crimson ribbon & outline
constexpr uint16_t COL_FLAME = 0xFB20;      // Fiery orange-red
constexpr uint16_t COL_GOLD = 0xFEA0;       // Qiankun ring & Wind-Fire gold
constexpr uint16_t COL_GOLD_DIM = 0xB420;   // Burnished gold shade
constexpr uint16_t COL_LOTUS = 0xF4D6;      // Lotus pink-coral
constexpr uint16_t COL_SKIN = 0xFED6;       // Warm chibi skin tone
constexpr uint16_t COL_SKIN_SHD = 0xDD30;   // Soft facial contour shade
constexpr uint16_t COL_HAIR = 0x18C4;       // Deep ink-black hair
constexpr uint16_t COL_HAIR_HI = 0x422B;    // Glossy hair highlight
constexpr uint16_t COL_ARMOR = 0x2966;      // Dark demon-child armor tunic
constexpr uint16_t COL_BLUSH = 0xFB52;      // Rosy cheek blush
constexpr uint16_t COL_ELIXIR = 0x2F54;     // Jade lotus elixir green
constexpr uint16_t COL_SPIRIT_BAR = 0x36BF; // Cyan-blue spirit bar
constexpr uint16_t COL_MOOD_BAR = 0xF9E8;   // Warm crimson-coral mood bar

static uint16_t xpForNextLevel(uint16_t level) {
    return (uint16_t)(60 + (uint32_t)(level > 0 ? level - 1 : 0) * 35);
}

static uint8_t getRealmIndex(uint16_t level) {
    if (level >= 10) return 3;
    if (level >= 7) return 2;
    if (level >= 4) return 1;
    return 0;
}

static const char *getRealmKey(uint16_t level) {
    switch (getRealmIndex(level)) {
        case 3: return "Cyber Nezha";
        case 2: return "Wind-Fire";
        case 1: return "Lotus Form";
        default: return "Demon Orb";
    }
}

static const char *kActionKeys[ACT_COUNT] = {"Feed", "Train", "Pet", "Patrol", "Rest"};

static void loadPetState(NezhaPetState &st) {
    Preferences prefs;
    if (!prefs.begin("nezha_pet", true)) return;
    st.level = constrain((int)prefs.getUShort("lv", 1), 1, 99);
    uint16_t maxXp = xpForNextLevel(st.level);
    st.xp = (uint16_t)constrain((int)prefs.getUShort("xp", 0), 0, (int)maxXp - 1);
    st.spirit = (uint8_t)constrain((int)prefs.getUChar("sp", 85), 0, 100);
    st.mood = (uint8_t)constrain((int)prefs.getUChar("md", 85), 0, 100);
    st.bond = (uint8_t)constrain((int)prefs.getUChar("bd", 20), 0, 100);
    st.patrols = prefs.getUShort("pt", 0);
    st.rfNodes = prefs.getUShort("rf", 0);
    prefs.end();
}

static void savePetState(const NezhaPetState &st) {
    Preferences prefs;
    if (!prefs.begin("nezha_pet", false)) return;
    prefs.putUShort("lv", st.level);
    prefs.putUShort("xp", st.xp);
    prefs.putUChar("sp", st.spirit);
    prefs.putUChar("md", st.mood);
    prefs.putUChar("bd", st.bond);
    prefs.putUShort("pt", st.patrols);
    prefs.putUShort("rf", st.rfNodes);
    prefs.end();
}

static void petTone(unsigned int freq, unsigned long ms) {
    if (!bruceConfig.soundEnabled) return;
    _tone(freq, ms);
}

static bool grantXp(NezhaPetState &st, uint16_t amount) {
    bool leveledUp = false;
    uint32_t total = (uint32_t)st.xp + amount;
    while (st.level < 99) {
        uint16_t need = xpForNextLevel(st.level);
        if (total < need) break;
        total -= need;
        st.level++;
        st.spirit = 100;
        st.mood = 100;
        st.bond = (uint8_t)min(100, (int)st.bond + 5);
        leveledUp = true;
    }
    st.xp = (st.level >= 99) ? xpForNextLevel(99) : (uint16_t)total;
    return leveledUp;
}

static String idleCaption(const NezhaPetState &st, uint8_t tick) {
    if (st.spirit < 22) {
        return bruce_i18n::tr("Low Spirit! Feed or Rest");
    }
    if (st.mood < 30) {
        return bruce_i18n::tr("Bored... Train or Pet me!");
    }
    static const char *kQuotes[] = {
        "My fate is mine!",
        "Wind-Fire Wheels ready!",
        "Tap Patrol to sense RF!",
        "Demon Child on guard!",
        "Lotus fire burning bright!"
    };
    return bruce_i18n::tr(kQuotes[(tick / 20) % 5]);
}

/*
 * Templated vector rendering of the Nezha mascot.
 * Works against either a Sprite or the TFT so the scene can be composed
 * off-screen and pushed without tearing; begins with the heart glyph helper.
 */
template <typename Canvas>
static void drawHeartOn(Canvas &c, int cx, int cy, int r, uint16_t col) {
    c.fillCircle(cx - r, cy - r / 2, r, col);
    c.fillCircle(cx + r, cy - r / 2, r, col);
    c.fillTriangle(cx - r * 2, cy - r / 3, cx + r * 2, cy - r / 3, cx, cy + r * 2, col);
}

template <typename Canvas>
static void drawWheelOn(Canvas &c, int cx, int cy, int r, float angle, uint16_t bg) {
    c.drawCircle(cx, cy, r, COL_FLAME);
    if (r >= 5) c.drawCircle(cx, cy, r - 1, COL_GOLD);
    for (int i = 0; i < 4; i++) {
        float a = angle + i * 1.5708f;
        int x1 = cx + (int)roundf(cosf(a) * (r - 1));
        int y1 = cy + (int)roundf(sinf(a) * (r - 1));
        c.drawLine(cx, cy, x1, y1, COL_GOLD);
        int fx = cx + (int)roundf(cosf(a + 0.35f) * (r + 2));
        int fy = cy + (int)roundf(sinf(a + 0.35f) * (r + 2));
        c.fillCircle(fx, fy, 1, COL_FLAME);
    }
    c.fillCircle(cx, cy, max(1, r / 3), bg);
}

template <typename Canvas>
static void renderNezhaScene(
    Canvas &c, int ox, int oy, int sw, int sh,
    const NezhaPetState &st, PetAnimMode mode, uint8_t frame
) {
    uint16_t stageBg = uiSurfaceColor();
    uint16_t pri = bruceConfig.priColor;
    uint16_t sec = bruceConfig.secColor;
    uint16_t borderCol = (mode == ANIM_LEVELUP) ? COL_GOLD : uiBorderDimColor();

    c.fillRect(ox, oy, sw, sh, bruceConfig.bgColor);
    c.fillRoundRect(ox, oy, sw, sh, 7, stageBg);
    c.drawRoundRect(ox, oy, sw, sh, 7, borderCol);

    const float sc = (sw < 110) ? 0.68f : 1.05f;
    const auto S = [sc](float v) -> int { return (int)roundf(v * sc); };

    float phase = frame * 0.42f;
    int bob = (mode == ANIM_REST) ? (int)roundf(sinf(phase * 0.5f) * 1.5f)
            : (mode == ANIM_TRAIN || mode == ANIM_LEVELUP) ? (int)roundf(sinf(phase * 1.5f) * 2.5f)
            : (int)roundf(sinf(phase) * 2.0f);

    int cx = ox + sw / 2;
    int cy = oy + sh / 2 + S(3) + bob;
    uint8_t realm = getRealmIndex(st.level);

    // 1. Evolution Aura / Backdrop FX
    if (mode == ANIM_PATROL) {
        int r1 = S(18) + ((frame * 4) % S(36));
        int r2 = S(18) + (((frame * 4) + S(18)) % S(36));
        if (r1 < sw / 2 - 4) c.drawCircle(cx, cy - S(4), r1, blendColors(stageBg, pri, 190));
        if (r2 < sw / 2 - 4) c.drawCircle(cx, cy - S(4), r2, blendColors(stageBg, COL_SPIRIT_BAR, 175));
    } else if (mode == ANIM_LEVELUP) {
        for (int i = 0; i < 8; i++) {
            float a = phase * 0.6f + i * 0.7854f;
            int x1 = cx + (int)roundf(cosf(a) * S(24));
            int y1 = cy - S(4) + (int)roundf(sinf(a) * S(24));
            int x2 = cx + (int)roundf(cosf(a) * S(46));
            int y2 = cy - S(4) + (int)roundf(sinf(a) * S(46));
            c.drawLine(x1, y1, x2, y2, COL_GOLD);
        }
    } else if (realm >= 3) {
        // Lv 10+ Cyber Nezha: Qiankun Halo behind head
        int hr = S(36);
        c.drawCircle(cx, cy - S(5), hr, blendColors(stageBg, COL_GOLD, 165));
        for (int i = 0; i < 4; i++) {
            float a = phase * 0.35f + i * 1.5708f;
            int gx = cx + (int)roundf(cosf(a) * hr);
            int gy = cy - S(5) + (int)roundf(sinf(a) * hr);
            c.fillCircle(gx, gy, max(1, S(2)), COL_GOLD);
        }
    } else {
        // Subtle 哪吒魔童 crimson-gold emblem halo
        c.drawCircle(cx, cy - S(4), S(33), blendColors(stageBg, COL_CRIMSON, (realm >= 1) ? 105 : 65));
    }

    // Floating cyan spirit sparks (matching the 哪吒魔童 emblem artwork!)
    int spY = (int)roundf(sinf(phase * 1.1f) * S(3));
    c.fillCircle(cx - S(34), cy - S(16) + spY, max(1, S(1)), COL_SPIRIT_BAR);
    c.fillCircle(cx + S(35), cy - S(14) - spY, max(1, S(1)), COL_SPIRIT_BAR);
    c.fillCircle(cx - S(30), cy + S(6) - spY, 1, COL_GOLD);
    c.fillCircle(cx + S(31), cy + S(8) + spY, 1, COL_GOLD);

    // 2. Evolution Lv 4+ (or Rest mode): Glowing Lotus Pedestal beneath Nezha
    if (realm >= 1 || mode == ANIM_REST) {
        int ly = oy + sh - S(13);
        uint16_t lp1 = blendColors(stageBg, COL_LOTUS, 200);
        uint16_t lp2 = blendColors(stageBg, COL_LOTUS, 135);
        c.fillTriangle(cx - S(28), ly, cx - S(13), ly - S(8), cx - S(4), ly + S(3), lp2);
        c.fillTriangle(cx + S(28), ly, cx + S(13), ly - S(8), cx + S(4), ly + S(3), lp2);
        c.fillTriangle(cx - S(15), ly + S(2), cx, ly - S(10), cx + S(15), ly + S(2), lp1);
        c.fillRoundRect(cx - S(19), ly, S(38), S(5), 2, COL_GOLD);
    }

    // 3. Fluttering Crimson-Gold Hun Tian Ling Ribbons (混天绫)
    int ribWave = (int)roundf(sinf(phase * 1.15f) * S(5));
    int ribY = cy + S(14);
    for (int t = 0; t < max(2, S(3)); t++) {
        uint16_t rc = (t == 0) ? COL_GOLD : ((t == 1) ? COL_FLAME : COL_CRIMSON);
        // Left sweeping ribbon + flame tail
        c.drawLine(cx - S(13), ribY + t, cx - S(30), ribY - S(6) + ribWave + t, rc);
        c.drawLine(cx - S(30), ribY - S(6) + ribWave + t, cx - S(44), ribY - S(14) - ribWave / 2 + t, COL_CRIMSON);
        // Right sweeping ribbon + flame tail
        c.drawLine(cx + S(13), ribY + t, cx + S(30), ribY - S(6) - ribWave + t, rc);
        c.drawLine(cx + S(30), ribY - S(6) - ribWave + t, cx + S(44), ribY - S(14) + ribWave / 2 + t, COL_CRIMSON);
    }

    // 4. Evolution Lv 7+ (or Train mode): Spinning Wind-Fire Wheels (风火轮)
    if (realm >= 2 || mode == ANIM_TRAIN) {
        int wy = cy + S(24);
        int wr = max(4, S(8));
        drawWheelOn(c, cx - S(29), wy, wr, phase * 2.0f, stageBg);
        drawWheelOn(c, cx + S(29), wy, wr, -phase * 2.0f, stageBg);
    }

    // 5. Chibi Armor Vest + Crossed Golden Qiankun Ring Collar (乾坤圈)
    int bodyY = cy + S(14);
    c.fillRoundRect(cx - S(15), bodyY, S(30), S(16), S(5), COL_ARMOR);
    c.drawRoundRect(cx - S(15), bodyY, S(30), S(16), S(5), COL_CRIMSON);
    // Crimson inner tunic trim & golden lotus crest
    c.fillTriangle(cx - S(5), bodyY + S(11), cx, bodyY + S(5), cx + S(5), bodyY + S(11), COL_CRIMSON);
    c.fillTriangle(cx - S(2), bodyY + S(10), cx, bodyY + S(7), cx + S(2), bodyY + S(10), COL_GOLD);
    // Signature crossed golden Qiankun Rings wrapping shoulders
    c.drawRoundRect(cx - S(14), bodyY - S(2), S(28), S(6), 3, COL_GOLD);
    c.drawLine(cx - S(22), bodyY + S(8), cx + S(22), bodyY + S(1), COL_GOLD);
    c.drawLine(cx - S(22), bodyY + S(9), cx + S(22), bodyY + S(2), COL_GOLD_DIM);
    c.drawLine(cx - S(22), bodyY + S(1), cx + S(22), bodyY + S(8), COL_GOLD);

    // 6. Twin Hair Buns (双丸子头) + Red Ties + Flowing Ribbon Tails
    int headY = cy - S(7);
    int headR = S(20);
    int bunOffX = S(18);
    int bunOffY = S(16);
    int bunR = S(9);
    // Crimson outline glow behind buns
    c.fillCircle(cx - bunOffX, headY - bunOffY, bunR + 1, COL_CRIMSON);
    c.fillCircle(cx + bunOffX, headY - bunOffY, bunR + 1, COL_CRIMSON);
    c.fillCircle(cx - bunOffX, headY - bunOffY, bunR, COL_HAIR);
    c.fillCircle(cx + bunOffX, headY - bunOffY, bunR, COL_HAIR);
    // Glossy specular arcs on buns
    c.drawCircle(cx - bunOffX - S(2), headY - bunOffY - S(2), max(2, bunR / 2), COL_HAIR_HI);
    c.drawCircle(cx + bunOffX + S(2), headY - bunOffY - S(2), max(2, bunR / 2), COL_HAIR_HI);
    // Red hair ties + fluttering side ribbon tails
    c.fillRoundRect(cx - bunOffX - S(4), headY - bunOffY + S(5), S(11), S(3), 1, COL_CRIMSON);
    c.fillRoundRect(cx + bunOffX - S(7), headY - bunOffY + S(5), S(11), S(3), 1, COL_CRIMSON);
    c.drawLine(cx - bunOffX - S(2), headY - bunOffY + S(8), cx - bunOffX - S(8), headY - S(2) + ribWave / 2, COL_CRIMSON);
    c.drawLine(cx + bunOffX + S(2), headY - bunOffY + S(8), cx + bunOffX + S(8), headY - S(2) - ribWave / 2, COL_CRIMSON);

    // 7. Ears, Hair Silhouette & Face Contour
    c.fillCircle(cx - headR, headY + S(3), max(2, S(4)), COL_SKIN);
    c.fillCircle(cx + headR, headY + S(3), max(2, S(4)), COL_SKIN);
    c.drawCircle(cx - headR, headY + S(3), max(2, S(4)), COL_HAIR);
    c.drawCircle(cx + headR, headY + S(3), max(2, S(4)), COL_HAIR);

    c.fillCircle(cx, headY - S(2), headR + S(2), COL_HAIR);
    c.fillCircle(cx, headY + S(1), headR - S(1), COL_SKIN);
    // Subtle chin definition
    c.fillTriangle(cx - S(12), headY + S(12), cx + S(12), headY + S(12), cx, headY + S(21), COL_SKIN);
    c.drawLine(cx - S(11), headY + S(13), cx, headY + S(21), COL_SKIN_SHD);
    c.drawLine(cx + S(11), headY + S(13), cx, headY + S(21), COL_SKIN_SHD);

    // Sweeping Layered 哪吒魔童 Hair Bangs (leaving central forehead window for the flame!)
    c.fillRect(cx - headR + S(1), headY - headR, (headR - S(1)) * 2, S(8), COL_HAIR);
    // Left layered bangs
    c.fillTriangle(cx - S(18), headY - S(12), cx - S(13), headY - S(1), cx - S(8), headY - S(12), COL_HAIR);
    c.fillTriangle(cx - S(12), headY - S(13), cx - S(6), headY - S(4), cx - S(1), headY - S(14), COL_HAIR);
    // Right layered bangs
    c.fillTriangle(cx + S(18), headY - S(12), cx + S(13), headY - S(1), cx + S(8), headY - S(12), COL_HAIR);
    c.fillTriangle(cx + S(12), headY - S(13), cx + S(6), headY - S(4), cx + S(1), headY - S(14), COL_HAIR);
    // Subtle hair sheen streaks
    c.drawLine(cx - S(12), headY - S(15), cx - S(8), headY - S(9), COL_HAIR_HI);
    c.drawLine(cx + S(12), headY - S(15), cx + S(8), headY - S(9), COL_HAIR_HI);

    // 8. Signature 哪吒魔童 Crimson-Gold Forehead Lotus Flame Mark (魔童印记)
    int markY = headY - S(7);
    uint16_t flameOut = ((frame & 1) && (mode == ANIM_TRAIN || mode == ANIM_LEVELUP)) ? COL_FLAME : COL_CRIMSON;
    c.fillTriangle(cx, markY - S(5), cx - S(4), markY + S(1), cx + S(4), markY + S(1), flameOut);
    c.fillTriangle(cx, markY + S(5), cx - S(3), markY, cx + S(3), markY, flameOut);
    c.fillTriangle(cx, markY - S(2), cx - S(2), markY + S(1), cx + S(2), markY + S(1), COL_GOLD);

    // 9. Authentic 哪吒魔童 Smoky Cat-Eye Eyeliner, Fiery Iris & Expressions
    int eyeY = headY + S(3);
    int eyeOffX = S(8);
    int eyeR = max(2, S(4));
    bool blink = (mode == ANIM_IDLE) && ((frame % 26) == 0);

    // Rosy cheek blush when happy/petted
    if (mode == ANIM_PET || mode == ANIM_FEED || mode == ANIM_LEVELUP || st.mood >= 72) {
        c.fillCircle(cx - S(13), eyeY + S(5), max(1, S(2)), COL_BLUSH);
        c.fillCircle(cx + S(13), eyeY + S(5), max(1, S(2)), COL_BLUSH);
    }

    if (mode == ANIM_REST || blink) {
        c.drawLine(cx - eyeOffX - S(4), eyeY, cx - eyeOffX + S(3), eyeY, COL_HAIR);
        c.drawLine(cx + eyeOffX - S(3), eyeY, cx + eyeOffX + S(4), eyeY, COL_HAIR);
    } else if (mode == ANIM_PET || mode == ANIM_FEED || mode == ANIM_LEVELUP) {
        // Joyful ^ ^ crescents
        c.drawLine(cx - eyeOffX - S(4), eyeY + S(1), cx - eyeOffX, eyeY - S(3), COL_HAIR);
        c.drawLine(cx - eyeOffX, eyeY - S(3), cx - eyeOffX + S(4), eyeY + S(1), COL_HAIR);
        c.drawLine(cx + eyeOffX - S(4), eyeY + S(1), cx + eyeOffX, eyeY - S(3), COL_HAIR);
        c.drawLine(cx + eyeOffX, eyeY - S(3), cx + eyeOffX + S(4), eyeY + S(1), COL_HAIR);
    } else {
        uint16_t irisCol = (mode == ANIM_TRAIN) ? COL_GOLD
                         : (mode == ANIM_PATROL) ? COL_SPIRIT_BAR
                         : COL_FLAME;
        // Smoky winged eyeliner (哪吒魔童 signature!)
        c.fillRoundRect(cx - eyeOffX - S(5), eyeY - S(3), S(10), S(6), 2, COL_HAIR);
        c.fillRoundRect(cx + eyeOffX - S(5), eyeY - S(3), S(10), S(6), 2, COL_HAIR);
        c.drawLine(cx - eyeOffX - S(6), eyeY - S(2), cx - eyeOffX - S(3), eyeY, COL_HAIR);
        c.drawLine(cx + eyeOffX + S(6), eyeY - S(2), cx + eyeOffX + S(3), eyeY, COL_HAIR);
        // Bright sclera + fiery crimson-amber iris + dark pupil + specular catchlight
        c.fillCircle(cx - eyeOffX, eyeY, max(2, eyeR - 1), TFT_WHITE);
        c.fillCircle(cx + eyeOffX, eyeY, max(2, eyeR - 1), TFT_WHITE);
        c.fillCircle(cx - eyeOffX, eyeY, max(1, eyeR - 1), irisCol);
        c.fillCircle(cx + eyeOffX, eyeY, max(1, eyeR - 1), irisCol);
        c.drawPixel(cx - eyeOffX, eyeY, COL_HAIR);
        c.drawPixel(cx + eyeOffX, eyeY, COL_HAIR);
        c.fillCircle(cx - eyeOffX - 1, eyeY - 1, 1, TFT_WHITE);
        c.fillCircle(cx + eyeOffX - 1, eyeY - 1, 1, TFT_WHITE);
        // Fierce angled Demon-Child eyebrows
        int browTilt = (mode == ANIM_TRAIN || mode == ANIM_PATROL) ? S(3) : S(2);
        c.drawLine(cx - eyeOffX - S(4), eyeY - eyeR - S(2), cx - eyeOffX + S(4), eyeY - eyeR - S(2) + browTilt, COL_HAIR);
        c.drawLine(cx - eyeOffX - S(4), eyeY - eyeR - S(1), cx - eyeOffX + S(4), eyeY - eyeR - S(1) + browTilt, COL_HAIR);
        c.drawLine(cx + eyeOffX + S(4), eyeY - eyeR - S(2), cx + eyeOffX - S(4), eyeY - eyeR - S(2) + browTilt, COL_HAIR);
        c.drawLine(cx + eyeOffX + S(4), eyeY - eyeR - S(1), cx + eyeOffX - S(4), eyeY - eyeR - S(1) + browTilt, COL_HAIR);
    }

    // Subtle nose dot
    c.drawPixel(cx, headY + S(8), COL_SKIN_SHD);

    // Mouth
    int mouthY = headY + S(12);
    if (mode == ANIM_REST) {
        c.drawLine(cx - S(2), mouthY, cx + S(2), mouthY, COL_HAIR);
    } else if (mode == ANIM_FEED) {
        c.fillCircle(cx, mouthY, max(2, S(3)), COL_CRIMSON);
    } else if (mode == ANIM_PET || mode == ANIM_LEVELUP) {
        c.fillTriangle(cx - S(5), mouthY - S(1), cx + S(5), mouthY - S(1), cx, mouthY + S(4), COL_CRIMSON);
        c.drawLine(cx - S(5), mouthY - S(1), cx + S(5), mouthY - S(1), TFT_WHITE);
    } else if (st.spirit < 22 || st.mood < 25) {
        c.drawLine(cx - S(4), mouthY + S(2), cx, mouthY, COL_HAIR);
        c.drawLine(cx, mouthY, cx + S(4), mouthY + S(2), COL_HAIR);
    } else {
        // Confident Demon-Child sideways grin with crisp white tooth glint
        c.drawLine(cx - S(5), mouthY, cx + S(1), mouthY + S(1), COL_HAIR);
        c.drawLine(cx + S(1), mouthY + S(1), cx + S(6), mouthY - S(2), COL_HAIR);
        c.drawLine(cx - S(2), mouthY, cx + S(3), mouthY - S(1), TFT_WHITE);
    }

    // 10. Floating Action Particles
    if (mode == ANIM_PET) {
        int dy = (frame * 3) % S(18);
        drawHeartOn(c, cx - S(26), headY - S(10) - dy / 2, max(2, S(3)), COL_CRIMSON);
        drawHeartOn(c, cx + S(26), headY - S(4) - dy / 2, max(1, S(2)), COL_LOTUS);
    } else if (mode == ANIM_FEED) {
        int ex = cx + S(32) - min(S(24), (int)(frame * 3));
        int ey = mouthY - S(2) + (int)roundf(sinf(phase) * 3.0f);
        c.fillCircle(ex, ey, max(3, S(5)), COL_ELIXIR);
        c.drawCircle(ex, ey, max(4, S(6)), COL_GOLD);
    } else if (mode == ANIM_TRAIN) {
        for (int i = 0; i < 4; i++) {
            int px = cx + ((i % 2 == 0) ? -1 : 1) * S(20 + i * 5);
            int py = cy + S(16) - ((frame * 5 + i * 9) % S(44));
            c.fillCircle(px, py, max(1, S(2)), (i & 1) ? COL_GOLD : COL_FLAME);
        }
    } else if (mode == ANIM_REST) {
        c.setTextSize(FP);
        c.setTextColor(uiTextMutedColor(), stageBg);
        int zy = headY - S(16) - ((frame * 2) % S(14));
        c.drawString((frame & 2) ? "Z" : "z", cx + S(19), zy, 1);
    }
    (void)sec;
}

static void drawNezhaMascot(
    int sx, int sy, int sw, int sh,
    const NezhaPetState &st, PetAnimMode mode, uint8_t frame
) {
#if !defined(BRUCE_COMPACT_CJK_FONT)
    // Tear-free offscreen PSRAM sprite buffer on 16MB/PSRAM boards (e.g. LilyGO T-Deck)
    static tft_sprite s_stageBuf(&tft);
    static int s_bufW = 0, s_bufH = 0;
    static bool s_bufReady = false;
    if (s_bufW != sw || s_bufH != sh) {
        if (s_bufReady) s_stageBuf.deleteSprite();
        s_stageBuf.setColorDepth(16);
        s_bufReady = (s_stageBuf.createSprite(sw, sh) != nullptr);
        s_bufW = sw;
        s_bufH = sh;
    }
    if (s_bufReady) {
        renderNezhaScene(s_stageBuf, 0, 0, sw, sh, st, mode, frame);
        s_stageBuf.pushSprite(sx, sy);
        return;
    }
#endif
    renderNezhaScene(tft, sx, sy, sw, sh, st, mode, frame);
}

/*
 * HUD chrome for the pet screen.
 * Draws the labeled stat bars (spirit/mood/bond), the telemetry panel, and
 * the five-button action dock used to feed, play, and otherwise interact.
 */
static void drawStatBar(int x, int y, int w, int h, const String &label, int val, int maxVal, uint16_t fillCol) {
    uint16_t bg = bruceConfig.bgColor;
    uint16_t track = uiSurfaceColor();
    int pct = (maxVal > 0) ? constrain((val * 100) / maxVal, 0, 100) : 0;

    tft.fillRect(x, y, w, h + 10, bg);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), bg);
    tft.drawString(label, x, y, 1);

    String numStr = (maxVal == 100) ? (String(val) + "%") : (String(val) + "/" + String(maxVal));
    tft.setTextColor(uiTextBrightColor(), bg);
    tft.drawRightString(numStr, x + w, y, 1);

    int barY = y + 9;
    tft.fillRoundRect(x, barY, w, h, h / 2, track);
    tft.drawRoundRect(x, barY, w, h, h / 2, uiBorderDimColor());
    int fillW = ((w - 2) * pct) / 100;
    if (fillW > 2) {
        tft.fillRoundRect(x + 1, barY + 1, fillW, h - 2, max(1, (h - 2) / 2), fillCol);
    }
}

static void drawPetHeader(const NezhaPetState &st, bool compact) {
    uint16_t bg = bruceConfig.bgColor;
    int hy = 0;
    int hh = compact ? 18 : 24;
    tft.fillRect(0, hy, tftWidth, hh + 2, bg);

    int tx = 8;
#if defined(HAS_TOUCH)
    if (!compact) {
        int cy = hy + hh / 2;
        tft.drawLine(16, cy - 4, 11, cy, uiTextMutedColor());
        tft.drawLine(15, cy - 4, 10, cy, uiTextMutedColor());
        tft.drawLine(11, cy, 16, cy + 4, uiTextMutedColor());
        tft.drawLine(10, cy, 15, cy + 4, uiTextMutedColor());
        tx = 24;
    }
#endif
    if (bruceConfig.nezhaThemeStyle > 0) {
        drawNezhaCrest(tx + 4, hy + hh / 2, compact ? 4 : 5, bruceConfig.priColor, bruceConfig.secColor, bruceConfig.nezhaThemeStyle);
        tx += compact ? 12 : 14;
    }

    uiDrawText16(bruce_i18n::tr("Nezha Pet"), tx, hy + (hh - 16) / 2, tftWidth - tx - 92, uiTextBrightColor(), bg, 0);

    // Level pill badge
    String lvStr = "Lv." + String(st.level);
    int lvW = (int)lvStr.length() * LW * FP + 10;
    int bat = getBattery();
    int rightPad = (bat > 0 && !compact) ? 34 : 6;
    int lvX = tftWidth - rightPad - lvW;
    tft.fillRoundRect(lvX, hy + (hh - 13) / 2, lvW, 13, 3, bruceConfig.priColor);
    tft.setTextSize(FP);
    tft.setTextColor(uiTextOnAccentColor(), bruceConfig.priColor);
    tft.drawCentreString(lvStr, lvX + lvW / 2, hy + (hh - LH * FP) / 2, 1);

    if (bat > 0 && !compact) {
        tft.setTextSize(FP);
        tft.setTextColor(uiTextMutedColor(), bg);
        tft.drawRightString(String(bat) + "%", tftWidth - 6, hy + (hh - LH * FP) / 2, 1);
    }

    int lineY = hy + hh + 1;
    tft.drawFastHLine(6, lineY, tftWidth - 12, uiBorderDimColor());
    if (bruceConfig.nezhaThemeStyle > 0) {
        tft.drawFastHLine(6, lineY, (tftWidth - 12) / 3, bruceConfig.priColor);
        tft.drawFastHLine(6 + (tftWidth - 12) / 3, lineY, 20, bruceConfig.secColor);
    }
}

static void drawTelemetryPanel(const NezhaPetState &st, const String &caption, bool compact) {
    uint16_t bg = bruceConfig.bgColor;
    uint16_t nextXp = xpForNextLevel(st.level);

    if (compact) {
        // 240x135 right column: x=96..236, y=21..106 (85px tall — zero overlap)
        int px = 96;
        int pw = tftWidth - px - 4;
        int y = 21;
        tft.fillRect(px, y, pw, 86, bg);
        uiDrawText16(bruce_i18n::tr(getRealmKey(st.level)), px, y, pw, bruceConfig.priColor, bg, 0);
        y += 16;
        drawStatBar(px, y, pw, 5, "SP", st.spirit, 100, COL_SPIRIT_BAR);
        y += 17;
        drawStatBar(px, y, pw, 5, "MD", st.mood, 100, COL_MOOD_BAR);
        y += 17;
        drawStatBar(px, y, pw, 5, "XP", st.xp, nextXp, bruceConfig.priColor);
        y += 17;
        uiDrawText16(caption, px, y, pw, uiTextMutedColor(), bg, 0);
        return;
    }

    // 320x240 right column: x=162..312, y=30..174 (clean breathing room above capY=tftHeight-60)
    int topY = 30;
    int px = 162;
    int pw = tftWidth - px - 8;

    // Realm Card
    tft.fillRoundRect(px, topY, pw, 22, 5, uiSurfaceColor());
    tft.drawRoundRect(px, topY, pw, 22, 5, uiBorderDimColor());
    uiDrawText16(bruce_i18n::tr(getRealmKey(st.level)), px + 7, topY + 3, pw - 14, bruceConfig.priColor, uiSurfaceColor(), 0);

    int y = topY + 28;
    drawStatBar(px, y, pw, 7, bruce_i18n::isChinese() ? "SP" : "Spirit", st.spirit, 100, COL_SPIRIT_BAR);
    y += 23;
    drawStatBar(px, y, pw, 7, bruce_i18n::isChinese() ? "MD" : "Mood", st.mood, 100, COL_MOOD_BAR);
    y += 23;
    drawStatBar(px, y, pw, 7, "XP", st.xp, nextXp, bruceConfig.priColor);
    y += 24;

    // Compact Bond + RF Patrol telemetry pills
    int halfW = (pw - 4) / 2;
    tft.fillRoundRect(px, y, halfW, 20, 4, uiSurfaceColor());
    tft.drawRoundRect(px, y, halfW, 20, 4, uiBorderDimColor());
    tft.setTextSize(FP);
    tft.setTextColor(uiTextMutedColor(), uiSurfaceColor());
    tft.drawString("BOND", px + 5, y + 6, 1);
    tft.setTextColor(COL_LOTUS, uiSurfaceColor());
    tft.drawRightString(String(st.bond) + "%", px + halfW - 5, y + 6, 1);

    int rx = px + halfW + 4;
    tft.fillRoundRect(rx, y, halfW, 20, 4, uiSurfaceColor());
    tft.drawRoundRect(rx, y, halfW, 20, 4, uiBorderDimColor());
    tft.setTextColor(uiTextMutedColor(), uiSurfaceColor());
    tft.drawString("RF", rx + 5, y + 6, 1);
    tft.setTextColor(COL_GOLD, uiSurfaceColor());
    tft.drawRightString(String(st.rfNodes), rx + halfW - 5, y + 6, 1);

    // Speech / status banner across full width cleanly above bottom dock
    int capY = tftHeight - 59;
    tft.fillRoundRect(8, capY, tftWidth - 16, 22, 5, uiSurfaceColor());
    tft.drawRoundRect(8, capY, tftWidth - 16, 22, 5, uiBorderDimColor());
    uiDrawText16(caption, tftWidth / 2, capY + 3, tftWidth - 28, uiTextBrightColor(), uiSurfaceColor(), 1);
}

static void drawActionDock(int selected, bool compact) {
    int dockY = compact ? (tftHeight - 25) : (tftHeight - 31);
    int dockH = compact ? 22 : 25;
    int margin = compact ? 4 : 8;
    int gap = compact ? 3 : 4;
    int btnW = (tftWidth - 2 * margin - gap * (ACT_COUNT - 1)) / ACT_COUNT;

    tft.fillRect(0, dockY - 1, tftWidth, dockH + 3, bruceConfig.bgColor);

    for (int i = 0; i < ACT_COUNT; i++) {
        int bx = margin + i * (btnW + gap);
        bool sel = (i == selected);
        uint16_t fill = sel ? bruceConfig.priColor : uiSurfaceColor();
        uint16_t fg = sel ? uiTextOnAccentColor() : uiTextBrightColor();
        uint16_t brd = sel ? bruceConfig.priColor : uiBorderDimColor();

        tft.fillRoundRect(bx, dockY, btnW, dockH, 5, fill);
        tft.drawRoundRect(bx, dockY, btnW, dockH, 5, brd);

        String lbl = bruce_i18n::tr(kActionKeys[i]);
        if (compact && !bruce_i18n::isChinese()) {
            tft.setTextSize(FP);
            tft.setTextColor(fg, fill);
            tft.drawCentreString(lbl, bx + btnW / 2, dockY + (dockH - LH * FP) / 2, 1);
        } else {
            uiDrawText16(lbl, bx + btnW / 2, dockY + (dockH - 16) / 2, btnW - 4, fg, fill, 1);
        }
    }
}

/*
 * Passive ambient RF patrol that feeds the pet's spirit meter.
 * Scans Wi-Fi SSIDs with no deauth or packet injection, restores the prior
 * Wi-Fi mode afterward, and counts discovered nodes for the state counters.
 */
static int runPassiveRfPatrol() {
    wifi_mode_t prevMode = WiFi.getMode();
    if (prevMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
        delay(60);
    }
    int n = WiFi.scanNetworks(false, true, false, 110);
    if (n < 0) n = 0;
    WiFi.scanDelete();
    if (prevMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);
    }
    return n;
}

} // namespace

void nezha_pet_start() {
    NezhaPetState st;
    loadPetState(st);

    const bool compact = (tftHeight < 180);
    const int stageX = compact ? 4 : 8;
    const int stageY = compact ? 21 : 30;
    const int stageW = compact ? 88 : 148;
    const int stageH = compact ? 85 : (tftHeight - stageY - 65);

#if defined(HAS_TOUCH)
    bool prevTouchZone = touchZoneOutsideFooterEnabled;
    touchZoneOutsideFooterEnabled = false;
#endif

    int selected = 0;
    PetAnimMode animMode = ANIM_IDLE;
    unsigned long animUntilMs = 0;
    unsigned long lastFrameMs = 0;
    unsigned long lastDecayMs = millis();
    uint8_t frame = 0;
    String caption = idleCaption(st, 0);

    bool needFullRedraw = true;
    bool needStatsRedraw = false;
    bool needDockRedraw = false;

    auto triggerAction = [&](int act) {
        bool leveled = false;
        unsigned long now = millis();

        switch (act) {
            case ACT_FEED: {
                st.spirit = (uint8_t)min(100, (int)st.spirit + 26);
                st.mood = (uint8_t)min(100, (int)st.mood + 10);
                st.bond = (uint8_t)min(100, (int)st.bond + 2);
                leveled = grantXp(st, 6);
                animMode = leveled ? ANIM_LEVELUP : ANIM_FEED;
                animUntilMs = now + 2200;
                caption = bruce_i18n::tr("Lotus elixir! SP +26");
                petTone(660, 45);
                petTone(880, 55);
                break;
            }
            case ACT_TRAIN: {
                if (st.spirit < 12) {
                    caption = bruce_i18n::tr("Too tired! Feed or Rest");
                    petTone(260, 70);
                } else {
                    st.spirit = (uint8_t)(st.spirit - 12);
                    st.mood = (uint8_t)min(100, (int)st.mood + 6);
                    st.bond = (uint8_t)min(100, (int)st.bond + 3);
                    uint16_t gain = (uint16_t)(16 + (st.bond / 15) * 2);
                    leveled = grantXp(st, gain);
                    animMode = leveled ? ANIM_LEVELUP : ANIM_TRAIN;
                    animUntilMs = now + 2400;
                    caption = String(bruce_i18n::tr("Wind-Fire Train!")) + " XP +" + String(gain);
                    petTone(520, 40);
                    petTone(780, 45);
                    petTone(1040, 55);
                }
                break;
            }
            case ACT_PET: {
                st.mood = (uint8_t)min(100, (int)st.mood + 18);
                st.bond = (uint8_t)min(100, (int)st.bond + 4);
                leveled = grantXp(st, 5);
                animMode = leveled ? ANIM_LEVELUP : ANIM_PET;
                animUntilMs = now + 2100;
                caption = bruce_i18n::tr("Hehe! Watch the hair buns!");
                petTone(740, 40);
                petTone(980, 50);
                break;
            }
            case ACT_PATROL: {
                if (st.spirit < 10) {
                    caption = bruce_i18n::tr("Need 10 SP to Patrol!");
                    petTone(260, 70);
                } else {
                    animMode = ANIM_PATROL;
                    caption = bruce_i18n::tr("Sensing ambient RF waves...");
                    drawNezhaMascot(stageX, stageY, stageW, stageH, st, animMode, frame + 2);
                    drawTelemetryPanel(st, caption, compact);
                    petTone(820, 35);

                    int found = runPassiveRfPatrol();
                    st.spirit = (uint8_t)(st.spirit - 10);
                    st.mood = (uint8_t)min(100, (int)st.mood + 8);
                    st.bond = (uint8_t)min(100, (int)st.bond + 2);
                    st.patrols++;
                    st.rfNodes = (uint16_t)min(65000, (int)st.rfNodes + found);
                    uint16_t gain = (uint16_t)min(35, 8 + found * 2);
                    leveled = grantXp(st, gain);
                    animMode = leveled ? ANIM_LEVELUP : ANIM_PATROL;
                    animUntilMs = millis() + 2600;
                    caption = String(bruce_i18n::tr("Sensed RF APs:")) + " " + String(found) + "  XP +" + String(gain);
                    petTone(980, 45);
                    petTone(1240, 60);
                }
                break;
            }
            case ACT_REST: {
                st.spirit = (uint8_t)min(100, (int)st.spirit + 38);
                st.mood = (uint8_t)min(100, (int)st.mood + 6);
                animMode = ANIM_REST;
                animUntilMs = now + 3600;
                caption = bruce_i18n::tr("Lotus meditation... SP +38");
                petTone(440, 60);
                petTone(554, 70);
                break;
            }
            default: break;
        }

        if (leveled) {
            caption = String(bruce_i18n::tr("LEVEL UP!")) + " Lv." + String(st.level) + " " +
                      String(bruce_i18n::tr(getRealmKey(st.level)));
            petTone(880, 50);
            petTone(1108, 55);
            petTone(1318, 90);
            needFullRedraw = true;
        } else {
            needStatsRedraw = true;
        }
        savePetState(st);
    };

    tft.fillScreen(bruceConfig.bgColor);
    SelPress = EscPress = PrevPress = NextPress = UpPress = DownPress = false;
#if defined(HAS_TOUCH)
    touchPoint.Clear();
#endif

    while (true) {
        if (check(EscPress)) break;

        if (check(PrevPress) || check(UpPress)) {
            selected = (selected + ACT_COUNT - 1) % ACT_COUNT;
            needDockRedraw = true;
        }
        if (check(NextPress) || check(DownPress)) {
            selected = (selected + 1) % ACT_COUNT;
            needDockRedraw = true;
        }
        if (check(SelPress)) {
            triggerAction(selected);
        }

#if defined(HAS_TOUCH)
        if (touchPoint.pressed) {
            int tx = touchPoint.x;
            int ty = touchPoint.y;
            touchPoint.Clear();

            if (!compact && tx < 42 && ty <= 28) {
                break; // Top-left back chevron
            }

            if (tx >= stageX && tx <= stageX + stageW && ty >= stageY && ty <= stageY + stageH) {
                selected = ACT_PET;
                needDockRedraw = true;
                triggerAction(ACT_PET);
            } else {
                int dockY = compact ? (tftHeight - 25) : (tftHeight - 31);
                if (ty >= dockY - 4) {
                    int margin = compact ? 4 : 8;
                    int gap = compact ? 3 : 4;
                    int btnW = (tftWidth - 2 * margin - gap * (ACT_COUNT - 1)) / ACT_COUNT;
                    for (int i = 0; i < ACT_COUNT; i++) {
                        int bx = margin + i * (btnW + gap);
                        if (tx >= bx && tx <= bx + btnW) {
                            selected = i;
                            needDockRedraw = true;
                            triggerAction(i);
                            break;
                        }
                    }
                }
            }
        }
#endif

        unsigned long now = millis();

        if (animMode != ANIM_IDLE && now >= animUntilMs) {
            animMode = ANIM_IDLE;
            caption = idleCaption(st, frame);
            needStatsRedraw = true;
        }

        if (now - lastDecayMs >= 45000UL) {
            lastDecayMs = now;
            if (animMode != ANIM_REST) {
                if (st.spirit > 0) st.spirit--;
                if ((frame & 1) && st.mood > 0) st.mood--;
                if (animMode == ANIM_IDLE) caption = idleCaption(st, frame);
                needStatsRedraw = true;
            }
        }

        if (needFullRedraw) {
            drawPetHeader(st, compact);
            drawTelemetryPanel(st, caption, compact);
            drawActionDock(selected, compact);
            drawNezhaMascot(stageX, stageY, stageW, stageH, st, animMode, frame);
            needFullRedraw = false;
            needStatsRedraw = false;
            needDockRedraw = false;
            lastFrameMs = now;
        } else {
            if (needStatsRedraw) {
                drawPetHeader(st, compact);
                drawTelemetryPanel(st, caption, compact);
                needStatsRedraw = false;
            }
            if (needDockRedraw) {
                drawActionDock(selected, compact);
                needDockRedraw = false;
            }
            unsigned long frameInterval = (animMode == ANIM_IDLE) ? 140UL : 90UL;
            if (now - lastFrameMs >= frameInterval) {
                lastFrameMs = now;
                frame++;
                drawNezhaMascot(stageX, stageY, stageW, stageH, st, animMode, frame);
                if (animMode == ANIM_IDLE && (frame % 24) == 0) {
                    caption = idleCaption(st, frame);
                    drawTelemetryPanel(st, caption, compact);
                }
            }
        }

        delay(15);
    }

    savePetState(st);
#if defined(HAS_TOUCH)
    touchZoneOutsideFooterEnabled = prevTouchZone;
    touchPoint.Clear();
#endif
    tft.fillScreen(bruceConfig.bgColor);
    SelPress = EscPress = PrevPress = NextPress = UpPress = DownPress = false;
    invalidateStatusBarCache();
    returnToMenu = true;
}

#endif // !defined(LITE_VERSION)
