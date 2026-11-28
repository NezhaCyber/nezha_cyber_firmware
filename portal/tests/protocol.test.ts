import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
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
