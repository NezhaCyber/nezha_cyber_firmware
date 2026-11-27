#ifndef __PLAYBOOKS_H__
#define __PLAYBOOKS_H__

#include <Arduino.h>
#include <FS.h>

// Interactive Workflow Playbooks launcher ("灵珠剧本")
void playbooks_menu();

// Run a JSON playbook string or file directly (also callable from CLI)
bool playbooks_run_json(const String &jsonText);
bool playbooks_run_file(FS &fs, const String &path);

#endif // __PLAYBOOKS_H__
