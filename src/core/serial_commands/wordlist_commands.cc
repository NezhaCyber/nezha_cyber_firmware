#include "wordlist_commands.h"

#if !defined(LITE_VERSION) && !defined(BRUCE_COMPACT_CJK_FONT)

#include "modules/others/wordlist_gen.h"
#include <globals.h>

uint32_t wordlistGenCallback(cmd *c) {
    Command cmd(c);
    Argument nameArg = cmd.getArgument("name");
    Argument modeArg = cmd.getArgument("mode");
    Argument specArg = cmd.getArgument("spec");

    String name = nameArg.getValue();
    String mode = modeArg.getValue();
    String spec = specArg.getValue();
    name.trim();
    mode.trim();
    spec.trim();

    if (!name.length() || !mode.length() || !spec.length()) {
        serialDevice->println("Usage: wordlist gen <name> <mode> <spec>");
        return false;
    }

    WordlistGenOptions opts;
    opts.name = name.c_str();
    opts.mode = mode.c_str();
    opts.spec = spec.c_str();

    // Dry-run first: refuse to silently replace an existing dictionary, and surface plan errors
    // before any byte is written.
    WordlistGenStats probe;
    WordlistGenOptions probeOpts = opts;
    probeOpts.stats = &probe;
    probeOpts.dryRun = true;
    if (!wordlist_gen_generate_ex(probeOpts)) {
        serialDevice->println(probe.error ? probe.error : "Generation failed");
        return false;
    }
    if (probe.existed) {
        serialDevice->println("File exists (choose a new name)");
        return false;
    }

    WordlistGenStats stats;
    opts.stats = &stats;
    const bool ok = wordlist_gen_generate_ex(opts);
    if (!ok) {
        serialDevice->println(stats.error ? stats.error : "Generation failed");
        return false;
    }
    serialDevice->printf(
        "Saved %s (%u words, %u bytes, %s)\n",
        stats.path,
        (unsigned)stats.written,
        (unsigned)stats.bytes,
        stats.onSd ? "SD" : "LittleFS"
    );
    return true;
}

void createWordlistCommands(SimpleCLI *cli) {
    Command wordlistCmd = cli->addCompositeCmd("wordlist");
    Command genCmd = wordlistCmd.addCommand("gen", wordlistGenCallback);
    genCmd.addPosArg("name");
    genCmd.addPosArg("mode");
    genCmd.addPosArg("spec");
}

#endif
