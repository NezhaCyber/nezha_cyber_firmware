/**
 * @file RFID2.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Read and Write RFID tags using RFID2 module from M5Stack
 * @version 0.1
 * @date 2024-08-19
 */

#include "RFIDInterface.h"
#include "core/bus_HAL.h"
#include <MFRC522Driver.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522v2.h>
#include <Wire.h>

class RFID2 : public RFIDInterface {
public:
    MFRC522 mfrc522;

    /*
     * Constructs the MFRC522/WS1850S driver: when use_i2c is set it leases the
     * I2C bus the module answered on (with a verify callback) and builds an
     * I2C driver; otherwise it wires an SPI driver from bruceConfigPins.PN532_bus.
     * The destructor frees the driver before the I2CLease member releases its bus.
     */
    RFID2(bool use_i2c = true);
    ~RFID2();

    /*
     * Brings up the reader: probes the I2C address when a lease is held,
     * initializes the MFRC522 PCD (cranking antenna gain to max on I2C),
     * reads the chip version register, and returns true only for a recognized
     * version.
     */
    bool begin();

    /*
     * RFIDInterface operations implemented for the MFRC522: read() detects and
     * dumps a card, clone()/write()/erase() manipulate a target tag,
     * write_ndef() programs an NDEF record, and load()/save() round-trip a
     * dump through the shared file parser.
     */
    int read(int cardBaudRate = 0);
    int clone();
    int erase();
    int write(int cardBaudRate = 0);
    int write_ndef();
    int load();
    int save(const String &filename);

private:
    bool _use_i2c;
    I2CLease _i2cLease; // whichever bus (sys_i2c or i2c_bus) the module answered on
    MFRC522Driver *_driver = nullptr;
    MFRC522DriverPinSimple ss_pin; // initialized from bruceConfigPins.PN532_bus.cs, see RFID2.cc

    /*
     * Data converters: set_uid() fills the printable UID/SAK/ATQA strings,
     * format_data() renders the read blocks into strAllPages, and parse_data()
     * rebuilds block data from a loaded dump string for write/clone.
     */
    void format_data();
    void parse_data();
    void set_uid();

    /*
     * Proximity-card (ISO/IEC 14443 A) helpers: PICC_IsNewCardPresent() polls
     * for a card, get_tag_type() classifies it, and the read/write/authenticate
     * block helpers handle MIFARE Classic (key-authenticated sectors) and
     * MIFARE Ultralight page access, plus erase and NDEF block programming.
     */
    bool PICC_IsNewCardPresent();

    String get_tag_type();
    int read_data_blocks();
    int read_mifare_classic_data_blocks(byte piccType);
    int read_mifare_classic_data_sector(byte sector);
    int authenticate_mifare_classic(byte block);
    int read_mifare_ultralight_data_blocks();

    int write_data_blocks();
    bool write_mifare_classic_data_block(int block, String data);
    bool write_mifare_ultralight_data_block(int block, String data);

    int erase_data_blocks();
    int write_ndef_blocks();
};
