#include <sdkconfig.h> // CONFIG_IDF_TARGET_ESP32P4 lives here; must precede the P4 guard below
#ifndef __ESP_FILE_SHARING_H__
#define __ESP_FILE_SHARING_H__
#if !defined(LITE_VERSION)
#include "esp_connection.h"

class FileSharing : public EspConnection {
public:
    /*
     * FileSharing connection wrapper.
     * Extends EspConnection to transfer files to and from a peer.
     */
    FileSharing();

    /*
     * File transfer entry points.
     * sendFile()/receiveFile() drive the upload and download flows.
     */
    void sendFile();
    void receiveFile();

private:
    String recvFileName;

    /*
     * File selection and reassembly helpers.
     * Pick a file to send, append incoming chunks, and derive a safe destination filename.
     */
    File selectFile();
    bool appendToFile(Message fileMessage);
    void createFilename(FS *fs, Message fileMessage);
};

#endif
#endif
