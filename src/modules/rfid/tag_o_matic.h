/**
 * @file tag_o_matic.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Read and Write RFID tags
 * @version 0.2
 * @date 2024-08-19
 */

#ifndef __TAG_O_MATIC_H__
#define __TAG_O_MATIC_H__

#include "RFIDInterface.h"
#include <set>

class TagOMatic {
public:
    enum RFID_State {
        READ_MODE,
        SCAN_MODE,
        CHECK_MODE,
        CLONE_MODE,
        CUSTOM_UID_MODE,
        WRITE_MODE,
        WRITE_NDEF_MODE,
        EMULATE_MODE,
        EMULATE_NDEF_MODE,
        ERASE_MODE,
        LOAD_MODE,
        SAVE_MODE
    };

    /*
     * Constructors and destructor: the default starts the generic RFID
     * front-end in READ_MODE; the state overload launches directly into a
     * chosen mode. The destructor frees the owned RFIDInterface driver, which
     * releases its I2C/SPI bus lease in the process.
     */
    TagOMatic();
    TagOMatic(RFID_State initial_state);
    ~TagOMatic();
// Headless constructor
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
    TagOMatic(bool headless_mode);
#endif
    /*
     * Lifecycle and driver selection: set_rfid_module() instantiates the
     * RFIDInterface driver for bruceConfigPins.rfidModule (PN532, RFID2/RC522,
     * or ST25R3916 over I2C/SPI); setup() brings it up and enters loop(),
     * which dispatches the active RFID_State until EscPress returns to menu.
     */
    void setup();
    void loop();
    void set_rfid_module();
// JS Support
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)

    String read_tag_headless(int timeout_seconds);
    String read_uid_headless(int timeout_seconds);
    int write_tag_headless(int timeout_seconds);
    String save_file_headless(const String &filename);
    int load_file_headless(const String &filename);
    RFIDInterface *getRFIDInterface() { return _rfid; } // Controlled Access Getter
#endif

private:
    RFIDInterface *_rfid;
    RFID_State _initial_state;
    bool _read_uid = false;
    bool _ndef_created = false;
    uint32_t _lastReadTime = 0;
    RFID_State current_state;
    std::set<String> _scanned_set;
    std::vector<String> _scanned_tags;
    String _sourceUID;
    String _sourcePages;

    /*
     * TFT rendering helpers: display_banner() paints the module title, the
     * dump_* helpers render the read UID/pages, NDEF record, or accumulated
     * scan list, and show_not_implemented_card() reports unsupported modes.
     */
    void display_banner();
    void dump_card_details();
    void dump_check_details();
    void dump_ndef_details();
    void dump_scan_results();
    void show_not_implemented_card();

    /*
     * State machine and flow control: select_state() builds the mode menu,
     * set_state() records the new RFID_State, delayWithReturn() yields to the
     * scheduler while honoring an early exit, and ndefEmulationSupported()
     * gates NDEF emulation on the active driver's capabilities.
     */
    void select_state();
    void set_state(RFID_State state);
    void delayWithReturn(uint32_t ms);
    bool ndefEmulationSupported();

    /*
     * Tag operations dispatched by the state machine: read/scan/check a tag,
     * write a custom UID, clone, emulate (plain or NDEF), erase, write data or
     * NDEF, and save/load dumps. Each delegates to the underlying
     * RFIDInterface driver so behavior is identical across PN532, RFID2, and
     * ST25R3916.
     */
    void read_card();
    void scan_cards();
    void check_card();
    void write_custom_uid();
    void clone_card();
    void emulate_card();
    void erase_card();
    void write_data();
    void write_ndef_data();
    void emulate_ndef_data();
    void save_file();
    void save_scan_result();
    void load_file();

    /*
     * NDEF record construction: the create_* helpers prompt for a value and
     * build a text, URL, Wi-Fi Simple Config, or custom-link record, while the
     * build_* helpers encode the matching NDEF payload bytes shared by the
     * write and emulate paths.
     */
    void create_ndef_message();
    void create_ndef_url();
    void create_ndef_text();
    void create_ndef_wifi();
    void create_ndef_link();
    void build_ndef_text_payload(const String &text);
    void build_ndef_url_payload(const String &url);
    void buildWifiNdef(const String &ssid, const String &password);
};

#endif
