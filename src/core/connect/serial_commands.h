#include <sdkconfig.h> // CONFIG_IDF_TARGET_ESP32P4 lives here; must precede the P4 guard below
#ifndef __ESP_SERIAL_CMD_H__
#define __ESP_SERIAL_CMD_H__
#if !defined(LITE_VERSION)
#include "esp_connection.h"

class EspSerialCmd : public EspConnection {
public:
    /*
     * EspSerialCmd connection wrapper.
     * Extends EspConnection for bidirectional command exchange with a peer.
     */
    EspSerialCmd();

    /*
     * Command transport entry points.
     * sendCommands()/receiveCommands() drive the send and receive phases over the link.
     */
    void sendCommands();
    void receiveCommands();

private:
    String recvCommand;

    /*
     * Rendering and message-construction helpers.
     * Build the on-screen command console and serialize a command into a transport Message.
     */
    void displayBanner();

    void displayRecvCommand(bool success);
    void displayRecvError();
    void displayRecvFooter();

    void displaySentCommand(const char *command);
    void displaySentError();
    void displaySentFooter();

    Message createCmdMessage();
};

#endif
#endif
