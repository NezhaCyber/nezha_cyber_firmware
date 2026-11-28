/**
 * @file gps_tracker.cc
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief GPS tracker
 * @version 0.1
 * @date 2024-11-20
 */

#include "gps_tracker.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "current_year.h"

#define MAX_WAIT 5000

GPSTracker::GPSTracker() { setup(); }

GPSTracker::~GPSTracker() {
    add_final_file_data();
    if (gpsConnected) end();
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#ifdef USE_BOOST
    hal_pmic_disable_otg();
#endif
}

void GPSTracker::setup() {
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#ifdef USE_BOOST /// ENABLE 5V OUTPUT
    hal_pmic_enable_otg();
#endif
    display_banner();
    padprintln("Initializing...");

    if (!begin_gps()) return;

    return loop();
}

bool GPSTracker::begin_gps() {
    releasePins();
    pinMode(bruceConfigPins.gps_bus.rx, INPUT);
    GPSserial.begin(
        bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx, bruceConfigPins.gps_bus.tx
    );

    int count = 0;
    padprintln("Waiting for GPS data");
    while (GPSserial.available() <= 0) {
        if (check(EscPress)) {
            end();
            return false;
        }
        displayTextLine("Waiting GPS: " + String(count) + "s");
        count++;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    gpsConnected = true;
    return true;
}

void GPSTracker::end() {
    GPSserial.end();
    restorePins();

    returnToMenu = true;
    gpsConnected = false;
}

void GPSTracker::loop() {
    int count = 0;
    returnToMenu = false;
    while (1) {
        display_banner();

        if (check(EscPress) || returnToMenu) return end();

        if (GPSserial.available() > 0) {
            count = 0;
            while (GPSserial.available() > 0) gps.encode(GPSserial.read());

            if (gps.location.isUpdated()) {
                padprintln("GPS location updated");
                set_position();
                add_coord();
            } else {
                padprintln("GPS location not updated");
                dump_gps_data();

                if (filename == "" && gps.date.year() >= CURRENT_YEAR && gps.date.year() < CURRENT_YEAR + 5)
                    create_filename();
            }
        } else {
            if (count > 5) {
                displayError("GPS not Found!");
                return end();
            }
            padprintln("No GPS data available");
            count++;
        }

        int tmp = millis();
        while (millis() - tmp < MAX_WAIT && !gps.location.isUpdated()) {
            if (check(EscPress) || returnToMenu) return end();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void GPSTracker::set_position() {
    double lat = gps.location.lat();
    double lng = gps.location.lng();

    if (initial_position_set) distance += gps.distanceBetween(cur_lat, cur_lng, lat, lng);
    else initial_position_set = true;

    cur_lat = lat;
    cur_lng = lng;
}

void GPSTracker::display_banner() {
    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 12;
    int cardY = BORDER_PAD_Y + 22;
    int cardW = tftWidth - 2 * cardX;
    int cardH = tftHeight - cardY - (compact ? 16 : 20);
    uint16_t surf = uiSurfaceColor();
    int x = cardX + 8;
    int w = cardW - 16;
    int y = cardY + (compact ? 7 : 10);
    int rowH = compact ? 12 : 16;

    drawMainBorderWithTitle("GPS Tracker");
    uiDrawCard(cardX, cardY, cardW, cardH, true);

    if (gpsCoordCount > 0) {
        String fname = (filename.length() > 4) ? filename.substring(0, filename.length() - 4) : filename;
        uiDrawKeyValueRow(x, y, w, "File", fname, uiTextBrightColor(), surf);
        y += rowH;
        char distBuf[20];
        snprintf(distBuf, sizeof(distBuf), "%.2f km", distance / 1000.0);
        uiDrawKeyValueRow(x, y, w, "Points", String(gpsCoordCount) + "  " + String(distBuf), bruceConfig.priColor, surf);
        y += rowH + 2;
        tft.drawFastHLine(x, y - 2, w, uiBorderDimColor());
        y += 2;
    }

    tft.setTextColor(uiTextBrightColor(), surf);
    tft.setTextSize(FP);
    tft.setCursor(x, y);
    printCenterFootnote("ESC: Stop & Save");
}

void GPSTracker::dump_gps_data() {
    bool compact = (tftHeight < 160);
    int cardX = compact ? 8 : 12;
    int cardW = tftWidth - 2 * cardX;
    int x = cardX + 8;
    int w = cardW - 16;
    int y = tft.getCursorY();
    int rowH = compact ? 12 : 16;
    uint16_t surf = uiSurfaceColor();

    if (!date_time_updated && (!gps.date.isUpdated() || !gps.time.isUpdated())) {
        tft.setTextColor(uiTextMutedColor(), surf);
        tft.drawString("Waiting for valid GPS fix...", x, y, 1);
        return;
    }
    date_time_updated = true;

    char dtBuf[24];
    snprintf(
        dtBuf,
        sizeof(dtBuf),
        "%04d-%02d-%02d %02d:%02d:%02d",
        gps.date.year(),
        gps.date.month(),
        gps.date.day(),
        gps.time.hour(),
        gps.time.minute(),
        gps.time.second()
    );
    uiDrawKeyValueRow(x, y, w, "UTC", String(dtBuf), uiTextBrightColor(), surf);
    y += rowH;

    char satBuf[24];
    snprintf(satBuf, sizeof(satBuf), "%u sats  HDOP %.2f", (unsigned)gps.satellites.value(), gps.hdop.hdop());
    uiDrawKeyValueRow(x, y, w, "Signal", String(satBuf), bruceConfig.secColor, surf);
}

void GPSTracker::create_filename() {
    char timestamp[20];
    sprintf(
        timestamp,
        "%02d%02d%02d_%02d%02d%02d",
        gps.date.year() % 100,
        gps.date.month() % 100,
        gps.date.day() % 100,
        gps.time.hour() % 100,
        gps.time.minute() % 100,
        gps.time.second() % 100
    );
    filename = String(timestamp) + "_gps_tracker.gpx";
}

void GPSTracker::add_initial_file_data(File file) {
    file.println("<?xml version=\"1.0\" encoding=\"ISO-8859-1\" standalone=\"yes\"?>");
    file.println("<?xml-stylesheet type=\"text/xsl\" href=\"details.xsl\"?>");
    file.println("<gpx");
    file.println("  version=\"1.1\"");
    file.println("  creator=\"Nezha Cyber Firmware\"");
    file.println("  xmlns=\"http://www.topografix.com/GPX/1/1\"");
    file.println("  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"");
    file.println(
        "  xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\""
    );
    file.println(">");
    file.println("  <metadata>");
    file.println("    <name>Nezha GPS Tracker</name>");
    file.println("    <desc>GPS Tracker using Nezha Cyber Firmware</desc>");
    file.println("    <link href=\"https://github.com/NezhaCyber/nezha_cyber_firmware\">");
    file.println("      <text>Nezha Cyber</text>");
    file.println("    </link>");
    file.println("  </metadata>");
    file.println("  <trk>");
    file.println("    <name>Nezha Route</name>");
    file.println("    <desc>GPS route captured by Nezha Cyber firmware</desc>");
    file.println("    <trkseg>");
}

void GPSTracker::add_final_file_data() {
    FS *fs;
    if (!getFsStorage(fs)) return;
    if (filename == "" || !(*fs).exists("/BruceGPS/" + filename)) return;

    File file = (*fs).open("/BruceGPS/" + filename, FILE_APPEND);

    if (!file) return;
    file.println("    </trkseg>");
    file.println("  </trk>");
    file.println("</gpx>");

    file.close();
}

void GPSTracker::add_coord() {
    FS *fs;
    if (!getFsStorage(fs)) {
        padprintln("Storage setup error");
        returnToMenu = true;
        return;
    }

    if (filename == "") create_filename();

    if (!(*fs).exists("/BruceGPS")) (*fs).mkdir("/BruceGPS");

    bool is_new_file = false;
    if (!(*fs).exists("/BruceGPS/" + filename)) is_new_file = true;
    File file = (*fs).open("/BruceGPS/" + filename, is_new_file ? FILE_WRITE : FILE_APPEND);

    if (!file) {
        padprintln("Failed to open file for writing");
        returnToMenu = true;
        return;
    }

    if (is_new_file) add_initial_file_data(file);

    file.printf("      <trkpt lat=\"%f\" lon=\"%f\">\n", gps.location.lat(), gps.location.lng());
    file.println("        <sym>Waypoint</sym>");
    file.printf("        <ele>%f</ele>\n", gps.altitude.meters());
    file.printf("        <hdop>%f</hdop>\n", gps.hdop.hdop());
    file.printf("        <sat>%ld</sat>\n", gps.satellites.value());
    file.println("      </trkpt>");

    gpsCoordCount++;

    file.close();

    padprintf(2, "Coord: %.6f, %.6f\n", gps.location.lat(), gps.location.lng());
}

void GPSTracker::releasePins() {
    rxPinReleased = false;
    if (bruceConfigPins.CC1101_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.NRF24_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
#if !defined(LITE_VERSION)
        bruceConfigPins.W5500_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.LoRa_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
#endif
        bruceConfigPins.SDCARD_bus.checkConflict(bruceConfigPins.gps_bus.rx)) {
        // T-Embed CC1101 and T-Display S3 Touch ties this pin to the NRF24 CS; switch it to input so the GPS
        // UART can drive it.
        pinMode(bruceConfigPins.gps_bus.rx, INPUT);
        rxPinReleased = true;
    }
}

void GPSTracker::restorePins() {
    if (rxPinReleased) {
        if (bruceConfigPins.CC1101_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
            bruceConfigPins.NRF24_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
#if !defined(LITE_VERSION)
            bruceConfigPins.W5500_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
            bruceConfigPins.LoRa_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
#endif
            bruceConfigPins.SDCARD_bus.checkConflict(bruceConfigPins.gps_bus.rx)) {
            // Restore the original board state after leaving the GPS app s
            // o the radio/other peripherals behave as expected
            pinMode(bruceConfigPins.gps_bus.rx, OUTPUT);
            if (bruceConfigPins.gps_bus.rx == bruceConfigPins.CC1101_bus.cs ||
                bruceConfigPins.gps_bus.rx == bruceConfigPins.NRF24_bus.cs ||
#if !defined(LITE_VERSION)
                bruceConfigPins.gps_bus.rx == bruceConfigPins.W5500_bus.cs ||
                bruceConfigPins.gps_bus.rx == bruceConfigPins.W5500_bus.cs ||
#endif
                bruceConfigPins.gps_bus.rx == bruceConfigPins.SDCARD_bus.cs) {
                // If it is conflicting to an SPI CS pin, keep it HIGH
                digitalWrite(bruceConfigPins.gps_bus.rx, HIGH);
            } else {
                // If it is conflicting with any other SPI pin, keep it LOW
                // Avoids CC1101 Jamming and nRF24 radio to keep enabled
                digitalWrite(bruceConfigPins.gps_bus.rx, LOW);
            }
        }
        rxPinReleased = false;
    }
}
