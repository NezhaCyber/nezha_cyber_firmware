#ifndef __RF_SCAN_H__
#define __RF_SCAN_H__

#include "protocols/rf_decoder.h"
#include "rf_utils.h"
#include "structs.h"

#define _MAX_TRIES 5

// Bound both the entry count and retained payload bytes (200 kB total).
// Full PSRAM builds capture up to 8192 symbols; other boards retain 256.
#define _MAX_CAPTURED 10

// One entry of the Scan/Copy capture list: the decoded/raw signal plus the
// frequency it was heard on and whether it has already been written to storage.
struct RfCapture {
    RfCodes code;
    float freq = 0.f;
    bool saved = false;
};

class RFScan {
public:
    enum RFMenuOption {
        REPLAY,
        REPLAY_RAW,
        SAVE,
        SAVE_RAW,
        SIGNAL_INFO,
        DELETE_SIGNAL,
        SCAN_OPTIONS,
        RESET,
        RANGE,
        THRESHOLD,
        CLOSE_MENU,
        MAIN_MENU,
        NO_ACTION,
    };

    /*
     * Constructor / destructor: bring the receiver up and enter the scan loop,
     * then stop RX and release the shared RF module on destruction.
     */
    RFScan();
    ~RFScan();

    /*
     * Life cycle: setup() arms the receiver, restores the configured scan range
     * and paints the capture list; loop() runs until the user leaves.
     */
    void setup();
    void loop();

private:
    RfRxSession _rx;
    RfCodes received;
    String title = "RF Scan Copy";
    bool restartScan = false;
    bool exitRequested = false;
    bool initFailed = false;
    uint64_t lastM5CaptureKey = 0;
    String lastM5CaptureProtocol = "";
    unsigned long lastM5CaptureMs = 0;
    bool ReadRAW = true;
    bool codesOnly = false;
    bool autoSave = false;
    char hexString[64];
    float frequency = 0.f;
    uint8_t _try = 0;
    FreqFound _freqs[_MAX_TRIES]; // get the best RSSI out of 5 tries
    int idx = range_limits[bruceConfigPins.rfScanRange][0];
    float found_freq = 0.f;
    int rssi = -80;
    int rssiThreshold = -65;

    // Capture list shown on the scan screen: every signal kept in memory until
    // the user deletes it or leaves the feature.
    std::vector<RfCapture> captures;
    int selected = 0;
    int scrollOffset = 0;
    bool listDirty = true;

    /*
     * State management: list navigation plus the signal and scan-option menus.
     * Both menus stop the receiver and release the shared bus before drawing,
     * and report back through set_option().
     */
    bool handle_list_input();
    void open_signal_menu(int index);
    void open_scan_options();
    void set_option(RFMenuOption option, int index);

    /*
     * Operations: decode or RAW-capture one received burst, de-duplicate it
     * against the list, and act on the selected entry (info / replay / save /
     * delete).
     */
    bool decode_signal(const std::vector<int> &durations);
    bool read_raw(const std::vector<int> &durations);
    bool is_m5_duplicate_capture(const RfCodes &data);
    bool is_duplicate_capture(const RfCodes &data) const;
    bool add_capture();
    void delete_capture(int index);
    void replay_signal(int index, bool asRaw = false);
    void save_signal(int index, bool asRaw = false);
    void show_signal_info(int index);
    void reset_signals();
    void set_threshold();
    // void set_range(); // Using similar function from rf_utils.h

    /*
     * Utils: receiver arming, the RSSI frequency hunt (best of _MAX_TRIES
     * samples), list selection movement and the capture-list renderer.
     */
    void enable_receive();
    void init_freqs();
    bool fast_scan();
    void move_selection(int step);
    void draw_capture_list();
    String capture_label(int index) const;
};

void display_info(
    RfCodes received, int signals, bool ReadRAW = false, bool codesOnly = false, bool autoSave = false,
    const String &title = "", bool headless = false
);
void display_signal_data(RfCodes received, bool headless = false);

bool rfSaveSignal(float frequency, RfCodes codes, bool raw, char *key, bool autoSave = false);

String rf_scan(float start_freq, float stop_freq, int max_loops = -1);
// Keeps native ProtoPirate reception armed while the previous capture is decoded.
// Other callers retain the one-shot lifecycle by omitting this optional session.
class RfReceiveSession {
public:
    ~RfReceiveSession() { stop(); }
    void stop();
    RfReceiveSession() = default;
    RfReceiveSession(const RfReceiveSession &) = delete;
    RfReceiveSession &operator=(const RfReceiveSession &) = delete;
private:
    RfRxSession rx;
    bool radioActive = false;
    float frequency = 0;
    String preset, registers;
    friend String rfReceiveSignal(float, int, bool, bool, const String &, bool, RfReceiveSession *);
};

String rfReceiveSignal(
    float frequency = 0, int maxSeconds = -1, bool raw = false, bool headless = false,
    const String &rxPreset = "", bool interruptOnNavigation = false, RfReceiveSession *session = nullptr
);

// Shared native capture-list presentation and navigation reset.
void rf_clear_nav_state();
struct RfCaptureListState {
    bool valid = false;
    String title, status, footer;
    std::vector<String> labels;
    int selected = -1, scrollOffset = 0, width = 0, height = 0;
    size_t capacity = 0;
};

void rf_draw_capture_list(
    const String &title, const String &status, const String &footer,
    const std::vector<String> &labels, int selected, int &scrollOffset, size_t capacity,
    RfCaptureListState *previous = nullptr
);

#endif
