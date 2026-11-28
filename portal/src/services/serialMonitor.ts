export interface SerialMonitorCallbacks {
  onChunk: (text: string) => void;
  onStatus: (connected: boolean) => void;
  onError: (message: string) => void;
}

/**
 * Lightweight 115200 bps Web Serial stream session used after flashing (or standalone)
 * to inspect ESP32-S3 boot logs and send Nezha Cyber CLI commands.
 */
export class SerialMonitorSession {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private decoder = new TextDecoder("utf-8", { fatal: false });
  private encoder = new TextEncoder();
  private running = false;

  get isOpen(): boolean {
    return this.running && this.port !== null;
  }

  async open(port: SerialPort, baudRate: number, callbacks: SerialMonitorCallbacks): Promise<void> {
    await this.close();

    this.port = port;
    this.running = true;

    try {
      if (!port.readable && !port.writable) {
        await port.open({ baudRate });
      }
    } catch (err) {
      this.running = false;
      this.port = null;
      throw err;
    }

    callbacks.onStatus(true);
    this.readLoop(callbacks);
  }

  private async readLoop(callbacks: SerialMonitorCallbacks): Promise<void> {
    if (!this.port?.readable) return;

    while (this.running && this.port?.readable) {
      try {
        this.reader = this.port.readable.getReader();
        while (this.running) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value && value.length > 0) {
            const decoded = this.decoder.decode(value, { stream: true });
            if (decoded) {
              callbacks.onChunk(decoded);
            }
          }
        }
      } catch (err) {
        if (this.running) {
          callbacks.onError(err instanceof Error ? err.message : String(err));
        }
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

    if (this.running) {
      await this.close();
      callbacks.onStatus(false);
    }
  }

  async sendCommand(command: string): Promise<void> {
    if (!this.port?.writable) {
      throw new Error("Serial monitor is not attached.");
    }
    const payload = command.endsWith("\n") ? command : `${command}\r\n`;
    this.writer = this.port.writable.getWriter();
    try {
      await this.writer.write(this.encoder.encode(payload));
    } finally {
      try {
        this.writer.releaseLock();
      } catch {
        // Ignore
      }
      this.writer = null;
    }
  }

  async pulseReset(): Promise<void> {
    if (!this.port) return;
    try {
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: true });
      await new Promise((r) => setTimeout(r, 120));
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } catch {
      // Some bridges do not implement modem signals
    }
  }

  async close(): Promise<void> {
    this.running = false;
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
        // Ignore close races
      }
      this.port = null;
    }
  }
}
