import type { DeviceFileEntry, StorageVolumeStats } from "../types/firmware.js";

// Y-Modem constants matching src/core/serial_commands/storage_commands.cc
export const YMODEM_SOH = 0x01;
export const YMODEM_EOT = 0x04;
export const YMODEM_ACK = 0x06;
export const YMODEM_NAK = 0x15;
export const YMODEM_CAN = 0x18;
export const YMODEM_CRC = 0x43; // 'C'
export const YMODEM_PAD = 0x1a;
export const YMODEM_BLOCK_SIZE = 128;

/**
 * CRC-16-CCITT (initial 0x0000, polynomial 0x1021) matching crc16() in
 * src/core/serial_commands/storage_commands.cc.
 */
export function crc16Ccitt(data: Uint8Array): number {
  let crc = 0x0000;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i] << 8;
    for (let bit = 0; bit < 8; bit++) {
      if ((crc & 0x8000) !== 0) {
        crc = ((crc << 1) ^ 0x1021) & 0xffff;
      } else {
        crc = (crc << 1) & 0xffff;
      }
    }
  }
  return crc;
}

export function buildSohPacket(blockNumber: number, payload128: Uint8Array): Uint8Array {
  if (payload128.length !== YMODEM_BLOCK_SIZE) {
    throw new Error(`Y-Modem SOH payload must be exactly ${YMODEM_BLOCK_SIZE} bytes`);
  }
  const pkt = new Uint8Array(3 + YMODEM_BLOCK_SIZE + 2);
  const seq = blockNumber & 0xff;
  pkt[0] = YMODEM_SOH;
  pkt[1] = seq;
  pkt[2] = (0xff - seq) & 0xff;
  pkt.set(payload128, 3);
  const crc = crc16Ccitt(payload128);
  pkt[3 + YMODEM_BLOCK_SIZE] = (crc >> 8) & 0xff;
  pkt[3 + YMODEM_BLOCK_SIZE + 1] = crc & 0xff;
  return pkt;
}

export function buildYmodemHeaderPayload(fileName: string, totalBytes: number): Uint8Array {
  const encoder = new TextEncoder();
  const payload = new Uint8Array(YMODEM_BLOCK_SIZE);
  const safeName = fileName.slice(0, 96);
  const nameBytes = encoder.encode(safeName);
  const sizeBytes = encoder.encode(String(Math.max(0, Math.floor(totalBytes))));
  payload.set(nameBytes, 0);
  payload[nameBytes.length] = 0x00;
  payload.set(sizeBytes, nameBytes.length + 1);
  payload[nameBytes.length + 1 + sizeBytes.length] = 0x00;
  return payload;
}

export function joinDevicePath(dir: string, name: string): string {
  const cleanDir = dir.endsWith("/") ? dir.slice(0, -1) : dir;
  const cleanName = name.startsWith("/") ? name.slice(1) : name;
  return `${cleanDir || ""}/${cleanName}`.replace(/\/+/g, "/");
}

export function quoteCliArg(path: string): string {
  if (/[\s"']/.test(path)) {
    return `"${path.replace(/"/g, '\\"')}"`;
  }
  return path;
}

export function parseFreeOutput(output: string, label: "SD" | "LittleFS"): StorageVolumeStats {
  const empty: StorageVolumeStats = {
    available: false,
    totalBytes: 0,
    usedBytes: 0,
    freeBytes: 0,
  };
  if (output.includes("No SD card installed")) return empty;

  const totalMatch = output.match(new RegExp(`${label} Total space:\\s*(\\d+)`, "i"));
  const usedMatch = output.match(new RegExp(`${label} Used space:\\s*(\\d+)`, "i"));
  const freeMatch = output.match(new RegExp(`${label} Free space:\\s*(\\d+)`, "i"));

  if (!totalMatch) return empty;
  const totalBytes = Number(totalMatch[1]) || 0;
  const usedBytes = Number(usedMatch?.[1] ?? 0) || 0;
  const freeBytes = Number(freeMatch?.[1] ?? Math.max(0, totalBytes - usedBytes)) || 0;
  return {
    available: totalBytes > 0,
    totalBytes,
    usedBytes,
    freeBytes,
  };
}

export function parseListDirectoryOutput(raw: string, dirPath: string): DeviceFileEntry[] {
  const normalized = dirPath.startsWith("/") ? dirPath : `/${dirPath}`;
  const entries: DeviceFileEntry[] = [];
  const seen = new Set<string>();

  for (const rawLine of raw.replace(/\r/g, "").split("\n")) {
    const line = rawLine.trim();
    if (!line || !line.includes("\t")) continue;
    if (line.startsWith("storage ") || line.startsWith("[")) continue;

    const tabIdx = line.indexOf("\t");
    const name = line.slice(0, tabIdx).trim();
    const meta = line.slice(tabIdx + 1).trim();
    if (!name || name === "." || name === "..") continue;

    const isDir = meta === "<DIR>";
    const size = isDir ? 0 : Number.parseInt(meta, 10);
    if (!isDir && Number.isNaN(size)) continue;

    const fullPath = joinDevicePath(normalized, name);
    if (seen.has(fullPath)) continue;
    seen.add(fullPath);

    entries.push({ name, path: fullPath, isDir, size });
  }

  entries.sort((a, b) => {
    if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
    return a.name.localeCompare(b.name, undefined, { sensitivity: "base" });
  });

  return entries;
}
