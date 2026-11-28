import {
  ESPLoader,
  Transport,
  type FlashOptions,
  type IEspLoaderTerminal,
  type LoaderOptions,
} from "esptool-js";
import {
  formatHexOffset,
  parseHexOffset,
  uint8ArrayToBinaryString,
} from "@/lib/utils";
import { normalizeChipFamily } from "@/lib/usbDevices";
import type { DetectedChipInfo, FlashProgressState } from "@/types/firmware";

export interface BinarySegmentInput {
  name: string;
  label: string;
  offset: string;
  bytes: Uint8Array;
}

export interface FlashSessionCallbacks {
  onLog: (line: string, level?: "system" | "device" | "error") => void;
  onProgress: (update: Partial<FlashProgressState>) => void;
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

export class EspFlasherSession {
  private transport: Transport | null = null;
  private loader: ESPLoader | null = null;
  private port: SerialPort | null = null;

  get isConnected(): boolean {
    return this.loader !== null && this.transport !== null;
  }

  get activePort(): SerialPort | null {
    return this.port;
  }

  async connect(
    port: SerialPort,
    baudrate: number,
    onLog: (line: string, level?: "system" | "device" | "error") => void,
  ): Promise<DetectedChipInfo> {
    await this.disconnect();

    this.port = port;
    this.transport = new Transport(port, false);

    const terminal: IEspLoaderTerminal = {
      clean() {
        // No-op: terminal state is managed by React
      },
      writeLine(data: string) {
        const clean = data.replace(/\r/g, "").trimEnd();
        if (clean.length > 0) {
          onLog(clean, "device");
        }
      },
      write(data: string) {
        const clean = data.replace(/\r/g, "").trimEnd();
        if (clean.length > 0) {
          onLog(clean, "device");
        }
      },
    };

    const options: LoaderOptions = {
      transport: this.transport,
      baudrate,
      romBaudrate: 115200,
      terminal,
      debugLogging: false,
    };

    this.loader = new ESPLoader(options);

    try {
      onLog(`[esptool] Handshaking bootloader at ${baudrate} bps (ROM 115200)...`, "system");
      const chipName = await this.loader.main();

      let chipDescription = chipName;
      let macAddress = "Unknown";
      let flashSizeMB = 0;
      let crystalFreqMHz: number | undefined;
      let features: string[] | undefined;

      try {
        chipDescription = await this.loader.chip.getChipDescription(this.loader);
      } catch {
        // Fallback to chipName
      }

      try {
        macAddress = await this.loader.chip.readMac(this.loader);
      } catch {
        // Ignore if ROM doesn't expose MAC
      }

      try {
        const sizeKb = await this.loader.getFlashSize();
        flashSizeMB = sizeKb > 0 ? Math.round((sizeKb / 1024) * 10) / 10 : 0;
      } catch {
        flashSizeMB = 0;
      }

      try {
        crystalFreqMHz = await this.loader.chip.getCrystalFreq(this.loader);
      } catch {
        // Optional
      }

      try {
        features = await this.loader.chip.getChipFeatures(this.loader);
      } catch {
        // Optional
      }

      const info: DetectedChipInfo = {
        chipName,
        chipDescription,
        chipFamily: normalizeChipFamily(chipName),
        macAddress: macAddress.toUpperCase(),
        flashSizeMB,
        crystalFreqMHz,
        features,
      };

      onLog(
        `[esptool] Synced ${info.chipDescription} | MAC ${info.macAddress} | Flash ${
          info.flashSizeMB > 0 ? `${info.flashSizeMB} MB` : "auto"
        }`,
        "system",
      );
      return info;
    } catch (err) {
      await this.disconnect();
      throw err;
    }
  }

  async flashSegments(
    segments: BinarySegmentInput[],
    options: { eraseAll: boolean },
    callbacks: FlashSessionCallbacks,
  ): Promise<void> {
    if (!this.loader) {
      throw new Error("ESP bootloader is not connected.");
    }
    if (segments.length === 0) {
      throw new Error("No firmware binary segments queued for flashing.");
    }

    const totalBytes = segments.reduce((sum, seg) => sum + seg.bytes.byteLength, 0);
    const segmentSizes = segments.map((seg) => seg.bytes.byteLength);

    const fileArray = segments.map((seg) => ({
      data: uint8ArrayToBinaryString(seg.bytes),
      address: parseHexOffset(seg.offset),
    }));

    const startTime = performance.now();

    if (options.eraseAll) {
      callbacks.onProgress({
        phase: "erasing",
        overallPercent: 2,
        totalBytes,
        writtenBytes: 0,
        totalParts: segments.length,
        currentPartIndex: 0,
        currentPartName: segments[0].label,
        currentPartOffset: formatHexOffset(fileArray[0].address),
        message: "Erasing entire SPI flash...",
      });
      callbacks.onLog("[esptool] Erasing entire SPI flash (eraseAll=true)...", "system");
    }

    const flashOptions: FlashOptions = {
      fileArray,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: options.eraseAll,
      compress: true,
      reportProgress: (fileIndex: number, written: number, total: number) => {
        const previousBytes = segmentSizes
          .slice(0, fileIndex)
          .reduce((acc, size) => acc + size, 0);
        const clampedWritten = Math.min(written, total);
        const cumulativeWritten = previousBytes + clampedWritten;
        const overallPercent =
          totalBytes > 0 ? Math.min(100, (cumulativeWritten / totalBytes) * 100) : 0;
        const partPercent = total > 0 ? Math.min(100, (clampedWritten / total) * 100) : 0;
        const elapsedSec = Math.max(0.15, (performance.now() - startTime) / 1000);
        const throughputBps = cumulativeWritten / elapsedSec;
        const remainingBytes = Math.max(0, totalBytes - cumulativeWritten);
        const etaSeconds = throughputBps > 512 ? remainingBytes / throughputBps : null;

        const seg = segments[fileIndex];
        callbacks.onProgress({
          phase: "flashing",
          overallPercent,
          writtenBytes: cumulativeWritten,
          totalBytes,
          currentPartIndex: fileIndex,
          totalParts: segments.length,
          currentPartName: seg ? `${seg.label} (${seg.name})` : `Segment #${fileIndex + 1}`,
          currentPartOffset: seg ? seg.offset : formatHexOffset(fileArray[fileIndex]?.address ?? 0),
          currentPartPercent: partPercent,
          throughputBps,
          etaSeconds,
        });
      },
    };

    callbacks.onLog(
      `[esptool] Programming ${segments.length} segment(s), total ${totalBytes.toLocaleString()} bytes...`,
      "system",
    );

    await this.loader.writeFlash(flashOptions);

    callbacks.onProgress({
      phase: "done",
      overallPercent: 100,
      writtenBytes: totalBytes,
      totalBytes,
      currentPartPercent: 100,
      etaSeconds: 0,
    });
    callbacks.onLog("[esptool] Flash programming & MD5 verification completed.", "system");
  }

  async hardReset(onLog?: (line: string, level?: "system" | "device" | "error") => void): Promise<void> {
    if (!this.transport) {
      throw new Error("No active serial transport to reset.");
    }
    onLog?.("[esptool] Pulsing RTS/DTR signals for hardware reset...", "system");
    try {
      await this.transport.setDTR(false);
      await this.transport.setRTS(true);
      await sleep(120);
      await this.transport.setRTS(false);
      await sleep(60);
      onLog?.("[esptool] Hard reset pulse sent. Device is booting firmware.", "system");
    } catch (err) {
      onLog?.(
        `[esptool] Reset pulse warning: ${err instanceof Error ? err.message : String(err)}`,
        "error",
      );
      throw err;
    }
  }

  async disconnect(): Promise<void> {
    if (this.transport) {
      try {
        await this.transport.disconnect();
      } catch {
        // Port may already be closed or unplugged
      }
      try {
        await this.transport.waitForUnlock(500);
      } catch {
        // Ignore unlock timeout
      }
    }
    this.transport = null;
    this.loader = null;
  }
}
