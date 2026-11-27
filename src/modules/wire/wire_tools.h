#ifndef __WIRE_TOOLS_H__
#define __WIRE_TOOLS_H__
#if !defined(LITE_VERSION)

#include "core/bus_HAL.h"
#include <Arduino.h>
#include <vector>

// Interactive bus tools, shared by the Wire Tools menu and the serial command interface.

// UART terminal: baudrate presets/custom, remappable pins, live send/receive view, USB bridge
void uart_terminal_setup();
void uart_usb_bridge();
// I2C: device scan on sys_i2c and i2c_bus, register read/write/dump on the bus the device is on
void i2c_bus_setup();
// SPI: JEDEC flash detection and raw transaction console on bruceConfigPins.outer_bus
void spi_bus_setup();
// JTAG: bitbang IDCODE reader (TAP reset + DR shift) with user-assigned TCK/TMS/TDI/TDO
void jtag_scan_setup();
// Logic analyzer (8/16 MB boards): RMT edge capture on 1-4 GPIOs, waveform viewer,
// UART/I2C/SPI/1-Wire decoders, CSV/VCD export, and SUMP/OLS ("PulseView") slave mode.
#if !defined(BRUCE_COMPACT_CJK_FONT)
void logic_analyzer_setup();
void sump_server_run(uint32_t baud, bool usb = false);
void sump_menu();
// Native ESP32/ESP32-S3 TWAI CAN Bus & OBD-II pocket sniffer
void can_bus_setup();
#endif
// Multimeter: continuity tests only (pin-to-pin and pin-to-GND) with beeper feedback
void continuity_tester_setup();

// Non-UI primitives used by the serial commands (core/serial_commands/wire_commands.cc)

bool wire_uart_begin(uint32_t baud, int8_t rx, int8_t tx);
void wire_uart_end();
int wire_uart_send(const String &text);
int wire_uart_read(String &out, uint32_t timeoutMs);
String wire_uart_status();
uint32_t wire_uart_autobaud(int8_t rx = -1, uint32_t timeoutMs = 400);

struct WireI2cDevice {
    I2CBusId bus;
    uint8_t addr;
};
// sda/scl >= 0: scan only the bus on those pins; otherwise every configured bus (sys_i2c, i2c_bus).
// Returns the device count, or -1 if no bus could be opened.
int wire_i2c_scan(std::vector<WireI2cDevice> &found, int8_t sda, int8_t scl);
// sda/scl >= 0 select the bus by pins; otherwise `bus` (I2CBusId::Sys or I2CBusId::User).
String wire_i2c_read_reg(uint8_t addr, uint8_t reg, I2CBusId bus, int8_t sda, int8_t scl);
String wire_i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value, I2CBusId bus, int8_t sda, int8_t scl);
String wire_i2c_dump_eeprom(uint8_t addr, uint32_t sizeBytes, bool addr16, I2CBusId bus);

bool wire_spi_begin(int8_t sck, int8_t miso, int8_t mosi, int8_t cs, uint32_t freqHz);
void wire_spi_end();
String wire_spi_transfer(const String &hexBytes); // returns response as hex string
String wire_spi_flash_detect();
String wire_spi_dump_flash(uint32_t sizeBytes, String &outPath);

// Returns "OK <idcode_hex>" or an error description; -1 pins keep the current assignment
String wire_jtag_read_idcode(int8_t tck, int8_t tms, int8_t tdi, int8_t tdo);

#if !defined(BRUCE_COMPACT_CJK_FONT)
// Logic analyzer (RMT edge capture). Resolutions: 100k/200k/500k/1M/2M/5M/10M/20M ticks per second,
// depth = symbol buffer per channel (1024/2048/4096). Pins < 0 reuse the Logic Analyzer menu's
// session pins. Returns "OK ..." or "ERR: ..." (captures into the session shared with the UI).
String wire_la_capture(uint32_t resHz, uint32_t depth, int8_t p1, int8_t p2, int8_t p3, int8_t p4);
// Streams the edges of the last capture as CSV lines "ch,time_us,level" to the serial device
void wire_la_dump();
// "res=<hz> depth=<symbols> ch=<n> pins=<p1>,... last=<edges per channel>"
String wire_la_status();

// Number of probes configured in the Logic Analyzer session (1-4, 0 = unconfigured)
uint8_t wire_la_channels();
// Session resolution, for display only
uint32_t wire_la_resolution();
// Single-shot RMT capture on the session pins with the given resolution, then streams
// nSamples bytes over `out` (probe c -> bit c), sampling at `rate` Hz.
bool wire_la_sump_stream(
    uint32_t resolutionHz, double rate, uint32_t nSamples, uint16_t trigMask, uint16_t trigVals,
    Print &out
);
#endif

// Continuity one-shot: pinB >= 0 tests A<->B (A driven LOW, B pulled up); pinB < 0 tests A to GND.
// Returns "OK SHORT ..." or "OK OPEN", or "ERR: ..."
String wire_cont_test(int8_t pinA, int8_t pinB);

#endif
#endif
