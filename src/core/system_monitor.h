#ifndef __SYSTEM_MONITOR_H__
#define __SYSTEM_MONITOR_H__

#include <Arduino.h>

// Interactive live Memory & Health HUD ("乾坤仪")
void showSystemMonitor();

// Compact single-line JSON health telemetry for `sysmon` CLI & WebSerial Companion
String systemMonitorJson();

#endif // __SYSTEM_MONITOR_H__
