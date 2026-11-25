#if !defined(LITE_VERSION)
#include "shell_terminal.h"

#include "core/display.h"
#if __has_include("core/i18n.h")
#include "core/i18n.h"
#else
namespace bruce_i18n {
static inline const char *tr(const char *s) { return s; }
} // namespace bruce_i18n
#endif
#include "core/mykeyboard.h"
#include "core/radio_mem.h"
#include "core/scrollableTextArea.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SerialDevice.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <globals.h>
#include <map>
#include <vector>

namespace {

constexpr size_t SHELL_MAX_LINES = 240;
constexpr size_t SHELL_MAX_HISTORY = 20;
constexpr size_t SHELL_INPUT_MAX = 160;
constexpr int SHELL_MAX_SCRIPT_DEPTH = 4;
constexpr size_t SHELL_MAX_CAT_BYTES = 16384;

// Temporary SerialDevice implementation that captures Bruce CLI output into a String buffer.
class ShellCaptureDevice : public SerialDevice {
public:
    String buffer;

    size_t println(const String &s) override {
        buffer += s;
        buffer += '\n';
        return s.length() + 1;
    }
    size_t println(size_t n) override { return println(String((unsigned long)n)); }
    size_t println(uint32_t n) override { return println(String(n)); }
    size_t println() override {
        buffer += '\n';
        return 1;
    }
    size_t println(int n, int format = DEC) override { return println(String(n, format)); }
    size_t print(int n, int format = DEC) override { return print(String(n, format)); }
    size_t print(const String &s) override {
        buffer += s;
        return s.length();
    }
    void vprintf(const char *fmt, va_list args) override {
        char tmp[256];
        vsnprintf(tmp, sizeof(tmp), fmt, args);
        buffer += tmp;
    }
    int read() override { return -1; }
    size_t write(uint8_t *str, size_t size) override {
        for (size_t i = 0; i < size; ++i) buffer += static_cast<char>(str[i]);
        return size;
    }
    void flush() override {}
    int available() override { return 0; }
    String readStringUntil(char) override { return ""; }
};

struct ResolvedPath {
    FS *fs;
    bool isSd;
    String path; // Normalized path on the underlying FS (always starts with '/')
    String vpath() const {
        String root = isSd ? "/sd" : "/fs";
        return (path == "/") ? root : (root + path);
    }
};

struct ShellState {
    bool useSd = false;
    String cwd = "/";
    int lastStatus = 0;
    bool exitRequested = false;
    bool clearRequested = false;
    std::map<String, String> vars;
    std::vector<String> history;
    ScrollableTextArea *area = nullptr;
    String *captureOut = nullptr; // Non-null during `>` / `>>` output redirection

    String promptShort() const {
        String root = useSd ? "/sd" : "/fs";
        String dir = (cwd == "/") ? root : (root + cwd);
        if (dir.length() > 12) dir = ".." + dir.substring(dir.length() - 10);
        return dir + "$ ";
    }

    void emitRaw(const String &text) {
        if (captureOut) {
            *captureOut += text;
            return;
        }
        if (!area) return;
        int start = 0;
        while (start <= (int)text.length()) {
            int nl = text.indexOf('\n', start);
            if (nl < 0) {
                String tail = text.substring(start);
                if (!tail.isEmpty()) appendLine(tail);
                break;
            }
            String line = text.substring(start, nl);
            if (line.endsWith("\r")) line.remove(line.length() - 1);
            appendLine(line);
            start = nl + 1;
        }
    }

    void emitLine(const String &line) {
        if (captureOut) {
            *captureOut += line;
            *captureOut += '\n';
            return;
        }
        appendLine(line);
    }

    void appendLine(const String &line) {
        if (!area) return;
        area->addLine(line);
        if (area->getMaxLines() > SHELL_MAX_LINES) {
            size_t extra = area->getMaxLines() - SHELL_MAX_LINES;
            area->linesBuffer.erase(area->linesBuffer.begin(), area->linesBuffer.begin() + extra);
            area->firstVisibleLine = (area->firstVisibleLine > extra) ? (area->firstVisibleLine - extra) : 0;
        }
        area->scrollToLine(area->getMaxLines());
    }
};

static String normalizeFsPath(const String &raw) {
    std::vector<String> parts;
    int i = 0;
    int n = raw.length();
    while (i < n) {
        while (i < n && raw[i] == '/') ++i;
        int j = i;
        while (j < n && raw[j] != '/') ++j;
        if (j > i) {
            String seg = raw.substring(i, j);
            if (seg == ".") {
                // no-op
            } else if (seg == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(seg);
            }
        }
        i = j;
    }
    if (parts.empty()) return "/";
    String out;
    for (const auto &p : parts) {
        out += '/';
        out += p;
    }
    return out;
}

static bool resolvePath(const ShellState &st, const String &arg, ResolvedPath &out) {
    String input = arg;
    input.trim();
    if (input.isEmpty() || input == "~") {
        out.isSd = st.useSd;
        out.fs = out.isSd ? static_cast<FS *>(&SD) : static_cast<FS *>(&LittleFS);
        out.path = "/";
        return true;
    }

    if (input == "/sd" || input.startsWith("/sd/")) {
        if (!sdcardMounted && !setupSdCard()) return false;
        out.isSd = true;
        out.fs = &SD;
        out.path = normalizeFsPath(input.length() == 3 ? "/" : input.substring(3));
        return true;
    }

    if (input == "/fs" || input.startsWith("/fs/")) {
        out.isSd = false;
        out.fs = &LittleFS;
        out.path = normalizeFsPath(input.length() == 3 ? "/" : input.substring(3));
        return true;
    }

    out.isSd = st.useSd;
    out.fs = out.isSd ? static_cast<FS *>(&SD) : static_cast<FS *>(&LittleFS);
    if (input.startsWith("/")) {
        out.path = normalizeFsPath(input);
    } else {
        String combined = (st.cwd == "/") ? ("/" + input) : (st.cwd + "/" + input);
        out.path = normalizeFsPath(combined);
    }
    return true;
}

static String lookupVar(const ShellState &st, const String &name) {
    if (name == "?") return String(st.lastStatus);
    if (name == "PWD") return (st.useSd ? "/sd" : "/fs") + (st.cwd == "/" ? String("") : st.cwd);
    if (name == "FS") return st.useSd ? "sd" : "littlefs";
    if (name == "USER") return "bruce";
    if (name == "HOST") return String(DEVICE_NAME);
    auto it = st.vars.find(name);
    if (it != st.vars.end()) return it->second;
    return "";
}

static String expandVars(const ShellState &st, const String &in) {
    String out;
    bool inSingle = false;
    size_t i = 0;
    size_t n = in.length();
    while (i < n) {
        char c = in[i];
        if (c == '\'') {
            inSingle = !inSingle;
            out += c;
            ++i;
            continue;
        }
        if (!inSingle && c == '$' && i + 1 < n) {
            if (in[i + 1] == '?') {
                out += lookupVar(st, "?");
                i += 2;
                continue;
            }
            if (in[i + 1] == '{') {
                int close = in.indexOf('}', i + 2);
                if (close > (int)(i + 2)) {
                    out += lookupVar(st, in.substring(i + 2, close));
                    i = close + 1;
                    continue;
                }
            }
            size_t j = i + 1;
            while (j < n && (isAlphaNumeric(in[j]) || in[j] == '_')) ++j;
            if (j > i + 1) {
                out += lookupVar(st, in.substring(i + 1, j));
                i = j;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

static std::vector<String> tokenizeLine(const String &line) {
    std::vector<String> tokens;
    String cur;
    bool inSingle = false;
    bool inDouble = false;
    bool hasToken = false;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == '\\' && !inSingle && i + 1 < line.length()) {
            cur += line[++i];
            hasToken = true;
            continue;
        }
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
            hasToken = true;
            continue;
        }
        if (c == '"' && !inSingle) {
            inDouble = !inDouble;
            hasToken = true;
            continue;
        }
        if (!inSingle && !inDouble) {
            if (c == '#') break; // comment
            if (c == ' ' || c == '\t') {
                if (hasToken) {
                    tokens.push_back(cur);
                    cur = "";
                    hasToken = false;
                }
                continue;
            }
            if (c == '>') {
                if (hasToken) {
                    tokens.push_back(cur);
                    cur = "";
                    hasToken = false;
                }
                if (i + 1 < line.length() && line[i + 1] == '>') {
                    tokens.push_back(">>");
                    ++i;
                } else {
                    tokens.push_back(">");
                }
                continue;
            }
        }
        cur += c;
        hasToken = true;
    }
    if (hasToken) tokens.push_back(cur);
    return tokens;
}

static std::vector<String> splitStatements(const String &line) {
    std::vector<String> stmts;
    String cur;
    bool inSingle = false;
    bool inDouble = false;
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (c == '\'' && !inDouble) inSingle = !inSingle;
        else if (c == '"' && !inSingle) inDouble = !inDouble;
        if (!inSingle && !inDouble) {
            if (c == '#') break;
            if (c == ';') {
                cur.trim();
                if (!cur.isEmpty()) stmts.push_back(cur);
                cur = "";
                continue;
            }
        }
        cur += c;
    }
    cur.trim();
    if (!cur.isEmpty()) stmts.push_back(cur);
    return stmts;
}

static int execSingleCommand(ShellState &st, const String &rawStmt, int depth);

static bool evalCondition(ShellState &st, const String &condExpr) {
    String expr = condExpr;
    expr.trim();
    if (expr.startsWith("[") && expr.endsWith("]")) {
        expr = expr.substring(1, expr.length() - 1);
        expr.trim();
    } else if (expr.startsWith("test ")) {
        expr = expr.substring(5);
        expr.trim();
    }
    std::vector<String> tok = tokenizeLine(expandVars(st, expr));
    if (tok.empty()) return false;

    bool negate = false;
    if (tok[0] == "!") {
        negate = true;
        tok.erase(tok.begin());
        if (tok.empty()) return false;
    }

    bool res = false;
    if (tok.size() == 2) {
        const String &op = tok[0];
        const String &val = tok[1];
        if (op == "-z") res = val.isEmpty();
        else if (op == "-n") res = !val.isEmpty();
        else if (op == "-e" || op == "-f" || op == "-d") {
            ResolvedPath rp;
            if (resolvePath(st, val, rp) && rp.fs->exists(rp.path)) {
                if (op == "-e") {
                    res = true;
                } else {
                    File f = rp.fs->open(rp.path);
                    if (f) {
                        res = (op == "-d") ? f.isDirectory() : !f.isDirectory();
                        f.close();
                    }
                }
            }
        }
    } else if (tok.size() == 3) {
        const String &a = tok[0];
        const String &op = tok[1];
        const String &b = tok[2];
        if (op == "=" || op == "==") res = (a == b);
        else if (op == "!=") res = (a != b);
        else if (op == "-eq") res = (a.toInt() == b.toInt());
        else if (op == "-ne") res = (a.toInt() != b.toInt());
        else if (op == "-gt") res = (a.toInt() > b.toInt());
        else if (op == "-lt") res = (a.toInt() < b.toInt());
        else if (op == "-ge") res = (a.toInt() >= b.toInt());
        else if (op == "-le") res = (a.toInt() <= b.toInt());
    } else if (tok.size() == 1) {
        res = !tok[0].isEmpty();
    }
    return negate ? !res : res;
}

static int execScriptLines(ShellState &st, const std::vector<String> &lines, int depth) {
    if (depth > SHELL_MAX_SCRIPT_DEPTH) {
        st.emitLine("sh: max recursion depth reached");
        return 1;
    }
    int rc = 0;
    size_t i = 0;
    while (i < lines.size() && !st.exitRequested) {
        if (check(EscPress)) {
            st.emitLine("^C");
            return 130;
        }
        String line = lines[i];
        line.trim();
        if (line.isEmpty() || line.startsWith("#")) {
            ++i;
            continue;
        }

        // Basic `if <cond>; then ... [else ...] fi`
        if (line.startsWith("if ")) {
            String condPart = line.substring(3);
            int semiThen = condPart.indexOf("; then");
            if (semiThen >= 0) condPart = condPart.substring(0, semiThen);
            condPart.trim();

            std::vector<String> thenBlock;
            std::vector<String> elseBlock;
            bool inElse = false;
            int nested = 0;
            size_t j = i + 1;
            if (semiThen < 0 && j < lines.size()) {
                String nextL = lines[j];
                nextL.trim();
                if (nextL == "then") ++j;
            }
            for (; j < lines.size(); ++j) {
                String l = lines[j];
                l.trim();
                if (l.startsWith("if ")) ++nested;
                else if (l == "fi") {
                    if (nested == 0) break;
                    --nested;
                } else if (l == "else" && nested == 0) {
                    inElse = true;
                    continue;
                }
                if (inElse) elseBlock.push_back(lines[j]);
                else thenBlock.push_back(lines[j]);
            }
            if (evalCondition(st, condPart)) {
                rc = execScriptLines(st, thenBlock, depth + 1);
            } else if (!elseBlock.empty()) {
                rc = execScriptLines(st, elseBlock, depth + 1);
            }
            i = j + 1;
            continue;
        }

        // Basic `for VAR in a b c; do ... done`
        if (line.startsWith("for ")) {
            String rest = line.substring(4);
            int inIdx = rest.indexOf(" in ");
            if (inIdx > 0) {
                String varName = rest.substring(0, inIdx);
                varName.trim();
                String listPart = rest.substring(inIdx + 4);
                int semiDo = listPart.indexOf("; do");
                if (semiDo >= 0) listPart = listPart.substring(0, semiDo);
                std::vector<String> items = tokenizeLine(expandVars(st, listPart));

                std::vector<String> body;
                int nested = 0;
                size_t j = i + 1;
                if (semiDo < 0 && j < lines.size()) {
                    String nextL = lines[j];
                    nextL.trim();
                    if (nextL == "do") ++j;
                }
                for (; j < lines.size(); ++j) {
                    String l = lines[j];
                    l.trim();
                    if (l.startsWith("for ")) ++nested;
                    else if (l == "done") {
                        if (nested == 0) break;
                        --nested;
                    }
                    body.push_back(lines[j]);
                }
                for (const auto &item : items) {
                    if (st.exitRequested || check(EscPress)) break;
                    st.vars[varName] = item;
                    rc = execScriptLines(st, body, depth + 1);
                }
                i = j + 1;
                continue;
            }
        }

        for (const auto &stmt : splitStatements(line)) {
            rc = execSingleCommand(st, stmt, depth);
            st.lastStatus = rc;
            if (st.exitRequested) break;
        }
        if (st.area) st.area->draw();
        ++i;
    }
    return rc;
}

static int runScriptFromFs(ShellState &st, FS &fs, const String &path, int depth) {
    if (!fs.exists(path)) {
        st.emitLine("sh: " + path + ": No such file");
        return 127;
    }
    File f = fs.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        st.emitLine("sh: " + path + ": Cannot open");
        return 1;
    }
    std::vector<String> lines;
    while (f.available()) {
        String l = f.readStringUntil('\n');
        if (l.endsWith("\r")) l.remove(l.length() - 1);
        lines.push_back(l);
        if (lines.size() >= 256) break;
    }
    f.close();
    return execScriptLines(st, lines, depth);
}

static bool isValidVarName(const String &s) {
    if (s.isEmpty() || !(isAlpha(s[0]) || s[0] == '_')) return false;
    for (size_t i = 1; i < s.length(); ++i) {
        if (!(isAlphaNumeric(s[i]) || s[i] == '_')) return false;
    }
    return true;
}

static int cmdLs(ShellState &st, const std::vector<String> &args) {
    bool longFmt = false;
    String target = "";
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-l" || args[i] == "-la" || args[i] == "-al") longFmt = true;
        else if (!args[i].startsWith("-")) target = args[i];
    }
    ResolvedPath rp;
    if (!resolvePath(st, target.isEmpty() ? st.cwd : target, rp)) {
        st.emitLine("ls: SD card not mounted");
        return 1;
    }
    File dir = rp.fs->open(rp.path);
    if (!dir) {
        st.emitLine("ls: cannot access '" + rp.vpath() + "'");
        return 1;
    }
    if (!dir.isDirectory()) {
        st.emitLine(rp.path + " (" + formatBytes(dir.size()) + ")");
        dir.close();
        return 0;
    }
    int count = 0;
    File entry = dir.openNextFile();
    while (entry) {
        String name = entry.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (entry.isDirectory()) {
            st.emitLine(longFmt ? ("d         " + name + "/") : (name + "/"));
        } else {
            if (longFmt) {
                char sz[16];
                snprintf(sz, sizeof(sz), "%8u ", (unsigned)entry.size());
                st.emitLine(String("-") + sz + name);
            } else {
                st.emitLine(name);
            }
        }
        ++count;
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    if (count == 0) st.emitLine("(empty)");
    return 0;
}

static int cmdCat(ShellState &st, const std::vector<String> &args) {
    if (args.size() < 2) {
        st.emitLine("usage: cat <file>");
        return 1;
    }
    ResolvedPath rp;
    if (!resolvePath(st, args[1], rp)) {
        st.emitLine("cat: SD card not mounted");
        return 1;
    }
    File f = rp.fs->open(rp.path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        st.emitLine("cat: " + rp.vpath() + ": No such file");
        return 1;
    }
    size_t total = 0;
    while (f.available() && total < SHELL_MAX_CAT_BYTES) {
        String line = f.readStringUntil('\n');
        total += line.length() + 1;
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        st.emitLine(line);
    }
    if (f.available()) st.emitLine("... (truncated at 16KB)");
    f.close();
    return 0;
}

static int cmdHeadTail(ShellState &st, const std::vector<String> &args, bool isTail) {
    int maxLines = 10;
    String fileArg;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-n" && i + 1 < args.size()) {
            maxLines = max(1, (int)args[++i].toInt());
        } else {
            fileArg = args[i];
        }
    }
    if (fileArg.isEmpty()) {
        st.emitLine(String("usage: ") + (isTail ? "tail" : "head") + " [-n N] <file>");
        return 1;
    }
    ResolvedPath rp;
    if (!resolvePath(st, fileArg, rp)) {
        st.emitLine("SD card not mounted");
        return 1;
    }
    File f = rp.fs->open(rp.path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        st.emitLine("cannot open " + rp.vpath());
        return 1;
    }
    if (!isTail) {
        int n = 0;
        while (f.available() && n < maxLines) {
            String l = f.readStringUntil('\n');
            if (l.endsWith("\r")) l.remove(l.length() - 1);
            st.emitLine(l);
            ++n;
        }
    } else {
        std::vector<String> ring;
        while (f.available()) {
            String l = f.readStringUntil('\n');
            if (l.endsWith("\r")) l.remove(l.length() - 1);
            ring.push_back(l);
            if ((int)ring.size() > maxLines) ring.erase(ring.begin());
        }
        for (const auto &l : ring) st.emitLine(l);
    }
    f.close();
    return 0;
}

static int cmdGrep(ShellState &st, const std::vector<String> &args) {
    bool ignoreCase = false;
    String pat, fileArg;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-i") ignoreCase = true;
        else if (pat.isEmpty()) pat = args[i];
        else fileArg = args[i];
    }
    if (pat.isEmpty() || fileArg.isEmpty()) {
        st.emitLine("usage: grep [-i] <text> <file>");
        return 1;
    }
    ResolvedPath rp;
    if (!resolvePath(st, fileArg, rp)) {
        st.emitLine("grep: SD card not mounted");
        return 1;
    }
    File f = rp.fs->open(rp.path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        st.emitLine("grep: cannot open " + rp.vpath());
        return 1;
    }
    String needle = pat;
    if (ignoreCase) needle.toLowerCase();
    int lineNo = 0;
    int hits = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        ++lineNo;
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        String hay = line;
        if (ignoreCase) hay.toLowerCase();
        if (hay.indexOf(needle) >= 0) {
            st.emitLine(String(lineNo) + ":" + line);
            ++hits;
            if (hits >= 100) {
                st.emitLine("... (100+ matches)");
                break;
            }
        }
    }
    f.close();
    return hits > 0 ? 0 : 1;
}

static int cmdWc(ShellState &st, const std::vector<String> &args) {
    if (args.size() < 2) {
        st.emitLine("usage: wc <file>");
        return 1;
    }
    ResolvedPath rp;
    if (!resolvePath(st, args[1], rp)) {
        st.emitLine("wc: SD card not mounted");
        return 1;
    }
    File f = rp.fs->open(rp.path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        st.emitLine("wc: cannot open " + rp.vpath());
        return 1;
    }
    uint32_t lines = 0, words = 0, bytes = 0;
    bool inWord = false;
    while (f.available()) {
        int c = f.read();
        if (c < 0) break;
        ++bytes;
        if (c == '\n') ++lines;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++words;
        }
    }
    f.close();
    st.emitLine(String(lines) + "L " + String(words) + "W " + String(bytes) + "B " + rp.vpath());
    return 0;
}

static int cmdHexdump(ShellState &st, const std::vector<String> &args) {
    if (args.size() < 2) {
        st.emitLine("usage: hexdump <file>");
        return 1;
    }
    ResolvedPath rp;
    if (!resolvePath(st, args[1], rp)) {
        st.emitLine("hexdump: SD card not mounted");
        return 1;
    }
    File f = rp.fs->open(rp.path, FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        st.emitLine("hexdump: cannot open " + rp.vpath());
        return 1;
    }
    uint8_t buf[8];
    uint32_t off = 0;
    while (f.available() && off < 512) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        char line[64];
        int pos = snprintf(line, sizeof(line), "%04X: ", (unsigned)off);
        for (int i = 0; i < 8; ++i) {
            if (i < n) pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[i]);
            else pos += snprintf(line + pos, sizeof(line) - pos, "   ");
        }
        for (int i = 0; i < n; ++i) {
            char ch = (buf[i] >= 32 && buf[i] < 127) ? (char)buf[i] : '.';
            if (pos < (int)sizeof(line) - 1) line[pos++] = ch;
        }
        line[pos] = '\0';
        st.emitLine(line);
        off += n;
    }
    if (f.available()) st.emitLine("... (first 512B shown)");
    f.close();
    return 0;
}

static int cmdCopyFile(ShellState &st, const String &srcArg, const String &dstArg, bool removeSrc) {
    ResolvedPath src, dst;
    if (!resolvePath(st, srcArg, src) || !resolvePath(st, dstArg, dst)) {
        st.emitLine("SD card not mounted");
        return 1;
    }
    if (src.fs == dst.fs && removeSrc && src.fs->rename(src.path, dst.path)) {
        return 0;
    }
    File in = src.fs->open(src.path, FILE_READ);
    if (!in || in.isDirectory()) {
        if (in) in.close();
        st.emitLine("cannot open " + src.vpath());
        return 1;
    }
    File out = dst.fs->open(dst.path, FILE_WRITE);
    if (!out) {
        in.close();
        st.emitLine("cannot write " + dst.vpath());
        return 1;
    }
    uint8_t buf[512];
    while (in.available()) {
        size_t n = in.read(buf, sizeof(buf));
        if (n > 0) out.write(buf, n);
    }
    in.close();
    out.close();
    if (removeSrc) src.fs->remove(src.path);
    return 0;
}

static int execSingleCommand(ShellState &st, const String &rawStmt, int depth) {
    String expanded = expandVars(st, rawStmt);
    std::vector<String> tokens = tokenizeLine(expanded);
    if (tokens.empty()) return 0;

    // Handle `VAR=value` assignment
    if (tokens.size() == 1) {
        int eq = tokens[0].indexOf('=');
        if (eq > 0) {
            String name = tokens[0].substring(0, eq);
            if (isValidVarName(name)) {
                st.vars[name] = tokens[0].substring(eq + 1);
                return 0;
            }
        }
    }

    // Parse optional output redirection `> file` or `>> file`
    bool redirect = false;
    bool appendMode = false;
    String redirectTarget;
    std::vector<String> args;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if ((tokens[i] == ">" || tokens[i] == ">>") && i + 1 < tokens.size()) {
            redirect = true;
            appendMode = (tokens[i] == ">>");
            redirectTarget = tokens[++i];
        } else {
            args.push_back(tokens[i]);
        }
    }
    if (args.empty()) return 0;

    String captured;
    String *prevCapture = st.captureOut;
    if (redirect) st.captureOut = &captured;

    const String &cmd = args[0];
    int rc = 0;

    if (cmd == "help") {
        st.emitLine("Files: ls cd pwd cat head tail grep wc");
        st.emitLine("       hexdump touch mkdir rm cp mv");
        st.emitLine("Shell: echo sh bash env unset history");
        st.emitLine("       sleep clear exit  (> >> ; $VAR)");
        st.emitLine("Sys:   free df ip uptime uname date");
        st.emitLine("       reboot  + any Bruce CLI cmd");
        st.emitLine("Paths: /sd/... (SD)  /fs/... (LittleFS)");
    } else if (cmd == "exit" || cmd == "quit") {
        st.exitRequested = true;
    } else if (cmd == "clear" || cmd == "cls") {
        st.clearRequested = true;
    } else if (cmd == "pwd") {
        st.emitLine((st.useSd ? "/sd" : "/fs") + (st.cwd == "/" ? String("") : st.cwd));
    } else if (cmd == "cd") {
        ResolvedPath rp;
        String target = (args.size() > 1) ? args[1] : "/";
        if (!resolvePath(st, target, rp)) {
            st.emitLine("cd: SD card not mounted");
            rc = 1;
        } else {
            File d = rp.fs->open(rp.path);
            if (!d || !d.isDirectory()) {
                if (d) d.close();
                st.emitLine("cd: not a directory: " + rp.vpath());
                rc = 1;
            } else {
                d.close();
                st.useSd = rp.isSd;
                st.cwd = rp.path;
            }
        }
    } else if (cmd == "ls") {
        rc = cmdLs(st, args);
    } else if (cmd == "cat") {
        rc = cmdCat(st, args);
    } else if (cmd == "head") {
        rc = cmdHeadTail(st, args, false);
    } else if (cmd == "tail") {
        rc = cmdHeadTail(st, args, true);
    } else if (cmd == "grep") {
        rc = cmdGrep(st, args);
    } else if (cmd == "wc") {
        rc = cmdWc(st, args);
    } else if (cmd == "hexdump") {
        rc = cmdHexdump(st, args);
    } else if (cmd == "echo") {
        String out;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) out += ' ';
            out += args[i];
        }
        st.emitLine(out);
    } else if (cmd == "touch") {
        if (args.size() < 2) {
            st.emitLine("usage: touch <file>");
            rc = 1;
        } else {
            ResolvedPath rp;
            if (!resolvePath(st, args[1], rp)) {
                st.emitLine("touch: SD card not mounted");
                rc = 1;
            } else {
                File f = rp.fs->open(rp.path, FILE_APPEND);
                if (!f) {
                    st.emitLine("touch: failed " + rp.vpath());
                    rc = 1;
                } else {
                    f.close();
                }
            }
        }
    } else if (cmd == "mkdir") {
        if (args.size() < 2) {
            st.emitLine("usage: mkdir <dir>");
            rc = 1;
        } else {
            ResolvedPath rp;
            if (!resolvePath(st, args[1], rp) || !rp.fs->mkdir(rp.path)) {
                st.emitLine("mkdir: failed");
                rc = 1;
            }
        }
    } else if (cmd == "rm") {
        if (args.size() < 2) {
            st.emitLine("usage: rm <path>");
            rc = 1;
        } else {
            ResolvedPath rp;
            if (!resolvePath(st, args[1], rp)) {
                st.emitLine("rm: SD card not mounted");
                rc = 1;
            } else if (!rp.fs->remove(rp.path) && !rp.fs->rmdir(rp.path)) {
                st.emitLine("rm: failed " + rp.vpath());
                rc = 1;
            }
        }
    } else if (cmd == "cp") {
        if (args.size() < 3) {
            st.emitLine("usage: cp <src> <dst>");
            rc = 1;
        } else {
            rc = cmdCopyFile(st, args[1], args[2], false);
        }
    } else if (cmd == "mv") {
        if (args.size() < 3) {
            st.emitLine("usage: mv <src> <dst>");
            rc = 1;
        } else {
            rc = cmdCopyFile(st, args[1], args[2], true);
        }
    } else if (cmd == "sh" || cmd == "bash" || cmd == "source" || cmd == ".") {
        if (args.size() < 2) {
            st.emitLine("usage: sh <script.sh>");
            rc = 1;
        } else {
            ResolvedPath rp;
            if (!resolvePath(st, args[1], rp)) {
                st.emitLine("sh: SD card not mounted");
                rc = 1;
            } else {
                rc = runScriptFromFs(st, *rp.fs, rp.path, depth + 1);
            }
        }
    } else if (cmd.endsWith(".sh") && (cmd.startsWith("./") || cmd.startsWith("/"))) {
        ResolvedPath rp;
        if (!resolvePath(st, cmd, rp)) {
            st.emitLine("sh: SD card not mounted");
            rc = 1;
        } else {
            rc = runScriptFromFs(st, *rp.fs, rp.path, depth + 1);
        }
    } else if (cmd == "sleep") {
        uint32_t ms = (args.size() > 1) ? (uint32_t)(args[1].toFloat() * 1000.0f) : 1000;
        uint32_t start = millis();
        while (millis() - start < ms) {
            if (check(EscPress)) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    } else if (cmd == "env") {
        st.emitLine("USER=bruce");
        st.emitLine("HOST=" + String(DEVICE_NAME));
        st.emitLine("PWD=" + lookupVar(st, "PWD"));
        st.emitLine("FS=" + lookupVar(st, "FS"));
        for (const auto &kv : st.vars) st.emitLine(kv.first + "=" + kv.second);
    } else if (cmd == "unset") {
        if (args.size() > 1) st.vars.erase(args[1]);
    } else if (cmd == "history") {
        for (size_t i = 0; i < st.history.size(); ++i) {
            st.emitLine(String(i + 1) + "  " + st.history[i]);
        }
    } else if (cmd == "free") {
        st.emitLine("Heap:  " + formatBytes(ESP.getFreeHeap()) + " / " + formatBytes(ESP.getHeapSize()));
        st.emitLine(
            "DMA:   " + formatBytes(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)) + " max / " +
            formatBytes(heap_caps_get_free_size(MALLOC_CAP_DMA)) + " free"
        );
        if (psramFound()) {
            st.emitLine("PSRAM: " + formatBytes(ESP.getFreePsram()) + " / " + formatBytes(ESP.getPsramSize()));
        } else {
            st.emitLine("PSRAM: none");
        }
    } else if (cmd == "df") {
        st.emitLine(
            "/fs  " + formatBytes(LittleFS.usedBytes()) + " / " + formatBytes(LittleFS.totalBytes())
        );
        if (sdcardMounted) {
            st.emitLine("/sd  " + formatBytes(SD.usedBytes()) + " / " + formatBytes(SD.totalBytes()));
        } else {
            st.emitLine("/sd  (not mounted)");
        }
    } else if (cmd == "uptime") {
        uint32_t sec = millis() / 1000;
        uint32_t h = sec / 3600;
        uint32_t m = (sec % 3600) / 60;
        uint32_t s = sec % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "up %02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
        st.emitLine(buf);
    } else if (cmd == "uname") {
        st.emitLine("Bruce " + String(BRUCE_VERSION) + " " + String(DEVICE_NAME) + " FreeRTOS");
    } else if (cmd == "ip" || cmd == "ifconfig") {
        st.emitLine("MAC: " + WiFi.macAddress());
        if (WiFi.isConnected()) st.emitLine("STA: " + WiFi.localIP().toString());
        if (WiFi.getMode() & WIFI_MODE_AP) st.emitLine("AP:  " + WiFi.softAPIP().toString());
        if (!WiFi.isConnected() && !(WiFi.getMode() & WIFI_MODE_AP)) st.emitLine("WiFi: offline");
    } else if (cmd == "date") {
#if defined(HAS_RTC)
        struct tm ti = _rtc.getTimeStruct();
#else
        struct tm ti = rtc.getTimeStruct();
#endif
        updateTimeStr(ti);
        char dbuf[20];
        snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d ", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        st.emitLine(String(dbuf) + String(timeStr));
    } else if (cmd == "reboot") {
        ESP.restart();
    } else {
        // Forward to Bruce's existing SerialCli engine and capture output to screen
        ShellCaptureDevice cap;
        SerialDevice *savedDev = serialDevice;
        serialDevice = &cap;
        bool ok = serialCli.parse(expanded);
        serialDevice = savedDev;
        if (!cap.buffer.isEmpty()) {
            st.emitRaw(cap.buffer);
        } else if (!ok) {
            st.emitLine(cmd + ": command not found (try 'help')");
            rc = 127;
        }
    }

    st.captureOut = prevCapture;
    if (redirect) {
        ResolvedPath rp;
        if (!resolvePath(st, redirectTarget, rp)) {
            st.emitLine("redirect: SD card not mounted");
            return 1;
        }
        File f = rp.fs->open(rp.path, appendMode ? FILE_APPEND : FILE_WRITE);
        if (!f) {
            st.emitLine("redirect: cannot write " + rp.vpath());
            return 1;
        }
        f.print(captured);
        f.close();
    }
    return rc;
}

} // namespace

void shell_run_script_file(FS &fs, const String &filepath) {
    drawMainBorderWithTitle("Shell Script");

    const int16_t areaY = BORDER_PAD_Y + FM * LH + 4;
    const int16_t footY = tftHeight - BORDER_PAD_X - LH;
    const int16_t areaH = footY - 4 - areaY;
    ScrollableTextArea area(FP, BORDER_PAD_X, areaY, tftWidth - 2 * BORDER_PAD_X, areaH, false, false);

    ShellState st;
    st.useSd = (&fs == &SD);
    st.area = &area;
    st.emitLine("$ sh " + filepath);
    area.draw(true);

    int rc = runScriptFromFs(st, fs, filepath, 0);
    st.emitLine("--- exit " + String(rc) + " ---");
    area.draw();

    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    tft.drawCentreString(bruce_i18n::tr("Press Any key to stop"), tftWidth / 2, footY, 1);

    delay(150);
    while (!check(EscPress) && !check(SelPress)) {
        if (check(PrevPress) || check(UpPress)) {
            area.scrollUp();
            area.draw();
        } else if (check(NextPress) || check(DownPress)) {
            area.scrollDown();
            area.draw();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void shell_terminal_setup() {
    setupSdCard();

    auto drawFrame = [&]() {
        drawMainBorderWithTitle("Terminal");
    };
    drawFrame();

    const int16_t areaY = BORDER_PAD_Y + FM * LH + 2;
    const int16_t inputY = tftHeight - BORDER_PAD_X - LH;
    const int16_t areaH = inputY - 4 - areaY;
    ScrollableTextArea area(FP, BORDER_PAD_X, areaY, tftWidth - 2 * BORDER_PAD_X, areaH, false, false);

    ShellState st;
    st.useSd = sdcardMounted;
    st.area = &area;
    st.emitLine("Bruce Shell " + String(BRUCE_VERSION) + " (" + (st.useSd ? "/sd" : "/fs") + ")");
    st.emitLine("Type 'help' or 'exit'");
    area.draw(true);

    String input;
    int histIdx = -1;
    bool inputDirty = true;
    uint32_t lastDelMs = 0;
    const int16_t maxChars = max<int16_t>(8, (tftWidth - 2 * BORDER_PAD_X) / LW - 1);

    auto drawInputLine = [&]() {
        tft.fillRect(BORDER_PAD_X, inputY, tftWidth - 2 * BORDER_PAD_X, LH, bruceConfig.bgColor);
        tft.drawFastHLine(BORDER_PAD_X, inputY - 2, tftWidth - 2 * BORDER_PAD_X, bruceConfig.secColor);
        tft.setTextSize(FP);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        String full = st.promptShort() + input + "_";
        if ((int16_t)full.length() > maxChars) full = full.substring(full.length() - maxChars);
        tft.drawString(full, BORDER_PAD_X, inputY, 1);
        inputDirty = false;
    };

    auto runInputLine = [&](const String &cmdLine) {
        String trimmed = cmdLine;
        trimmed.trim();
        st.emitLine(st.promptShort() + trimmed);
        if (!trimmed.isEmpty()) {
            if (trimmed == "!!" && !st.history.empty()) trimmed = st.history.back();
            if (st.history.empty() || st.history.back() != trimmed) {
                st.history.push_back(trimmed);
                if (st.history.size() > SHELL_MAX_HISTORY) st.history.erase(st.history.begin());
            }
            for (const auto &stmt : splitStatements(trimmed)) {
                st.lastStatus = execSingleCommand(st, stmt, 0);
                if (st.exitRequested) break;
            }
            if (st.clearRequested) {
                area.clear();
                st.clearRequested = false;
            }
        }
        area.draw(true);
    };

    // Clear stale key state before entering loop
    _getKeyPress();
    check(SelPress);
    check(EscPress);

    while (!st.exitRequested && !returnToMenu) {
        wakeUpScreen();

        bool executed = false;
        bool kbHandled = false;

        // Works on all boards with physical keyboards (T-Deck, Cardputer, T-LoRa-Pager)
        keyStroke key = _getKeyPress();
        if (key.pressed) {
            kbHandled = true;
            if (key.del) {
                if (!input.isEmpty()) {
                    input.remove(input.length() - 1);
                    EscPress = false; // Backspace edits input instead of exiting
                    lastDelMs = millis();
                    inputDirty = true;
                } else if (millis() - lastDelMs < 400) {
                    EscPress = false; // Prevent accidental exit when holding Backspace
                }
            }
            if (key.enter) {
                SelPress = false;
                executed = true;
            }
            if (!key.del && !key.enter) {
                for (char c : key.word) {
                    if (c == '\n' || c == '\r' || c == '\x08') continue;
                    if (c >= 32 && c < 127 && input.length() < SHELL_INPUT_MAX) {
                        input += c;
                        inputDirty = true;
                    }
                }
            }
        }

        if (executed) {
            String cmdToRun = input;
            input = "";
            histIdx = -1;
            runInputLine(cmdToRun);
            inputDirty = true;
        }

        // Scroll output, or Fn+Up/Down to browse command history
        if (check(PrevPress) || check(UpPress)) {
            if (key.fn && !st.history.empty()) {
                if (histIdx < 0) histIdx = (int)st.history.size() - 1;
                else if (histIdx > 0) --histIdx;
                input = st.history[histIdx];
                inputDirty = true;
            } else {
                area.scrollUp();
                area.draw();
            }
        } else if (check(NextPress) || check(DownPress)) {
            if (key.fn && !st.history.empty()) {
                if (histIdx >= 0 && histIdx + 1 < (int)st.history.size()) {
                    input = st.history[++histIdx];
                } else {
                    histIdx = -1;
                    input = "";
                }
                inputDirty = true;
            } else {
                area.scrollDown();
                area.draw();
            }
        }

        // On non-keyboard devices (or Trackball click), open Bruce's modal keyboard
        if (!kbHandled && check(SelPress)) {
            String msg = keyboard(input, SHELL_INPUT_MAX, "Terminal");
            drawFrame();
            area.draw(true);
            if (msg != "\x1B") {
                input = "";
                histIdx = -1;
                runInputLine(msg);
            }
            inputDirty = true;
        }

        if (check(EscPress)) break;
        if (inputDirty) drawInputLine();
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    check(SelPress);
    check(EscPress);
}

#endif // !LITE_VERSION
