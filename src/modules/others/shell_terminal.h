#ifndef __SHELL_TERMINAL_H__
#define __SHELL_TERMINAL_H__

#if !defined(LITE_VERSION)

#include <FS.h>

void shell_terminal_setup();
void shell_run_script_file(FS &fs, const String &filepath);

#endif // !LITE_VERSION

#endif // __SHELL_TERMINAL_H__
