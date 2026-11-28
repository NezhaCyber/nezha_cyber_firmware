import { useCallback, useEffect, useRef, useState } from "react";
import { ESP_SERIAL_FILTERS } from "@/lib/usbDevices";
import { UsbFileTransferSession } from "@/services/usbFileTransfer";
import type {
  DeviceFileEntry,
  DeviceStorageInfo,
  UsbTransferProgress,
} from "@/types/firmware";

const EMPTY_STORAGE: DeviceStorageInfo = {
  sd: { available: false, totalBytes: 0, usedBytes: 0, freeBytes: 0 },
  littlefs: { available: false, totalBytes: 0, usedBytes: 0, freeBytes: 0 },
};

const IDLE_TRANSFER: UsbTransferProgress = {
  active: false,
  direction: "upload",
  fileName: "",
  transferredBytes: 0,
  totalBytes: 0,
  percent: 0,
  speedBps: 0,
  statusText: "",
};

export interface UseUsbFileTransferOptions {
  selectedPort: SerialPort | null;
  releaseOtherSerialUsers: () => Promise<void>;
  onPortAuthorized: () => Promise<void>;
  onLog: (text: string, source?: "system" | "device" | "tx" | "error") => void;
}

export function useUsbFileTransfer(options: UseUsbFileTransferOptions) {
  const sessionRef = useRef<UsbFileTransferSession>(new UsbFileTransferSession());

  const [connected, setConnected] = useState(false);
  const [connecting, setConnecting] = useState(false);
  const [loadingList, setLoadingList] = useState(false);
  const [currentPath, setCurrentPath] = useState("/");
  const [entries, setEntries] = useState<DeviceFileEntry[]>([]);
  const [storageInfo, setStorageInfo] = useState<DeviceStorageInfo>(EMPTY_STORAGE);
  const [deviceBanner, setDeviceBanner] = useState<string>("");
  const [transfer, setTransfer] = useState<UsbTransferProgress>(IDLE_TRANSFER);
  const [error, setError] = useState<string | null>(null);
  const [previewFile, setPreviewFile] = useState<{ name: string; content: string } | null>(null);

  useEffect(() => {
    return () => {
      void sessionRef.current.close();
    };
  }, []);

  const refreshDirectory = useCallback(async (targetPath: string) => {
    if (!sessionRef.current.isConnected) return;
    setLoadingList(true);
    setError(null);
    try {
      const list = await sessionRef.current.listDirectory(targetPath);
      setCurrentPath(targetPath);
      setEntries(list);
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally {
      setLoadingList(false);
    }
  }, []);

  const refreshStorage = useCallback(async () => {
    if (!sessionRef.current.isConnected) return;
    try {
      const stats = await sessionRef.current.getStorageInfo();
      setStorageInfo(stats);
    } catch {
      // Optional telemetry
    }
  }, []);

  const connect = useCallback(
    async (forceChooser = false) => {
      if (!("serial" in navigator)) return;
      setError(null);
      setConnecting(true);

      try {
        let port = forceChooser ? null : options.selectedPort;
        if (!port) {
          port = await navigator.serial.requestPort({ filters: ESP_SERIAL_FILTERS });
          await options.onPortAuthorized();
        }

        // Release flasher / monitor so the serial port is not locked
        await options.releaseOtherSerialUsers();

        await sessionRef.current.open(port, 115200, options.onLog);
        setConnected(true);

        const info = await sessionRef.current.probeDeviceInfo();
        const firstLine = info
          .split("\n")
          .map((s) => s.trim())
          .find((s) => s.length > 0 && !s.startsWith("info"));
        setDeviceBanner(firstLine ?? "Nezha Cyber USB Storage");

        await refreshStorage();
        await refreshDirectory("/");
      } catch (err) {
        if (err instanceof DOMException && err.name === "NotFoundError") {
          setConnecting(false);
          return;
        }
        const msg = err instanceof Error ? err.message : String(err);
        setError(msg);
        await sessionRef.current.close();
        setConnected(false);
      } finally {
        setConnecting(false);
      }
    },
    [options, refreshDirectory, refreshStorage],
  );

  const disconnect = useCallback(async () => {
    await sessionRef.current.close();
    setConnected(false);
    setEntries([]);
    setCurrentPath("/");
    setStorageInfo(EMPTY_STORAGE);
    setDeviceBanner("");
    setTransfer(IDLE_TRANSFER);
    setPreviewFile(null);
  }, []);

  const uploadFiles = useCallback(
    async (files: FileList | File[]) => {
      if (!sessionRef.current.isConnected) return;
      setError(null);
      const list = Array.from(files);
      try {
        for (let i = 0; i < list.length; i++) {
          await sessionRef.current.uploadFileYmodem(list[i], currentPath, setTransfer, {
            index: i + 1,
            count: list.length,
          });
        }
        await refreshDirectory(currentPath);
        await refreshStorage();
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
        setTransfer(IDLE_TRANSFER);
      }
    },
    [currentPath, refreshDirectory, refreshStorage],
  );

  const cancelUpload = useCallback(() => {
    sessionRef.current.cancelTransfer();
  }, []);

  const makeFolder = useCallback(
    async (folderName: string) => {
      if (!sessionRef.current.isConnected) return;
      const clean = folderName.trim().replace(/^\/+|\/+$/g, "");
      if (!clean) return;
      setError(null);
      const target = currentPath === "/" ? `/${clean}` : `${currentPath}/${clean}`;
      try {
        await sessionRef.current.createDirectory(target);
        await refreshDirectory(currentPath);
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [currentPath, refreshDirectory],
  );

  const removeEntry = useCallback(
    async (entry: DeviceFileEntry) => {
      if (!sessionRef.current.isConnected) return;
      setError(null);
      try {
        await sessionRef.current.deleteEntry(entry);
        await refreshDirectory(currentPath);
        await refreshStorage();
      } catch (err) {
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [currentPath, refreshDirectory, refreshStorage],
  );

  const downloadOrPreview = useCallback(
    async (entry: DeviceFileEntry, triggerDownload: boolean) => {
      if (!sessionRef.current.isConnected || entry.isDir) return;
      setError(null);
      setTransfer({
        active: true,
        direction: "download",
        fileName: entry.name,
        transferredBytes: 0,
        totalBytes: entry.size,
        percent: 35,
        speedBps: 0,
        statusText: `Reading ${entry.name}...`,
      });

      try {
        const content = await sessionRef.current.readSmallFile(entry.path);
        setTransfer(IDLE_TRANSFER);
        if (triggerDownload) {
          const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
          const url = URL.createObjectURL(blob);
          const a = document.createElement("a");
          a.href = url;
          a.download = entry.name;
          a.click();
          URL.revokeObjectURL(url);
        } else {
          setPreviewFile({ name: entry.name, content });
        }
      } catch (err) {
        setTransfer(IDLE_TRANSFER);
        setError(err instanceof Error ? err.message : String(err));
      }
    },
    [],
  );

  return {
    connected,
    connecting,
    loadingList,
    currentPath,
    entries,
    storageInfo,
    deviceBanner,
    transfer,
    error,
    previewFile,
    setPreviewFile,
    connect,
    disconnect,
    refreshDirectory,
    uploadFiles,
    cancelUpload,
    makeFolder,
    removeEntry,
    downloadOrPreview,
  };
}
