#include "mykeyboard.h"
#include "core/i18n.h"
#include "core/utils.h"
#include "core/wifi/webInterface.h"
#include "modules/ir/TV-B-Gone.h"
#include "modules/ir/custom_ir.h"
#include "modules/rf/rf_send.h"
#include "powerSave.h"
#include "sd_functions.h"
#include <ArduinoJson.h>

#ifndef HAS_1_BUTTON
#define HAS_1_BUTTON 0
#else
#undef HAS_1_BUTTON
#define HAS_1_BUTTON 1
#endif

int max_FM_size = tftWidth / (LW * FM) - 1;
int max_FP_size = tftWidth / (LW)-2;

// QWERTY KEYSET
const int qwerty_keyboard_width = 12;
const int qwerty_keyboard_height = 4;
char qwerty_keyset[qwerty_keyboard_height][qwerty_keyboard_width][2] = {
    //  4 lines, with 12 characters, capital and lowercase
    {{'1', '!'},
     {'2', '@'},
     {'3', '#'},
     {'4', '$'},
     {'5', '%'},
     {'6', '^'},
     {'7', '&'},
     {'8', '*'},
     {'9', '('},
     {'0', ')'},
     {'-', '_'},
     {'=', '+'} },
    {{'q', 'Q'},
     {'w', 'W'},
     {'e', 'E'},
     {'r', 'R'},
     {'t', 'T'},
     {'y', 'Y'},
     {'u', 'U'},
     {'i', 'I'},
     {'o', 'O'},
     {'p', 'P'},
     {'[', '{'},
     {']', '}'} },
    {{'a', 'A'},
     {'s', 'S'},
     {'d', 'D'},
     {'f', 'F'},
     {'g', 'G'},
     {'h', 'H'},
     {'j', 'J'},
     {'k', 'K'},
     {'l', 'L'},
     {';', ':'},
     {'"', '\''},
     {'|', '\\'}},
    {{'\\', '|'},
     {'z', 'Z'},
     {'x', 'X'},
     {'c', 'C'},
     {'v', 'V'},
     {'b', 'B'},
     {'n', 'N'},
     {'m', 'M'},
     {',', '<'},
     {'.', '>'},
     {'?', '/'},
     {'/', '/'} }
};

// AZERTY KEYSET (French)
const int azerty_keyboard_width = 12;
const int azerty_keyboard_height = 4;
char azerty_keyset[azerty_keyboard_height][azerty_keyboard_width][2] = {
    // Row 1: numbers / symbols
    {{'1', '&'},
     {'2', '\xc3'}, // é
     {'3', '"'},
     {'4', '\''},
     {'5', '('},
     {'6', '-'},
     {'7', '\xe8'}, // è
     {'8', '_'},
     {'9', '\xe7'}, // ç
     {'0', '\xe0'}, // à
     {')', '\xb0'},
     {'=', '+'}                      },
    // Row 2: AZERTY row
    {{'a', 'A'},
     {'z', 'Z'},
     {'e', 'E'},
     {'r', 'R'},
     {'t', 'T'},
     {'y', 'Y'},
     {'u', 'U'},
     {'i', 'I'},
     {'o', 'O'},
     {'p', 'P'},
     {'.', '\xa8'},                                       // . / ¨
     {'$', '\xa3'}}, // $ / £
    // Row 3: QSDFGH row
    {{'q', 'Q'},
     {'s', 'S'},
     {'d', 'D'},
     {'f', 'F'},
     {'g', 'G'},
     {'h', 'H'},
     {'j', 'J'},
     {'k', 'K'},
     {'l', 'L'},
     {'m', 'M'},
     {'\xf9', '%'},                                       // ù / %
     {'*', '\xb5'}}, // * / µ
    // Row 4: WXCVBN row
    {{'<', '>'},
     {'w', 'W'},
     {'x', 'X'},
     {'c', 'C'},
     {'v', 'V'},
     {'b', 'B'},
     {'n', 'N'},
     {',', '?'},
     {';', '.'},
     {':', '/'},
     {'!', '\xa7'},                                       // ! / §
     {'-', '_'}   }
};

// QWERTZ KEYSET (German)
const int qwertz_keyboard_width = 12;
const int qwertz_keyboard_height = 4;
char qwertz_keyset[qwertz_keyboard_height][qwertz_keyboard_width][2] = {
    // Row 1
    {{'1', '!'},
     {'2', '"'},
     {'3', '\xa7'}, // §
     {'4', '$'},
     {'5', '%'},
     {'6', '&'},
     {'7', '/'},
     {'8', '('},
     {'9', ')'},
     {'0', '='},
     {'\xdf', '?'},                          // ß / ?
     {'\'', '`'}                         },
    // Row 2: QWERTZ row
    {{'q', 'Q'},
     {'w', 'W'},
     {'e', 'E'},
     {'r', 'R'},
     {'t', 'T'},
     {'z', 'Z'},
     {'u', 'U'},
     {'i', 'I'},
     {'o', 'O'},
     {'p', 'P'},
     {'\xfc', '\xdc'},                       // ü / Ü
     {'+', '*'}                       },
    // Row 3: ASDF row
    {{'a', 'A'},
     {'s', 'S'},
     {'d', 'D'},
     {'f', 'F'},
     {'g', 'G'},
     {'h', 'H'},
     {'j', 'J'},
     {'k', 'K'},
     {'l', 'L'},
     {'\xf6', '\xd6'}, // ö / Ö
     {'\xe4', '\xc4'}, // ä / Ä
     {'#', '\''}},
    // Row 4: YXCVBN row
    {{'<', '>'},
     {'y', 'Y'},
     {'x', 'X'},
     {'c', 'C'},
     {'v', 'V'},
     {'b', 'B'},
     {'n', 'N'},
     {'m', 'M'},
     {',', ';'},
     {'.', ':'},
     {'-', '_'},
     {'/', '/'}                                             }
};

// HEX KEYSET
const int hex_keyboard_width = 4;
const int hex_keyboard_height = 4;
char hex_keyset[hex_keyboard_height][hex_keyboard_width][2] = {
    {{'0', '0'}, {'1', '1'}, {'2', '2'}, {'3', '3'}},
    {{'4', '4'}, {'5', '5'}, {'6', '6'}, {'7', '7'}},
    {{'8', '8'}, {'9', '9'}, {'A', 'a'}, {'B', 'b'}},
    {{'C', 'c'}, {'D', 'd'}, {'E', 'e'}, {'F', 'f'}},
};

// NUMBERS ONLY KEYSET
const int numpad_keyboard_width = 4;
const int numpad_keyboard_height = 3;
char numpad_keyset[numpad_keyboard_height][numpad_keyboard_width][2] = {
    // 3 lines, with 4 characters each:
    {{'1', '1'}, {'2', '2'}, {'3', '3'}, {'\0', '\0'}},
    {{'4', '4'}, {'5', '5'}, {'6', '6'}, {'.', '.'}  },
    {{'7', '7'}, {'8', '8'}, {'9', '9'}, {'0', '0'}  }
};

#if defined(HAS_TOUCH)
struct box_t {
    int x;
    int y;
    int w;
    int h;
    std::uint16_t color;
    int touch_id = -1;
    char key;
    char key_sh;

    void clear(void) {
        for (int i = 0; i < 8; ++i) { tft.fillRect(x, y, w, h, bruceConfig.bgColor); }
    }
    void draw(void) {
        int ie = touch_id < 0 ? 4 : 8;
        for (int i = 0; i < ie; ++i) {
            tft.drawRect(x, y, w, h, color);
            tft.setTextColor(color);
            tft.drawString(String(key), x + w / 2 - FM * LW / 2, y + h / 2 - FM * LH / 2);
        }
    }
    bool contain(int x, int y) {
        return this->x <= x && x < (this->x + this->w) && this->y <= y && y < (this->y + this->h);
    }
};

#endif

// Retrieves the current keyStroke from InputHandler, resets it after use.
// This function is used in loopTask to get the latest key press.
static SemaphoreHandle_t inputLock_ = nullptr;
static StaticSemaphore_t inputLockStorage;

void inputLockInit() {
    if (inputLock_ == nullptr) inputLock_ = xSemaphoreCreateRecursiveMutexStatic(&inputLockStorage);
}

void inputLock() {
    if (inputLock_ != nullptr) xSemaphoreTakeRecursive(inputLock_, portMAX_DELAY);
}

void inputUnlock() {
    if (inputLock_ != nullptr) xSemaphoreGiveRecursive(inputLock_);
}

keyStroke _getKeyPress() {
    // Waiting on the mutex instead of suspending the input task: the writer finishes its
    // update and releases, so neither side is ever frozen mid-allocation.
    inputLock();
    keyStroke key = KeyStroke;
    KeyStroke.Clear();
    inputUnlock();
    return key;
} // Returns a keyStroke that the keyboards won't recognize by default

/*********************************************************************
** Function: checkShortcutPress
** location: mykeyboard.cc
** runs a function called by the shortcut action
**********************************************************************/
bool checkShortcutPress() {
    static JsonDocument shortcutsJson; // parsed only once
    bool executed = false;

    // lazy init
    if (shortcutsJson.size() == 0) {
        FS *fs;
        if (!getFsStorage(fs)) return false;
        File file = fs->open("/shortcuts.json", FILE_READ);
        if (!file) {
            log_e("Shortcuts Config file not found. Using default values");
            JsonObject shortcuts = shortcutsJson.to<JsonObject>(); // root
            shortcuts["i"] = "loader open ir";
            shortcuts["r"] = "loader open rf";
            shortcuts["s"] = "loader open rf";
            shortcuts["b"] = "loader open badusb";
            shortcuts["w"] = "loader open webui";
            shortcuts["f"] = "loader open files";
            return false;
        }
        // else
        if (deserializeJson(shortcutsJson, file)) {
            log_e("Failed to parse shortcuts.json");
            file.close();
            return false;
        }
        file.close();
    }

    keyStroke key = _getKeyPress();

    // parse shortcutsJson and check the keys
    for (JsonPair kv : shortcutsJson.as<JsonObject>()) {
        const char *shortcut_key = kv.key().c_str();
        const char *shortcut_value = kv.value().as<const char *>();
        if (!shortcut_key || !shortcut_key[0] || !shortcut_value) continue;

        // check for matching keys
        for (auto i : key.word) {
            if (i == *shortcut_key) { // compare the 1st char of the key string
                // execute the associated action
                serialCli.parse(String(shortcut_value));
                executed = true;
            }
        }
    }
    return executed;
}

/*********************************************************************
** Function: checkNumberShortcutPress
** location: mykeyboard.cc
** return the pressed number
**********************************************************************/
int checkNumberShortcutPress() {
    // shortctus to quickly select options
    keyStroke key = _getKeyPress();
    for (auto i : key.word) {
        char c;
        for (c = '1'; c <= '9'; c++)
            if (i == c) return (c - '1');
    }
    // else
    return -1;
}

/*********************************************************************
** Function: checkLetterShortcutPress
** location: mykeyboard.cc
** return the pressed letter
**********************************************************************/
char checkLetterShortcutPress() {
    // shortctus to quickly select options
    keyStroke key = _getKeyPress();
    for (auto i : key.word) {
        char c;
        for (c = 'a'; c <= 'z'; c++)
            if (i == c) return (c);
        for (c = 'A'; c <= 'Z'; c++)
            if (i == c) return (c);
    }
    // else
    return -1;
}

/*********************************************************************
** Shared keyboard helper functions
**********************************************************************/

/// Handles character deletion from the text string and screen
bool handleDelete(String &current_text, int &cursor_x, int &cursor_y) {
    if (current_text.length() == 0) return false;

    // remove from string
    current_text.remove(current_text.length() - 1);
    // delete from screen:
    int fontSize = FM;
    if (current_text.length() > max_FP_size) {
        tft.setTextSize(FP);
        fontSize = FP;
    } else tft.setTextSize(FM);
    tft.setCursor((cursor_x - fontSize * LW), cursor_y);
    uint16_t boxBg = uiSurfaceColor();
    tft.setTextColor(uiTextBrightColor(), boxBg);
    tft.print(" ");
    tft.setCursor(cursor_x - fontSize * LW, cursor_y);
    cursor_x = tft.getCursorX();
    cursor_y = tft.getCursorY();
    return true;
}

/// Handles adding a character to the text string
bool handleCharacterAdd(
    String &current_text, char character, int &cursor_x, int &cursor_y, const int max_size, bool mask_input
) {
    if (current_text.length() >= max_size) return false;

    current_text += character;
    if (current_text.length() != (max_FP_size + 1)) {
        if (mask_input) {
            tft.print("*");
        } else {
            tft.print(character);
        }
    }
    cursor_x = tft.getCursorX();
    cursor_y = tft.getCursorY();
    return true;
}

/// Handles adding space to the text string
bool handleSpaceAdd(String &current_text, const int max_size) {
    if (current_text.length() >= max_size) return false;
    current_text += ' ';
    return true;
}

// Enum for keyboard action results
enum KeyboardAction { KEYBOARD_CONTINUE, KEYBOARD_OK, KEYBOARD_CANCEL, KEYBOARD_REDRAW };

/// Handles keyboard selection logic for regular keyboard
KeyboardAction handleKeyboardSelection(
    int &x, int &y, String &current_text, bool &caps, bool &direction, int &cursor_x, int &cursor_y,
    const int max_size, char character, bool mask_input
) {
    tft.setCursor(cursor_x, cursor_y);

    if (y == -1) {
        switch (x) {
            case 0: // OK button
                return KEYBOARD_OK;
            case 1: // CAP button
                caps = !caps;
                return KEYBOARD_REDRAW;
            case 2: // DEL button
                if (handleDelete(current_text, cursor_x, cursor_y)) return KEYBOARD_REDRAW;
                break;
            case 3: // SPACE button
                if (handleSpaceAdd(current_text, max_size)) return KEYBOARD_REDRAW;
                break;
            case 4: // BACK button
                current_text = "\x1B";
                return KEYBOARD_CANCEL;
#if HAS_1_BUTTON
            case 5: // Direction button
                direction = !direction;
                return KEYBOARD_REDRAW;
#endif
            default: break;
        }

    } else if (y > -1 && current_text.length() < max_size) {
        // add a letter to current_text
        if (handleCharacterAdd(current_text, character, cursor_x, cursor_y, max_size, mask_input)) {
            if (current_text.length() >= max_size) { // put the Cursor at "Ok" when max size reached
                x = 0;
                y = -1;
            }

            return KEYBOARD_REDRAW;
        }
    }

    return KEYBOARD_CONTINUE;
}

template <int KeyboardHeight, int KeyboardWidth>
String generalKeyboard(
    String current_text, int max_size, String textbox_title, char keys[KeyboardHeight][KeyboardWidth][2],
    bool mask_input = false
) {
    max_FM_size = tftWidth / (LW * FM) - 1;
    max_FP_size = tftWidth / (LW)-2;
    resetTftDisplay();

#if defined(HAS_TOUCH)
    // Disable global touchHeatMap zones while typing so taps never trigger phantom Sel/Esc/Prev/Next
    struct KeyboardTouchZoneGuard {
        bool prev;
        KeyboardTouchZoneGuard() : prev(touchZoneOutsideFooterEnabled) {
            touchZoneOutsideFooterEnabled = false;
        }
        ~KeyboardTouchZoneGuard() { touchZoneOutsideFooterEnabled = prev; }
    } kbTouchZoneGuard;
#endif

    // Flush any stale navigation, touch, or keyboard events carried over from the calling menu
    inputLock();
    touchPoint.Clear();
    KeyStroke.Clear();
    SelPress = false;
    EscPress = false;
    NextPress = false;
    PrevPress = false;
    UpPress = false;
    DownPress = false;
    inputUnlock();

    /* SUPPORT VARIABLES */
    bool caps = false;
    bool last_caps = false;
    bool selection_made = false; // used for detecting if a key or a button was selected
    bool redraw = true;
    unsigned long last_input_time = millis(); // used for button input debouncing
#if defined(HAS_TOUCH)
    unsigned long last_touch_time = millis(); // separate snappy touch cooldown
#endif
    bool show_password = false;
#if defined(HAS_KEYBOARD)
    // Devices with a built-in physical keyboard (T-Deck, Cardputer, T-LoRa-Pager, Tab5) default to a
    // clean text-input dialog instead of cluttering the screen with a redundant virtual QWERTY grid.
    // On boards with both HAS_KEYBOARD and HAS_TOUCH, the user can optionally toggle the touch keypad.
    bool showOnScreenKeys = false;
#else
    constexpr bool showOnScreenKeys = true;
#endif
    // cursor coordinates: keep track of where the next character should be printed (in screen pixels)
    int cursor_x = 0;
    int cursor_y = 0;
    // keyboard navigation coordinates: keep track of which key (or button) is currently selected
    int x = 0;
    int y = -1; // -1 is where the top action buttons live, above the keys[][][] grid
    int old_x = 0;
    int old_y = 0;

    /*====================Initial Setup====================*/

    bool direction = true;
    bool last_dir = true;
#if defined(HAS_KEYBOARD) && defined(HAS_TOUCH)
    constexpr int has_kb_toggle = 1;
    const int kb_toggle_button = 5 + HAS_1_BUTTON;
#else
    constexpr int has_kb_toggle = 0;
#endif
    const int password_button = 5 + HAS_1_BUTTON + has_kb_toggle;

    const int PAD = FP + 1;
    const int KBLH = (BORDER_OFFSET_FROM_SCREEN_EDGE + 1) + LH * FM;
    const int counter_height = LH * FP;
    const int top_button_text_y = (FP + 1) + (KBLH - LH * FM) / 2;
    const int counter_y = KBLH + 4;
    const int textbox_y = KBLH + counter_height + 5;
    const int textbox_text_y = textbox_y + 2;
    const int keyboard_start_y = textbox_y + KBLH + 2;

    const int kb_toggle_x = 6 * PAD + 15 * LW * FM + HAS_1_BUTTON * (PAD + 4 * LW * FM);
    const int password_button_x = kb_toggle_x + has_kb_toggle * (PAD + 3 * LW * FM);
    const int password_button_width = tftWidth - password_button_x - PAD;
#if defined(HAS_TOUCH)
    // Keep existing controls in place and only add Show/Hide when its label fits.
    const bool password_toggle = mask_input && password_button_width >= 4 * LW * FP + 2 * PAD;
#else
    const bool password_toggle = false;
#endif
    const int buttons_number = password_button + (password_toggle ? 1 : 0);

    const int btns_layout[6 + HAS_1_BUTTON + has_kb_toggle][3] = {
        {1 * PAD + 0 * LW * FM,  3 * LW * FM,           1 * PAD + 0 * LW * FM + LW * FM / 2  }, // Ok
        {2 * PAD + 3 * LW * FM,  3 * LW * FM,           2 * PAD + 3 * LW * FM + LW * FM / 2  }, // ab/A@
        {3 * PAD + 6 * LW * FM,  3 * LW * FM,           3 * PAD + 6 * LW * FM + LW * FM / 2  }, // <-
        {4 * PAD + 9 * LW * FM,  3 * LW * FM,           4 * PAD + 9.5 * LW * FM + LW * FM / 2}, // _ {Space}
        {5 * PAD + 12 * LW * FM, 3 * LW * FM,           5 * PAD + 12 * LW * FM + LW * FM / 2 }, // Ex
#if HAS_1_BUTTON
        {6 * PAD + 15 * LW * FM, 4 * LW * FM,           6 * PAD + 15 * LW * FM + LW * FM / 2 }, // R/D or L/U
#endif
#if defined(HAS_KEYBOARD) && defined(HAS_TOUCH)
        {kb_toggle_x,            3 * LW * FM,           kb_toggle_x + LW * FM / 2            }, // KB toggle
#endif
        {password_button_x,      password_button_width, 0                                    }, // Show/Hide
    };

#if defined(HAS_KEYBOARD)
    // Compact top action bar used when physical-keyboard dialog mode is active (showOnScreenKeys == false)
    enum CompactAction : uint8_t { COMPACT_OK = 0, COMPACT_DEL, COMPACT_CANCEL, COMPACT_PASS, COMPACT_KB };
    struct CompactPill {
        int x;
        int w;
        CompactAction action;
        const char *label;
        bool smallFont;
        bool primaryOutline;
    };
    CompactPill compactPills[5];
    int compactPillCount = 0;
    auto rebuildCompactPills = [&]() {
        compactPillCount = 0;
        const int pillW = 3 * LW * FM + 4;
        int cx = PAD + 2;
        compactPills[compactPillCount++] = {cx, pillW, COMPACT_OK, "Ok", false, true};
        cx += pillW + PAD + 2;
        compactPills[compactPillCount++] = {cx, pillW, COMPACT_DEL, "<-", false, false};
        cx += pillW + PAD + 2;
        compactPills[compactPillCount++] = {cx, pillW, COMPACT_CANCEL, "Ex", false, false};
        cx += pillW + PAD + 2;
        if (mask_input) {
            int pw = 4 * LW * FP + 12;
            compactPills[compactPillCount++] = {
                cx, pw, COMPACT_PASS, show_password ? "Hide" : "Show", true, false
            };
        }
#if defined(HAS_TOUCH)
        const int kbW = 3 * LW * FM + 4;
        compactPills[compactPillCount++] = {tftWidth - kbW - PAD - 2, kbW, COMPACT_KB, "KB", false, false};
#endif
    };
    rebuildCompactPills();
    int compactFocus = 0;
#endif

    const int key_width = tftWidth / KeyboardWidth;
    const int key_height = (tftHeight - keyboard_start_y) / KeyboardHeight;
    const int text_offset_x = key_width / 2 - LW * FM / 2;
    const int text_offset_y = key_height / 2 - LH * FM / 2;

#if defined(HAS_TOUCH) // filling touch box list
    // Calculate actual box count
    const int keyboard_boxes = KeyboardHeight * KeyboardWidth;
    const int box_count = keyboard_boxes + buttons_number;

    box_t box_list[box_count];

    int k = 0;
    // Setup keyboard touch boxes
    for (int i = 0; i < KeyboardWidth; i++) {      // x coord
        for (int j = 0; j < KeyboardHeight; j++) { // y coord
            box_list[k].key = keys[j][i][0];
            box_list[k].key_sh = keys[j][i][1];
            box_list[k].color = ~bruceConfig.bgColor;
            box_list[k].x = i * key_width;
            box_list[k].y = j * key_height + keyboard_start_y;
            box_list[k].w = key_width;
            box_list[k].h = key_height;
            k++;
        }
    }
    const int buttons_start_index = k;
    // Setup buttons_strings touch boxes
    for (int i = 0; i < buttons_number; i++) {
        box_list[k].key = ' ';
        box_list[k].key_sh = ' ';
        box_list[k].color = ~bruceConfig.bgColor;
        box_list[k].x = btns_layout[i][0];
        box_list[k].y = 0;
        box_list[k].w = btns_layout[i][1];
        box_list[k].h = KBLH + 4;
        k++;
    }

    k = 0;
#endif

    tft.fillScreen(bruceConfig.bgColor); // reset the screen

    uint8_t longNextPress = 0;
    uint8_t longPrevPress = 0;
    unsigned long LongPressTmp = millis();

    // main loop
    bool firstGridDraw = true;
    while (1) {
        if (redraw) {
            uint16_t bgCol = bruceConfig.bgColor;
            uint16_t priCol = bruceConfig.priColor;
            uint16_t surfCol = uiSurfaceColor();
            uint16_t surfHiCol = uiSurfaceHiColor();
            uint16_t bdimCol = uiBorderDimColor();
            uint16_t brightCol = uiTextBrightColor();
            uint16_t mutedCol = uiTextMutedColor();
            uint16_t onAccentCol = uiTextOnAccentColor(priCol);

            String visible_text =
                mask_input && !show_password ? repeatString(current_text.length(), "*") : current_text;

#if defined(HAS_KEYBOARD)
            if (!showOnScreenKeys) {
                rebuildCompactPills();
                if (compactFocus >= compactPillCount) compactFocus = 0;

                // 1. Clean top action pills (Ok / <- / Ex / [Show] / [KB])
                tft.fillRect(0, 0, tftWidth, KBLH + 5, bgCol);
                tft.setTextSize(FM);
                for (int i = 0; i < compactPillCount; ++i) {
                    const CompactPill &p = compactPills[i];
                    bool focused = (i == compactFocus);
                    bool activeToggle = (p.action == COMPACT_PASS && show_password);
                    uint16_t fill = focused ? priCol : (activeToggle ? surfHiCol : surfCol);
                    uint16_t fg = focused ? onAccentCol : (activeToggle ? priCol : brightCol);
                    uint16_t outline = focused ? priCol : (p.primaryOutline ? priCol : bdimCol);
                    tft.fillRoundRect(p.x, 3, p.w, KBLH, 4, fill);
                    if (!focused) tft.drawRoundRect(p.x, 3, p.w, KBLH, 4, outline);
                    tft.setTextColor(fg, fill);
                    if (p.smallFont) {
                        tft.setTextSize(FP);
                        tft.drawCentreString(p.label, p.x + p.w / 2, 3 + (KBLH - LH * FP) / 2, 1);
                        tft.setTextSize(FM);
                    } else {
                        tft.drawCentreString(p.label, p.x + p.w / 2, 3 + (KBLH - LH * FM) / 2, 1);
                    }
                }

                // 2. Title, live counter, and restrained 哪吒魔童 ribbon divider
                const int titleY = KBLH + 10;
                tft.setTextSize(FP);
                String chars_counter = String(current_text.length()) + "/" + String(max_size);
                tft.fillRect(0, titleY - 1, tftWidth, LH * FP + 8, bgCol);
                tft.setTextColor(mutedCol, bgCol);
                tft.drawRightString(chars_counter, tftWidth - 6, titleY, 1);

                String trTitle = bruce_i18n::tr(textbox_title);
                int16_t maxTitlePx = tftWidth - ((chars_counter.length() * LW * FP) + 20);
                if (maxTitlePx < 24) maxTitlePx = 24;
                tft.setTextColor(brightCol, bgCol);
                tft.drawString(bruce_i18n::utf8TruncateToWidth(trTitle, maxTitlePx, FP), 6, titleY);
                drawNezhaRibbonRule(6, titleY + LH * FP + 4, tftWidth - 12);

                // 3. Generous multi-line rounded text input box with visible caret
                const int boxX = 6;
                const int boxY = titleY + LH * FP + 11;
                const int hintH = LH * FP + 10;
                const int boxW = tftWidth - 12;
                const int boxH = tftHeight - boxY - hintH - 4;
                tft.fillRoundRect(boxX, boxY, boxW, boxH, 6, surfCol);
                tft.drawRoundRect(boxX, boxY, boxW, boxH, 6, priCol);

                const int innerPad = 8;
                const int innerW = boxW - innerPad * 2;
                const int innerH = boxH - innerPad * 2;
                int textScale = FM;
                int charW = LW * textScale;
                int lineH = LH * textScale + 4;
                int colsPerLine = innerW / charW;
                if (colsPerLine < 1) colsPerLine = 1;
                int maxLines = innerH / lineH;
                if (maxLines < 1) maxLines = 1;

                String displayStr = visible_text + "_";
                if (static_cast<int>(displayStr.length()) > colsPerLine * maxLines) {
                    textScale = FP;
                    charW = LW * textScale;
                    lineH = LH * textScale + 3;
                    colsPerLine = innerW / charW;
                    if (colsPerLine < 1) colsPerLine = 1;
                    maxLines = innerH / lineH;
                    if (maxLines < 1) maxLines = 1;
                }

                tft.setTextSize(textScale);
                int totalLen = static_cast<int>(displayStr.length());
                int totalLines = (totalLen + colsPerLine - 1) / colsPerLine;
                int startLine = (totalLines > maxLines) ? (totalLines - maxLines) : 0;
                for (int ln = startLine; ln < totalLines; ++ln) {
                    int startIdx = ln * colsPerLine;
                    int endIdx = startIdx + colsPerLine;
                    if (endIdx > totalLen) endIdx = totalLen;
                    String seg = displayStr.substring(startIdx, endIdx);
                    int drawY = boxY + innerPad + (ln - startLine) * lineH;
                    bool isLastSeg = (ln == totalLines - 1);
                    if (isLastSeg && seg.length() > 0) {
                        String body = seg.substring(0, seg.length() - 1);
                        tft.setTextColor(brightCol, surfCol);
                        tft.drawString(body, boxX + innerPad, drawY);
                        int caretX = boxX + innerPad + static_cast<int>(body.length()) * charW;
                        tft.setTextColor(priCol, surfCol);
                        tft.drawString("_", caretX, drawY);
                    } else {
                        tft.setTextColor(brightCol, surfCol);
                        tft.drawString(seg, boxX + innerPad, drawY);
                    }
                }

                // 4. Concise bottom keyboard hint bar
                const int hintY = tftHeight - LH * FP - 5;
                tft.fillRect(0, hintY - 2, tftWidth, LH * FP + 7, bgCol);
                tft.setTextSize(FP);
                tft.setTextColor(mutedCol, bgCol);
#if defined(HAS_TOUCH)
                const char *hintText = "Enter: OK  Del: Erase  [Ex]: Back  [KB]: Touch";
#else
                const char *hintText = "Enter: OK  Del: Erase  Ex: Back";
#endif
                tft.drawCentreString(
                    bruce_i18n::utf8TruncateToWidth(hintText, tftWidth - 8, FP), tftWidth / 2, hintY, 1
                );

                firstGridDraw = false;
                redraw = false;
            } else
#endif
            {
                tft.setCursor(0, 0);
                tft.setTextSize(FM);

                // Draw the top row action pills
                bool capsChanged = (caps != last_caps);
                if (firstGridDraw || y < 0 || old_y < 0 || capsChanged || direction != last_dir) {
                    last_caps = caps;
                    last_dir = direction;
                    tft.fillRect(0, 1, tftWidth, KBLH + 2, bgCol);

                    auto drawTopPill = [&](int idx, const char *lbl, bool activeToggle, bool primaryOutline,
                                           bool smallFont) {
                        bool focused = (x == idx && y == -1);
                        uint16_t fill = focused ? priCol : (activeToggle ? surfHiCol : surfCol);
                        uint16_t fg = focused ? onAccentCol : (activeToggle ? priCol : brightCol);
                        uint16_t outline = focused ? priCol : (primaryOutline ? priCol : bdimCol);
                        int bx = btns_layout[idx][0];
                        int bw = btns_layout[idx][1];
                        tft.fillRoundRect(bx, 2, bw, KBLH, 4, fill);
                        if (!focused) tft.drawRoundRect(bx, 2, bw, KBLH, 4, outline);
                        tft.setTextColor(fg, fill);
                        if (smallFont) {
                            tft.setTextSize(FP);
                            tft.drawCentreString(lbl, bx + bw / 2, 2 + (KBLH - LH * FP) / 2, 1);
                            tft.setTextSize(FM);
                        } else {
                            tft.drawString(lbl, btns_layout[idx][2], top_button_text_y);
                        }
                    };

                    drawTopPill(0, "Ok", false, true, false);
                    drawTopPill(1, caps ? "ab" : "A@", caps, false, false);
                    drawTopPill(2, "<-", false, false, false);
                    drawTopPill(3, "_", false, false, false);
                    drawTopPill(4, "Ex", false, false, false);
#if HAS_1_BUTTON
                    drawTopPill(5, direction ? "R/D" : "L/U", !direction, false, false);
#endif
#if defined(HAS_KEYBOARD) && defined(HAS_TOUCH)
                    drawTopPill(kb_toggle_button, "KB", true, false, false);
#endif
                    if (password_toggle) {
                        drawTopPill(
                            password_button, show_password ? "Hide" : "Show", show_password, false, true
                        );
                    }
                }

                // Prints the chars counter & clean muted title
                tft.setTextSize(FP);
                String chars_counter = String(current_text.length()) + "/" + String(max_size);
                tft.fillRect(0, counter_y, tftWidth, LH * FP + 2, bgCol);
                tft.setTextColor(mutedCol, bgCol);
                tft.drawRightString(chars_counter, tftWidth - 5, counter_y, 1);

                String trTitle = bruce_i18n::tr(textbox_title);
                int16_t maxTitlePx = tftWidth - ((chars_counter.length() * LW * FP) + 18);
                if (maxTitlePx < 24) maxTitlePx = 24;
                tft.setTextColor(mutedCol, bgCol);
                tft.drawString(bruce_i18n::utf8TruncateToWidth(trTitle, maxTitlePx, FP), 5, counter_y);

                // Clean rounded neutral input field with crisp accent outline
                tft.fillRoundRect(3, textbox_y, tftWidth - 6, KBLH, 5, surfCol);
                tft.drawRoundRect(3, textbox_y, tftWidth - 6, KBLH, 5, priCol);
                tft.setTextColor(brightCol, surfCol);

                if (current_text.length() > max_FM_size) {
                    tft.setTextSize(FP);
                    if (current_text.length() > max_FP_size) {
                        tft.drawString(visible_text.substring(0, max_FP_size), 7, textbox_text_y);
                        tft.drawString(
                            visible_text.substring(max_FP_size, current_text.length()),
                            7,
                            textbox_text_y + LH * FP
                        );
                    } else {
                        tft.drawString(visible_text, 7, textbox_text_y);
                    }
                } else {
                    tft.setTextSize(FM);
                    tft.drawString(visible_text, 7, textbox_text_y);
                }

                // Draw keycap grid (partial update on cursor move; full draw on init / caps change)
                tft.setTextSize(FM);
                bool redrawAllKeys = firstGridDraw || capsChanged;
                for (int i = 0; i < KeyboardHeight; i++) {
                    for (int j = 0; j < KeyboardWidth; j++) {
                        bool isSel = (x == j && y == i);
                        bool wasSel = (old_x == j && old_y == i);
                        if (!redrawAllKeys && !isSel && !wasSel) continue;

                        int key_x = j * key_width;
                        int key_y = i * key_height + keyboard_start_y;
                        uint16_t tileBg = isSel ? priCol : surfCol;
                        uint16_t tileFg = isSel ? onAccentCol : brightCol;

                        tft.fillRect(key_x, key_y, key_width, key_height, bgCol);
                        tft.fillRoundRect(key_x + 1, key_y + 1, key_width - 2, key_height - 2, 4, tileBg);
                        if (!isSel) {
                            tft.drawRoundRect(key_x + 1, key_y + 1, key_width - 2, key_height - 2, 4, bdimCol);
                        }
                        tft.setTextColor(tileFg, tileBg);
                        char ch = caps ? keys[i][j][1] : keys[i][j][0];
                        if (ch != '\0') {
                            tft.drawString(String(ch), key_x + text_offset_x, key_y + text_offset_y);
                        }
                    }
                }
                firstGridDraw = false;
                old_x = x;
                old_y = y;
                redraw = false;
            }
        }

        // Cursor Handler
        if (current_text.length() > max_FM_size) {
            tft.setTextSize(FP);
            if (current_text.length() > (max_FP_size)) {
                cursor_y = textbox_text_y + LH * FP;
                cursor_x = BORDER_OFFSET_FROM_SCREEN_EDGE + (current_text.length() - max_FP_size) * LW * FP;
            } else {
                cursor_y = textbox_text_y;
                cursor_x = BORDER_OFFSET_FROM_SCREEN_EDGE + current_text.length() * LW * FP;
            }
        } else {
            cursor_y = textbox_text_y;
            cursor_x = BORDER_OFFSET_FROM_SCREEN_EDGE + current_text.length() * LW * FM;
        }
        // Prioritize Serial Input for navigation
        if (SerialCmdPress) { // only for Remote Control, if no type of input was detected on device
            if (check(SelPress)) {
                selection_made = true;
            }
            /* Next-Prev Btns to move in X axis (right-left) */
            else if (check(NextPress)) {
                NextPress = false;
                longNextPress = false;
                x++;
                if ((y < 0 && x >= buttons_number) || x >= KeyboardWidth) x = 0;
                redraw = true;
            }
            /* Down-Up Btns to move in Y axis */
            else if (check(PrevPress)) {
                PrevPress = false;
                longPrevPress = false;
                x--;
                if (y < 0 && x >= buttons_number) x = buttons_number - 1;
                else if (x < 0) x = KeyboardWidth - 1;
                redraw = true;
            }
            /* Down-Up Btns to move in Y axis */
            else if (check(DownPress)) {
                y++;
                if (y >= KeyboardHeight) { y = -1; }
                redraw = true;
            } else if (check(UpPress)) {
                y--;
                if (y < -1) y = KeyboardHeight - 1;
                redraw = true;
            }
            last_input_time = millis() + 100;
        }

#if defined(HAS_KEYBOARD)
        // Poll physical keyboard immediately on every tick (no 250ms throttle that drops fast typing)
        {
            keyStroke key = _getKeyPress();
            if (key.pressed) {
                wakeUpScreen();
                if ((key.fn && key.del) ||
                    (key.exit_key && (key.word.empty() || key.word[0] == (char)0x1B))) {
                    EscPress = false;
                    current_text = "\x1B";
                    break;
                }
                bool appended = false;
                for (char c : key.word) {
                    if (c == (char)0x08 || c == (char)0x0D || c == (char)0x0A || c == (char)0x1B) {
                        continue;
                    }
                    if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) <= 0x7E &&
                        static_cast<int>(current_text.length()) < max_size) {
                        current_text += c;
                        appended = true;
                    }
                }
                if (appended) redraw = true;
                if (key.del) {
                    EscPress = false; // Backspace erases a char in text input, never exits the dialog
                    if (current_text.length() > 0) {
                        current_text.remove(current_text.length() - 1);
                        redraw = true;
                    }
                }
                if (key.enter) {
                    SelPress = false;
                    break;
                }
            }
        }
#endif

#if defined(HAS_TOUCH) // T-Deck, Tab5, CYD, Core2, CoreS3
        {
            bool hasTouch = false;
            int tx = 0;
            int ty = 0;
            inputLock();
            if (touchPoint.pressed) {
                hasTouch = true;
                tx = touchPoint.x;
                ty = touchPoint.y;
                touchPoint.Clear();
            }
            inputUnlock();

            if (hasTouch && (millis() - last_touch_time >= 140)) {
                last_touch_time = millis();
                wakeUpScreen();
                SelPress = false;
                EscPress = false;
                NextPress = false;
                PrevPress = false;
                UpPress = false;
                DownPress = false;

#if defined(HAS_KEYBOARD)
                if (!showOnScreenKeys) {
                    bool exitOk = false;
                    bool exitCancel = false;
                    for (int i = 0; i < compactPillCount; ++i) {
                        const CompactPill &p = compactPills[i];
                        if (tx >= p.x && tx < p.x + p.w && ty >= 0 && ty < KBLH + 8) {
                            compactFocus = i;
                            switch (p.action) {
                                case COMPACT_OK: exitOk = true; break;
                                case COMPACT_DEL:
                                    if (current_text.length() > 0) {
                                        current_text.remove(current_text.length() - 1);
                                        redraw = true;
                                    }
                                    break;
                                case COMPACT_CANCEL:
                                    current_text = "\x1B";
                                    exitCancel = true;
                                    break;
                                case COMPACT_PASS:
                                    show_password = !show_password;
                                    redraw = true;
                                    break;
                                case COMPACT_KB:
                                    showOnScreenKeys = true;
                                    tft.fillScreen(bruceConfig.bgColor);
                                    x = 0;
                                    y = -1;
                                    firstGridDraw = true;
                                    redraw = true;
                                    break;
                            }
                            break;
                        }
                    }
                    if (exitOk || exitCancel) break;
                } else
#endif
                {
                    x = 0;
                    y = -1;
                    bool touchHandled = false;
                    bool exitOk = false;
                    bool exitCancel = false;

                    if (password_toggle &&
                        box_list[buttons_start_index + password_button].contain(tx, ty)) {
                        show_password = !show_password;
                        touchHandled = true;
                    }
#if defined(HAS_KEYBOARD) && defined(HAS_TOUCH)
                    else if (box_list[buttons_start_index + kb_toggle_button].contain(tx, ty)) {
                        showOnScreenKeys = false;
                        tft.fillScreen(bruceConfig.bgColor);
                        firstGridDraw = true;
                        touchHandled = true;
                    }
#endif
                    else if (box_list[buttons_start_index].contain(tx, ty)) { // OK btn
                        exitOk = true;
                    } else if (box_list[buttons_start_index + 1].contain(tx, ty)) { // CAPS btn
                        caps = !caps;
                        touchHandled = true;
                    } else if (box_list[buttons_start_index + 2].contain(tx, ty)) { // DEL btn
                        if (current_text.length() > 0) {
                            current_text.remove(current_text.length() - 1);
                            touchHandled = true;
                        }
                    } else if (box_list[buttons_start_index + 3].contain(tx, ty)) { // SPACE btn
                        if (static_cast<int>(current_text.length()) < max_size) {
                            handleSpaceAdd(current_text, max_size);
                            touchHandled = true;
                        }
                    } else if (box_list[buttons_start_index + 4].contain(tx, ty)) { // BACK btn
                        current_text = "\x1B";
                        exitCancel = true;
                    }
#if HAS_1_BUTTON
                    else if (box_list[buttons_start_index + 5].contain(tx, ty)) { // DIRECTION btn
                        direction = !direction;
                        touchHandled = true;
                    }
#endif
                    else {
                        for (k = 0; k < keyboard_boxes; k++) {
                            if (box_list[k].contain(tx, ty)) {
                                char ch = caps ? box_list[k].key_sh : box_list[k].key;
                                if (ch != '\0' && static_cast<int>(current_text.length()) < max_size) {
                                    current_text += ch;
                                    touchHandled = true;
                                }
                                break;
                            }
                        }
                    }

                    if (exitOk || exitCancel) break;
                    if (touchHandled) { redraw = true; }
                }
            }
        }
#endif

        if (millis() - last_input_time > 250) { // INPUT DEBOUCING
            // waits at least 250ms before accepting another button input, to prevent rapid repeats

#if HAS_1_BUTTON
            auto moveKeyboardCursorNext = [&]() {
                if (y < 0) {
                    x++;
                    if (x >= buttons_number) {
                        y = 0;
                        x = 0;
                    }
                } else {
                    x++;
                    if (x >= KeyboardWidth) {
                        x = 0;
                        y++;
                    }
                    if (y >= KeyboardHeight) { y = -1; }
                }
                redraw = true;
            };

            auto moveKeyboardCursorPrev = [&]() {
                if (y < 0) {
                    x--;
                    if (x >= buttons_number) x = buttons_number - 2;
                    if (x < 0) {
                        y = KeyboardHeight - 1;
                        x = KeyboardWidth - 1;
                    }
                } else {
                    x--;
                    if (x < 0) {
                        y--;
                        if (y < 0) {
                            y = -1;
                            x = buttons_number - 1;
                        } else {
                            x = KeyboardWidth - 1;
                        }
                    }
                }
                redraw = true;
            };

            if (check(EscPress)) {
                direction = !direction;
                redraw = true;
            }
            if (check(SelPress)) {
                selection_made = true;
            } else if (check(NextPress)) {
                if (direction) moveKeyboardCursorNext();
                else moveKeyboardCursorPrev();
            } else if (check(PrevPress)) {
                if (direction) {
                    y++;
                    if (y >= KeyboardHeight) { y = -1; }
                } else {
                    y--;
                    if (y < -1) y = KeyboardHeight - 1;
                }
                redraw = true;
            }
#elif defined(HAS_3_BUTTONS) // StickCs and Core
            if (check(SelPress)) {
                selection_made = true;
            } else {
                /* Down Btn to move in X axis (to the right) */
                if (longNextPress || NextPress) {
                    unsigned long now = millis();
                    if (!longNextPress) {
                        longNextPress = 1;
                        LongPress = true;
                        LongPressTmp = now;
                    }
                    delay(1); // does not work without it
                    // Check if the button is held long enough (long press)
                    if (now - LongPressTmp > 300) {
                        x--; // Long press action
                        longNextPress = 2;
                        LongPress = false;
                        check(NextPress);
                        LongPressTmp = now;
                    } else if (!NextPress) {
                        if (longNextPress != 2) x++; // Short press action
                        longNextPress = 0;
                    } else {
                        continue;
                    }
                    LongPress = false;
                    // delay(10);
                    if (y < 0 && x >= buttons_number) x = 0;
                    if (x >= KeyboardWidth) x = 0;
                    else if (x < 0) x = KeyboardWidth - 1;

                    // Skip over keys with '\0' value
                    if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                        while (keys[y][x][caps] == '\0') {
                            x++;
                            if (x >= KeyboardWidth) {
                                x = 0;
                                y++;
                                if (y >= KeyboardHeight) {
                                    y = -1;
                                    break;
                                }
                            }
                        }
                    }

                    redraw = true;
                }
                /* UP Btn to move in Y axis (Downwards) */
                if (longPrevPress || PrevPress) {
                    unsigned long now = millis();
                    if (!longPrevPress) {
                        longPrevPress = 1;
                        LongPress = true;
                        LongPressTmp = now;
                    }
                    delay(1); // does not work without it
                    // Check if the button is held long enough (long press)
                    if (now - LongPressTmp > 300) {
                        y--; // Long press action
                        longPrevPress = 2;
                        LongPress = false;
                        check(PrevPress);
                        LongPressTmp = now;
                    } else if (!PrevPress) {
                        if (longPrevPress != 2) y++; // Short press action
                        longPrevPress = 0;
                    } else {
                        continue;
                    }
                    LongPress = false;
                    if (y >= KeyboardHeight) {
                        y = -1;
                    } else if (y < -1) y = KeyboardHeight - 1;

                    // Skip over keys with '\0' value
                    if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                        while (keys[y][x][caps] == '\0') {
                            y++;
                            if (y >= KeyboardHeight) {
                                y = -1;
                                break;
                            }
                        }
                    }

                    redraw = true;
                }
            }
#elif defined(HAS_5_BUTTONS) // Smoochie and Marauder-Mini
            if (check(SelPress)) {
                selection_made = true;
            } else {
                /* Down Btn to move in X axis (to the right) */
                if (check(NextPress)) {
                    x++;
                    if ((y < 0 && x >= buttons_number) || x >= KeyboardWidth) x = 0;

                    // Skip over keys with '\0' value
                    if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                        while (keys[y][x][caps] == '\0') {
                            x++;
                            if (x >= KeyboardWidth) {
                                x = 0;
                                y++;
                                if (y >= KeyboardHeight) {
                                    y = -1;
                                    break;
                                }
                            }
                        }
                    }

                    redraw = true;
                }
                if (check(PrevPress)) {
                    x--;
                    if (y < 0 && x >= buttons_number) x = buttons_number - 1;
                    else if (x < 0) x = KeyboardWidth - 1;

                    // Skip over keys with '\0' value when moving backwards
                    if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                        while (keys[y][x][caps] == '\0') {
                            x--;
                            if (x < 0) {
                                x = KeyboardWidth - 1;
                                y--;
                                if (y < 0) {
                                    y = -1;
                                    x = buttons_number - 1;
                                    break;
                                }
                            }
                        }
                    }

                    redraw = true;
                }
                /* UP Btn to move in Y axis (Downwards) */
                if (check(DownPress)) {
                    y++;
                    if (y > KeyboardHeight - 1) { y = -1; }

                    // Skip over keys with '\0' value
                    if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                        while (keys[y][x][caps] == '\0') {
                            y++;
                            if (y >= KeyboardHeight) {
                                y = -1;
                                break;
                            }
                        }
                    }

                    redraw = true;
                }
                if (check(UpPress)) {
                    y--;
                    if (y < -1) y = KeyboardHeight - 1;

                    // Skip over keys with '\0' value when moving upwards
                    if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                        while (keys[y][x][caps] == '\0') {
                            y--;
                            if (y < 0) {
                                y = -1;
                                break;
                            }
                        }
                    }

                    redraw = true;
                }
            }
#elif defined(HAS_KEYBOARD)  // Cardputer, T-Deck, T-LoRa-Pager, Tab5
            if (!showOnScreenKeys) {
                if (check(NextPress) || check(DownPress)) {
                    compactFocus = (compactFocus + 1) % compactPillCount;
                    redraw = true;
                    last_input_time = millis();
                } else if (check(PrevPress) || check(UpPress)) {
                    compactFocus = (compactFocus - 1 + compactPillCount) % compactPillCount;
                    redraw = true;
                    last_input_time = millis();
                }
#if !defined(T_LORA_PAGER) // T-LoRa-Pager does not have a dedicated select button
                else if (check(SelPress)) {
                    const CompactPill &p = compactPills[compactFocus];
                    last_input_time = millis();
                    if (p.action == COMPACT_OK) {
                        break;
                    } else if (p.action == COMPACT_CANCEL) {
                        current_text = "\x1B";
                        break;
                    } else if (p.action == COMPACT_DEL) {
                        if (current_text.length() > 0) {
                            current_text.remove(current_text.length() - 1);
                            redraw = true;
                        }
                    } else if (p.action == COMPACT_PASS) {
                        show_password = !show_password;
                        redraw = true;
                    }
#if defined(HAS_TOUCH)
                    else if (p.action == COMPACT_KB) {
                        showOnScreenKeys = true;
                        tft.fillScreen(bruceConfig.bgColor);
                        x = 0;
                        y = -1;
                        firstGridDraw = true;
                        redraw = true;
                    }
#endif
                }
#endif
            } else {
                if (check(SelPress)) {
                    selection_made = true;
                } else if (check(NextPress)) {
                    x++;
                    if ((y < 0 && x >= buttons_number) || x >= KeyboardWidth) x = 0;
                    redraw = true;
                    last_input_time = millis();
                } else if (check(PrevPress)) {
                    x--;
                    if (y < 0 && x < 0) x = buttons_number - 1;
                    else if (x < 0) x = KeyboardWidth - 1;
                    redraw = true;
                    last_input_time = millis();
                } else if (check(DownPress)) {
                    y++;
                    if (y >= KeyboardHeight) y = -1;
                    if (y < 0 && x >= buttons_number) x = buttons_number - 1;
                    redraw = true;
                    last_input_time = millis();
                } else if (check(UpPress)) {
                    y--;
                    if (y < -1) y = KeyboardHeight - 1;
                    if (y < 0 && x >= buttons_number) x = buttons_number - 1;
                    redraw = true;
                    last_input_time = millis();
                }
            }
#endif

#if defined(HAS_ENCODER) // T-Embed and T-LoRa-Pager and WaveSentry
                         // WaveSentry has Touchscreen and Encoder, but the touchscreen is prioritized
                         // if touchscreen is pressed, ignore the encoder input
#if !defined(HAS_TOUCH)
            LongPress = true;
#endif
            if ((check(SelPress) || selection_made) && touchPoint.pressed == false) {
                LongPress = false;
                selection_made = true;
            } else {
                int32_t rotarySteps = drainRotarySteps();
                if (rotarySteps != 0 && touchPoint.pressed == false) {
                    check(NextPress);
                    check(PrevPress);
                    while (rotarySteps < 0) {
                        if (EscPress) {
                            y++;
                        } else if (
                            (x >= buttons_number - 1 && y <= -1) || (x >= KeyboardWidth - 1 && y >= 0)
                        ) {
                            y++;
                            x = 0;
                        } else x++;

                        if (y >= KeyboardHeight) y = -1;
                        if (y == -1 && x >= buttons_number) x = 0;

                        if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                            while (keys[y][x][caps] == '\0') {
                                x++;
                                if (x >= KeyboardWidth) {
                                    x = 0;
                                    y++;
                                    if (y >= KeyboardHeight) {
                                        y = -1;
                                        break;
                                    }
                                }
                            }
                        }

                        rotarySteps++;
                        yield();
                        PrevPress = false;
                        NextPress = false;
                        UpPress = false;
                        DownPress = false;
                        redraw = true;
                    }
                    while (rotarySteps > 0) {
                        if (EscPress) {
                            y--;
                        } else if (x <= 0) {
                            y--;
                            if (y == -1) x = buttons_number - 1;
                            else x = KeyboardWidth - 1;
                        } else x--;

                        if (y < -1) {
                            y = KeyboardHeight - 1;
                            x = KeyboardWidth - 1;
                        }

                        if (y >= 0 && y < KeyboardHeight && x >= 0 && x < KeyboardWidth) {
                            while (keys[y][x][caps] == '\0') {
                                x--;
                                if (x < 0) {
                                    x = KeyboardWidth - 1;
                                    y--;
                                    if (y < 0) {
                                        y = -1;
                                        x = buttons_number - 1;
                                        break;
                                    }
                                }
                            }
                        }

                        rotarySteps--;
                        vTaskDelay(4 / portTICK_PERIOD_MS);
                        PrevPress = false;
                        NextPress = false;
                        UpPress = false;
                        DownPress = false;
                        redraw = true;
                    }
                }
            }
#endif
        } // end of physical input detection
        if (selection_made) { // if something was selected then handle it
            selection_made = false;

            char selected_char = (y == -1) ? ' ' : keys[y][x][caps];

            if (selected_char == '\0') { continue; } // if we selected a key which have the value of

            if (password_toggle && y == -1 && x == password_button) {
                show_password = !show_password;
                redraw = true;
                last_input_time = millis();
                continue;
            }
#if defined(HAS_KEYBOARD) && defined(HAS_TOUCH)
            if (y == -1 && x == kb_toggle_button) {
                showOnScreenKeys = !showOnScreenKeys;
                tft.fillScreen(bruceConfig.bgColor);
                firstGridDraw = true;
                redraw = true;
                last_input_time = millis();
                continue;
            }
#endif

            KeyboardAction action = handleKeyboardSelection(
                x,
                y,
                current_text,
                caps,
                direction,
                cursor_x,
                cursor_y,
                max_size,
                selected_char,
                mask_input && !show_password
            );

            if (action == KEYBOARD_OK) { // OK BTN
                break;
            } else if (action == KEYBOARD_CANCEL) { // BACK BTN
                current_text = "\x1B";              // ASCII ESC CHARACTER
                break;
            } else if (action == KEYBOARD_REDRAW) {
                redraw = true;
            }

            last_input_time = millis();
        }
#ifdef HAS_ENCODER
        vTaskDelay(pdMS_TO_TICKS(4));
#else
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
    }

    // Resets screen when finished writing
    tft.fillScreen(bruceConfig.bgColor);
    resetTftDisplay();

    return current_text;
}

/// Opens a simple option menu to select the keyboard layout/language.
/// The choice is stored in bruceConfig.keyboardLang and saved to flash.
void setKeyboardLanguage() {
    std::vector<Option> langOptions = {
        {"QWERTY (English)",
         []() {
             bruceConfig.keyboardLang = "QWERTY";
             bruceConfig.saveFile();
         }                           },
        {"AZERTY (Français)",
         []() {
             bruceConfig.keyboardLang = "AZERTY";
             bruceConfig.saveFile();
         }                           },
        {"QWERTZ (Deutsch)",
         []() {
             bruceConfig.keyboardLang = "QWERTZ";
             bruceConfig.saveFile();
         }                           },
        {"Back",              []() {}},
    };

    // Show current selection in the title
    String title = String("Keyboard: ") + bruceConfig.keyboardLang;
    loopOptions(langOptions, MENU_TYPE_SUBMENU, title.c_str());
}

/// This calls the keyboard. The keyset is chosen based on bruceConfig.keyboardLang.
/// Supported values: "QWERTY" (default), "AZERTY" (French), "QWERTZ" (German).
/// Returns the user typed string, or the ASCII ESC character if cancelled.
String keyboard(const String &current_text, int max_size, const String &textbox_title, bool mask_input) {
    String lang = bruceConfig.keyboardLang;
    if (lang == "AZERTY") {
        return generalKeyboard<azerty_keyboard_height, azerty_keyboard_width>(
            current_text, max_size, textbox_title, azerty_keyset, mask_input
        );
    } else if (lang == "QWERTZ") {
        return generalKeyboard<qwertz_keyboard_height, qwertz_keyboard_width>(
            current_text, max_size, textbox_title, qwertz_keyset, mask_input
        );
    } else {
        // Default: QWERTY
        return generalKeyboard<qwerty_keyboard_height, qwerty_keyboard_width>(
            current_text, max_size, textbox_title, qwerty_keyset, mask_input
        );
    }
}

/// This calls a keyboard with the characters useful to write hexadecimal codes.
/// Returns the user typed strings, return the ASCII ESC character if the operation was cancelled
String hex_keyboard(const String &current_text, int max_size, const String &textbox_title, bool mask_input) {
    return generalKeyboard<hex_keyboard_height, hex_keyboard_width>(
        current_text, max_size, textbox_title, hex_keyset, mask_input
    );
}

/// This calls a numbers only keyboard. Returns the user typed strings, return the ASCII ESC character
/// if the operation was cancelled
String num_keyboard(const String &current_text, int max_size, const String &textbox_title, bool mask_input) {
    return generalKeyboard<numpad_keyboard_height, numpad_keyboard_width>(
        current_text, max_size, textbox_title, numpad_keyset, mask_input
    );
}

void powerOff() { displayWarning("Not available", true); }
void goToDeepSleep() {
#if DEEPSLEEP_WAKEUP_PIN >= 0

#if SOC_PM_SUPPORT_EXT0_WAKEUP
    esp_sleep_enable_ext0_wakeup((gpio_num_t)DEEPSLEEP_WAKEUP_PIN, DEEPSLEEP_PIN_ACT);
#elif SOC_PM_SUPPORT_EXT1_WAKEUP
    esp_sleep_enable_ext1_wakeup((gpio_num_t)DEEPSLEEP_WAKEUP_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
    esp_deep_sleep_start();
#else
    displayWarning("Not available", true);
#endif
}

void checkReboot() {}
