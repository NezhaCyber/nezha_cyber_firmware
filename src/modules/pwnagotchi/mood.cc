/*
Thanks to thoses developers for their projects:
* @7h30th3r0n3 : https://github.com/7h30th3r0n3/Evil-M5Core2 and https://github.com/7h30th3r0n3/PwnGridSpam
* @viniciusbo : https://github.com/viniciusbo/m5-palnagotchi
* @sduenasg : https://github.com/sduenasg/pio_palnagotchi

Thanks to @bmorcelli for his help doing a better code.
*/
#if !defined(LITE_VERSION)
#include "mood.h"

// Nezha Demon Child (哪吒魔童) cyber-pet moods — stored as const char* to save Flash & RAM
static const char *const kNezhaMoodFaces[] = {
    "(v__v)", // 0 - sleeping
    "(=__=)", // 1 - awakening
    "(O__O)", // 2 - awake
    "( O_O)", // 3 - observing right
    "(O_O )", // 4 - observing left
    "( 0_0)", // 5 - observing happy right
    "(0_0 )", // 6 - observing happy left
    "(+__+)", // 7 - intense
    "(-@_@)", // 8 - cool
    "(0__0)", // 9 - happy
    "(^__^)", // 10 - grateful
    "(^o^ )", // 11 - excited
    "(+__+)", // 12 - smart
    "(*__*)", // 13 - friendly
    "(@__@)", // 14 - motivated
    "(>__<)", // 15 - demotivated
    "(-__-)", // 16 - bored
    "(T_T )", // 17 - sad
    "(;__;)", // 18 - lonely
    "(X__X)", // 19 - broken
    "(#__#)", // 20 - debugging
    "(-_-')", // 21 - angry
    "(^__^)", // 22 - helping
};

static const char *const kNezhaMoodPhrases[] = {
    "Zzzz... lotus rest",            // 0 - sleeping
    "Waking Demon Child...",         // 1 - awakening
    "Nezha is awake!",               // 2 - awake
    "Scanning airwaves...",          // 3 - observing right
    "Watching the spectrum...",      // 4 - observing left
    "Hunting handshakes!",           // 5 - observing happy right
    "Wind-Fire Wheels spinning!",    // 6 - observing happy left
    "So many signals nearby!",       // 7 - intense
    "My fate is mine!",              // 8 - cool
    "More peers, more power!",       // 9 - happy
    "Nezha Cyber crew!",             // 10 - grateful
    "Fire-tipped spear ready!",      // 11 - excited
    "802.11 frames decoded.",        // 12 - smart
    "Hey peer, join Nezha!",         // 13 - friendly
    "Lotus fire online!",            // 14 - motivated
    "No handshakes yet...",          // 15 - demotivated
    "Let's go wardriving...",        // 16 - bored
    "Quiet spectrum today...",       // 17 - sad
    "Where are my Nezha peers?",     // 18 - lonely
    "Radio hiccup detected.",        // 19 - broken
    "Tracing beacons...",            // 20 - debugging
    "Channel jammed, grrr!",         // 21 - angry
    "Nezha Cyber guardian.",         // 22 - helping
};

uint8_t current_mood = 0;
String current_phrase = "";
String current_face = "";
bool current_broken = false;
const int number_of_moods = sizeof(kNezhaMoodFaces) / sizeof(kNezhaMoodFaces[0]);

uint8_t getCurrentMoodId() { return current_mood; }
int getNumberOfMoods() { return number_of_moods; }
String getCurrentMoodFace() { return current_face; }
String getCurrentMoodPhrase() { return current_phrase; }
bool isCurrentMoodBroken() { return current_broken; }

void setMood(uint8_t mood, String face, String phrase, bool broken) {
    if (mood >= number_of_moods) mood = 0;
    current_mood = mood;
    current_broken = broken;

    current_face = (face.length() > 0) ? face : String(kNezhaMoodFaces[current_mood]);
    current_phrase = (phrase.length() > 0) ? phrase : String(kNezhaMoodPhrases[current_mood]);
}
#endif
