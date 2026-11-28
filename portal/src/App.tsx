import { useCallback, useEffect, useMemo, useState } from "react";
import { motion } from "framer-motion";
import { ShieldCheck } from "lucide-react";
import { BrowserCompatibilityAlert } from "@/components/BrowserCompatibilityAlert";
import { CyberBackdrop } from "@/components/CyberBackdrop";
import { DeviceConnectionPanel } from "@/components/DeviceConnectionPanel";
import { DownloadModeGuide } from "@/components/DownloadModeGuide";
import {
  FirmwareSelectorPanel,
  type FirmwareSourceMode,
  type ManifestFlashLayout,
} from "@/components/FirmwareSelectorPanel";
import { FlashActionPanel } from "@/components/FlashActionPanel";
import { HardwarePreview } from "@/components/HardwarePreview";
import { SerialMonitorPanel } from "@/components/SerialMonitorPanel";
import { TopBar } from "@/components/TopBar";
import { Badge } from "@/components/ui/badge";
import { Separator } from "@/components/ui/separator";
import { TooltipProvider } from "@/components/ui/tooltip";
import { ThemeProvider } from "@/context/ThemeContext";
import { useManifest } from "@/hooks/useManifest";
import { useWebSerial } from "@/hooks/useWebSerial";
import { LocaleProvider, useLocale } from "@/i18n/LocaleContext";
import type { BinarySegmentInput } from "@/services/espFlasher";
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
  const { manifest, loading: manifestLoading, error: manifestError } = useManifest();

  const [sourceMode, setSourceMode] = useState<FirmwareSourceMode>("manifest");
  const [selectedVersion, setSelectedVersion] = useState<string>("");
  const [selectedBoardId, setSelectedBoardId] = useState<string>("lilygo-t-deck");
  const [flashLayout, setFlashLayout] = useState<ManifestFlashLayout>("merged");
  const [eraseAll, setEraseAll] = useState<boolean>(false);
  const [localParts, setLocalParts] = useState<CustomLocalPart[]>([]);
  const [monitorOpen, setMonitorOpen] = useState<boolean>(true);
  const [autoMatchedNotice, setAutoMatchedNotice] = useState<boolean>(false);

  const handleAutoDetectBoard = useCallback((boardId: string) => {
    setSelectedBoardId(boardId);
    setAutoMatchedNotice(true);
  }, []);

  const serial = useWebSerial({
    onAutoDetectBoard: handleAutoDetectBoard,
  });

  // Initialize release selection once manifest loads
  useEffect(() => {
    if (manifest && manifest.releases.length > 0 && !selectedVersion) {
      setSelectedVersion(manifest.releases[0].version);
    }
  }, [manifest, selectedVersion]);

  const selectedRelease = useMemo(() => {
    if (!manifest || manifest.releases.length === 0) return null;
    return (
      manifest.releases.find((r) => r.version === selectedVersion) ?? manifest.releases[0]
    );
  }, [manifest, selectedVersion]);

  const selectedBoard = useMemo(() => {
    if (!selectedRelease || selectedRelease.builds.length === 0) return null;
    return (
      selectedRelease.builds.find((b) => b.id === selectedBoardId) ?? selectedRelease.builds[0]
    );
  }, [selectedRelease, selectedBoardId]);

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
    setMonitorOpen(true);
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

  const handleOpenMonitorAfterFlash = async () => {
    setMonitorOpen(true);
    await serial.attachSerialMonitor();
  };

  const hasChipMismatch =
    serial.chipInfo !== null &&
    selectedBoard !== null &&
    serial.chipInfo.chipFamily !== "unknown" &&
    serial.chipInfo.chipFamily !== selectedBoard.chipFamily &&
    sourceMode === "manifest";

  const segmentCount =
    sourceMode === "manifest" ? activeManifestParts.length : localParts.length;
  const canFlash = serial.isConnected && segmentCount > 0 && !hasChipMismatch;

  const repository = manifest?.repository ?? "NezhaCyber/nezha_cyber_firmware";

  return (
    <div className="relative flex min-h-screen flex-col">
      <CyberBackdrop phase={serial.progress.phase} />

      <TopBar
        repository={repository}
        phase={serial.progress.phase}
        isConnected={serial.isConnected}
        detectedPortsCount={serial.knownPorts.length}
      />

      <main className="mx-auto w-full max-w-7xl flex-1 px-4 py-6 sm:px-6 lg:py-8">
        {/* Hero Telemetry Header */}
        <motion.section
          initial={{ opacity: 0, y: 12 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.35, ease: "easeOut" }}
          className="mb-6 flex flex-col gap-3 md:flex-row md:items-end md:justify-between"
        >
          <div className="max-w-2xl space-y-1.5">
            <div className="flex flex-wrap items-center gap-2">
              <Badge variant="crimson" className="telemetry-mono text-[10px]">
                NEZHA CYBER LAB
              </Badge>
              {serial.isSupported && (
                <Badge variant="cyan" className="telemetry-mono text-[10px]">
                  <ShieldCheck className="h-3 w-3" />
                  {t("browserSupportedBadge")}
                </Badge>
              )}
            </div>
            <h1 className="text-2xl font-bold tracking-tight text-foreground sm:text-3xl">
              {t("brandTitle")}{" "}
              <span className="text-muted-foreground font-normal">/ Web Serial Flasher</span>
            </h1>
            <p className="text-xs leading-relaxed text-muted-foreground sm:text-sm">
              {t("brandTagline")}
            </p>
          </div>
        </motion.section>

        <BrowserCompatibilityAlert isSupported={serial.isSupported} />

        {/* Main 12-Column Hardware Workstation Grid */}
        <div className="grid gap-5 lg:grid-cols-12">
          {/* Left Primary Workflow Column (Steps 01 -> 02 -> 03) */}
          <motion.div
            initial={{ opacity: 0, y: 14 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.4, delay: 0.05 }}
            className="space-y-5 lg:col-span-7"
          >
            <DeviceConnectionPanel
              isSupported={serial.isSupported}
              knownPorts={serial.knownPorts}
              selectedPort={serial.selectedPort}
              onSelectPort={serial.setSelectedPort}
              baudRate={serial.baudRate}
              onChangeBaudRate={serial.setBaudRate}
              chipInfo={serial.chipInfo}
              selectedBoard={selectedBoard}
              phase={serial.progress.phase}
              isBusy={serial.isBusy}
              onRequestPort={serial.requestAndConnectPort}
              onConnectPort={serial.connectPort}
              onDisconnect={serial.disconnectAll}
              onRefreshPorts={serial.refreshAuthorizedPorts}
            />

            <FirmwareSelectorPanel
              manifest={manifest}
              manifestLoading={manifestLoading}
              manifestError={manifestError}
              sourceMode={sourceMode}
              onChangeSourceMode={setSourceMode}
              selectedRelease={selectedRelease}
              onSelectReleaseVersion={setSelectedVersion}
              selectedBoard={selectedBoard}
              onSelectBoardId={(id) => {
                setSelectedBoardId(id);
                setAutoMatchedNotice(false);
              }}
              flashLayout={flashLayout}
              onChangeFlashLayout={setFlashLayout}
              localParts={localParts}
              onAddLocalFiles={handleAddLocalFiles}
              onUpdateLocalPartOffset={handleUpdateLocalPartOffset}
              onRemoveLocalPart={handleRemoveLocalPart}
              activeOffset={serial.progress.currentPartOffset}
              phase={serial.progress.phase}
              autoMatchedNotice={autoMatchedNotice}
              isBusy={serial.isBusy}
            />

            <FlashActionPanel
              isConnected={serial.isConnected}
              isBusy={serial.isBusy}
              canFlash={canFlash}
              eraseAll={eraseAll}
              onChangeEraseAll={setEraseAll}
              progress={serial.progress}
              errorMessage={serial.errorMessage}
              onStartFlash={handleStartFlash}
              onHardReset={serial.hardReset}
              onOpenMonitor={handleOpenMonitorAfterFlash}
              segmentCount={segmentCount}
            />
          </motion.div>

          {/* Right Instrument Column: Hardware Schematic & Download Mode Guide */}
          <motion.aside
            initial={{ opacity: 0, y: 14 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.4, delay: 0.12 }}
            className="space-y-5 lg:col-span-5"
          >
            <HardwarePreview
              board={selectedBoard}
              chipInfo={serial.chipInfo}
              phase={serial.progress.phase}
              percent={serial.progress.overallPercent}
            />
            <DownloadModeGuide board={selectedBoard} />
          </motion.aside>
        </div>

        {/* Full-Width Collapsible Live Serial Monitor (Step 04) */}
        <motion.section
          initial={{ opacity: 0, y: 14 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.4, delay: 0.18 }}
          className="mt-5"
        >
          <SerialMonitorPanel
            open={monitorOpen}
            onToggleOpen={() => setMonitorOpen((v) => !v)}
            isSupported={serial.isSupported}
            monitorConnected={serial.monitorConnected}
            isFlashingBusy={serial.isBusy}
            lines={serial.terminalLines}
            onAttach={serial.attachSerialMonitor}
            onDetach={serial.detachSerialMonitor}
            onClear={serial.clearTerminal}
            onSendCommand={serial.sendSerialCommand}
          />
        </motion.section>
      </main>

      <footer className="mt-8 border-t border-border/70 bg-background/60 backdrop-blur-md">
        <div className="mx-auto flex max-w-7xl flex-col gap-2 px-4 py-5 text-xs text-muted-foreground sm:flex-row sm:items-center sm:justify-between sm:px-6">
          <div className="telemetry-mono">{t("footerAuthor")}</div>
          <Separator orientation="vertical" className="hidden h-3 sm:block" />
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
