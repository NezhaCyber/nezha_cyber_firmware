#include "playbooks.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/serial_commands/cli.h"
#include "core/settings.h"
#include "core/utils.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <globals.h>

struct BuiltinPlaybook {
    const char *name;
    const char *json;
};

static const BuiltinPlaybook kBuiltinPlaybooks[] = {
    {
        "Health Snapshot",
        "{\"name\":\"Health Snapshot\",\"repeat\":1,\"steps\":["
        "{\"cmd\":\"sysmon\",\"delay_ms\":200},"
        "{\"cmd\":\"free\",\"delay_ms\":200},"
        "{\"cmd\":\"uptime\",\"delay_ms\":200}]}"
    },
    {
        "I2C Bus Audit",
        "{\"name\":\"I2C Bus Audit\",\"repeat\":1,\"steps\":["
        "{\"cmd\":\"wire i2c scan\",\"delay_ms\":300},"
        "{\"cmd\":\"sysmon\",\"delay_ms\":200}]}"
    },
    {
        "Storage & Info Check",
        "{\"name\":\"Storage & Info\",\"repeat\":1,\"steps\":["
        "{\"cmd\":\"info\",\"delay_ms\":200},"
        "{\"cmd\":\"storage list /\",\"delay_ms\":300}]}"
    },
};

bool playbooks_run_json(const String &jsonText) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonText);
    if (err) {
        displayError(String("JSON err: ") + err.c_str(), true);
        return false;
    }

    const char *pbName = doc["name"] | "Playbook";
    int repeat = doc["repeat"] | 1;
    if (repeat < 1) repeat = 1;
    if (repeat > 50) repeat = 50;

    JsonArray steps = doc["steps"].as<JsonArray>();
    if (steps.isNull() || steps.size() == 0) {
        displayError("Playbook has no steps", true);
        return false;
    }

    drawMainBorderWithTitle(pbName);
    tft.setTextSize(FP);
    const int x = BORDER_PAD_X;
    const int y0 = BORDER_PAD_Y + LH + 6;
    const int w = tftWidth - 2 * BORDER_PAD_X;

    bool aborted = false;
    size_t totalSteps = steps.size();

    for (int r = 0; r < repeat && !aborted; ++r) {
        size_t idx = 0;
        for (JsonObject step : steps) {
            if (check(EscPress)) {
                aborted = true;
                break;
            }
            const char *cmdStr = step["cmd"] | "";
            uint32_t delayMs = step["delay_ms"] | 150;
            if (delayMs > 30000) delayMs = 30000;

            idx++;
            tft.fillRect(x, y0, w, LH * 4 + 12, bruceConfig.bgColor);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            char hdr[48];
            snprintf(hdr, sizeof(hdr), "Run %d/%d  Step %u/%u", r + 1, repeat, (unsigned)idx, (unsigned)totalSteps);
            tft.drawString(hdr, x, y0);
            tft.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
            tft.drawString(String("> ") + cmdStr, x, y0 + LH + 4);
            tft.setTextColor(uiTextMutedColor(), bruceConfig.bgColor);
            tft.drawString("ESC to abort", x, y0 + (LH + 4) * 2);

            if (cmdStr[0] != '\0') { serialCli.parse(String(cmdStr)); }

            uint32_t t0 = millis();
            while (millis() - t0 < delayMs) {
                if (check(EscPress)) {
                    aborted = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
    }

    if (aborted) {
        displayWarning("Playbook aborted", true);
        return false;
    }
    displaySuccess(String(pbName) + " done", true);
    return true;
}

bool playbooks_run_file(FS &fs, const String &path) {
    File f = fs.open(path, FILE_READ);
    if (!f) {
        displayError("Cannot open playbook", true);
        return false;
    }
    if (f.size() == 0 || f.size() > 8192) {
        f.close();
        displayError("Playbook too large (>8KB)", true);
        return false;
    }
    String json = f.readString();
    f.close();
    return playbooks_run_json(json);
}

static void save_example_playbook() {
    FS *fs = nullptr;
    if (setupSdCard()) fs = &SD;
    else if (setupLittleFS()) fs = &LittleFS;
    if (!fs) {
        displayError("No storage available", true);
        return;
    }
    if (!fs->exists("/NezhaPlaybooks")) fs->mkdir("/NezhaPlaybooks");
    File f = fs->open("/NezhaPlaybooks/example_recon.json", FILE_WRITE);
    if (!f) {
        displayError("Failed creating example", true);
        return;
    }
    f.println("{");
    f.println("  \"name\": \"Example Recon\",");
    f.println("  \"repeat\": 1,");
    f.println("  \"steps\": [");
    f.println("    { \"cmd\": \"sysmon\", \"delay_ms\": 250 },");
    f.println("    { \"cmd\": \"wire i2c scan\", \"delay_ms\": 500 },");
    f.println("    { \"cmd\": \"info\", \"delay_ms\": 250 }");
    f.println("  ]");
    f.println("}");
    f.flush();
    f.close();
    displaySuccess("Saved /NezhaPlaybooks/example_recon.json", true);
}

static void collectPlaybooksFromFs(FS &fs, const char *tag, std::vector<Option> &out) {
    if (!fs.exists("/NezhaPlaybooks")) return;
    File dir = fs.open("/NezhaPlaybooks");
    if (!dir || !dir.isDirectory()) return;

    int count = 0;
    File entry = dir.openNextFile();
    while (entry && count < 24) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            if (name.endsWith(".json")) {
                String fullPath = name.startsWith("/") ? name : (String("/NezhaPlaybooks/") + name);
                int slash = fullPath.lastIndexOf('/');
                String label = String("[") + tag + "] " + (slash >= 0 ? fullPath.substring(slash + 1) : fullPath);
                out.push_back({label, [&fs, fullPath]() { playbooks_run_file(fs, fullPath); }});
                count++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
}

void playbooks_menu() {
    returnToMenu = false;
    while (!returnToMenu) {
        std::vector<Option> localOpts;
        if (setupSdCard()) collectPlaybooksFromFs(SD, "SD", localOpts);
        if (setupLittleFS()) collectPlaybooksFromFs(LittleFS, "FS", localOpts);

        for (size_t i = 0; i < sizeof(kBuiltinPlaybooks) / sizeof(kBuiltinPlaybooks[0]); ++i) {
            localOpts.push_back(
                {String("* ") + kBuiltinPlaybooks[i].name,
                 [i]() { playbooks_run_json(String(kBuiltinPlaybooks[i].json)); }}
            );
        }
        localOpts.push_back({"Create Sample JSON", save_example_playbook});
        localOpts.push_back({"Back", []() { returnToMenu = true; }});

        loopOptions(localOpts, MENU_TYPE_SUBMENU, "Playbooks");
    }
    returnToMenu = false;
}
