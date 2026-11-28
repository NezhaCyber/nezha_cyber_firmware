/**
 * @file gps_tracker.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief GPS tracker
 * @version 0.1
 * @date 2024-11-20
 */

#ifndef __GPS_TRACKER_H__
#define __GPS_TRACKER_H__

#include <TinyGPS++.h>
#include <globals.h>

class GPSTracker {
public:
    /*
     * Construction runs the whole tracking session (setup() then loop()), so an
     * instance lives only while the tracker screen is open. The destructor
     * closes the GPX document and releases UART2 when a GPS was found.
     */
    GPSTracker();
    ~GPSTracker();

    /*
     * setup() powers the GPS through the IO expander, brings up the UART and then
     * blocks in loop() until ESC or returnToMenu.
     */
    void setup();
    void loop();

private:
    bool date_time_updated = false;
    bool initial_position_set = false;
    double cur_lat;
    double cur_lng;
    double distance = 0;
    String filename = "";
    TinyGPSPlus gps;
    HardwareSerial GPSserial = HardwareSerial(2);
    int gpsCoordCount = 0;
    bool rxPinReleased = false;

    /*
     * UART2 bring-up and teardown. releasePins()/restorePins() handle boards
     * where the GPS RX line is shared with a radio or SD chip select, and
     * rxPinReleased remembers whether that pin has to be handed back.
     */
    bool begin_gps(void);
    void end(void);
    void releasePins(void);
    void restorePins(void);

    /*
     * Screen rendering: the banner is redrawn on every loop pass, and
     * dump_gps_data() prints the last parsed fix when a location is not updated.
     */
    void display_banner(void);
    void dump_gps_data(void);

    /*
     * Track recording: create_filename() derives a timestamped ".gpx" name from
     * the NMEA date/time, the initial and final helpers write the GPX prologue and
     * epilogue, and add_coord() appends each accepted fix under /BruceGPS.
     */
    void set_position(void);
    void add_coord(void);
    void add_initial_file_data(File file);
    void add_final_file_data(void);
    void create_filename(void);
};

#endif // GPS_TRACKER_H
