#ifndef __MASS_STORAGE_H__
#define __MASS_STORAGE_H__

#include <globals.h>
#if defined(SOC_USB_OTG_SUPPORTED)
#include <USBMSC.h>

class MassStorage {
public:
    static bool shouldStop;
    static int32_t status;

    /*
     * Constructor / destructor for the USB mass-storage gadget.
     * Owns the USBMSC instance that exposes storage to a connected host.
     */
    MassStorage();
    ~MassStorage();

    /*
     * Runtime lifecycle hooks for the mass-storage feature.
     * setup() wires the USB callbacks; loop() drives the MSC state machine.
     */
    void setup();
    void loop();

    /*
     * Runtime control surface for an active transfer.
     * setShouldStop() signals the transfer loop to exit cleanly.
     */
    static void setShouldStop(bool value) { shouldStop = value; }

    /*
     * User-facing status rendering.
     * displayMessage() surfaces transfer state and prompts on the TFT.
     */
    static void displayMessage(String message);

private:
    USBMSC msc;

    /*
     * Private USB bring-up helpers.
     * beginUsb() starts the MSC stack; the setup* callbacks register read/write/start-stop handlers.
     */
    void beginUsb(void);
    void setupUsbCallback(void);
    void setupUsbEvent(void);
};

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
int32_t usbReadCallback(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject);

void drawUSBStickIcon(bool plugged);

#endif // MASS_STORAGE_H
#endif // SOC_USB_OTG_SUPPORTED
