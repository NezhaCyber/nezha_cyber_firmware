import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { AnimatePresence, motion } from "framer-motion";
import { BrowserCompatibilityAlert } from "@/components/BrowserCompatibilityAlert";
import { CyberBackdrop } from "@/components/CyberBackdrop";
import { SerialMonitorPanel } from "@/components/SerialMonitorPanel";
import {
  SimpleFlasherView,
  type FirmwareSourceMode,
  type ManifestFlashLayout,
  type ReleaseChannelFilter,
} from "@/components/SimpleFlasherView";
import { TopBar, type PortalTab } from "@/components/TopBar";
import { UsbFileTransferView } from "@/components/UsbFileTransferView";
import { TooltipProvider } from "@/components/ui/tooltip";
import { ThemeProvider } from "@/context/ThemeContext";
import { useManifest } from "@/hooks/useManifest";
import { useUsbFileTransfer } from "@/hooks/useUsbFileTransfer";
import { useWebSerial } from "@/hooks/useWebSerial";
import { LocaleProvider, useLocale } from "@/i18n/LocaleContext";
import type { BinarySegmentInput } from "@/services/espFlasher";
import {
  checkBoardFlashCompatibility,
  pickBestCompatibleBoard,
} from "@/services/githubReleases";
import type { CustomLocalPart } from "@/types/firmware";

function guessOffsetFromFilename(filename: string, isEsp32Classic: boolean): string {
  const lower = filename.toLowerCase();
  if (lower.includes("partition")) return "0x8000";
  if (lower.includes("bootloader")) return isEsp32Classic ? "0x1000" : "0x0";
  if (lower.includes("ota") || lower === "firmware.bin") return "0x10000";
  return "0x0";
}

function PortalWorkspace() {
  const { t } = useLocale();
  const {
    manifest,
    loading: manifestLoading,
    syncingGitHub,
    source: manifestSource,
    error: manifestError,
    reload: reloadManifest,
  } = useManifest();

  const [activeTab, setActiveTab] = useState<PortalTab>("flash");
  const [sourceMode, setSourceMode] = useState<FirmwareSourceMode>("manifest");
  const [channelFilter, setChannelFilter] = useState<ReleaseChannelFilter>("all");
  const [selectedVersion, setSelectedVersion] = useState<string>("");
  const [selectedBoardId, setSelectedBoardId] = useState<string>("lilygo-t-deck");
  const [flashLayout, setFlashLayout] = useState<ManifestFlashLayout>("merged");
  const [eraseAll, setEraseAll] = useState<boolean>(false);
  const [localParts, setLocalParts] = useState<CustomLocalPart[]>([]);

  // Ref bridge so flasher / monitor cleanly release the USB Files session before acquiring port
  const releaseUsbFilesRef = useRef<() => Promise<void>>(async () => {});

  const handleAutoDetectBoard = useCallback((boardId: string) => {
    setSelectedBoardId(boardId);
  }, []);

  const serial = useWebSerial({
    onAutoDetectBoard: handleAutoDetectBoard,
    onBeforeAcquirePort: async () => {
      await releaseUsbFilesRef.current();
    },
  });

  const usbFiles = useUsbFileTransfer({
    selectedPort: serial.selectedPort?.port ?? null,
    releaseOtherSerialUsers: serial.disconnectAll,
    onPortAuthorized: serial.refreshAuthorizedPorts,
    onLog: serial.appendLog,
  });

  releaseUsbFilesRef.current = usbFiles.disconnect;

  const filteredReleases = useMemo(() => {
    const all = manifest?.releases ?? [];
    if (channelFilter === "all") return all;
    if (channelFilter === "beta") return all.filter((r) => r.channel === "beta");
    return all.filter((r) => r.channel !== "beta");
  }, [manifest, channelFilter]);

  useEffect(() => {
    if (filteredReleases.length === 0) return;
    if (!selectedVersion || !filteredReleases.some((r) => r.version === selectedVersion)) {
      const preferred =
        filteredReleases.find((r) => r.channel === "stable") ?? filteredReleases[0];
      setSelectedVersion(preferred.version);
    }
  }, [filteredReleases, selectedVersion]);

  const selectedRelease = useMemo(() => {
    if (filteredReleases.length === 0) return manifest?.releases[0] ?? null;
    return (
      filteredReleases.find((r) => r.version === selectedVersion) ?? filteredReleases[0]
    );
  }, [filteredReleases, manifest, selectedVersion]);

  const selectedBoard = useMemo(() => {
    if (!selectedRelease || selectedRelease.builds.length === 0) return null;
    return (
      selectedRelease.builds.find((b) => b.id === selectedBoardId) ?? selectedRelease.builds[0]
    );
  }, [selectedRelease, selectedBoardId]);

  // When actual chip + flash size is detected over Web Serial, auto-switch if current selection won't fit
  useEffect(() => {
    if (!serial.chipInfo || !selectedRelease) return;
    const currentCompat = checkBoardFlashCompatibility(selectedBoard, serial.chipInfo);
    if (!currentCompat.compatible) {
      const best = pickBestCompatibleBoard(
        selectedRelease.builds,
        serial.chipInfo,
        serial.selectedPort?.suggestedBoardId,
      );
      if (best && best.id !== selectedBoard?.id) {
        setSelectedBoardId(best.id);
      }
    }
  }, [serial.chipInfo, serial.selectedPort, selectedRelease, selectedBoard]);

  const activeManifestParts = useMemo(() => {
    if (!selectedBoard) return [];
    if (flashLayout === "partitioned" && selectedBoard.flashModes.partitioned) {
      return selectedBoard.flashModes.partitioned.parts;
    }
    return selectedBoard.flashModes.merged.parts;
  }, [selectedBoard, flashLayout]);

  const handleAddLocalFiles = (files: FileList | File[]) => {
    const isEsp32Classic = selectedBoard?.chipFamily === "esp32";
    const additions: CustomLocalPart[] = Array.from(files).map((file, idx) => ({
      id: `${file.name}-${Date.now()}-${idx}`,
      file,
      offset: guessOffsetFromFilename(file.name, isEsp32Classic),
      label: file.name,
    }));
    setLocalParts((prev) => [...prev, ...additions]);
  };

  const handleUpdateLocalPartOffset = (id: string, offset: string) => {
    setLocalParts((prev) => prev.map((p) => (p.id === id ? { ...p, offset } : p)));
  };

  const handleRemoveLocalPart = (id: string) => {
    setLocalParts((prev) => prev.filter((p) => p.id !== id));
  };

  const handleStartFlash = async () => {
    if (sourceMode === "manifest") {
      if (activeManifestParts.length === 0) return;
      await serial.flashManifestParts(activeManifestParts, eraseAll);
      return;
    }

    if (localParts.length === 0) return;
    const segments: BinarySegmentInput[] = [];
    for (const part of localParts) {
      const buf = new Uint8Array(await part.file.arrayBuffer());
      segments.push({
        name: part.file.name,
        label: part.label,
        offset: part.offset,
        bytes: buf,
      });
    }
    await serial.flashLocalSegments(segments, eraseAll);
  };

  const handleOpenConsoleAfterFlash = async () => {
    setActiveTab("console");
    if (serial.isConnected) {
      await serial.hardReset();
      await new Promise((r) => setTimeout(r, 250));
    }
    await serial.attachSerialMonitor();
  };

  const handleOpenUsbFilesAfterFlash = async () => {
    await serial.hardReset();
    setActiveTab("files");
  };

  const boardCompat = useMemo(
    () =>
      sourceMode === "manifest"
        ? checkBoardFlashCompatibility(selectedBoard, serial.chipInfo)
        : {
            compatible: true,
            chipMismatch: false,
            flashOverflow: false,
            requiredFlashMB: 0,
            detectedFlashMB: serial.chipInfo?.flashSizeMB ?? 0,
            binarySizeBytes: null,
          },
    [sourceMode, selectedBoard, serial.chipInfo],
  );

  const suggestedCompatibleBoard = useMemo(() => {
    if (!serial.chipInfo || !selectedRelease || boardCompat.compatible) return null;
    return pickBestCompatibleBoard(selectedRelease.builds, serial.chipInfo);
  }, [serial.chipInfo, selectedRelease, boardCompat.compatible]);

  const segmentCount =
    sourceMode === "manifest" ? activeManifestParts.length : localParts.length;
  const canFlash = serial.isConnected && segmentCount > 0 && boardCompat.compatible;

  const repository = manifest?.repository ?? "NezhaCyber/nezha_cyber_firmware";

  return (
    <div className="relative flex min-h-screen flex-col">
      <CyberBackdrop phase={usbFiles.transfer.active ? "flashing" : serial.progress.phase} />

      <TopBar
        repository={repository}
        activeTab={activeTab}
        onChangeTab={setActiveTab}
        phase={serial.progress.phase}
        usbTransferActive={usbFiles.transfer.active}
      />

      <main className="mx-auto w-full max-w-5xl flex-1 px-4 py-7 sm:px-6 sm:py-10">
        <BrowserCompatibilityAlert isSupported={serial.isSupported} />

        <AnimatePresence mode="wait">
          {activeTab === "flash" && (
            <motion.div
              key="tab-flash"
              initial={{ opacity: 0, y: 10 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -6 }}
              transition={{ duration: 0.22, ease: "easeOut" }}
            >
              <SimpleFlasherView
                isSupported={serial.isSupported}
                manifest={manifest}
                manifestLoading={manifestLoading}
                syncingGitHub={syncingGitHub}
                manifestSource={manifestSource}
                manifestError={manifestError}
                onReloadManifest={reloadManifest}
                channelFilter={channelFilter}
                onChangeChannelFilter={setChannelFilter}
                filteredReleases={filteredReleases}
                selectedRelease={selectedRelease}
                onSelectReleaseVersion={setSelectedVersion}
                selectedBoard={selectedBoard}
                onSelectBoardId={setSelectedBoardId}
                boardCompat={boardCompat}
                suggestedCompatibleBoard={suggestedCompatibleBoard}
                sourceMode={sourceMode}
                onChangeSourceMode={setSourceMode}
                flashLayout={flashLayout}
                onChangeFlashLayout={setFlashLayout}
                localParts={localParts}
                onAddLocalFiles={handleAddLocalFiles}
                onUpdateLocalPartOffset={handleUpdateLocalPartOffset}
                onRemoveLocalPart={handleRemoveLocalPart}
                knownPorts={serial.knownPorts}
                selectedPort={serial.selectedPort}
                baudRate={serial.baudRate}
                onChangeBaudRate={serial.setBaudRate}
                chipInfo={serial.chipInfo}
                progress={serial.progress}
                errorMessage={serial.errorMessage}
                isConnected={serial.isConnected}
                isBusy={serial.isBusy}
                eraseAll={eraseAll}
                onChangeEraseAll={setEraseAll}
                canFlash={canFlash}
                segmentCount={segmentCount}
                onRequestPort={serial.requestAndConnectPort}
                onConnectPort={serial.connectPort}
                onDisconnect={serial.disconnectAll}
                onStartFlash={handleStartFlash}
                onHardReset={serial.hardReset}
                onOpenUsbFiles={handleOpenUsbFilesAfterFlash}
                onOpenConsole={handleOpenConsoleAfterFlash}
              />
            </motion.div>
          )}

          {activeTab === "files" && (
            <motion.div
              key="tab-files"
              initial={{ opacity: 0, y: 10 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -6 }}
              transition={{ duration: 0.22, ease: "easeOut" }}
            >
              <UsbFileTransferView
                isSupported={serial.isSupported}
                connected={usbFiles.connected}
                connecting={usbFiles.connecting}
                loadingList={usbFiles.loadingList}
                currentPath={usbFiles.currentPath}
                entries={usbFiles.entries}
                storageInfo={usbFiles.storageInfo}
                deviceBanner={usbFiles.deviceBanner}
                transfer={usbFiles.transfer}
                error={usbFiles.error}
                previewFile={usbFiles.previewFile}
                onClosePreview={() => usbFiles.setPreviewFile(null)}
                onConnect={usbFiles.connect}
                onDisconnect={usbFiles.disconnect}
                onNavigate={usbFiles.refreshDirectory}
                onUploadFiles={usbFiles.uploadFiles}
                onCancelTransfer={usbFiles.cancelUpload}
                onCreateFolder={usbFiles.makeFolder}
                onDeleteEntry={usbFiles.removeEntry}
                onDownloadOrPreview={usbFiles.downloadOrPreview}
              />
            </motion.div>
          )}

          {activeTab === "console" && (
            <motion.div
              key="tab-console"
              initial={{ opacity: 0, y: 10 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0, y: -6 }}
              transition={{ duration: 0.22, ease: "easeOut" }}
            >
              <SerialMonitorPanel
                isSupported={serial.isSupported}
                monitorConnected={serial.monitorConnected}
                monitorConnecting={serial.monitorConnecting}
                isFlashingBusy={serial.isBusy || usbFiles.transfer.active}
                selectedPort={serial.selectedPort}
                knownPorts={serial.knownPorts}
                monitorBaudRate={serial.monitorBaudRate}
                onChangeMonitorBaudRate={serial.setMonitorBaudRate}
                lineEnding={serial.lineEnding}
                onChangeLineEnding={serial.setLineEnding}
                localEcho={serial.localEcho}
                onChangeLocalEcho={serial.setLocalEcho}
                monitorError={serial.monitorError}
                lines={serial.terminalLines}
                pendingLine={serial.pendingLine}
                rxBytes={serial.rxBytes}
                txBytes={serial.txBytes}
                onAttach={serial.attachSerialMonitor}
                onDetach={serial.detachSerialMonitor}
                onHardReset={serial.hardReset}
                onClear={serial.clearTerminal}
                onSendCommand={serial.sendSerialCommand}
                onSendCtrlC={serial.sendSerialCtrlC}
              />
            </motion.div>
          )}
        </AnimatePresence>
      </main>

      <footer className="mt-10 border-t border-border/60 bg-background/60 backdrop-blur-md">
        <div className="mx-auto flex max-w-5xl flex-col gap-1.5 px-4 py-4 text-[11px] text-muted-foreground sm:flex-row sm:items-center sm:justify-between sm:px-6">
          <div className="telemetry-mono">{t("footerAuthor")}</div>
          <div>{t("footerNotice")}</div>
        </div>
      </footer>
    </div>
  );
}

export default function App() {
  return (
    <ThemeProvider>
      <LocaleProvider>
        <TooltipProvider delayDuration={200}>
          <PortalWorkspace />
        </TooltipProvider>
      </LocaleProvider>
    </ThemeProvider>
  );
}
