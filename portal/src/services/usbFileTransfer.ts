import {
  buildSohPacket,
  buildYmodemHeaderPayload,
  joinDevicePath,
  parseFreeOutput,
  parseListDirectoryOutput,
  quoteCliArg,
  YMODEM_ACK,
  YMODEM_BLOCK_SIZE,
  YMODEM_CAN,
  YMODEM_CRC,
  YMODEM_EOT,
  YMODEM_NAK,
  YMODEM_PAD,
} from "@/lib/ymodemProtocol";
import type {
  DeviceFileEntry,
  DeviceStorageInfo,
  UsbTransferProgress,
} from "@/types/firmware";

/**
 * Direct USB Serial File Transfer Client for Nezha Cyber Firmware.
 * Speaks the firmware's native SimpleCLI `storage` commands & 128-byte Y-Modem
 * receiver at 115200 bps — zero Wi-Fi or SD card removal required.
 */
export class UsbFileTransferSession {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private encoder = new TextEncoder();
  private decoder = new TextDecoder("utf-8", { fatal: false });
  private rxQueue: number[] = [];
  private rxWaiters: Array<() => void> = [];
  private pumpActive = false;
  private cancelled = false;
  private onLog?: (line: string, source?: "system" | "device" | "tx" | "error") => void;

  get isConnected(): boolean {
    return this.port !== null && this.pumpActive;
  }

  async open(
    port: SerialPort,
    baudRate = 115200,
    onLog?: (line: string, source?: "system" | "device" | "tx" | "error") => void,
  ): Promise<void> {
    await this.close();
    this.onLog = onLog;
    this.port = port;
    this.rxQueue = [];
    this.cancelled = false;

    if (!port.readable && !port.writable) {
      await port.open({ baudRate });
    }

    if (!port.readable || !port.writable) {
      throw new Error("USB serial port streams are unavailable.");
    }

    this.writer = port.writable.getWriter();
    this.pumpActive = true;
    void this.startReadPump();
    this.onLog?.(
      `[usb-files] Connected to Nezha Cyber USB storage channel @ ${baudRate} bps.`,
      "system",
    );
  }

  private async startReadPump(): Promise<void> {
    if (!this.port?.readable) return;
    while (this.pumpActive && this.port?.readable) {
      try {
        this.reader = this.port.readable.getReader();
        while (this.pumpActive) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value && value.length > 0) {
            for (let i = 0; i < value.length; i++) {
              this.rxQueue.push(value[i]);
            }
            const waiters = this.rxWaiters.splice(0, this.rxWaiters.length);
            for (const wake of waiters) wake();
          }
        }
      } catch {
        break;
      } finally {
        try {
          this.reader?.releaseLock();
        } catch {
          // Ignore already released lock
        }
        this.reader = null;
      }
    }
    this.pumpActive = false;
    const waiters = this.rxWaiters.splice(0, this.rxWaiters.length);
    for (const wake of waiters) wake();
  }

  private drainQueue(): Uint8Array {
    if (this.rxQueue.length === 0) return new Uint8Array(0);
    const out = new Uint8Array(this.rxQueue);
    this.rxQueue.length = 0;
    return out;
  }

  private waitForData(timeoutMs: number): Promise<boolean> {
    if (this.rxQueue.length > 0) return Promise.resolve(true);
    if (!this.pumpActive) return Promise.resolve(false);
    return new Promise((resolve) => {
      let settled = false;
      const timer = setTimeout(() => {
        if (!settled) {
          settled = true;
          resolve(false);
        }
      }, timeoutMs);
      this.rxWaiters.push(() => {
        if (!settled) {
          settled = true;
          clearTimeout(timer);
          resolve(this.rxQueue.length > 0);
        }
      });
    });
  }

  private async readByte(timeoutMs: number): Promise<number | null> {
    const deadline = performance.now() + timeoutMs;
    while (this.rxQueue.length === 0) {
      const remaining = deadline - performance.now();
      if (remaining <= 0 || !this.pumpActive) return null;
      await this.waitForData(Math.min(remaining, 250));
    }
    return this.rxQueue.shift() ?? null;
  }

  private async writeRaw(bytes: Uint8Array): Promise<void> {
    if (!this.writer) throw new Error("USB serial writer is not connected.");
    await this.writer.write(bytes);
  }

  private async sendLine(cmd: string): Promise<void> {
    this.onLog?.(`> ${cmd}`, "tx");
    await this.writeRaw(this.encoder.encode(`${cmd}\n`));
  }

  async runCliCommand(cmd: string, idleWindowMs = 380, maxWaitMs = 4500): Promise<string> {
    this.drainQueue();
    await this.sendLine(cmd);

    const collected: number[] = [];
    const start = performance.now();
    let lastRx = start;

    while (performance.now() - start < maxWaitMs) {
      const hasMore = await this.waitForData(idleWindowMs);
      if (this.rxQueue.length > 0) {
        while (this.rxQueue.length > 0) {
          collected.push(this.rxQueue.shift()!);
        }
        lastRx = performance.now();
      } else if (!hasMore) {
        if (collected.length > 0 || performance.now() - lastRx >= idleWindowMs) {
          break;
        }
      }
    }

    const text = this.decoder.decode(new Uint8Array(collected)).replace(/\r/g, "");
    if (text.trim().length > 0) {
      this.onLog?.(text.trimEnd(), "device");
    }
    return text;
  }

  async probeDeviceInfo(): Promise<string> {
    return this.runCliCommand("info", 350, 2500);
  }

  async getStorageInfo(): Promise<DeviceStorageInfo> {
    const sdRaw = await this.runCliCommand("storage free sd", 350, 2500);
    const lfsRaw = await this.runCliCommand("storage free littlefs", 350, 2500);
    return {
      sd: parseFreeOutput(sdRaw, "SD"),
      littlefs: parseFreeOutput(lfsRaw, "LittleFS"),
    };
  }

  async listDirectory(dirPath = "/"): Promise<DeviceFileEntry[]> {
    const normalized = dirPath.startsWith("/") ? dirPath : `/${dirPath}`;
    const raw = await this.runCliCommand(`storage list ${quoteCliArg(normalized)}`, 420, 4500);
    return parseListDirectoryOutput(raw, normalized);
  }

  async createDirectory(dirPath: string): Promise<string> {
    return this.runCliCommand(`storage mkdir ${quoteCliArg(dirPath)}`, 350, 2500);
  }

  async deleteEntry(entry: DeviceFileEntry): Promise<string> {
    const sub = entry.isDir ? "rmdir" : "remove";
    return this.runCliCommand(`storage ${sub} ${quoteCliArg(entry.path)}`, 350, 2500);
  }

  async renameEntry(oldPath: string, newPath: string): Promise<string> {
    return this.runCliCommand(
      `storage rename ${quoteCliArg(oldPath)} ${quoteCliArg(newPath)}`,
      350,
      2500,
    );
  }

  async computeMd5(filePath: string): Promise<string> {
    const out = await this.runCliCommand(`storage md5 ${quoteCliArg(filePath)}`, 500, 5000);
    const match = out.match(/[a-f0-9]{32}/i);
    return match ? match[0].toLowerCase() : out.trim();
  }

  async readSmallFile(filePath: string): Promise<string> {
    return this.runCliCommand(`storage read ${quoteCliArg(filePath)}`, 550, 6000);
  }

  cancelTransfer(): void {
    this.cancelled = true;
  }

  async uploadFileYmodem(
    file: File,
    destinationDir: string,
    onProgress: (p: UsbTransferProgress) => void,
    queueMeta?: { index: number; count: number },
  ): Promise<void> {
    this.cancelled = false;
    const bytes = new Uint8Array(await file.arrayBuffer());
    const remotePath = joinDevicePath(destinationDir, file.name);
    const totalBytes = bytes.byteLength;
    const totalBlocks = Math.max(1, Math.ceil(totalBytes / YMODEM_BLOCK_SIZE));
    const startTime = performance.now();
    const queuePrefix =
      queueMeta && queueMeta.count > 1 ? `[${queueMeta.index}/${queueMeta.count}] ` : "";

    onProgress({
      active: true,
      direction: "upload",
      fileName: `${queuePrefix}${file.name}`,
      transferredBytes: 0,
      totalBytes,
      percent: 0,
      speedBps: 0,
      statusText: `Handshake: ${remotePath}`,
    });

    this.drainQueue();
    await this.sendLine(`storage ymodem ${quoteCliArg(remotePath)}`);

    let bannerText = "";
    let gotCrc = false;
    const handshakeDeadline = performance.now() + 12000;
    let sawPromptLine = false;

    while (performance.now() < handshakeDeadline && !this.cancelled) {
      const b = await this.readByte(500);
      if (b === null) continue;
      if (b === YMODEM_CRC && (sawPromptLine || bannerText.includes("128-byte blocks"))) {
        gotCrc = true;
        break;
      }
      if (b >= 0x09 && b <= 0x7e) {
        bannerText += String.fromCharCode(b);
        if (bannerText.includes("128-byte blocks only")) {
          sawPromptLine = true;
        }
        if (bannerText.includes("Failed to open file") || bannerText.includes("ERROR:")) {
          throw new Error(bannerText.trim());
        }
      }
    }

    if (this.cancelled) {
      await this.writeRaw(new Uint8Array([YMODEM_CAN, YMODEM_CAN]));
      throw new Error("Transfer cancelled by user.");
    }

    if (!gotCrc) {
      throw new Error(
        "Device did not enter Y-Modem receive mode. Make sure Nezha Cyber firmware is booted (not in Download Mode) and an SD card or LittleFS is mounted.",
      );
    }

    // Header Block 0 (filename\0size\0)
    const headerPayload = buildYmodemHeaderPayload(file.name, totalBytes);
    const headerPkt = buildSohPacket(0, headerPayload);
    await this.sendPacketWithAck(headerPkt, 0);

    // Sequential 128-byte SOH data blocks
    for (let blockIdx = 1; blockIdx <= totalBlocks; blockIdx++) {
      if (this.cancelled) {
        await this.writeRaw(new Uint8Array([YMODEM_CAN, YMODEM_CAN]));
        throw new Error("Transfer cancelled by user.");
      }

      const offset = (blockIdx - 1) * YMODEM_BLOCK_SIZE;
      const chunk = bytes.subarray(offset, Math.min(offset + YMODEM_BLOCK_SIZE, totalBytes));
      const payload = new Uint8Array(YMODEM_BLOCK_SIZE);
      payload.fill(YMODEM_PAD);
      payload.set(chunk, 0);

      const pkt = buildSohPacket(blockIdx & 0xff, payload);
      await this.sendPacketWithAck(pkt, blockIdx);

      const transferred = Math.min(blockIdx * YMODEM_BLOCK_SIZE, totalBytes);
      const elapsedSec = Math.max(0.1, (performance.now() - startTime) / 1000);
      onProgress({
        active: true,
        direction: "upload",
        fileName: `${queuePrefix}${file.name}`,
        transferredBytes: transferred,
        totalBytes,
        percent: totalBytes > 0 ? Math.min(100, (transferred / totalBytes) * 100) : 100,
        speedBps: transferred / elapsedSec,
        statusText: `Block ${blockIdx}/${totalBlocks}`,
      });
    }

    // Final EOT + ACK
    await this.writeRaw(new Uint8Array([YMODEM_EOT]));
    const eotDeadline = performance.now() + 6000;
    let eotAcked = false;
    while (performance.now() < eotDeadline) {
      const resp = await this.readByte(500);
      if (resp === YMODEM_ACK) {
        eotAcked = true;
        break;
      }
      if (resp === YMODEM_NAK) {
        await this.writeRaw(new Uint8Array([YMODEM_EOT]));
      }
    }

    if (!eotAcked) {
      throw new Error("Timed out waiting for final EOT acknowledgement.");
    }

    await this.waitForData(350);
    const tailMsg = this.decoder.decode(this.drainQueue()).trim();
    if (tailMsg) {
      this.onLog?.(tailMsg, "device");
    }

    onProgress({
      active: false,
      direction: "upload",
      fileName: `${queuePrefix}${file.name}`,
      transferredBytes: totalBytes,
      totalBytes,
      percent: 100,
      speedBps: totalBytes / Math.max(0.1, (performance.now() - startTime) / 1000),
      statusText: "Complete",
    });
  }

  private async sendPacketWithAck(packet: Uint8Array, blockNum: number): Promise<void> {
    const MAX_RETRIES = 6;
    for (let attempt = 1; attempt <= MAX_RETRIES; attempt++) {
      this.rxQueue = this.rxQueue.filter((b) => b !== YMODEM_CRC);
      await this.writeRaw(packet);

      const deadline = performance.now() + 4500;
      while (performance.now() < deadline) {
        const resp = await this.readByte(500);
        if (resp === null) continue;
        if (resp === YMODEM_ACK) return;
        if (resp === YMODEM_NAK) break;
        if (resp === YMODEM_CAN) {
          throw new Error(`Device cancelled transfer at block ${blockNum}.`);
        }
      }
    }
    throw new Error(`Y-Modem block ${blockNum} failed after ${MAX_RETRIES} retries.`);
  }

  async close(): Promise<void> {
    this.cancelled = true;
    this.pumpActive = false;

    if (this.reader) {
      try {
        await this.reader.cancel();
      } catch {
        // Ignore
      }
      try {
        this.reader.releaseLock();
      } catch {
        // Ignore
      }
      this.reader = null;
    }

    if (this.writer) {
      try {
        this.writer.releaseLock();
      } catch {
        // Ignore
      }
      this.writer = null;
    }

    if (this.port) {
      try {
        if (this.port.readable || this.port.writable) {
          await this.port.close();
        }
      } catch {
        // Ignore close race
      }
      this.port = null;
    }
  }
}
