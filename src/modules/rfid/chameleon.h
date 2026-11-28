/**
 * @file chameleon.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Emulate tags using a Chameleon device
 * @version 0.1
 * @date 2024-10-09
 */

#ifndef __CHAMELEON_H__
#define __CHAMELEON_H__
#include <chameleonUltra.h>
#include <set>

class Chameleon {
public:
    typedef struct {
        String uid;
        String bcc;
        String sak;
        String atqa;
        String piccType;
    } PrintableUID;

    typedef struct {
        String tagType;
        String uid;
    } ScanResult;

    enum AppMode {
        BATTERY_INFO_MODE,
        FACTORY_RESET_MODE,

        LF_READ_MODE,
        LF_SCAN_MODE,
        LF_CLONE_MODE,
        LF_EMULATION_MODE,
        LF_SAVE_MODE,
        LF_LOAD_MODE,
        LF_CUSTOM_UID_MODE,

        HF_READ_MODE,
        HF_SCAN_MODE,
        HF_EMULATION_MODE,
        HF_SAVE_MODE,
        HF_LOAD_MODE,
        HF_CLONE_MODE,
        HF_WRITE_MODE,
        HF_CUSTOM_UID_MODE,

        FULL_SCAN_MODE,

        // WRITE_NDEF_MODE,
        // ERASE_MODE,
    };

    /*
     * Constructs the Chameleon Ultra front-end (the ChameleonUltra driver is
     * created with logging enabled) and tears it down; the device itself is
     * not contacted until connect() is called from setup().
     */
    Chameleon();
    ~Chameleon();

    /*
     * Lifecycle and device link: connect() searches for and pairs with the
     * Chameleon Ultra over its transport; setup() connects, drops into
     * BATTERY_INFO_MODE, and enters loop(), which dispatches the LF/HF
     * AppMode state machine until EscPress returns to the menu.
     */
    void setup();
    void loop();
    bool connect();

private:
    ChameleonUltra chmUltra = ChameleonUltra(true);
    ChameleonUltra::LfTag lfTagData;
    ChameleonUltra::HfTag hfTagData;
    AppMode currentMode;
    PrintableUID printableHFUID;
    String printableLFUID;
    String dumpFilename = "";
    String strDump = "";
    bool _lf_read_uid = false;
    bool _hf_read_uid = false;
    bool _battery_set = false;
    bool pageReadSuccess = false;
    uint32_t _lastReadTime = 0;
    String strAllPages = "";
    int totalPages = 0;
    int dataPages = 0;
    std::set<String> _scanned_set;
    std::vector<ScanResult> _scanned_tags;

    /*
     * Mode state machine: addOptionSetMode() registers a menu option bound to
     * an AppMode, selectMode() renders that menu, and setMode() records the
     * chosen LF/HF mode before loop() dispatches the matching operation.
     */
    void addOptionSetMode(const char *name, AppMode mode);
    void selectMode();
    void setMode(AppMode mode);

    /*
     * TFT rendering helpers: displayBanner() paints the module title,
     * dumpHFCardDetails() renders the read HF tag's UID/ATQA/SAK/type, and
     * dumpScanResults() lists the tags accumulated during a scan.
     */
    void displayBanner();
    void dumpHFCardDetails();
    void dumpScanResults();

    /*
     * Tag operations split by band: the LF_* group reads/scans/clones/emulates
     * low-frequency (e.g. EM410x) tags and saves/loads their dumps; the HF_*
     * group does the same for high-frequency tags plus data writing. fullScan
     * sweeps both bands, while getBatteryInfo/factoryReset are device-level
     * commands routed through the Chameleon.
     */
    void readLFTag();
    void scanLFTags();
    void cloneLFTag();
    void customLFUid();
    void emulateLF();
    void saveFileLF();
    void loadFileLF();

    void readHFTag();
    void scanHFTags();
    void cloneHFTag();
    void writeHFData();
    void customHFUid();
    void emulateHF();
    void saveFileHF();
    void loadFileHF();

    void fullScanTags();

    void getBatteryInfo();
    void factoryReset();
    // void erase_card();
    // void write_ndef_data();

    /*
     * Parsing, persistence, and tag helpers: the format and parse pairs for
     * LFUID/HFData convert between the Chameleon's raw data and printable
     * strings;
     * writeFileLF/HF and readFileLF/HF persist dumps to storage; readHFDataBlocks
     * pulls MIFARE Classic (key-authenticated) or Ultralight pages. selectSlot
     * picks the active Chameleon emulation slot, isMifareClassic classifies by
     * SAK, and saveScanResult stores a scan session.
     */
    void formatLFUID();
    void parseLFUID();
    bool writeFileLF(String filename);
    bool readFileLF();

    void formatHFData();
    void parseHFData();
    bool writeFileHF(String filename);
    bool readFileHF();
    bool readHFDataBlocks();
    bool readMifareClassicDataBlocks(uint8_t *key);
    bool readMifareUltralightDataBlocks();
    bool writeHFDataBlocks();

    uint8_t selectSlot();
    bool isMifareClassic(byte sak);
    void saveScanResult();
    void delayWithReturn(uint32_t ms);
};

#endif
