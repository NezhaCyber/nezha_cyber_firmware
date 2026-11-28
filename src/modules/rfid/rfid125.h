/**
 * @file rfid125.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Read RFID 125kHz tags
 * @version 0.1
 * @date 2024-08-13
 */

#ifndef __RFID125_H__
#define __RFID125_H__

#include <globals.h>

#define RFID125_PACKET_SIZE 14
#define RFID125_START_MARK 0x02
#define RFID125_END_MARK 0x03
#if !defined(RFID125_RX_PIN) || !defined(RFID125_TX_PIN)
#define RFID125_RX_PIN bruceConfigPins.uart_bus.rx
#define RFID125_TX_PIN bruceConfigPins.uart_bus.tx
#endif

class RFID125 {
public:
    enum RFID125_State {
        READ_MODE,
        // CLONE_MODE,
        // WRITE_MODE,
        // WRITE_NDEF_MODE,
        // ERASE_MODE,
        // LOAD_MODE,
        SAVE_MODE,
    };

    /*
     * Constructors: the default starts the 125 kHz reader in READ_MODE; the
     * state overload launches directly into another mode (SAVE_MODE is folded
     * back to READ_MODE since a tag must be read before it can be saved).
     */
    RFID125();
    RFID125(RFID125_State initial_state);

    /*
     * Arduino lifecycle: setup() opens UART1 at 9600 8N1 on RFID125_RX/TX,
     * applies the initial state and enters loop(); loop() runs the mode state
     * machine until EscPress tears down the stream and returns to the menu.
     */
    void setup();
    void loop();

private:
    HardwareSerial *_stream = NULL;
    RFID125_State _initial_state;
    RFID125_State _current_state;
    String _printable_data;
    String _printable_checksum;
    bool _tag_read = false;
    char _tag_data[RFID125_PACKET_SIZE];

    /*
     * TFT rendering helpers: cls() clears the screen, display_banner() paints
     * the module title, and dump_card_details() renders the captured tag data
     * and checksum strings for the active mode.
     */
    void cls();
    void display_banner();
    void dump_card_details();

    /*
     * State machine: select_state() builds the mode menu from the enabled
     * RFID125_State values; set_state() records the new state and resets the
     * per-read flag before the loop dispatches the matching operation.
     */
    void select_state();
    void set_state(RFID125_State state);

    /*
     * Tag operations: read_card() drives the READ_MODE loop, read_card_data()
     * pulls a 14-byte START_MARK/END_MARK framed packet off the UART and
     * validates its XOR checksum, and save_file() persists the captured tag.
     * Clone/write/erase/load are stubbed for the read-only EM4100-style modules
     * this class targets.
     */
    void read_card();
    bool read_card_data();
    // void clone_card();
    // void erase_card();
    // void write_data();
    // void write_ndef_data();
    void save_file();
    void clear_stream();
    // void load_file();

    /*
     * File handlers: write_file() serializes the captured tag to storage
     * (LittleFS/SD via the shared FS abstraction); load_from_file() is
     * reserved but unimplemented for these read-only modules.
     */
    bool write_file(const String &filename);
    // bool load_from_file();

    /*
     * Converters: format_data() rebuilds the printable hex data and checksum
     * strings from the raw 14-byte packet buffer so the display and file
     * writers share one formatted representation.
     */
    void format_data();
    // void parse_data();
};

#endif
