import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import {
  encodeCommandLine,
  getInitialMonitorSignals,
  SerialLineBuffer,
  stripAnsiSequences,
} from "../src/lib/serialTerminal.js";
import {
  buildSohPacket,
  buildYmodemHeaderPayload,
  crc16Ccitt,
  joinDevicePath,
  parseFreeOutput,
  parseListDirectoryOutput,
  quoteCliArg,
  YMODEM_BLOCK_SIZE,
  YMODEM_SOH,
} from "../src/lib/ymodemProtocol.js";
import {
  checkBoardFlashCompatibility,
  mergeGitHubReleasesIntoManifest,
  pickBestCompatibleBoard,
  type GitHubReleaseApiItem,
} from "../src/services/githubReleases.js";
import { SerialMonitorSession } from "../src/services/serialMonitor.js";
import type { DetectedChipInfo, FirmwareManifest } from "../src/types/firmware.js";

test("crc16Ccitt matches firmware CCITT-0x0000 polynomial 0x1021", () => {
  const input = new TextEncoder().encode("123456789");
  // Standard XMODEM / YMODEM CRC-16-CCITT (init=0x0000, poly=0x1021) of "123456789" is 0x31C3
  assert.equal(crc16Ccitt(input), 0x31c3);
  assert.equal(crc16Ccitt(new Uint8Array(128)), 0x0000);
});

test("buildSohPacket constructs valid 133-byte Y-Modem frame with big-endian CRC", () => {
  const payload = buildYmodemHeaderPayload("capture.sub", 4096);
  assert.equal(payload.length, YMODEM_BLOCK_SIZE);

  // Verify null-terminated filename and decimal size format parsed by storage_commands.cc
  const decoded = new TextDecoder().decode(payload);
  assert.ok(decoded.startsWith("capture.sub\x004096\x00"));

  const pkt = buildSohPacket(0, payload);
  assert.equal(pkt.length, 3 + YMODEM_BLOCK_SIZE + 2);
  assert.equal(pkt[0], YMODEM_SOH);
  assert.equal(pkt[1], 0x00);
  assert.equal(pkt[2], 0xff);

  const expectedCrc = crc16Ccitt(payload);
  const embeddedCrc = (pkt[131] << 8) | pkt[132];
  assert.equal(embeddedCrc, expectedCrc);

  // Block wraparound 255 -> 0
  const pkt255 = buildSohPacket(255, payload);
  assert.equal(pkt255[1], 0xff);
  assert.equal(pkt255[2], 0x00);
});

test("parseListDirectoryOutput parses firmware tab-delimited output and ignores noise", () => {
  const sample = [
    "storage list /",
    "[I][Nezha] mounted SD",
    "ir\t<DIR>",
    "subghz\t<DIR>",
    "bruce.conf\t1284",
    "demo.js\t512",
    "invalid line without tab",
  ].join("\r\n");

  const entries = parseListDirectoryOutput(sample, "/");
  assert.equal(entries.length, 4);
  assert.deepEqual(
    entries.map((e) => [e.name, e.isDir, e.size, e.path]),
    [
      ["ir", true, 0, "/ir"],
      ["subghz", true, 0, "/subghz"],
      ["bruce.conf", false, 1284, "/bruce.conf"],
      ["demo.js", false, 512, "/demo.js"],
    ],
  );
});

test("parseFreeOutput handles mounted SD, missing SD, and LittleFS", () => {
  const sdOk = parseFreeOutput(
    "SD Total space: 31914983424 Bytes\nSD Used space: 1048576 Bytes\nSD Free space: 31913934848 Bytes\n",
    "SD",
  );
  assert.equal(sdOk.available, true);
  assert.equal(sdOk.totalBytes, 31914983424);
  assert.equal(sdOk.usedBytes, 1048576);

  const sdMissing = parseFreeOutput("No SD card installed\n", "SD");
  assert.equal(sdMissing.available, false);

  const lfsOk = parseFreeOutput(
    "LittleFS Total space: 9961472 Bytes\nLittleFS Used space: 49152 Bytes\nLittleFS Free space: 9912320 Bytes\n",
    "LittleFS",
  );
  assert.equal(lfsOk.available, true);
  assert.equal(lfsOk.freeBytes, 9912320);
});

test("joinDevicePath and quoteCliArg normalize paths safely", () => {
  assert.equal(joinDevicePath("/", "test.ir"), "/test.ir");
  assert.equal(joinDevicePath("/subghz/", "/garage.sub"), "/subghz/garage.sub");
  assert.equal(quoteCliArg("/ir/tv.ir"), "/ir/tv.ir");
  assert.equal(quoteCliArg("/my files/tv remote.ir"), '"/my files/tv remote.ir"');
});

test("public/firmware/manifest.json schema and repository offsets are valid", () => {
  const manifestPath = path.resolve(process.cwd(), "public/firmware/manifest.json");
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8")) as FirmwareManifest;

  assert.ok(manifest.releases.length > 0);
  for (const rel of manifest.releases) {
    const seenIds = new Set<string>();
    for (const build of rel.builds) {
      assert.ok(!seenIds.has(build.id), `Duplicate board id ${build.id} in ${rel.version}`);
      seenIds.add(build.id);

      assert.equal(build.flashModes.merged.parts[0].offset, "0x0");
      if (build.flashModes.partitioned) {
        const offsets = build.flashModes.partitioned.parts.map((p) => p.offset);
        const expectedBoot = build.chipFamily === "esp32" ? "0x1000" : "0x0";
        assert.deepEqual(offsets, [expectedBoot, "0x8000", "0x10000"]);
      }
    }
  }
});

test("4MB device compatibility guard blocks >4MB firmware and selects <4MB M5StickC build", () => {
  const manifestPath = path.resolve(process.cwd(), "public/firmware/manifest.json");
  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8")) as FirmwareManifest;
  const builds = manifest.releases[0].builds;

  const stickc4mb: DetectedChipInfo = {
    chipName: "ESP32-PICO-D4",
    chipDescription: "ESP32-PICO-D4",
    chipFamily: "esp32",
    macAddress: "AA:BB:CC:DD:EE:FF",
    flashSizeMB: 4,
  };

  const core2_16mb = builds.find((b) => b.id === "m5stack-core2")!;
  const cplus2_8mb = builds.find((b) => b.id === "m5stack-cplus2")!;
  const stickc_4mb = builds.find((b) => b.id === "m5stack-cplus1_1")!;
  const stickc_lite = builds.find((b) => b.id === "LAUNCHER_m5stack-cplus1_1")!;

  assert.equal(checkBoardFlashCompatibility(core2_16mb, stickc4mb).flashOverflow, true);
  assert.equal(checkBoardFlashCompatibility(cplus2_8mb, stickc4mb).flashOverflow, true);
  assert.equal(checkBoardFlashCompatibility(stickc_4mb, stickc4mb).compatible, true);
  assert.equal(checkBoardFlashCompatibility(stickc_lite, stickc4mb).compatible, true);

  // Oversized binary (>4MB) attached to a 4MB entry must also be blocked
  const oversized = { ...stickc_4mb, binarySizeBytes: 4 * 1024 * 1024 + 4096 };
  assert.equal(checkBoardFlashCompatibility(oversized, stickc4mb).flashOverflow, true);

  const picked = pickBestCompatibleBoard(builds, stickc4mb);
  assert.equal(picked?.id, "m5stack-cplus1_1");
});

test("mergeGitHubReleasesIntoManifest merges Stable, Beta, and Archive versions with asset sizes", () => {
  const manifestPath = path.resolve(process.cwd(), "public/firmware/manifest.json");
  const base = JSON.parse(fs.readFileSync(manifestPath, "utf8")) as FirmwareManifest;

  const mockReleases: GitHubReleaseApiItem[] = [
    {
      tag_name: "v2.2.0-beta.1",
      prerelease: true,
      draft: false,
      published_at: "2026-11-29T10:00:00Z",
      html_url: "https://github.com/NezhaCyber/nezha_cyber_firmware/releases/tag/v2.2.0-beta.1",
      body: "# Beta preview with new RF tools",
      assets: [
        {
          name: "NezhaCyber-lilygo-t-deck.bin",
          size: 5120000,
          browser_download_url: "https://example.com/v2.2.0-beta.1/NezhaCyber-lilygo-t-deck.bin",
        },
        {
          name: "NezhaCyber-m5stack-cplus1_1.bin",
          size: 3800000,
          browser_download_url: "https://example.com/v2.2.0-beta.1/NezhaCyber-m5stack-cplus1_1.bin",
        },
      ],
    },
    {
      tag_name: "v2.1.0",
      prerelease: false,
      draft: false,
      published_at: "2026-11-28T10:00:00Z",
      html_url: "https://github.com/NezhaCyber/nezha_cyber_firmware/releases/tag/v2.1.0",
      body: "Stable release",
      assets: [
        {
          name: "NezhaCyber-m5stack-cplus1_1.bin",
          size: 3750000,
          browser_download_url: "https://example.com/v2.1.0/NezhaCyber-m5stack-cplus1_1.bin",
        },
      ],
    },
  ];

  const merged = mergeGitHubReleasesIntoManifest(base, mockReleases);
  assert.equal(merged.releases.length, 2);
  assert.equal(merged.releases[0].version, "v2.2.0-beta.1");
  assert.equal(merged.releases[0].channel, "beta");
  assert.equal(merged.releases[0].builds.length, 2);
  assert.equal(merged.releases[0].builds[0].binarySizeBytes, 5120000);
  assert.equal(merged.releases[0].builds[1].binarySizeBytes, 3800000);
  assert.equal(merged.releases[1].channel, "stable");
});

test("SerialLineBuffer strips ANSI/OSC, handles UTF-8, and exposes live '# ' prompt", () => {
  const buf = new SerialLineBuffer();

  assert.equal(stripAnsiSequences("\x1b[32mOK\x1b[0m \x1b]0;title\x07哪吒"), "OK 哪吒");
  assert.equal(encodeCommandLine("info\r\n", "lf"), "info\n");
  assert.equal(encodeCommandLine("info", "crlf"), "info\r\n");
  assert.equal(encodeCommandLine("info", "none"), "info");

  const r1 = buf.feed("\x1b[36mNezha Cyber v2.1\x1b[0m\r\nDevice: 哪吒\r\n# ");
  assert.deepEqual(r1.completedLines, ["Nezha Cyber v2.1", "Device: 哪吒"]);
  assert.equal(r1.pendingLine, "# ");

  const r2 = buf.feed("info\n");
  assert.deepEqual(r2.completedLines, ["# info"]);
  assert.equal(r2.pendingLine, "");

  // Standalone CR progress overwrite
  const r3 = buf.feed("10%\r50%\r100%\n");
  assert.deepEqual(r3.completedLines, ["100%"]);
});

test("getInitialMonitorSignals enables DTR for ESP32-S3 USB-CDC and clears RTS for UART bridges", () => {
  assert.deepEqual(getInitialMonitorSignals(0x303a), {
    dataTerminalReady: true,
    requestToSend: false,
  });
  assert.deepEqual(getInitialMonitorSignals(0x1a86), {
    dataTerminalReady: false,
    requestToSend: false,
  });
  assert.deepEqual(getInitialMonitorSignals(0x0403), {
    dataTerminalReady: false,
    requestToSend: false,
  });
});

test("SerialMonitorSession reopens at target baud, asserts DTR, streams RX, and serializes concurrent TX", async () => {
  const signalsLog: Array<{ dataTerminalReady?: boolean; requestToSend?: boolean }> = [];
  const openBauds: number[] = [];
  const txPayloads: Uint8Array[] = [];
  let rxController: ReadableStreamDefaultController<Uint8Array> | null = null;
  let isOpen = true; // Simulate port left open at 921600 by esptool flasher
  let closeCount = 0;

  const makeStreams = () => ({
    readable: new ReadableStream<Uint8Array>({
      start(ctrl) {
        rxController = ctrl;
      },
    }),
    writable: new WritableStream<Uint8Array>({
      write(chunk) {
        txPayloads.push(new Uint8Array(chunk));
      },
    }),
  });

  let streams = makeStreams();
  const mockPort = {
    get readable() {
      return isOpen ? streams.readable : null;
    },
    get writable() {
      return isOpen ? streams.writable : null;
    },
    getInfo() {
      return { usbVendorId: 0x303a, usbProductId: 0x1001 };
    },
    async open(opts: { baudRate: number }) {
      openBauds.push(opts.baudRate);
      isOpen = true;
      streams = makeStreams();
    },
    async close() {
      closeCount++;
      isOpen = false;
      rxController = null;
    },
    async setSignals(sig: { dataTerminalReady?: boolean; requestToSend?: boolean }) {
      signalsLog.push(sig);
    },
  } as unknown as SerialPort;

  const session = new SerialMonitorSession();
  const rxChunks: string[] = [];
  let totalRxBytes = 0;
  let totalTxBytes = 0;
  const statuses: boolean[] = [];
  const errors: string[] = [];

  await session.open(mockPort, 115200, {
    onChunk: (txt, n) => {
      rxChunks.push(txt);
      totalRxBytes += n;
    },
    onTxBytes: (n) => {
      totalTxBytes += n;
    },
    onStatus: (ok) => statuses.push(ok),
    onError: (e) => errors.push(e),
  });

  // Must have closed stale open port and reopened at 115200, with ESP32-S3 DTR=true, RTS=false
  assert.equal(closeCount, 1);
  assert.deepEqual(openBauds, [115200]);
  assert.deepEqual(signalsLog[0], { dataTerminalReady: true, requestToSend: false });
  assert.deepEqual(statuses, [true]);

  // Simulate device sending live output + prompt
  const enc = new TextEncoder();
  rxController!.enqueue(enc.encode("Nezha Cyber v2.1\r\n# "));
  await new Promise((r) => setTimeout(r, 30));
  assert.equal(rxChunks.join(""), "Nezha Cyber v2.1\r\n# ");
  assert.equal(totalRxBytes, enc.encode("Nezha Cyber v2.1\r\n# ").byteLength);

  // Concurrent writes MUST serialize cleanly without throwing "WritableStream is locked"
  await Promise.all([
    session.sendCommand("info", "lf"),
    session.sendCommand("sysmon", "lf"),
    session.sendCtrlC(),
  ]);
  const dec = new TextDecoder();
  assert.deepEqual(
    txPayloads.map((b) => dec.decode(b)),
    ["info\n", "sysmon\n", "\x03"],
  );
  assert.equal(totalTxBytes, 5 + 7 + 1);

  // Pulse reset restores ESP32-S3 DTR=true, RTS=false after RTS toggle
  await session.pulseReset();
  assert.deepEqual(signalsLog.slice(-2), [
    { dataTerminalReady: false, requestToSend: true },
    { dataTerminalReady: true, requestToSend: false },
  ]);

  // Clean close releases reader lock and closes port without errors
  await session.close();
  assert.equal(session.isOpen, false);
  assert.equal(errors.length, 0);
});
