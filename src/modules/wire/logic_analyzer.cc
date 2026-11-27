#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
#include "wire_tools.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/settings.h"
#include "core/utils.h"
#include "core/sd_functions.h"
#include <globals.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <driver/rmt_rx.h>
#include <algorithm>

/*********************************************************************
**  Logic Analyzer (RMT edge capture)
**
**  One RMT RX channel per probe GPIO. The ping-pong engine copies symbols
**  into a per-channel user buffer with no CPU involvement, so a capture is
**  gap-free from arm to either buffer-full or idle-timeout (signal_range_max).
**  Edges are derived from the RMT symbols after the capture and rendered in
**  an interactive waveform viewer (zoom / pan / cursor measurements).
**********************************************************************/

#define LA_MAX_CHANNELS 4
// RMT symbol durations are 15 bits: anything longer ends the frame (idle timeout)
#define LA_MAX_SYM_TICKS 32767
// S3 RMT peripheral clock (APB); resolutions must divide it exactly
#define LA_CLK_HZ 80000000ULL

static const uint32_t laRates[] = {100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000, 20000000};
static const uint32_t laDepths[] = {1024, 2048, 4096}; // symbols per channel buffer

struct LaSession {
    uint8_t nch = 2;
    int8_t pins[LA_MAX_CHANNELS] = {-1, -1, -1, -1};
    uint32_t resolution = 1000000; // RMT tick
    uint32_t depth = 2048;         // symbols per channel
    uint8_t pull = 1;              // 0 none, 1 up, 2 down
    uint32_t glitchNs = 0;         // RMT RX noise filter

    bool valid = false; // a completed capture is available
    uint8_t initLevel[LA_MAX_CHANNELS] = {0, 0, 0, 0};
    uint32_t symbols[LA_MAX_CHANNELS] = {0, 0, 0, 0};
    bool truncated[LA_MAX_CHANNELS] = {false, false, false, false};
    uint8_t *bufs[LA_MAX_CHANNELS] = {nullptr, nullptr, nullptr, nullptr};
    // packed edges: (time_in_ticks << 1) | level_after_edge, ascending in time
    std::vector<uint32_t> edges[LA_MAX_CHANNELS];
};

static LaSession laSession;

/*********************************************************************
**  Formatting helpers
**********************************************************************/
static String la_fmt_rate(uint32_t hz) {
    if (hz < 1000000) return String(hz / 1000) + " kHz";
    return String(hz / 1000000.0f, hz % 1000000 ? 1 : 0) + " MHz";
}

static String la_fmt_time(uint64_t ticks, uint32_t res) {
    double us = (double)ticks * 1000000.0 / res;
    if (us < 1000) return String(us, 0) + "us";
    if (us < 1000000) return String(us / 1000.0, 3) + "ms";
    return String(us / 1000000.0, 3) + "s";
}

static String la_fmt_freq(double hz) {
    if (hz < 1000) return String(hz, 0) + "Hz";
    if (hz < 1000000) return String(hz / 1000.0, 2) + "kHz";
    return String(hz / 1000000.0, 2) + "MHz";
}

/*********************************************************************
**  Capture core
**********************************************************************/
// user_ctx points at the channel's LaRxState; the exact symbol count comes
// with the rx_done event, so no buffer rescanning is needed afterwards.
struct LaRxState {
    volatile bool done;
    volatile uint32_t count;
};

static bool IRAM_ATTR la_rx_done_cb(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user) {
    LaRxState *st = (LaRxState *)user;
    st->count = edata->num_symbols;
    st->done = true;
    return false;
}

static void la_free_bufs(LaSession &s) {
    for (uint8_t c = 0; c < LA_MAX_CHANNELS; c++) {
        if (s.bufs[c]) {
            heap_caps_free(s.bufs[c]);
            s.bufs[c] = nullptr;
        }
    }
}

static void la_clear_results(LaSession &s) {
    for (uint8_t c = 0; c < LA_MAX_CHANNELS; c++) {
        s.edges[c].clear();
        s.edges[c].shrink_to_fit();
    }
    s.valid = false;
}

// Arms one RMT RX channel per probe and waits for every channel to finish
// (buffer full or idle timeout). Fills s.symbols/s.initLevel and leaves the
// symbol buffers allocated in s.bufs for the caller to consume and free.
static bool la_run_capture(LaSession &s) {
    if (s.nch == 0 || s.nch > LA_MAX_CHANNELS) return false;
    for (uint8_t c = 0; c < s.nch; c++)
        if (s.pins[c] < 0) return false;
    for (uint8_t c = 0; c < s.nch; c++)
        for (uint8_t k = c + 1; k < s.nch; k++)
            if (s.pins[c] == s.pins[k]) return false;

    size_t bufBytes = (size_t)s.depth * sizeof(rmt_symbol_word_t);
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < bufBytes * s.nch + 16 * 1024) return false;

    la_clear_results(s);

    rmt_channel_handle_t chans[LA_MAX_CHANNELS] = {nullptr, nullptr, nullptr, nullptr};
    LaRxState rx[LA_MAX_CHANNELS];
    for (uint8_t c = 0; c < LA_MAX_CHANNELS; c++) {
        rx[c].done = false;
        rx[c].count = 0;
    }
    rmt_receive_config_t rcfg = {};
    rcfg.signal_range_min_ns = s.glitchNs;
    rcfg.signal_range_max_ns = (uint64_t)LA_MAX_SYM_TICKS * 1000000000ULL / s.resolution;

    bool ok = true;
    for (uint8_t c = 0; c < s.nch && ok; c++) {
        int8_t pin = s.pins[c];
        if (s.pull == 1) pinMode(pin, INPUT_PULLUP);
        else if (s.pull == 2) pinMode(pin, INPUT_PULLDOWN);
        else pinMode(pin, INPUT);

        rmt_rx_channel_config_t ccfg = {};
        ccfg.gpio_num = (gpio_num_t)pin;
        ccfg.clk_src = RMT_CLK_SRC_DEFAULT;
        ccfg.resolution_hz = s.resolution;
        ccfg.mem_block_symbols = 48;
        ccfg.intr_priority = 0;
        if (rmt_new_rx_channel(&ccfg, &chans[c]) != ESP_OK) { ok = false; break; }

        rmt_rx_event_callbacks_t cbs = {};
        cbs.on_recv_done = la_rx_done_cb;
        if (rmt_rx_register_event_callbacks(chans[c], &cbs, (void *)&rx[c]) != ESP_OK) { ok = false; break; }

        s.bufs[c] = (uint8_t *)heap_caps_malloc(bufBytes, MALLOC_CAP_8BIT);
        if (s.bufs[c] == nullptr) { ok = false; break; }

        s.initLevel[c] = digitalRead(pin) == HIGH ? 1 : 0;
        rmt_enable(chans[c]);
        // Arm immediately after reading the idle level so the first symbol
        // still measures time-to-first-edge for this channel
        if (rmt_receive(chans[c], s.bufs[c], bufBytes, &rcfg) != ESP_OK) { ok = false; break; }
    }

    uint32_t idleMs = rcfg.signal_range_max_ns / 1000000 + 2;
    uint32_t deadline = millis() + idleMs + 5000;
    if (ok) {
        displayRedStripe("Capturing...");
        while (millis() < deadline) {
            uint8_t finished = 0;
            for (uint8_t c = 0; c < s.nch; c++) finished += rx[c].done;
            if (finished >= s.nch) break;
            if (check(EscPress)) { ok = false; break; }
            wakeUpScreen();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (ok) {
            for (uint8_t c = 0; c < s.nch; c++) {
                if (!rx[c].done) { ok = false; break; } // deadline hit: incomplete frame, discard
            }
        }
        if (ok) {
            for (uint8_t c = 0; c < s.nch; c++) s.symbols[c] = rx[c].count;
        }
    }

    for (uint8_t c = 0; c < s.nch; c++) {
        if (chans[c]) {
            rmt_disable(chans[c]);
            rmt_del_channel(chans[c]);
            chans[c] = nullptr;
        }
        pinMode(s.pins[c], INPUT);
    }

    if (!ok) la_free_bufs(s);
    return ok;
}

// Converts one channel's symbol stream into packed edges and frees its buffer
static void la_extract(LaSession &s, uint8_t c) {
    if (s.bufs[c] == nullptr) return;
    auto *syms = (rmt_symbol_word_t *)s.bufs[c];
    uint32_t n = s.symbols[c];
    if (n > s.depth) n = s.depth;
    s.truncated[c] = (n >= s.depth);

    s.edges[c].clear();
    s.edges[c].reserve((size_t)n * 2);
    uint64_t t = 0;
    uint8_t lvl = s.initLevel[c];
    for (uint32_t i = 0; i < n; i++) {
        t += syms[i].duration0;
        s.edges[c].push_back((uint32_t)((t << 1) | (uint32_t)(lvl ^ 1)));
        lvl ^= 1;
        t += syms[i].duration1;
        s.edges[c].push_back((uint32_t)((t << 1) | (uint32_t)(lvl ^ 1)));
        lvl ^= 1;
        if (t > 0x7FFFFFFE) break; // saturated ticks; further edges would wrap
    }
    heap_caps_free(s.bufs[c]);
    s.bufs[c] = nullptr;
    s.valid = true;
}

/*********************************************************************
**  Waveform viewer
**********************************************************************/
static void la_viewer() {
    LaSession &s = laSession;
    uint8_t nch = s.nch;

    uint64_t maxT = 0;
    for (uint8_t c = 0; c < nch; c++)
        if (!s.edges[c].empty()) maxT = std::max<uint64_t>(maxT, s.edges[c].back() >> 1);
    if (maxT == 0) maxT = 1000;

    double span = (double)maxT;
    double view0 = 0;
    uint8_t measCh = 0;

    const int W = tftWidth;
    const int H = tftHeight;
    const int headerH = 13;
    const int footerH = 15;
    const int lanesY = headerH;
    const int lanesH = H - headerH - footerH;
    const int x0 = 17;
    const int wpx = W - x0;
    const double laneH = (double)lanesH / nch;

    std::vector<int8_t> colState((size_t)wpx + 1);

    auto render = [&]() {
        uint16_t fg = bruceConfig.priColor;
        uint16_t bg = bruceConfig.bgColor;
        uint16_t dim = blendColors(fg, bg, 70);
        tft.fillScreen(bg);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(FP);
        tft.setTextColor(fg, bg);

        uint64_t tCursor = (uint64_t)(view0 + span / 2);
        tft.drawString("LA " + la_fmt_time(tCursor, s.resolution), 2, 2);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(la_fmt_time((uint64_t)span, s.resolution), W - 2, 2);
        tft.setTextDatum(TL_DATUM);

        bool anyTrunc = false;
        for (uint8_t c = 0; c < nch; c++) anyTrunc |= s.truncated[c];

        int cx = x0 + wpx / 2;
        for (uint8_t c = 0; c < nch; c++) {
            int y0 = lanesY + (int)(c * laneH);
            int y1 = lanesY + (int)((c + 1) * laneH) - 1;
            tft.drawFastHLine(0, y1, W, dim);
            tft.drawFastVLine(x0 - 2, y0, y1 - y0, dim);
            tft.drawString(String(s.pins[c]), 1, y0 + 1);

            int yHigh = y0 + 1;
            int yLow = y1 - 1;
            const std::vector<uint32_t> &e = s.edges[c];
            int8_t *cols = colState.data();
            memset(cols, -1, wpx);

            uint8_t lvl = s.initLevel[c];
            int prevCol = -1;
            for (size_t i = 0; i < e.size(); i++) {
                int col = (int)(((double)(e[i] >> 1) - view0) * ((double)wpx / span));
                if (col < 0) { lvl = e[i] & 1; prevCol = -1; continue; }
                if (col >= wpx) break;
                for (int x = prevCol + 1; x < col; x++) cols[x] = lvl;
                cols[col] = 2; // transition inside this column
                lvl = e[i] & 1;
                prevCol = col;
            }
            for (int x = prevCol + 1; x < wpx; x++) cols[x] = lvl;

            // flat runs, transitions as vertical bars
            int runStart = 0;
            for (int x = 1; x <= wpx; x++) {
                bool flush = (x == wpx) || (cols[x] != cols[runStart]);
                if (!flush) continue;
                int8_t st = cols[runStart];
                if (st == 2) {
                    tft.drawFastVLine(x0 + runStart, yHigh - 1, yLow - yHigh + 3, fg);
                } else {
                    int y = st ? yHigh : yLow;
                    tft.drawFastHLine(x0 + runStart, y, x - runStart, fg);
                }
                runStart = x;
            }
            if (!e.empty()) {
                uint64_t tLast = e.back() >> 1;
                int ex = (int)((double)tLast * wpx / span);
                if (s.truncated[c] && ex >= 0 && ex < wpx) tft.drawFastVLine(x0 + ex, yHigh - 1, yLow - yHigh + 3, TFT_RED);
            }
            tft.drawFastVLine(cx, yHigh, yLow - yHigh + 1, TFT_RED);
        }

        // footer: measurement on the selected channel at the cursor
        String foot = "Ch" + String(s.pins[measCh]);
        const std::vector<uint32_t> &e = s.edges[measCh];
        size_t lo = 0, hi = e.size();
        while (lo < hi) { // last edge index <= tCursor
            size_t mid = (lo + hi) / 2;
            if ((e[mid] >> 1) <= tCursor) lo = mid + 1;
            else hi = mid;
        }
        if (lo == 0 || lo >= e.size()) {
            foot += " no edges at cursor";
        } else {
            uint64_t tA = e[lo - 1] >> 1, tB = e[lo] >> 1;
            uint8_t lvlA = e[lo - 1] & 1;
            foot = "Ch" + String(s.pins[measCh]) + " " + (lvlA ? "H" : "L") + " W:" + la_fmt_time(tB - tA, s.resolution);
            if (lo + 1 < e.size()) {
                uint64_t tC = e[lo + 1] >> 1;
                uint64_t period = tC - tA;
                if (period) {
                    foot += " F:" + la_fmt_freq((double)s.resolution / period);
                    uint64_t high = lvlA ? (tB - tA) : (tC - tB);
                    foot += " D:" + String((int)(100.0 * high / period)) + "%";
                }
            }
        }
        if (anyTrunc) foot += " TRUNC";
        tft.setTextColor(anyTrunc ? TFT_RED : fg, bg);
        tft.drawString(foot, 2, H - FP * 8 - 2);
        tft.setTextColor(fg, bg);
    };

    render();
    while (!check(EscPress)) {
        bool dirty = false;
        if (check(NextPress)) { view0 += span * 0.25; dirty = true; }
        if (check(PrevPress)) { view0 -= span * 0.25; dirty = true; }
        if (check(UpPress) || check(DownPress)) {
            double tCenter = view0 + span / 2;
            span *= check(UpPress) ? 0.5 : 2.0;
            if (span < 32) span = 32;
            if (span > (double)maxT * 2) span = (double)maxT * 2;
            view0 = tCenter - span / 2;
            dirty = true;
        }
        if (check(SelPress)) { measCh = (measCh + 1) % nch; dirty = true; }
        if (dirty) {
            if (view0 < 0) view0 = 0;
            if (view0 > (double)maxT) view0 = maxT;
            render();
        }
        wakeUpScreen();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    check(EscPress);
    check(SelPress);
    check(NextPress);
    check(PrevPress);
    check(UpPress);
    check(DownPress);
}

/*********************************************************************
**  Run a capture and open the viewer (menu action)
**********************************************************************/
static void la_capture_and_view() {
    LaSession &s = laSession;
    if (!la_run_capture(s)) {
        la_free_bufs(s);
        la_clear_results(s);
        displayError("Capture failed/aborted", true);
        return;
    }
    for (uint8_t c = 0; c < s.nch; c++) la_extract(s, c);
    la_viewer();
}

/*********************************************************************
**  CSV export: one line per edge "channel,time_us,level"
**********************************************************************/
static void la_save_csv() {
    LaSession &s = laSession;
    if (!s.valid) {
        displayError("Run a capture first", true);
        return;
    }
    FS *fs = nullptr;
    if (!getFsStorage(fs) || fs == nullptr) {
        displayError("No storage available", true);
        return;
    }
    if (!fs->exists("/BruceLA")) fs->mkdir("/BruceLA");
    char filename[32];
    int index = 0;
    do { snprintf(filename, sizeof(filename), "/BruceLA/la_%d.csv", index++); } while (fs->exists(filename));

    File file = fs->open(filename, FILE_WRITE, true);
    if (!file) {
        displayError("Error creating file", true);
        return;
    }
    file.println("channel,time_us,level");
    for (uint8_t c = 0; c < s.nch; c++) {
        uint64_t t = 0;
        uint8_t lvl = s.initLevel[c];
        file.printf("# ch%d pin %d idle=%u\n", c + 1, s.pins[c], lvl);
        for (uint32_t e : s.edges[c]) {
            uint64_t te = e >> 1;
            uint8_t lv = e & 1;
            file.printf("%u,%.3f,%u\n", c + 1, (double)te * 1000000.0 / s.resolution, lv);
            t = te;
            lvl = lv;
        }
    }
    file.close();
    displaySuccess("Saved " + String(filename), true);
}

/*********************************************************************
**  VCD (Value Change Dump) Export — opens directly in PulseView/GTKWave
**********************************************************************/
static void la_save_vcd() {
    LaSession &s = laSession;
    if (!s.valid) {
        displayError("No capture yet", true);
        return;
    }

    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) {
        displayError("No storage mounted", true);
        return;
    }

    if (!fs->exists("/NezhaLA")) fs->mkdir("/NezhaLA");
    char filename[48] = {};
    for (uint16_t n = 0; n < 1000; ++n) {
        snprintf(filename, sizeof(filename), "/NezhaLA/la_%03u.vcd", (unsigned)n);
        if (!fs->exists(filename)) break;
    }
    File file = fs->open(filename, FILE_WRITE);
    if (!file) {
        displayError("Failed creating VCD", true);
        return;
    }

    // Timescale in nanoseconds matching RMT tick resolution
    uint32_t nsPerTick = (s.resolution > 0) ? (uint32_t)(1000000000ULL / s.resolution) : 1000;
    if (nsPerTick == 0) nsPerTick = 1;

    file.println("$version Nezha Cyber Logic Analyzer $end");
    file.printf("$timescale %uns $end\n", (unsigned)nsPerTick);
    file.println("$scope module logic $end");
    for (uint8_t c = 0; c < s.nch; ++c) {
        file.printf("$var wire 1 %c ch%u_gpio%d $end\n", (char)('!' + c), (unsigned)(c + 1), (int)s.pins[c]);
    }
    file.println("$upscope $end");
    file.println("$enddefinitions $end");
    file.println("$dumpvars");
    for (uint8_t c = 0; c < s.nch; ++c) {
        file.printf("%u%c\n", (unsigned)s.initLevel[c], (char)('!' + c));
    }
    file.println("$end");

    // K-way merge across channels in strictly ascending tick order (O(1) extra RAM)
    size_t idx[LA_MAX_CHANNELS] = {0, 0, 0, 0};
    uint64_t lastTick = UINT64_MAX;
    while (true) {
        uint64_t minTick = UINT64_MAX;
        for (uint8_t c = 0; c < s.nch; ++c) {
            if (idx[c] < s.edges[c].size()) {
                uint64_t t = s.edges[c][idx[c]] >> 1;
                if (t < minTick) minTick = t;
            }
        }
        if (minTick == UINT64_MAX) break;
        if (minTick != lastTick) {
            file.printf("#%llu\n", (unsigned long long)minTick);
            lastTick = minTick;
        }
        for (uint8_t c = 0; c < s.nch; ++c) {
            while (idx[c] < s.edges[c].size() && (uint64_t)(s.edges[c][idx[c]] >> 1) == minTick) {
                uint8_t lv = s.edges[c][idx[c]] & 1;
                file.printf("%u%c\n", (unsigned)lv, (char)('!' + c));
                idx[c]++;
            }
        }
    }
    file.flush();
    file.close();
    displaySuccess("Saved " + String(filename), true);
}

/*********************************************************************
**  On-Device Protocol Decoders (UART, I2C, SPI, 1-Wire)
**********************************************************************/
static uint8_t la_sample_at(const LaSession &s, uint8_t ch, uint64_t tick) {
    const auto &e = s.edges[ch];
    uint8_t lvl = s.initLevel[ch];
    size_t lo = 0, hi = e.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if ((uint64_t)(e[mid] >> 1) <= tick) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0) lvl = e[lo - 1] & 1;
    return lvl;
}

static void la_decode_uart() {
    const LaSession &s = laSession;
    if (!s.valid || s.edges[0].size() < 2) {
        displayError("Need capture on CH1", true);
        return;
    }
    // Estimate shortest pulse on CH1 as 1 bit period
    uint64_t minPulse = UINT64_MAX;
    for (size_t i = 1; i < s.edges[0].size(); ++i) {
        uint64_t dt = (uint64_t)(s.edges[0][i] >> 1) - (uint64_t)(s.edges[0][i - 1] >> 1);
        if (dt >= 2 && dt < minPulse) minPulse = dt;
    }
    if (minPulse == UINT64_MAX) {
        displayError("Cannot infer bit rate", true);
        return;
    }
    uint32_t estBaud = (uint32_t)(s.resolution / minPulse);

    ScrollableTextArea area("UART Decode (CH1)");
    area.addLine("Est baud: ~" + String(estBaud) + " bps");

    const auto &e = s.edges[0];
    uint64_t maxT = e.back() >> 1;
    String asciiLine;
    String hexLine;
    size_t decodedCount = 0;

    for (size_t i = 0; i < e.size() && decodedCount < 128; ++i) {
        // Start bit = falling edge (level transitions to 0)
        if ((e[i] & 1) != 0) continue;
        uint64_t startT = e[i] >> 1;
        if (startT + minPulse * 9 + minPulse / 2 > maxT) break;

        uint8_t byteVal = 0;
        for (int bit = 0; bit < 8; ++bit) {
            uint64_t sampleT = startT + (uint64_t)((bit + 1.5) * (double)minPulse);
            if (la_sample_at(s, 0, sampleT)) byteVal |= (1U << bit);
        }
        uint8_t stopBit = la_sample_at(s, 0, startT + (uint64_t)(9.5 * (double)minPulse));

        char hbuf[8];
        snprintf(hbuf, sizeof(hbuf), "%02X%s", byteVal, stopBit ? " " : "!");
        hexLine += hbuf;
        asciiLine += (byteVal >= 32 && byteVal <= 126) ? (char)byteVal : '.';
        decodedCount++;
        if (decodedCount % 8 == 0) {
            area.addLine(hexLine + "| " + asciiLine);
            hexLine = "";
            asciiLine = "";
        }

        // Advance past stop bit
        uint64_t nextStart = startT + minPulse * 9 + minPulse / 2;
        while (i + 1 < e.size() && (uint64_t)(e[i + 1] >> 1) < nextStart) ++i;
    }
    if (!hexLine.isEmpty()) area.addLine(hexLine + "| " + asciiLine);
    if (decodedCount == 0) area.addLine("No valid 8N1 frames found");
    area.show(true);
    area.clear();
}

static void la_decode_i2c() {
    const LaSession &s = laSession;
    if (!s.valid || s.nch < 2) {
        displayError("Need CH1=SDA CH2=SCL", true);
        return;
    }
    ScrollableTextArea area("I2C Decode (1:SDA 2:SCL)");
    // Walk both SDA (ch0) and SCL (ch1) edges in chronological order
    size_t i0 = 0, i1 = 0;
    uint8_t sda = s.initLevel[0], scl = s.initLevel[1];
    bool inFrame = false;
    bool firstByte = true;
    uint8_t shift = 0;
    uint8_t bitCnt = 0;
    String line;
    size_t events = 0;

    while ((i0 < s.edges[0].size() || i1 < s.edges[1].size()) && events < 256) {
        uint64_t t0 = (i0 < s.edges[0].size()) ? (s.edges[0][i0] >> 1) : UINT64_MAX;
        uint64_t t1 = (i1 < s.edges[1].size()) ? (s.edges[1][i1] >> 1) : UINT64_MAX;
        if (t0 <= t1) {
            uint8_t newSda = s.edges[0][i0++] & 1;
            if (scl == 1) {
                if (sda == 1 && newSda == 0) {
                    if (!line.isEmpty()) area.addLine(line);
                    line = "[START]";
                    inFrame = true;
                    firstByte = true;
                    bitCnt = 0;
                    shift = 0;
                    events++;
                } else if (sda == 0 && newSda == 1 && inFrame) {
                    line += " [STOP]";
                    area.addLine(line);
                    line = "";
                    inFrame = false;
                    events++;
                }
            }
            sda = newSda;
        } else {
            uint8_t newScl = s.edges[1][i1++] & 1;
            if (inFrame && scl == 0 && newScl == 1) {
                if (bitCnt < 8) {
                    shift = (uint8_t)((shift << 1) | (sda & 1));
                    bitCnt++;
                } else {
                    const char *ack = (sda == 0) ? "A" : "N";
                    char tok[20];
                    if (firstByte) {
                        snprintf(
                            tok,
                            sizeof(tok),
                            " 0x%02X(%c):%s",
                            (unsigned)(shift >> 1),
                            (shift & 1) ? 'R' : 'W',
                            ack
                        );
                        firstByte = false;
                    } else {
                        snprintf(tok, sizeof(tok), " %02X:%s", (unsigned)shift, ack);
                    }
                    line += tok;
                    if (line.length() > 36) {
                        area.addLine(line);
                        line = " ";
                    }
                    bitCnt = 0;
                    shift = 0;
                    events++;
                }
            }
            scl = newScl;
        }
    }
    if (!line.isEmpty()) area.addLine(line);
    if (events == 0) area.addLine("No I2C START/STOP found");
    area.show(true);
    area.clear();
}

static void la_decode_spi() {
    const LaSession &s = laSession;
    if (!s.valid || s.nch < 2) {
        displayError("Need CH1=SCK CH2=MOSI", true);
        return;
    }
    const bool hasMiso = s.nch >= 3;
    const bool hasCs = s.nch >= 4;
    ScrollableTextArea area("SPI Mode0 (1:SCK 2:MO)");
    uint8_t mosiByte = 0, misoByte = 0;
    uint8_t bitCnt = 0;
    size_t decoded = 0;
    String row;

    for (uint32_t e : s.edges[0]) {
        if ((e & 1) != 1) continue; // sample on SCK rising edge
        uint64_t t = e >> 1;
        if (hasCs && la_sample_at(s, 3, t) != 0) {
            bitCnt = 0;
            mosiByte = misoByte = 0;
            continue;
        }
        mosiByte = (uint8_t)((mosiByte << 1) | la_sample_at(s, 1, t));
        if (hasMiso) misoByte = (uint8_t)((misoByte << 1) | la_sample_at(s, 2, t));
        bitCnt++;
        if (bitCnt == 8) {
            char tok[16];
            if (hasMiso) snprintf(tok, sizeof(tok), "%02X/%02X ", mosiByte, misoByte);
            else snprintf(tok, sizeof(tok), "%02X ", mosiByte);
            row += tok;
            decoded++;
            if (row.length() >= 30) {
                area.addLine(row);
                row = "";
            }
            bitCnt = 0;
            mosiByte = misoByte = 0;
            if (decoded >= 128) break;
        }
    }
    if (!row.isEmpty()) area.addLine(row);
    if (decoded == 0) area.addLine("No SPI bytes decoded");
    area.show(true);
    area.clear();
}

static void la_decode_onewire() {
    const LaSession &s = laSession;
    if (!s.valid || s.edges[0].size() < 2) {
        displayError("Need capture on CH1", true);
        return;
    }
    ScrollableTextArea area("1-Wire Decode (CH1)");
    const auto &e = s.edges[0];
    uint8_t curByte = 0;
    uint8_t bitCnt = 0;
    String row;
    size_t events = 0;

    for (size_t i = 0; i + 1 < e.size() && events < 256; ++i) {
        // Measure low pulse width (falling edge followed by rising edge)
        if ((e[i] & 1) == 0 && (e[i + 1] & 1) == 1) {
            uint64_t lowTicks = (uint64_t)(e[i + 1] >> 1) - (uint64_t)(e[i] >> 1);
            double lowUs = (double)lowTicks * 1000000.0 / (double)s.resolution;
            if (lowUs >= 380.0) {
                if (!row.isEmpty()) {
                    area.addLine(row);
                    row = "";
                }
                area.addLine("[RESET " + String((int)lowUs) + "us]");
                bitCnt = 0;
                curByte = 0;
                events++;
            } else if (lowUs >= 1.0 && lowUs <= 150.0) {
                uint8_t bit = (lowUs < 25.0) ? 1 : 0;
                curByte |= (uint8_t)(bit << bitCnt);
                bitCnt++;
                if (bitCnt == 8) {
                    char tok[6];
                    snprintf(tok, sizeof(tok), "%02X ", curByte);
                    row += tok;
                    if (row.length() >= 30) {
                        area.addLine(row);
                        row = "";
                    }
                    curByte = 0;
                    bitCnt = 0;
                    events++;
                }
            }
        }
    }
    if (!row.isEmpty()) area.addLine(row);
    if (events == 0) area.addLine("No 1-Wire slots found");
    area.show(true);
    area.clear();
}

static void la_decode_menu() {
    if (!laSession.valid) {
        displayError("Capture signal first", true);
        return;
    }
    std::vector<Option> decOpts = {
        {"UART 8N1 (CH1)",       la_decode_uart   },
        {"I2C (CH1:SDA CH2:SCL)", la_decode_i2c   },
        {"SPI (1:SCK 2:MO 3:MI)", la_decode_spi   },
        {"1-Wire (CH1)",         la_decode_onewire},
        {"Back",                 []() {}          },
    };
    loopOptions(decOpts, MENU_TYPE_SUBMENU, "Protocol Decoder");
}

/*********************************************************************
**  GPIO picker (same interaction as JTAG pin assignment)
**********************************************************************/
static int8_t la_pick_pin(const char *title, int8_t current) {
    options.clear();
    int8_t picked = current;
    for (int i = -1; i <= GPIO_NUM_MAX; i++) {
        options.push_back({String(i), [i, &picked]() { picked = (int8_t)i; }, i == current});
    }
    int index = (current < 0) ? 0 : current + 1;
    loopOptions(options, MENU_TYPE_SUBMENU, title, index);
    return picked;
}

/*********************************************************************
**  Function: logic_analyzer_setup
**********************************************************************/
static void la_pins_menu();

void logic_analyzer_setup() {
    returnToMenu = false;
    while (!returnToMenu) {
        LaSession &s = laSession;
        String pinsLabel;
        for (uint8_t c = 0; c < s.nch; c++) pinsLabel += " " + String(s.pins[c]);
        String glitchLabel = s.glitchNs == 0 ? "off" : String(s.glitchNs) + "ns";
        const char *pullLabel = s.pull == 1 ? "Up" : (s.pull == 2 ? "Down" : "None");

        options = {
            {"Channels: " + String(s.nch), []() { laSession.nch = laSession.nch % LA_MAX_CHANNELS + 1; }       },
            {"Pins:" + pinsLabel,          la_pins_menu                                                        },
            {"Rate: " + la_fmt_rate(s.resolution),
             []() {
                 for (uint8_t i = 0; i < sizeof(laRates) / sizeof(laRates[0]); i++) {
                     if (laRates[i] == laSession.resolution) {
                         laSession.resolution = laRates[(i + 1) % (sizeof(laRates) / sizeof(laRates[0]))];
                         return;
                     }
                 }
                 laSession.resolution = laRates[0];
             }                                                                                                  },
            {"Depth: " + String(s.depth),
             []() {
                 for (uint8_t i = 0; i < sizeof(laDepths) / sizeof(laDepths[0]); i++) {
                     if (laDepths[i] == laSession.depth) {
                         laSession.depth = laDepths[(i + 1) % (sizeof(laDepths) / sizeof(laDepths[0]))];
                         return;
                     }
                 }
                 laSession.depth = laDepths[0];
             }                                                                                                  },
            {String("Pull: ") + pullLabel, []() { laSession.pull = (laSession.pull + 1) % 3; }                 },
            {"Glitch: " + glitchLabel,
             []() {
                 uint32_t g[] = {0, 20, 50, 100};
                 for (uint8_t i = 0; i < 4; i++) {
                     if (g[i] == laSession.glitchNs) {
                         laSession.glitchNs = g[(i + 1) % 4];
                         return;
                     }
                 }
                 laSession.glitchNs = 0;
             }                                                                                                  },
            {"Capture",                    la_capture_and_view                                                 },
            {"Decode Protocol",            la_decode_menu                                                      },
            {"SUMP Mode",                  []() { sump_menu(); }                                               },
            {"Save VCD (PulseView)",       la_save_vcd                                                         },
            {"Save CSV",                   la_save_csv                                                         },
        };
        addOptionToMainMenu();
        loopOptions(options, MENU_TYPE_SUBMENU, "Logic Analyzer");
    }
}

static void la_pins_menu() {
    for (uint8_t c = 0; c < laSession.nch; c++) {
        String title = "CH" + String(c + 1) + " Pin";
        laSession.pins[c] = la_pick_pin(title.c_str(), laSession.pins[c]);
    }
}

/*********************************************************************
**  Serial-command primitives (wire la ...), see wire_tools.h
**********************************************************************/
String wire_la_capture(uint32_t resHz, uint32_t depth, int8_t p1, int8_t p2, int8_t p3, int8_t p4) {
    LaSession &s = laSession;
    if (resHz < 20000 || resHz > LA_CLK_HZ || LA_CLK_HZ % resHz != 0)
        return "ERR: res must divide 80000000 (20000..80000000)";
    if (depth < 64) depth = 64;
    if (depth > 8192) depth = 8192;
    int8_t newPins[LA_MAX_CHANNELS] = {p1, p2, p3, p4};
    uint8_t nch = 0;
    for (uint8_t c = 0; c < LA_MAX_CHANNELS; c++)
        if (newPins[c] >= 0) nch = c + 1;
    if (nch == 0) {
        // keep the pins configured in the UI menu
        nch = s.nch;
        for (uint8_t c = 0; c < LA_MAX_CHANNELS; c++) newPins[c] = s.pins[c];
    }

    LaSession req = s;
    req.nch = nch;
    for (uint8_t c = 0; c < LA_MAX_CHANNELS; c++) req.pins[c] = newPins[c];
    req.resolution = resHz;
    req.depth = depth;

    if (!la_run_capture(req)) return "ERR: capture failed (pins/heap/aborted)";
    for (uint8_t c = 0; c < nch; c++) la_extract(req, c);
    s = req; // becomes the session capture for dump/save/viewer

    String out = "OK ch=" + String(nch) + " res=" + String(resHz);
    out += " edges=";
    for (uint8_t c = 0; c < nch; c++) out += (c ? "," : "") + String(s.edges[c].size());
    uint64_t maxT = 0;
    for (uint8_t c = 0; c < nch; c++)
        if (!s.edges[c].empty()) maxT = std::max<uint64_t>(maxT, s.edges[c].back() >> 1);
    out += " window_us=" + String((double)maxT * 1000000.0 / resHz, 1);
    return out;
}

void wire_la_dump() {
    LaSession &s = laSession;
    if (!s.valid) {
        serialDevice->println("ERR: no capture");
        return;
    }
    serialDevice->println("OK");
    for (uint8_t c = 0; c < s.nch; c++) {
        uint8_t lvl = s.initLevel[c];
        serialDevice->println("# ch=" + String(c + 1) + " pin=" + String(s.pins[c]) + " idle=" + String(lvl));
        for (uint32_t e : s.edges[c]) {
            serialDevice->printf("%u,%.3f,%u\n", c + 1, (double)(e >> 1) * 1000000.0 / s.resolution, e & 1);
        }
    }
}

String wire_la_status() {
    LaSession &s = laSession;
    String out = "OK res=" + String(s.resolution) + " depth=" + String(s.depth) + " ch=" + String(s.nch) + " pins=";
    for (uint8_t c = 0; c < s.nch; c++) out += (c ? "," : "") + String(s.pins[c]);
    out += " pull=" + String(s.pull) + " valid=" + (s.valid ? "1" : "0");
    if (s.valid) {
        out += " edges=";
        for (uint8_t c = 0; c < s.nch; c++) out += (c ? "," : "") + String(s.edges[c].size());
    }
    return out;
}

uint8_t wire_la_channels() { return laSession.nch; }

uint32_t wire_la_resolution() { return laSession.resolution; }

bool wire_la_sump_stream(
    uint32_t resolutionHz, double rate, uint32_t nSamples, uint16_t trigMask, uint16_t trigVals,
    Print &out
) {
    if (nSamples == 0 || rate <= 0) return false;
    LaSession req = laSession;
    req.resolution = resolutionHz;
    if (!la_run_capture(req)) return false;
    for (uint8_t c = 0; c < req.nch; c++) la_extract(req, c);

    // Generate and stream the sample grid on the fly (probe c -> bit c, level
    // sampled every resolution/rate ticks) — no large intermediate buffer, so
    // this works on fragmented heaps. Stage-0 trigger: samples before the first
    // match are skipped.
    const double tps = (double)resolutionHz / rate; // ticks per sample
    uint8_t chunk[256];
    uint8_t chunkLen = 0;
    bool started = trigMask == 0;

    struct ChanWalk {
        const std::vector<uint32_t> *e;
        size_t idx;
        uint8_t lvl;
        uint8_t bit;
    };
    ChanWalk walks[LA_MAX_CHANNELS];
    for (uint8_t c = 0; c < req.nch; c++) {
        walks[c] = {&req.edges[c], 0, req.initLevel[c], (uint8_t)(1 << c)};
    }

    for (uint32_t si = 0; si < nSamples; si++) {
        double tStart = si * tps;
        uint8_t b = 0;
        for (uint8_t c = 0; c < req.nch; c++) {
            ChanWalk &w = walks[c];
            const std::vector<uint32_t> &e = *w.e;
            while (w.idx < e.size() && (double)(e[w.idx] >> 1) <= tStart) {
                w.lvl = e[w.idx] & 1;
                w.idx++;
            }
            if (w.lvl) b |= w.bit;
        }
        if (!started) {
            if ((b & trigMask) == (trigVals & trigMask)) started = true;
            else continue;
        }
        chunk[chunkLen++] = b;
        if (chunkLen == sizeof(chunk)) {
            out.write(chunk, sizeof(chunk));
            chunkLen = 0;
        }
    }
    if (chunkLen) out.write(chunk, chunkLen);
    return true;
}
#endif
