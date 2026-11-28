import {
  encodeCommandLine,
  getInitialMonitorSignals,
  type LineEndingMode,
} from "../lib/serialTerminal.js";

export interface SerialMonitorCallbacks {
  onChunk: (text: string, byteCount: number) => void;
  onTxBytes?: (byteCount: number) => void;
  onStatus: (connected: boolean) => void;
  onError: (message: string) => void;
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

/**
 * Production Web Serial live console session for ESP32 / ESP32-S3 / ESP32-C5.
 *
 * Key hardware guarantees:
 * 1. Always re-opens the SerialPort at the requested baudRate (avoiding stale 921600 flasher rate).
 * 2. Sets chip-appropriate DTR/RTS signals (DTR=true for ESP32-S3 native USB-CDC so HWCDC flushes TX;
 *    DTR=false, RTS=false for UART bridges so EN is never held in reset).
 * 3. Serializes all outgoing writes through a queue so rapid sends never throw "WritableStream is locked".
 * 4. Guards reader teardown with a monotonic sessionId and awaits readLoop exit before port.close().
 */
export class SerialMonitorSession {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private readLoopPromise: Promise<void> | null = null;
  private writeQueue: Promise<void> = Promise.resolve();
  private decoder = new TextDecoder("utf-8", { fatal: false });
  private encoder = new TextEncoder();
  private running = false;
  private sessionId = 0;
  private baudRate = 115200;
  private callbacks: SerialMonitorCallbacks | null = null;

  get isOpen(): boolean {
    return this.running && this.port !== null;
  }

  get currentBaudRate(): number {
    return this.baudRate;
  }

  get activePort(): SerialPort | null {
    return this.port;
  }

  async open(port: SerialPort, baudRate: number, callbacks: SerialMonitorCallbacks): Promise<void> {
    await this.close();

    const mySession = ++this.sessionId;
    this.port = port;
    this.baudRate = baudRate;
    this.callbacks = callbacks;
    this.decoder = new TextDecoder("utf-8", { fatal: false });
    this.writeQueue = Promise.resolve();
    this.running = true;

    try {
      // If left open from esptool-js or a previous session at a different baud rate, cleanly reopen
      if (port.readable || port.writable) {
        try {
          await port.close();
          await sleep(120);
        } catch {
          // If the port is already open at our rate or cannot be closed cleanly, try opening below
        }
      }

      if (!port.readable && !port.writable) {
        try {
          await port.open({ baudRate, bufferSize: 16384 });
        } catch (firstErr) {
          // Chrome Web Serial occasionally throws a transient InvalidStateError right after esptool/USB-Files closes a port
          await sleep(180);
          if (!port.readable && !port.writable) {
            await port.open({ baudRate, bufferSize: 16384 });
          } else {
            throw firstErr;
          }
        }
      }

      // Assert safe DTR/RTS signals for the detected USB chip so serial output flows immediately
      const usbVendorId = typeof port.getInfo === "function" ? port.getInfo().usbVendorId : undefined;
      const signals = getInitialMonitorSignals(usbVendorId);
      if (typeof port.setSignals === "function") {
        try {
          await port.setSignals(signals);
        } catch {
          // Some virtual serial adapters do not implement setSignals
        }
      }
    } catch (err) {
      if (this.sessionId === mySession) {
        this.running = false;
        this.port = null;
        this.callbacks = null;
      }
      throw err;
    }

    if (this.sessionId !== mySession) return;
    callbacks.onStatus(true);
    this.readLoopPromise = this.readLoop(mySession, port, callbacks);
  }

  private async readLoop(
    mySession: number,
    port: SerialPort,
    callbacks: SerialMonitorCallbacks,
  ): Promise<void> {
    while (this.running && this.sessionId === mySession && port.readable) {
      let localReader: ReadableStreamDefaultReader<Uint8Array> | null = null;
      try {
        localReader = port.readable.getReader();
        this.reader = localReader;

        while (this.running && this.sessionId === mySession) {
          const { value, done } = await localReader.read();
          if (done) break;
          if (value && value.length > 0) {
            const decoded = this.decoder.decode(value, { stream: true });
            if (decoded) {
              callbacks.onChunk(decoded, value.byteLength);
            }
          }
        }
      } catch (err) {
        if (this.running && this.sessionId === mySession) {
          const msg = err instanceof Error ? err.message : String(err);
          // Framing/break errors on ESP32 reboot are recoverable when port stays readable
          if (/break|framing|overrun|parity/i.test(msg) && port.readable) {
            callbacks.onError(`Serial line glitch (${msg}) — continuing...`);
            await sleep(50);
            continue;
          }
          callbacks.onError(msg);
        }
        break;
      } finally {
        if (localReader) {
          try {
            localReader.releaseLock();
          } catch {
            // Ignore already released lock
          }
        }
        if (this.reader === localReader) {
          this.reader = null;
        }
      }
    }

    // Flush any remaining UTF-8 bytes in decoder
    if (this.sessionId === mySession) {
      const tail = this.decoder.decode();
      if (tail) {
        callbacks.onChunk(tail, 0);
      }
    }

    // Unexpected disconnect (e.g. USB cable unplugged)
    if (this.running && this.sessionId === mySession) {
      this.running = false;
      this.port = null;
      callbacks.onStatus(false);
    }
  }

  async sendRaw(bytes: Uint8Array): Promise<void> {
    const port = this.port;
    if (!this.running || !port?.writable) {
      throw new Error("Serial monitor is not attached.");
    }

    const op = this.writeQueue.then(async () => {
      if (!this.running || !port.writable) {
        throw new Error("Serial monitor closed before write completed.");
      }
      const writer = port.writable.getWriter();
      try {
        await writer.write(bytes);
        this.callbacks?.onTxBytes?.(bytes.byteLength);
      } finally {
        try {
          writer.releaseLock();
        } catch {
          // Ignore
        }
      }
    });

    // Keep queue alive even if one write fails
    this.writeQueue = op.catch(() => {});
    return op;
  }

  async sendCommand(command: string, lineEnding: LineEndingMode = "lf"): Promise<void> {
    const payload = encodeCommandLine(command, lineEnding);
    await this.sendRaw(this.encoder.encode(payload));
  }

  async sendCtrlC(): Promise<void> {
    await this.sendRaw(new Uint8Array([0x03]));
  }

  async setSignals(dtr: boolean, rts: boolean): Promise<void> {
    if (!this.port || typeof this.port.setSignals !== "function") return;
    await this.port.setSignals({ dataTerminalReady: dtr, requestToSend: rts });
  }

  async pulseReset(): Promise<void> {
    const port = this.port;
    if (!port || typeof port.setSignals !== "function") return;
    const usbVendorId = typeof port.getInfo === "function" ? port.getInfo().usbVendorId : undefined;
    const idleSignals = getInitialMonitorSignals(usbVendorId);
    try {
      await port.setSignals({ dataTerminalReady: false, requestToSend: true });
      await sleep(120);
      await port.setSignals(idleSignals);
    } catch {
      // Some USB bridges do not implement modem control lines
    }
  }

  async close(): Promise<void> {
    this.sessionId++;
    this.running = false;

    const activeReader = this.reader;
    this.reader = null;
    if (activeReader) {
      try {
        await activeReader.cancel();
      } catch {
        // Ignore cancel errors on disconnected ports
      }
    }

    const loop = this.readLoopPromise;
    this.readLoopPromise = null;
    if (loop) {
      try {
        await Promise.race([loop, sleep(400)]);
      } catch {
        // Ignore
      }
    }

    try {
      await Promise.race([this.writeQueue, sleep(300)]);
    } catch {
      // Ignore
    }

    const port = this.port;
    this.port = null;
    this.callbacks = null;

    if (port) {
      try {
        if (port.readable || port.writable) {
          await port.close();
        }
      } catch {
        // Ignore close races on unplugged USB devices
      }
    }
  }
}
