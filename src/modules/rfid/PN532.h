/**
 * @file PN532.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Read and Write RFID tags using PN532 module
 * @version 0.1
 * @date 2024-08-19
 */

#include "RFIDInterface.h"
#include "core/bus_HAL.h"
#define private public
#include <Adafruit_PN532.h>
#undef private

class PN532 : public RFIDInterface {
public:
    enum CONNECTION_TYPE { I2C = 1, I2C_SPI = 2, SPI = 3 };
    enum PICC_Type {
        PICC_TYPE_MIFARE_MINI = 0x09, // MIFARE Classic protocol, 320 bytes
        PICC_TYPE_MIFARE_1K = 0x08,   // MIFARE Classic protocol, 1KB
        PICC_TYPE_MIFARE_4K = 0x18,   // MIFARE Classic protocol, 4KB
        PICC_TYPE_MIFARE_UL = 0x00,   // MIFARE Ultralight or Ultralight C
    };

// Devices such as T-Embed CC1101 uses an embedded PN532 that needs the IRQ and RST pins to work
// If using other device that uses, set -DPN532_IRQ=pin_num and -DPN532_RF_REST=pin_num to platformio.ini
// of this particular device, should not be used in other devices on I2C mode
#if defined(PN532_IRQ) && defined(PN532_RF_REST)
    Adafruit_PN532 nfc = Adafruit_PN532(PN532_IRQ, PN532_RF_REST);
#else
    Adafruit_PN532 nfc = Adafruit_PN532();
#endif

    /*
     * Constructs the PN532 front-end for the chosen CONNECTION_TYPE (I2C,
     * I2C_SPI, or SPI). SPI pins are bound immediately from
     * bruceConfigPins.PN532_bus; I2C modes defer acquiring their bus lease to
     * begin(), since it depends on which bus the PN532 actually answers on.
     */
    PN532(CONNECTION_TYPE connection_type = I2C);

    /*
     * Brings up the PN532: in I2C mode acquires an I2CLease (probing with
     * retries and a user-bus fallback so a sleeping chip can still be woken),
     * binds the Wire interface, runs nfc.begin(), and confirms presence via
     * the firmware version register or a successful I2C probe.
     */
    bool begin();

    /*
     * RFIDInterface operations for the PN532: read/write/clone/erase target
     * ISO14443-A cards at the given baud, write_ndef() programs an NDEF
     * record, emulate() (an override) performs card emulation where supported,
     * and load()/save() round-trip dumps through the shared file parser.
     */
    int read(int cardBaudRate = PN532_MIFARE_ISO14443A);
    int clone();
    int erase();
    int write(int cardBaudRate = PN532_MIFARE_ISO14443A);
    int write_ndef();
    int emulate() override;
    int load();
    int save(const String &filename);
    String emulationCaveat() const override;

    // Bus the PN532 talks through in I2C modes (nullptr in SPI mode or before begin()).
    TwoWire *i2cWire() const { return _i2cLease.wire(); }

private:
    bool _use_i2c;
    CONNECTION_TYPE _connection_type;
    I2CLease _i2cLease; // sys_i2c or i2c_bus, whichever the PN532 answered on

    /*
     * Data converters: set_uid() fills the printable UID/SAK/ATQA strings,
     * format_data() and format_data_felica() render read pages (MIFARE or
     * FeliCa IDm/PMm/SysCode) into strAllPages, and parse_data() rebuilds
     * block data from a loaded dump string for write/clone.
     */
    void format_data();
    void format_data_felica(uint8_t idm[8], uint8_t pmm[8], uint16_t sys_code);
    void parse_data();
    void set_uid();

    /*
     * Proximity-card and tag-type helpers: get_tag_type() classifies the
     * detected card, and the read/write helpers cover MIFARE Classic
     * (key-authenticated sectors), MIFARE Ultralight pages, NDEF Type 4 Tag
     * data, and FeliCa block read/write, plus erase and NDEF block programming.
     */
    String get_tag_type();
    int read_data_blocks();
    int read_mifare_classic_data_blocks();
    int read_mifare_classic_data_sector(byte sector);
    int authenticate_mifare_classic(byte block);
    int read_mifare_ultralight_data_blocks();
    int read_ndef_t4t_data();

    int write_data_blocks();
    bool write_mifare_classic_data_block(int block, String data);
    bool write_mifare_ultralight_data_block(int block, String data);

    int read_felica_data();

    int erase_data_blocks();
    int write_ndef_blocks();

    int write_felica_data_block(int block, String data);
};
