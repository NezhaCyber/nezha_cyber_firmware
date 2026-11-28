/*
 * Wi-Fi handshake password recovery API. Exposes wifi_recover_menu(), the
 * screen entry point that lets the user pick a wordlist and a .pcap capture
 * and then starts the cracker. Declared extern "C" and compiled out of
 * LITE_VERSION builds along with its implementation.
 */
#ifndef WIFI_RECOVER_H
#define WIFI_RECOVER_H
#ifndef LITE_VERSION
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called from WifiMenu::optionsMenu()
void wifi_recover_menu();

#ifdef __cplusplus
}
#endif
#endif
#endif // WIFI_RECOVER_H
