/**
 * @file startup_app.h
 * @author Rennan Cockles (https://github.com/rennancockles)
 * @brief Bruce startup apps
 * @version 0.1
 * @date 2024-11-20
 */

#ifndef __STARTUP_APP_H__
#define __STARTUP_APP_H__

#include <Arduino.h>
#include <map>
#include <vector>

class StartupApp {
public:
    /*
     * Builds the startup-application registry.
     * Maps app names to the launchers invoked when a startup app is configured.
     */
    StartupApp();

    /*
     * Startup-app dispatch.
     * startApp() runs the registered launcher by name; getAppNames() lists the available entries.
     */
    bool startApp(const String &appName) const;
    std::vector<String> getAppNames() const;

private:
    std::map<String, std::function<void()>> _startupApps = {};
};

#endif
