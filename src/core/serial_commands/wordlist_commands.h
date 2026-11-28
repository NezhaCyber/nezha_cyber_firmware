#ifndef __SERIAL_WORDLIST_CMD_H__
#define __SERIAL_WORDLIST_CMD_H__

#include <SimpleCLI.h>

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)
void createWordlistCommands(SimpleCLI *cli);
#endif

#endif
