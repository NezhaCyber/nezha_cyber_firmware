export type LineEndingMode = "lf" | "crlf" | "cr" | "none";

export interface SerialSignalPreset {
  dataTerminalReady: boolean;
  requestToSend: boolean;
}

/**
 * Strip ANSI CSI, OSC, and 2-byte escape sequences while preserving UTF-8 / CJK text.
 */
export function stripAnsiSequences(text: string): string {
  return text
    .replace(/\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)/g, "")
    .replace(/\x1b\[[0-9;?]*[ -/]*[@-~]/g, "")
    .replace(/\x1b[@-Z\\-_]/g, "");
}

/**
 * Encode an outgoing command with the selected line ending.
 * Nezha Cyber SimpleCLI reads until '\n' (readStringUntil('\n')), so LF is the cleanest default.
 */
export function encodeCommandLine(command: string, lineEnding: LineEndingMode): string {
  const clean = command.replace(/[\r\n]+$/, "");
  switch (lineEnding) {
    case "crlf":
      return `${clean}\r\n`;
    case "cr":
      return `${clean}\r`;
    case "none":
      return clean;
    case "lf":
    default:
      return `${clean}\n`;
  }
}

/**
 * Pick safe initial DTR/RTS modem signals when opening a serial monitor:
 * - Espressif native USB-CDC/JTAG (0x303A, ESP32-S3/C5/S2): DTR=true enables HWCDC/USBCDC TX output,
 *   while RTS=false keeps the chip running.
 * - External USB-UART bridges (CP210x/CH9102/CH340/FTDI): DTR=false, RTS=false guarantees EN=HIGH
 *   and IO0=HIGH so auto-reset circuits never hold the chip in reset.
 */
export function getInitialMonitorSignals(usbVendorId?: number): SerialSignalPreset {
  if (usbVendorId === 0x303a) {
    return { dataTerminalReady: true, requestToSend: false };
  }
  return { dataTerminalReady: false, requestToSend: false };
}

export interface ParsedChunkResult {
  completedLines: string[];
  pendingLine: string;
}

/**
 * Stateful streaming line splitter that handles:
 * - LF (\n) and CRLF (\r\n)
 * - Standalone CR (\r) progress overwrites
 * - Incomplete trailing prompts (e.g. Nezha CLI "# ") exposed live in `pendingLine`
 * - Multi-chunk ANSI escape sequences cleanly stripped
 */
export class SerialLineBuffer {
  private current = "";

  feed(rawChunk: string): ParsedChunkResult {
    const cleaned = stripAnsiSequences(rawChunk);
    const completedLines: string[] = [];

    for (let i = 0; i < cleaned.length; i++) {
      const ch = cleaned[i];
      if (ch === "\n") {
        completedLines.push(this.current);
        this.current = "";
      } else if (ch === "\r") {
        // If followed by \n, treat \r\n as a single line break; otherwise CR returns cursor to line start
        if (cleaned[i + 1] === "\n") {
          continue;
        }
        // Only overwrite if more characters follow on the same line, preserving prompt text until overwritten
        if (i + 1 < cleaned.length) {
          this.current = "";
        }
      } else if (ch === "\b" || ch === "\x7f") {
        if (this.current.length > 0) {
          this.current = this.current.slice(0, -1);
        }
      } else {
        this.current += ch;
      }
    }

    return {
      completedLines,
      pendingLine: this.current,
    };
  }

  flush(): string | null {
    if (this.current.length === 0) return null;
    const out = this.current;
    this.current = "";
    return out;
  }

  reset(): void {
    this.current = "";
  }
}
