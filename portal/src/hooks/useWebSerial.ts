import { useCallback, useEffect, useRef, useState } from "react";
import {
  SerialLineBuffer,
  type LineEndingMode,
} from "@/lib/serialTerminal";
import { describeSerialPort, ESP_SERIAL_FILTERS } from "@/lib/usbDevices";
import { formatTimeStamp, resolveAssetUrl } from "@/lib/utils";
import {
  EspFlasherSession,
  type BinarySegmentInput,
} from "@/services/espFlasher";
import { SerialMonitorSession } from "@/services/serialMonitor";
import type {
  DetectedChipInfo,
  FlashPart,
  FlashProgressState,
  TerminalLine,
  UsbPortInfoSummary,
} from "@/types/firmware";

const MAX_TERMINAL_LINES = 1000;

const INITIAL_PROGRESS: FlashProgressState = {
  phase: "idle",
  overallPercent: 0,
  writtenBytes: 0,
  totalBytes: 0,
  currentPartIndex: 0,
  totalParts: 0,
  currentPartName: "",
  currentPartOffset: "0x0",
  currentPartPercent: 0,
  throughputBps: 0,
  etaSeconds: null,
  message: "",
};

export interface UseWebSerialOptions {
  onAutoDetectBoard?: (boardId: string) => void;
  onBeforeAcquirePort?: () => Promise<void>;
}

export function useWebSerial(options?: UseWebSerialOptions) {
  const isSupported = typeof navigator !== "undefined" && "serial" in navigator;

  const [knownPorts, setKnownPorts] = useState<UsbPortInfoSummary[]>([]);
  const [selectedPort, setSelectedPort] = useState<UsbPortInfoSummary | null>(null);
  const [baudRate, setBaudRate] = useState<number>(921600);
  const [monitorBaudRate, setMonitorBaudRate] = useState<number>(115200);
  const [lineEnding, setLineEnding] = useState<LineEndingMode>("lf");
  const [localEcho, setLocalEcho] = useState<boolean>(true);
  const [chipInfo, setChipInfo] = useState<DetectedChipInfo | null>(null);
  const [progress, setProgress] = useState<FlashProgressState>(INITIAL_PROGRESS);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [monitorError, setMonitorError] = useState<string | null>(null);
  const [monitorConnected, setMonitorConnected] = useState<boolean>(false);
  const [monitorConnecting, setMonitorConnecting] = useState<boolean>(false);
  const [terminalLines, setTerminalLines] = useState<TerminalLine[]>([]);
  const [pendingLine, setPendingLine] = useState<string>("");
  const [rxBytes, setRxBytes] = useState<number>(0);
  const [txBytes, setTxBytes] = useState<number>(0);

  const flasherRef = useRef<EspFlasherSession>(new EspFlasherSession());
  const monitorRef = useRef<SerialMonitorSession>(new SerialMonitorSession());
  const lineBufferRef = useRef<SerialLineBuffer>(new SerialLineBuffer());
  const lineIdRef = useRef<number>(1);
  const autoBoardCbRef = useRef(options?.onAutoDetectBoard);
  const beforeAcquireCbRef = useRef(options?.onBeforeAcquirePort);
  autoBoardCbRef.current = options?.onAutoDetectBoard;
  beforeAcquireCbRef.current = options?.onBeforeAcquirePort;

  const appendLog = useCallback(
    (text: string, source: TerminalLine["source"] = "system") => {
      const timestamp = formatTimeStamp();
      const splitLines = text.split(/\r?\n/);
      setTerminalLines((prev) => {
        const additions: TerminalLine[] = splitLines
          .filter((l) => l.length > 0)
          .map((l) => ({
            id: lineIdRef.current++,
            timestamp,
            text: l,
            source,
          }));
        if (additions.length === 0) return prev;
        const merged = [...prev, ...additions];
        return merged.length > MAX_TERMINAL_LINES
          ? merged.slice(merged.length - MAX_TERMINAL_LINES)
          : merged;
      });
    },
    [],
  );

  const appendRawChunk = useCallback((chunk: string, byteCount: number) => {
    if (byteCount > 0) {
      setRxBytes((n) => n + byteCount);
    }
    const { completedLines, pendingLine: nextPending } = lineBufferRef.current.feed(chunk);
    setPendingLine(nextPending);

    if (completedLines.length > 0) {
      const timestamp = formatTimeStamp();
      setTerminalLines((prev) => {
        const additions: TerminalLine[] = completedLines.map((line) => ({
          id: lineIdRef.current++,
          timestamp,
          text: line,
          source: "device",
        }));
        const merged = [...prev, ...additions];
        return merged.length > MAX_TERMINAL_LINES
          ? merged.slice(merged.length - MAX_TERMINAL_LINES)
          : merged;
      });
    }
  }, []);

  const refreshAuthorizedPorts = useCallback(async () => {
    if (!isSupported) return;
    try {
      const ports = await navigator.serial.getPorts();
      const summaries = ports.map((p, idx) => describeSerialPort(p, idx));
      setKnownPorts(summaries);

      setSelectedPort((current) => {
        if (current && summaries.some((s) => s.port === current.port)) {
          return summaries.find((s) => s.port === current.port) ?? current;
        }
        const preferred =
          summaries.find((s) => s.likelyChipFamily === "esp32s3") ?? summaries[0] ?? null;
        if (preferred?.suggestedBoardId) {
          autoBoardCbRef.current?.(preferred.suggestedBoardId);
        }
        return preferred;
      });
    } catch {
      // Ignore permission errors during passive scan
    }
  }, [isSupported]);

  useEffect(() => {
    if (!isSupported) return;
    void refreshAuthorizedPorts();

    const handleConnect = (event: Event) => {
      const serialEvent = event as Event & { target: SerialPort };
      const port = serialEvent.target;
      if (port) {
        const summary = describeSerialPort(port, 0);
        appendLog(
          `[usb] Detected ${summary.deviceLabel} (${summary.vendorHex}:${summary.productHex})`,
          "system",
        );
        if (summary.suggestedBoardId) {
          autoBoardCbRef.current?.(summary.suggestedBoardId);
        }
      }
      void refreshAuthorizedPorts();
    };

    const handleDisconnect = async () => {
      appendLog("[usb] USB serial device disconnected.", "system");
      if (flasherRef.current.isConnected) {
        await flasherRef.current.disconnect();
        setChipInfo(null);
        setProgress(INITIAL_PROGRESS);
      }
      if (monitorRef.current.isOpen) {
        await monitorRef.current.close();
        setMonitorConnected(false);
        setPendingLine("");
      }
      void refreshAuthorizedPorts();
    };

    navigator.serial.addEventListener("connect", handleConnect);
    navigator.serial.addEventListener("disconnect", handleDisconnect);
    return () => {
      navigator.serial.removeEventListener("connect", handleConnect);
      navigator.serial.removeEventListener("disconnect", handleDisconnect);
    };
  }, [isSupported, refreshAuthorizedPorts, appendLog]);

  useEffect(() => {
    return () => {
      void flasherRef.current.disconnect();
      void monitorRef.current.close();
    };
  }, []);

  const disconnectAll = useCallback(async () => {
    if (monitorRef.current.isOpen) {
      await monitorRef.current.close();
      setMonitorConnected(false);
      setPendingLine("");
    }
    if (flasherRef.current.isConnected) {
      await flasherRef.current.disconnect();
    }
    setChipInfo(null);
    setProgress(INITIAL_PROGRESS);
    setErrorMessage(null);
    setMonitorError(null);
  }, []);

  const connectPort = useCallback(
    async (targetSummary: UsbPortInfoSummary, customBaud?: number) => {
      setErrorMessage(null);
      await beforeAcquireCbRef.current?.();
      if (monitorRef.current.isOpen) {
        await monitorRef.current.close();
        setMonitorConnected(false);
        setPendingLine("");
      }

      setProgress({ ...INITIAL_PROGRESS, phase: "connecting" });
      try {
        const detected = await flasherRef.current.connect(
          targetSummary.port,
          customBaud ?? baudRate,
          (msg, lvl) => appendLog(msg, lvl ?? "system"),
        );
        setChipInfo(detected);
        setSelectedPort(targetSummary);
        setProgress({ ...INITIAL_PROGRESS, phase: "connected" });

        if (detected.chipFamily === "esp32s3" && targetSummary.suggestedBoardId) {
          autoBoardCbRef.current?.(targetSummary.suggestedBoardId);
        } else if (detected.chipFamily === "esp32") {
          autoBoardCbRef.current?.("m5stack-cplus1_1");
        }
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setErrorMessage(msg);
        setChipInfo(null);
        setProgress({ ...INITIAL_PROGRESS, phase: "error", message: msg });
        appendLog(`[error] ${msg}`, "error");
      }
    },
    [baudRate, appendLog],
  );

  const requestAndConnectPort = useCallback(
    async (unfiltered = false) => {
      if (!isSupported) return;
      setErrorMessage(null);
      try {
        const port = await navigator.serial.requestPort(
          unfiltered ? {} : { filters: ESP_SERIAL_FILTERS },
        );
        const summary = describeSerialPort(port, knownPorts.length);
        setSelectedPort(summary);
        if (summary.suggestedBoardId) {
          autoBoardCbRef.current?.(summary.suggestedBoardId);
        }
        await refreshAuthorizedPorts();
        await connectPort(summary);
      } catch (err) {
        if (err instanceof DOMException && err.name === "NotFoundError") {
          return;
        }
        const msg = err instanceof Error ? err.message : String(err);
        setErrorMessage(msg);
        appendLog(`[usb] ${msg}`, "error");
      }
    },
    [isSupported, knownPorts.length, refreshAuthorizedPorts, connectPort, appendLog],
  );

  const flashManifestParts = useCallback(
    async (parts: FlashPart[], eraseAll: boolean) => {
      if (!flasherRef.current.isConnected) {
        setErrorMessage("Connect a device in Download Mode before flashing.");
        return;
      }
      setErrorMessage(null);
      setProgress({
        ...INITIAL_PROGRESS,
        phase: "downloading",
        totalParts: parts.length,
        currentPartName: parts[0]?.name ?? "",
        currentPartOffset: parts[0]?.offset ?? "0x0",
      });

      try {
        const segments: BinarySegmentInput[] = [];
        for (let i = 0; i < parts.length; i++) {
          const part = parts[i];
          const url = resolveAssetUrl(part.path);
          appendLog(`[fetch] Downloading ${part.name} (${part.offset})...`, "system");
          const response = await fetch(url, { cache: "no-store" });
          if (!response.ok) {
            throw new Error(
              `Firmware binary not found (${response.status}): ${part.path}`,
            );
          }
          const contentType = response.headers.get("content-type") || "";
          if (contentType.includes("text/html")) {
            throw new Error(
              `Binary missing at ${part.path}. Drop built .bin into portal/public/firmware/ or use Local .bin.`,
            );
          }
          const buf = new Uint8Array(await response.arrayBuffer());
          if (buf.byteLength < 64) {
            throw new Error(`Firmware file ${part.name} appears empty (${buf.byteLength} bytes).`);
          }
          segments.push({
            name: part.name,
            label: part.label,
            offset: part.offset,
            bytes: buf,
          });
        }

        await flasherRef.current.flashSegments(
          segments,
          { eraseAll },
          {
            onLog: (line, level) => appendLog(line, level ?? "system"),
            onProgress: (update) => setProgress((prev) => ({ ...prev, ...update })),
          },
        );
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setErrorMessage(msg);
        setProgress((prev) => ({ ...prev, phase: "error", message: msg }));
        appendLog(`[error] ${msg}`, "error");
      }
    },
    [appendLog],
  );

  const flashLocalSegments = useCallback(
    async (segments: BinarySegmentInput[], eraseAll: boolean) => {
      if (!flasherRef.current.isConnected) {
        setErrorMessage("Connect a device in Download Mode before flashing.");
        return;
      }
      setErrorMessage(null);
      try {
        await flasherRef.current.flashSegments(
          segments,
          { eraseAll },
          {
            onLog: (line, level) => appendLog(line, level ?? "system"),
            onProgress: (update) => setProgress((prev) => ({ ...prev, ...update })),
          },
        );
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setErrorMessage(msg);
        setProgress((prev) => ({ ...prev, phase: "error", message: msg }));
        appendLog(`[error] ${msg}`, "error");
      }
    },
    [appendLog],
  );

  const hardReset = useCallback(async () => {
    setErrorMessage(null);
    setMonitorError(null);
    if (flasherRef.current.isConnected) {
      setProgress((prev) => ({ ...prev, phase: "resetting" }));
      try {
        await flasherRef.current.hardReset((msg, lvl) => appendLog(msg, lvl ?? "system"));
        await flasherRef.current.disconnect();
        setChipInfo(null);
        setProgress((prev) => ({ ...prev, phase: "done" }));
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setErrorMessage(msg);
        setProgress((prev) => ({ ...prev, phase: "error", message: msg }));
      }
      return;
    }

    if (monitorRef.current.isOpen) {
      appendLog("[monitor] Pulsing RTS reset...", "system");
      await monitorRef.current.pulseReset();
    }
  }, [appendLog]);

  const attachSerialMonitor = useCallback(
    async (options?: {
      forceSelectPort?: boolean;
      unfiltered?: boolean;
      customBaud?: number;
      portOverride?: UsbPortInfoSummary;
    }) => {
      if (!isSupported) return;
      setMonitorError(null);
      setMonitorConnecting(true);

      try {
        await beforeAcquireCbRef.current?.();

        let targetSummary: UsbPortInfoSummary | null =
          options?.portOverride ?? (options?.forceSelectPort ? null : selectedPort);

        if (!targetSummary) {
          try {
            const requested = await navigator.serial.requestPort(
              options?.unfiltered ? {} : { filters: ESP_SERIAL_FILTERS },
            );
            targetSummary = describeSerialPort(requested, knownPorts.length);
            setSelectedPort(targetSummary);
            await refreshAuthorizedPorts();
          } catch (err) {
            if (err instanceof DOMException && err.name === "NotFoundError") {
              return;
            }
            const msg = err instanceof Error ? err.message : String(err);
            setMonitorError(msg);
            appendLog(`[monitor] ${msg}`, "error");
            return;
          }
        }

        // Cleanly release flasher & let browser finish closing the OS serial handle before reopening
        if (flasherRef.current.isConnected) {
          await flasherRef.current.disconnect();
          setChipInfo(null);
          await new Promise((r) => setTimeout(r, 150));
        }

        const effectiveBaud = options?.customBaud ?? monitorBaudRate;
        lineBufferRef.current.reset();
        setPendingLine("");
        setRxBytes(0);
        setTxBytes(0);

        await monitorRef.current.open(targetSummary.port, effectiveBaud, {
          onChunk: appendRawChunk,
          onTxBytes: (bytes) => setTxBytes((n) => n + bytes),
          onStatus: (open) => {
            setMonitorConnected(open);
            if (!open) {
              const tail = lineBufferRef.current.flush();
              if (tail) appendLog(tail, "device");
              setPendingLine("");
            }
            appendLog(
              open
                ? `[monitor] Connected to ${targetSummary!.deviceLabel} (${targetSummary!.vendorHex}:${targetSummary!.productHex}) @ ${effectiveBaud} bps.`
                : "[monitor] Disconnected.",
              "system",
            );
          },
          onError: (msg) => {
            setMonitorError(msg);
            appendLog(`[monitor] ${msg}`, "error");
          },
        });
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setMonitorConnected(false);
        setMonitorError(msg);
        appendLog(`[monitor] Failed to open port: ${msg}`, "error");
      } finally {
        setMonitorConnecting(false);
      }
    },
    [
      isSupported,
      selectedPort,
      knownPorts.length,
      monitorBaudRate,
      refreshAuthorizedPorts,
      appendLog,
      appendRawChunk,
    ],
  );

  const changeMonitorBaudRate = useCallback(
    async (nextBaud: number) => {
      setMonitorBaudRate(nextBaud);
      if (monitorRef.current.isOpen && selectedPort) {
        await attachSerialMonitor({ customBaud: nextBaud, portOverride: selectedPort });
      }
    },
    [selectedPort, attachSerialMonitor],
  );

  const detachSerialMonitor = useCallback(async () => {
    setMonitorError(null);
    await monitorRef.current.close();
    const tail = lineBufferRef.current.flush();
    if (tail) appendLog(tail, "device");
    setPendingLine("");
    setMonitorConnected(false);
    appendLog("[monitor] Detached.", "system");
  }, [appendLog]);

  const sendSerialCommand = useCallback(
    async (cmd: string) => {
      setMonitorError(null);
      try {
        if (localEcho) {
          appendLog(cmd.length > 0 ? `> ${cmd}` : "> ↵", "tx");
        }
        await monitorRef.current.sendCommand(cmd, lineEnding);
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        setMonitorError(msg);
        appendLog(`[monitor] ${msg}`, "error");
      }
    },
    [appendLog, lineEnding, localEcho],
  );

  const sendSerialCtrlC = useCallback(async () => {
    setMonitorError(null);
    try {
      if (localEcho) appendLog("^C", "tx");
      await monitorRef.current.sendCtrlC();
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setMonitorError(msg);
      appendLog(`[monitor] ${msg}`, "error");
    }
  }, [appendLog, localEcho]);

  const clearTerminal = useCallback(() => {
    lineBufferRef.current.reset();
    setPendingLine("");
    setTerminalLines([]);
    setRxBytes(0);
    setTxBytes(0);
  }, []);

  const isBusy =
    progress.phase === "connecting" ||
    progress.phase === "downloading" ||
    progress.phase === "erasing" ||
    progress.phase === "flashing" ||
    progress.phase === "verifying" ||
    progress.phase === "resetting";

  return {
    isSupported,
    knownPorts,
    selectedPort,
    setSelectedPort,
    baudRate,
    setBaudRate,
    monitorBaudRate,
    setMonitorBaudRate: changeMonitorBaudRate,
    lineEnding,
    setLineEnding,
    localEcho,
    setLocalEcho,
    chipInfo,
    progress,
    errorMessage,
    monitorError,
    isConnected: chipInfo !== null,
    isBusy,
    monitorConnected,
    monitorConnecting,
    terminalLines,
    pendingLine,
    rxBytes,
    txBytes,
    refreshAuthorizedPorts,
    requestAndConnectPort,
    connectPort,
    disconnectAll,
    flashManifestParts,
    flashLocalSegments,
    hardReset,
    attachSerialMonitor,
    detachSerialMonitor,
    sendSerialCommand,
    sendSerialCtrlC,
    clearTerminal,
    appendLog,
  };
}
