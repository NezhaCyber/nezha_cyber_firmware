#include "log_commands.h"
#include "core/logging/nzlog.h"
#include "core/sd_functions.h"
#include <SD.h>
#include <cstring>
#include <esp32-hal-psram.h>
#include <globals.h>

namespace {

constexpr size_t kTailChunk = 8192;
constexpr int kMaxTailLines = 200;

const char *levelToken(nzlog::Level lvl) {
    switch (lvl) {
        case nzlog::VERBOSE: return "v";
        case nzlog::DEBUG: return "d";
        case nzlog::INFO: return "i";
        case nzlog::WARN: return "w";
        case nzlog::ERROR: return "e";
        case nzlog::FATAL: return "f";
        default: return "i";
    }
}

bool parseLevel(const String &token, nzlog::Level *out) {
    String t = token;
    t.toLowerCase();
    if (t == "v" || t == "verbose" || t == "0") *out = nzlog::VERBOSE;
    else if (t == "d" || t == "debug" || t == "1") *out = nzlog::DEBUG;
    else if (t == "i" || t == "info" || t == "2") *out = nzlog::INFO;
    else if (t == "w" || t == "warn" || t == "3") *out = nzlog::WARN;
    else if (t == "e" || t == "error" || t == "4") *out = nzlog::ERROR;
    else if (t == "f" || t == "fatal" || t == "5") *out = nzlog::FATAL;
    else return false;
    return true;
}

void printTail(uint32_t lines) {
    nzlog::begin(); // retry opening if the card was mounted late
    if (!sdcardMounted) {
        serialDevice->println("No SD card mounted. Logs are serial-only.");
        return;
    }

    File f = SD.open(nzlog::path(), FILE_READ);
    if (!f) {
        serialDevice->println("No log file yet: " + String(nzlog::path()));
        return;
    }

    size_t size = f.size();
    size_t want = size > kTailChunk ? kTailChunk : size;
    size_t start = size - want;

    char *buf = psramFound() ? static_cast<char *>(ps_malloc(kTailChunk + 1))
                             : static_cast<char *>(malloc(kTailChunk + 1));
    if (buf == nullptr) {
        f.close();
        serialDevice->println("Out of memory reading log tail");
        return;
    }

    f.seek(start);
    size_t got = want > 0 ? f.readBytes(buf, want) : 0;
    buf[got] = '\0';
    f.close();

    // Count complete lines (a partial first line from a tail seek is ignored).
    char *begin = buf;
    if (start > 0) {
        char *nl = static_cast<char *>(memchr(buf, '\n', got));
        if (nl == nullptr) {
            free(buf);
            serialDevice->println("No complete line in the last 8KB");
            return;
        }
        begin = nl + 1;
    }

    const char *linePtrs[kMaxTailLines];
    uint8_t count = 0;
    char *p = begin;
    char *end = buf + got;
    while (p < end && count < kMaxTailLines) {
        char *nl = static_cast<char *>(memchr(p, '\n', static_cast<size_t>(end - p)));
        if (nl == nullptr) break;
        *nl = '\0';
        linePtrs[count++] = p;
        p = nl + 1;
    }

    uint32_t first = count > lines ? 0 : (uint32_t)count - lines;
    serialDevice->println("----- tail of " + String(nzlog::path()) + " -----");
    for (uint32_t i = first; i < count; ++i) { serialDevice->println(linePtrs[i]); }
    serialDevice->println("----- end of log -----");
    free(buf);
}

uint32_t logCallback(cmd *c) {
    Command cmd(c);
    Argument arg = cmd.getArgument("action");
    String action = arg.getValue();
    action.toLowerCase();

    if (action == "" || action == "help" || action == "?") {
        serialDevice->println(
            "log commands:\n"
            "  log tail [lines]   - print the last N lines (default 30)\n"
            "  log level [v|d|i|w|e|f] - get/set the SD log level\n"
            "  log clear          - delete nezha.log and its rotated copy\n"
            "  log info           - log file path, size and sink status\n"
            "  log serial [v..f]  - get/set the serial mirror level"
        );
        return true;
    }

    if (action == "tail" || action == "cat" || action == "show") {
        String countArg = cmd.getArgument("lines").getValue();
        if (countArg.length() == 0) countArg = cmd.getArgument("value").getValue();
        long requested = countArg.length() > 0 ? countArg.toInt() : 30;
        if (requested <= 0) requested = 30;
        if (requested > kMaxTailLines) requested = kMaxTailLines;
        printTail((uint32_t)requested);
        return true;
    }

    if (action == "level") {
        String levelArg = cmd.getArgument("value").getValue();
        if (levelArg.length() == 0) {
            serialDevice->println(
                "SD log level: " + String(levelToken(nzlog::level())) + " (" + nzlog::levelName(nzlog::level()) + ")"
            );
            return true;
        }
        nzlog::Level parsed;
        if (!parseLevel(levelArg, &parsed)) {
            serialDevice->println("Invalid level. Use v/d/i/w/e/f.");
            return false;
        }
        nzlog::setLevel(parsed);
        serialDevice->println("SD log level -> " + String(nzlog::levelName(parsed)));
        return true;
    }

    if (action == "serial") {
        String levelArg = cmd.getArgument("value").getValue();
        if (levelArg.length() == 0) {
            serialDevice->println(
                "Serial log level: " + String(nzlog::levelName(nzlog::serialLevel()))
            );
            return true;
        }
        nzlog::Level parsed;
        if (!parseLevel(levelArg, &parsed)) {
            serialDevice->println("Invalid level. Use v/d/i/w/e/f.");
            return false;
        }
        nzlog::setSerialLevel(parsed);
        serialDevice->println("Serial log level -> " + String(nzlog::levelName(parsed)));
        return true;
    }

    if (action == "clear" || action == "reset") {
        if (nzlog::clear()) serialDevice->println("Log files cleared.");
        else serialDevice->println("Could not clear log files (SD card required).");
        return true;
    }

    if (action == "info") {
        serialDevice->println("path : " + String(nzlog::path()));
        serialDevice->println("sink : " + String(nzlog::fileReady() ? "SD (real-time append)" : "serial only"));
        serialDevice->println("size : " + String((unsigned)nzlog::fileSize()) + " bytes");
        serialDevice->println("level: " + String(nzlog::levelName(nzlog::level())));
        return true;
    }

    serialDevice->println("Unknown log action: " + action);
    return false;
}

} // namespace

void createLogCommands(SimpleCLI *cli) {
    Command log = cli->addCommand("log,logger,logs", logCallback);
    log.addPosArg("action", "help");
    log.addPosArg("value", "");
    log.addPosArg("lines", "");
}
