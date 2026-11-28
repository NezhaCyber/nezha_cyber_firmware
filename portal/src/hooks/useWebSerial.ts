import { useCallback, useEffect, useRef, useState } from "react";
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

const MAX_TERMINAL_LINES = 800;

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
  const [chipInfo, setChipInfo] = useState<DetectedChipInfo | null>(null);
  const [progress, setProgress] = useState<FlashProgressState>(INITIAL_PROGRESS);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [monitorConnected, setMonitorConnected] = useState<boolean>(false);
  const [terminalLines, setTerminalLines] = useState<TerminalLine[]>([]);

  const flasherRef = useRef<EspFlasherSession>(new EspFlasherSession());
  const monitorRef = useRef<SerialMonitorSession>(new SerialMonitorSession());
  const lineIdRef = useRef<number>(1);
  const rxBufferRef = useRef<string>("");
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

  const appendRawChunk = useCallback((chunk: string) => {
    rxBufferRef.current += chunk;
    const parts = rxBufferRef.current.split(/\r?\n/);
    rxBufferRef.current = parts.pop() ?? "";
    if (parts.length > 0) {
      const timestamp = formatTimeStamp();
      setTerminalLines((prev) => {
        const additions: TerminalLine[] = parts.map((line) => ({
          id: lineIdRef.current++,
          timestamp,
          text: line.replace(/\x1b\[[0-9;]*m/g, ""),
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
    }
    if (flasherRef.current.isConnected) {
      await flasherRef.current.disconnect();
    }
    setChipInfo(null);
    setProgress(INITIAL_PROGRESS);
    setErrorMessage(null);
  }, []);

  const connectPort = useCallback(
    async (targetSummary: UsbPortInfoSummary, customBaud?: number) => {
      setErrorMessage(null);
      await beforeAcquireCbRef.current?.();
      if (monitorRef.current.isOpen) {
        await monitorRef.current.close();
        setMonitorConnected(false);
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

  const attachSerialMonitor = useCallback(async () => {
    if (!isSupported) return;
    setErrorMessage(null);
    await beforeAcquireCbRef.current?.();

    let targetPort = selectedPort?.port ?? null;
    if (!targetPort) {
      try {
        const requested = await navigator.serial.requestPort({ filters: ESP_SERIAL_FILTERS });
        const summary = describeSerialPort(requested, knownPorts.length);
        setSelectedPort(summary);
        targetPort = requested;
        await refreshAuthorizedPorts();
      } catch (err) {
        if (err instanceof DOMException && err.name === "NotFoundError") return;
        const msg = err instanceof Error ? err.message : String(err);
        appendLog(`[monitor] ${msg}`, "error");
        return;
      }
    }

    if (flasherRef.current.isConnected) {
      await flasherRef.current.disconnect();
      setChipInfo(null);
    }

    try {
      await monitorRef.current.open(targetPort, 115200, {
        onChunk: appendRawChunk,
        onStatus: (open) => {
          setMonitorConnected(open);
          appendLog(
            open ? "[monitor] Attached at 115200 bps." : "[monitor] Disconnected.",
            "system",
          );
        },
        onError: (msg) => appendLog(`[monitor] ${msg}`, "error"),
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      setErrorMessage(msg);
      appendLog(`[monitor] ${msg}`, "error");
    }
  }, [isSupported, selectedPort, knownPorts.length, refreshAuthorizedPorts, appendLog, appendRawChunk]);

  const detachSerialMonitor = useCallback(async () => {
    await monitorRef.current.close();
    setMonitorConnected(false);
    appendLog("[monitor] Detached.", "system");
  }, [appendLog]);

  const sendSerialCommand = useCallback(
    async (cmd: string) => {
      const trimmed = cmd.trim();
      if (!trimmed) return;
      try {
        appendLog(`> ${trimmed}`, "tx");
        await monitorRef.current.sendCommand(trimmed);
      } catch (err) {
        appendLog(`[monitor] ${err instanceof Error ? err.message : String(err)}`, "error");
      }
    },
    [appendLog],
  );

  const clearTerminal = useCallback(() => {
    rxBufferRef.current = "";
    setTerminalLines([]);
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
    chipInfo,
    progress,
    errorMessage,
    isConnected: chipInfo !== null,
    isBusy,
    monitorConnected,
    terminalLines,
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
    clearTerminal,
    appendLog,
  };
}
