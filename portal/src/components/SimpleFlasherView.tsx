import React, { useRef, useState } from "react";
import { AnimatePresence, motion } from "framer-motion";
import {
  AlertCircle,
  AlertTriangle,
  Cable,
  Check,
  CheckCircle2,
  ChevronDown,
  ChevronUp,
  Cpu,
  Download,
  ExternalLink,
  Flame,
  FolderSync,
  FolderUp,
  HardDrive,
  HelpCircle,
  Loader2,
  PlugZap,
  Plus,
  RefreshCw,
  RotateCcw,
  SlidersHorizontal,
  Terminal,
  Trash2,
  Unplug,
  Usb,
} from "lucide-react";
import { FlashMemoryMap } from "@/components/FlashMemoryMap";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import type { ManifestSource } from "@/hooks/useManifest";
import { useLocale } from "@/i18n/LocaleContext";
import { cn, formatBytes, formatDuration } from "@/lib/utils";
import {
  checkBoardFlashCompatibility,
  computeFlashUsagePct,
  inferEdition,
  type BoardCompatibilityResult,
} from "@/services/githubReleases";
import type {
  BoardBuild,
  CustomLocalPart,
  DetectedChipInfo,
  FirmwareEdition,
  FirmwareManifest,
  FirmwareRelease,
  FlashProgressState,
  UsbPortInfoSummary,
} from "@/types/firmware";

export type FirmwareSourceMode = "manifest" | "local";
export type ManifestFlashLayout = "merged" | "partitioned";
export type ReleaseChannelFilter = "all" | "stable" | "beta";

interface SimpleFlasherViewProps {
  isSupported: boolean;
  manifest: FirmwareManifest | null;
  manifestLoading: boolean;
  syncingGitHub: boolean;
  manifestSource: ManifestSource;
  manifestError: string | null;
  onReloadManifest: () => void;
  channelFilter: ReleaseChannelFilter;
  onChangeChannelFilter: (filter: ReleaseChannelFilter) => void;
  filteredReleases: FirmwareRelease[];
  selectedRelease: FirmwareRelease | null;
  onSelectReleaseVersion: (version: string) => void;
  selectedBoard: BoardBuild | null;
  onSelectBoardId: (id: string) => void;
  boardCompat: BoardCompatibilityResult;
  suggestedCompatibleBoard: BoardBuild | null;
  sourceMode: FirmwareSourceMode;
  onChangeSourceMode: (mode: FirmwareSourceMode) => void;
  flashLayout: ManifestFlashLayout;
  onChangeFlashLayout: (layout: ManifestFlashLayout) => void;
  localParts: CustomLocalPart[];
  onAddLocalFiles: (files: FileList | File[]) => void;
  onUpdateLocalPartOffset: (id: string, offset: string) => void;
  onRemoveLocalPart: (id: string) => void;
  knownPorts: UsbPortInfoSummary[];
  selectedPort: UsbPortInfoSummary | null;
  baudRate: number;
  onChangeBaudRate: (baud: number) => void;
  chipInfo: DetectedChipInfo | null;
  progress: FlashProgressState;
  errorMessage: string | null;
  isConnected: boolean;
  isBusy: boolean;
  eraseAll: boolean;
  onChangeEraseAll: (v: boolean) => void;
  canFlash: boolean;
  segmentCount: number;
  onRequestPort: (unfiltered?: boolean) => void;
  onConnectPort: (summary: UsbPortInfoSummary) => void;
  onDisconnect: () => void;
  onStartFlash: () => void;
  onHardReset: () => void;
  onOpenUsbFiles: () => void;
  onOpenConsole: () => void;
}

const BAUD_OPTIONS = [921600, 460800, 230400, 115200];

export function SimpleFlasherView({
  isSupported,
  manifest,
  manifestLoading,
  syncingGitHub,
  manifestSource,
  manifestError,
  onReloadManifest,
  channelFilter,
  onChangeChannelFilter,
  filteredReleases,
  selectedRelease,
  onSelectReleaseVersion,
  selectedBoard,
  onSelectBoardId,
  boardCompat,
  suggestedCompatibleBoard,
  sourceMode,
  onChangeSourceMode,
  flashLayout,
  onChangeFlashLayout,
  localParts,
  onAddLocalFiles,
  onUpdateLocalPartOffset,
  onRemoveLocalPart,
  knownPorts,
  selectedPort,
  baudRate,
  onChangeBaudRate,
  chipInfo,
  progress,
  errorMessage,
  isConnected,
  isBusy,
  eraseAll,
  onChangeEraseAll,
  canFlash,
  segmentCount,
  onRequestPort,
  onConnectPort,
  onDisconnect,
  onStartFlash,
  onHardReset,
  onOpenUsbFiles,
  onOpenConsole,
}: SimpleFlasherViewProps) {
  const { locale, t } = useLocale();
  const [showMoreBoards, setShowMoreBoards] = useState(false);
  const [showBootGuide, setShowBootGuide] = useState(false);
  const [showAdvanced, setShowAdvanced] = useState(false);
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  const allBuilds = selectedRelease?.builds ?? [];
  const featuredBuilds = allBuilds.filter((b) => b.featured);
  const secondaryBuilds = allBuilds.filter((b) => !b.featured);
  const visibleBuilds = showMoreBoards
    ? allBuilds
    : featuredBuilds.length > 0
      ? featuredBuilds
      : allBuilds.slice(0, 4);

  const betaCount = (manifest?.releases ?? []).filter((r) => r.channel === "beta").length;
  const stableCount = (manifest?.releases ?? []).filter((r) => r.channel !== "beta").length;

  const editionLabel = (ed: FirmwareEdition): string => {
    switch (ed) {
      case "lite":
        return t("editionLite");
      case "compact":
        return t("editionCompact");
      case "standard":
        return t("editionStandard");
      default:
        return t("editionFull");
    }
  };

  // If user or USB auto-detect selected a secondary board, reveal it cleanly
  const selectedIsSecondary =
    selectedBoard !== null && secondaryBuilds.some((b) => b.id === selectedBoard.id);

  const isFlashing =
    progress.phase === "downloading" ||
    progress.phase === "erasing" ||
    progress.phase === "flashing" ||
    progress.phase === "verifying";
  const isDone = progress.phase === "done";
  const isConnecting = progress.phase === "connecting";

  const hasChipMismatch = boardCompat.chipMismatch;
  const hasFlashOverflow = boardCompat.flashOverflow;

  const step1Done = selectedBoard !== null;
  const step2Done = isConnected || isDone;
  const step3Done = isDone;

  const statusLabel: Record<FlashProgressState["phase"], string> = {
    idle: t("statusIdle"),
    connecting: t("connectingBtn"),
    connected: t("statusConnected"),
    downloading: t("statusDownloading"),
    erasing: t("statusErasing"),
    flashing: t("statusFlashing"),
    verifying: t("statusVerifying"),
    resetting: t("statusResetting"),
    done: t("statusDone"),
    error: t("statusError"),
  };

  const handleDrop = (e: React.DragEvent<HTMLDivElement>) => {
    e.preventDefault();
    if (isBusy) return;
    if (e.dataTransfer.files?.length) {
      onAddLocalFiles(e.dataTransfer.files);
    }
  };

  return (
    <div className="mx-auto max-w-3xl space-y-6">
      {/* Compact Guided 3-Step Indicator */}
      <div className="flex items-center justify-center gap-2 sm:gap-4">
        {[
          { num: 1, label: t("guideStep1"), done: step1Done, active: !step2Done },
          { num: 2, label: t("guideStep2"), done: step2Done, active: step1Done && !step2Done },
          { num: 3, label: t("guideStep3"), done: step3Done, active: step2Done && !step3Done },
        ].map((step, idx) => (
          <React.Fragment key={step.num}>
            <div className="flex items-center gap-2">
              <span
                className={cn(
                  "telemetry-mono flex h-6 w-6 items-center justify-center rounded-full text-[11px] font-bold transition-colors",
                  step.done
                    ? "bg-cyber-emerald text-white"
                    : step.active
                      ? "bg-cyber-crimson text-white shadow-[0_0_16px_-3px_hsl(var(--cyber-crimson))]"
                      : "border border-border bg-muted text-muted-foreground",
                )}
              >
                {step.done ? <Check className="h-3.5 w-3.5" /> : step.num}
              </span>
              <span
                className={cn(
                  "text-xs font-medium",
                  step.done || step.active ? "text-foreground" : "text-muted-foreground",
                )}
              >
                {step.label}
              </span>
            </div>
            {idx < 2 && <div className="h-px w-6 bg-border sm:w-12" />}
          </React.Fragment>
        ))}
      </div>

      {/* STEP 1: Release Version + Visual Board Picker */}
      <section className="space-y-3.5">
        {/* Live GitHub Release Version & Channel Selector */}
        <div className="rounded-xl border border-border/80 bg-card/75 p-3.5">
          <div className="flex flex-wrap items-center justify-between gap-2">
            <div className="flex flex-wrap items-center gap-2">
              <span className="text-xs font-semibold text-foreground">
                {t("releaseVersionLabel")}
              </span>
              <Badge
                variant={manifestSource === "github" ? "cyan" : "secondary"}
                className="telemetry-mono text-[10px]"
              >
                {syncingGitHub ? (
                  <Loader2 className="mr-1 h-2.5 w-2.5 animate-spin" />
                ) : null}
                {manifestSource === "github"
                  ? t("releaseSourceGithub")
                  : t("releaseSourceBundled")}
              </Badge>
            </div>

            <div className="flex items-center gap-1.5">
              {/* Channel Filter Pills: All / Stable / Beta */}
              <div className="inline-flex rounded-lg border border-border/80 bg-muted/40 p-0.5 text-[11px]">
                {(
                  [
                    { key: "all", label: t("channelAll"), show: true },
                    { key: "stable", label: `${t("channelStable")}${stableCount > 0 ? ` (${stableCount})` : ""}`, show: true },
                    { key: "beta", label: `${t("channelBeta")}${betaCount > 0 ? ` (${betaCount})` : ""}`, show: betaCount > 0 },
                  ] as const
                )
                  .filter((c) => c.show)
                  .map((c) => (
                    <button
                      key={c.key}
                      type="button"
                      disabled={isBusy}
                      onClick={() => onChangeChannelFilter(c.key)}
                      className={cn(
                        "rounded-md px-2 py-0.5 font-medium transition-colors",
                        channelFilter === c.key
                          ? "bg-card text-foreground shadow-sm"
                          : "text-muted-foreground hover:text-foreground",
                      )}
                    >
                      {c.label}
                    </button>
                  ))}
              </div>

              <Button
                type="button"
                variant="ghost"
                size="sm"
                disabled={isBusy || syncingGitHub}
                onClick={onReloadManifest}
                title={t("releaseRefreshBtn")}
                className="h-7 w-7 p-0 text-muted-foreground hover:text-foreground"
              >
                <RefreshCw className={cn("h-3.5 w-3.5", syncingGitHub && "animate-spin")} />
              </Button>
            </div>
          </div>

          <div className="mt-2.5 flex flex-col gap-2 sm:flex-row sm:items-center">
            <div className="min-w-0 flex-1">
              <Select
                value={selectedRelease?.version ?? ""}
                onValueChange={onSelectReleaseVersion}
                disabled={isBusy || filteredReleases.length === 0}
              >
                <SelectTrigger className="telemetry-mono h-9 text-xs">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  {filteredReleases.map((rel) => (
                    <SelectItem key={rel.version} value={rel.version}>
                      {rel.version} ·{" "}
                      {rel.channel === "stable"
                        ? t("channelStable")
                        : rel.channel === "beta"
                          ? t("channelBeta")
                          : t("channelArchive")}
                      {rel.releaseDate ? ` (${rel.releaseDate})` : ""}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            {selectedRelease?.releaseUrl && (
              <a
                href={selectedRelease.releaseUrl}
                target="_blank"
                rel="noreferrer"
                className="inline-flex h-9 shrink-0 items-center justify-center gap-1.5 rounded-md border border-border/80 bg-background/60 px-3 text-xs font-medium text-muted-foreground transition-colors hover:border-foreground/30 hover:text-foreground"
              >
                <span>{t("releaseViewGithub")}</span>
                <ExternalLink className="h-3.5 w-3.5" />
              </a>
            )}
          </div>

          {selectedRelease && (
            <p className="mt-2 text-[11px] leading-relaxed text-muted-foreground">
              {locale === "zh" ? selectedRelease.notesZh : selectedRelease.notesEn}
            </p>
          )}
        </div>

        <div className="flex flex-wrap items-end justify-between gap-2">
          <div>
            <h2 className="text-base font-semibold tracking-tight text-foreground">
              {t("chooseBoardTitle")}
            </h2>
            <p className="text-xs text-muted-foreground">{t("chooseBoardHint")}</p>
          </div>
          {selectedBoard?.downloadUrl && (
            <a
              href={selectedBoard.downloadUrl}
              target="_blank"
              rel="noreferrer"
              className="inline-flex items-center gap-1 text-[11px] font-medium text-cyber-cyan hover:underline"
            >
              <Download className="h-3 w-3" />
              <span>{t("downloadBinBtn")}</span>
            </a>
          )}
        </div>

        {manifestError && (
          <Alert variant="destructive">
            <AlertCircle className="h-4 w-4" />
            <AlertDescription>{manifestError}</AlertDescription>
          </Alert>
        )}

        <div className="grid gap-3 sm:grid-cols-2">
          {(selectedIsSecondary && !showMoreBoards
            ? [...visibleBuilds, selectedBoard]
            : visibleBuilds
          ).map((board) => {
            const active = selectedBoard?.id === board.id;
            const autoMatched =
              selectedPort !== null && selectedPort.suggestedBoardId === board.id;
            const edition = inferEdition(board);
            const usagePct = computeFlashUsagePct(board);
            const compat = checkBoardFlashCompatibility(board, chipInfo);
            const binSize =
              board.binarySizeBytes ?? board.flashModes.merged.parts[0]?.sizeBytes ?? null;
            const appLimit = board.maxAppBytes ?? null;

            return (
              <motion.button
                key={board.id}
                type="button"
                whileHover={{ y: -2 }}
                whileTap={{ scale: 0.99 }}
                disabled={isBusy || manifestLoading}
                onClick={() => onSelectBoardId(board.id)}
                className={cn(
                  "group relative flex flex-col items-start rounded-xl border p-4 text-left transition-all",
                  active
                    ? "border-cyber-crimson/80 bg-card shadow-[0_0_28px_-12px_hsl(var(--cyber-crimson)/0.55)] ring-1 ring-cyber-crimson/35"
                    : "border-border/80 bg-card/70 hover:border-foreground/25 hover:bg-card",
                  chipInfo && !compat.compatible && "opacity-60",
                )}
              >
                <div className="flex w-full items-start justify-between gap-2">
                  <div className="flex items-center gap-2.5">
                    <div
                      className={cn(
                        "flex h-9 w-9 shrink-0 items-center justify-center rounded-lg border transition-colors",
                        active
                          ? "border-cyber-crimson/40 bg-cyber-crimson/12 text-cyber-crimson"
                          : "border-border bg-muted/60 text-muted-foreground",
                      )}
                    >
                      <Cpu className="h-4 w-4" />
                    </div>
                    <div>
                      <div className="text-sm font-semibold text-foreground">
                        {locale === "zh" ? board.boardNameZh : board.boardName}
                      </div>
                      <div className="telemetry-mono text-[11px] text-muted-foreground">
                        {board.mcu} · {board.flashSize}
                        {board.psram && board.psram.toLowerCase() !== "none"
                          ? ` · ${board.psram}`
                          : ""}
                      </div>
                    </div>
                  </div>

                  <div className="flex flex-col items-end gap-1">
                    {chipInfo && compat.flashOverflow ? (
                      <Badge variant="amber" className="text-[10px]">
                        {t("incompatibleFlashBadge")}
                      </Badge>
                    ) : autoMatched ? (
                      <Badge variant="cyan" className="text-[10px]">
                        <Usb className="h-3 w-3" />
                        {t("usbDetectedBadge")}
                      </Badge>
                    ) : (
                      <Badge
                        variant={
                          edition === "lite" || edition === "compact"
                            ? "amber"
                            : active
                              ? "crimson"
                              : "secondary"
                        }
                        className="telemetry-mono text-[10px]"
                      >
                        {editionLabel(edition)}
                      </Badge>
                    )}
                  </div>
                </div>

                <div className="mt-2.5 text-[11px] text-muted-foreground">{board.display}</div>

                {/* Firmware Memory Footprint & Partition Ceiling */}
                <div className="mt-2.5 w-full space-y-1 border-t border-border/60 pt-2">
                  <div className="telemetry-mono flex items-center justify-between text-[10px] text-muted-foreground">
                    <span className="inline-flex items-center gap-1">
                      <HardDrive className="h-3 w-3" />
                      {binSize
                        ? `${t("firmwareSizeLabel")}: ${formatBytes(binSize)}`
                        : `${t("memoryLimitLabel")}: ${appLimit ? formatBytes(appLimit) : board.flashSize}`}
                    </span>
                    <span>
                      {usagePct !== null
                        ? `${usagePct.toFixed(1)}%${appLimit ? ` / ${formatBytes(appLimit)}` : ""}`
                        : board.flashSize}
                    </span>
                  </div>
                  {usagePct !== null && (
                    <div className="h-1.5 w-full overflow-hidden rounded-full bg-muted">
                      <div
                        className={cn(
                          "h-full rounded-full transition-all",
                          usagePct > 95
                            ? "bg-destructive"
                            : usagePct > 85
                              ? "bg-cyber-amber"
                              : "bg-cyber-emerald",
                        )}
                        style={{ width: `${Math.min(100, Math.max(4, usagePct))}%` }}
                      />
                    </div>
                  )}
                </div>
              </motion.button>
            );
          })}
        </div>

        {secondaryBuilds.length > 0 && (
          <div className="flex justify-center">
            <button
              type="button"
              onClick={() => setShowMoreBoards((v) => !v)}
              className="inline-flex items-center gap-1 rounded-full border border-border/75 bg-muted/40 px-3 py-1 text-[11px] font-medium text-muted-foreground transition-colors hover:border-border hover:text-foreground"
            >
              <span>
                {showMoreBoards
                  ? t("moreBoardsHide")
                  : `${t("moreBoardsShow")} (${secondaryBuilds.length})`}
              </span>
              {showMoreBoards ? (
                <ChevronUp className="h-3.5 w-3.5" />
              ) : (
                <ChevronDown className="h-3.5 w-3.5" />
              )}
            </button>
          </div>
        )}
      </section>

      {/* STEP 2 & 3: Focused Single Action Card */}
      <Card className="overflow-hidden border-border/90">
        <CardContent className="space-y-5 p-6">
          <AnimatePresence mode="wait">
            {/* STATE A: NOT CONNECTED */}
            {!isConnected && !isDone && (
              <motion.div
                key="state-connect"
                initial={{ opacity: 0, y: 8 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -6 }}
                className="space-y-5"
              >
                <div className="flex flex-col items-center text-center">
                  <div className="mb-3 flex h-14 w-14 items-center justify-center rounded-2xl border border-cyber-crimson/35 bg-cyber-crimson/10 text-cyber-crimson">
                    {isConnecting ? (
                      <Loader2 className="h-6 w-6 animate-spin" />
                    ) : (
                      <Cable className="h-6 w-6" />
                    )}
                  </div>
                  <h3 className="text-lg font-semibold text-foreground">{t("connectTitle")}</h3>
                  <p className="mt-1 max-w-md text-xs leading-relaxed text-muted-foreground">
                    {t("connectSubtitle")}
                  </p>
                </div>

                {/* Auto-detected port quick bar */}
                {selectedPort && (
                  <div className="flex items-center justify-between rounded-lg border border-cyber-cyan/40 bg-cyber-cyan/8 px-3.5 py-2.5 text-xs">
                    <div className="flex min-w-0 items-center gap-2">
                      <span className="h-2 w-2 shrink-0 rounded-full bg-cyber-cyan animate-pulse" />
                      <span className="truncate font-medium text-foreground">
                        {locale === "zh" ? selectedPort.deviceLabelZh : selectedPort.deviceLabel}
                      </span>
                    </div>
                    <span className="telemetry-mono shrink-0 text-[10px] text-muted-foreground">
                      {selectedPort.vendorHex}:{selectedPort.productHex}
                    </span>
                  </div>
                )}

                <div className="flex flex-col items-center gap-2.5 sm:flex-row sm:justify-center">
                  {selectedPort ? (
                    <>
                      <Button
                        variant="cyber"
                        size="lg"
                        disabled={!isSupported || isBusy}
                        onClick={() => onConnectPort(selectedPort)}
                        className="w-full sm:w-auto sm:min-w-[240px]"
                      >
                        {isConnecting ? (
                          <Loader2 className="h-4 w-4 animate-spin" />
                        ) : (
                          <PlugZap className="h-4 w-4" />
                        )}
                        {isConnecting ? t("connectingBtn") : t("connectAutoBtn")}
                      </Button>
                      <Button
                        variant="outline"
                        size="lg"
                        disabled={!isSupported || isBusy}
                        onClick={() => onRequestPort(false)}
                        className="w-full sm:w-auto"
                      >
                        {t("chooseAnotherPort")}
                      </Button>
                    </>
                  ) : (
                    <Button
                      variant="cyber"
                      size="lg"
                      disabled={!isSupported || isBusy}
                      onClick={() => onRequestPort(false)}
                      className="w-full sm:w-auto sm:min-w-[260px]"
                    >
                      {isConnecting ? (
                        <Loader2 className="h-4 w-4 animate-spin" />
                      ) : (
                        <Usb className="h-4 w-4" />
                      )}
                      {isConnecting ? t("connectingBtn") : t("connectBtn")}
                    </Button>
                  )}
                </div>

                {/* Beginner Download Mode Helper (collapsible so screen stays calm) */}
                <div className="pt-1 text-center">
                  <button
                    type="button"
                    onClick={() => setShowBootGuide((v) => !v)}
                    className="inline-flex items-center gap-1.5 text-xs font-medium text-muted-foreground transition-colors hover:text-cyber-amber"
                  >
                    <HelpCircle className="h-3.5 w-3.5" />
                    <span>{t("bootModeHelpToggle")}</span>
                    {showBootGuide ? (
                      <ChevronUp className="h-3.5 w-3.5" />
                    ) : (
                      <ChevronDown className="h-3.5 w-3.5" />
                    )}
                  </button>
                </div>

                <AnimatePresence initial={false}>
                  {(showBootGuide || Boolean(errorMessage)) && (
                    <motion.div
                      initial={{ height: 0, opacity: 0 }}
                      animate={{ height: "auto", opacity: 1 }}
                      exit={{ height: 0, opacity: 0 }}
                      className="overflow-hidden"
                    >
                      <div className="rounded-lg border border-border/80 bg-muted/35 p-4 space-y-3">
                        <div className="flex items-center justify-between">
                          <span className="text-xs font-semibold text-foreground">
                            {t("downloadGuideTitle")}
                          </span>
                          <Badge variant="amber" className="telemetry-mono text-[10px]">
                            GPIO0
                          </Badge>
                        </div>

                        {selectedBoard && (
                          <div className="flex flex-col items-center gap-3 rounded-md border border-cyber-crimson/35 bg-cyber-crimson/8 px-3.5 py-3 sm:flex-row">
                            <svg
                              viewBox="0 0 112 68"
                              className="h-14 w-24 shrink-0"
                              fill="none"
                              aria-hidden="true"
                            >
                              <rect
                                x="2"
                                y="4"
                                width="108"
                                height="60"
                                rx="9"
                                className="fill-card stroke-border"
                                strokeWidth="1.5"
                              />
                              <rect
                                x="10"
                                y="11"
                                width="62"
                                height="30"
                                rx="4"
                                className="fill-[#07090f] stroke-border"
                                strokeWidth="1"
                              />
                              <rect
                                x="10"
                                y="45"
                                width="92"
                                height="12"
                                rx="3"
                                className="fill-secondary/90 stroke-border"
                                strokeWidth="1"
                              />
                              <motion.circle
                                cx="90"
                                cy="26"
                                r="11"
                                className="stroke-cyber-crimson"
                                strokeWidth="1.8"
                                strokeDasharray="3 2"
                                animate={{ opacity: [0.35, 1, 0.35], scale: [1, 1.12, 1] }}
                                transition={{ duration: 1.6, repeat: Infinity, ease: "easeInOut" }}
                                style={{ transformOrigin: "90px 26px" }}
                              />
                              <circle
                                cx="90"
                                cy="26"
                                r="6"
                                className="fill-cyber-crimson"
                              />
                            </svg>
                            <div className="text-xs leading-relaxed text-foreground">
                              {locale === "zh"
                                ? selectedBoard.downloadModeHintZh
                                : selectedBoard.downloadModeHintEn}
                            </div>
                          </div>
                        )}

                        <div className="grid gap-2.5 sm:grid-cols-3">
                          {[
                            { title: t("downloadStep1Title"), desc: t("downloadStep1Desc") },
                            { title: t("downloadStep2Title"), desc: t("downloadStep2Desc") },
                            { title: t("downloadStep3Title"), desc: t("downloadStep3Desc") },
                          ].map((s) => (
                            <div
                              key={s.title}
                              className="rounded-md border border-border/70 bg-background/60 p-2.5"
                            >
                              <div className="text-xs font-semibold text-foreground">{s.title}</div>
                              <p className="mt-1 text-[11px] leading-relaxed text-muted-foreground">
                                {s.desc}
                              </p>
                            </div>
                          ))}
                        </div>
                      </div>
                    </motion.div>
                  )}
                </AnimatePresence>
              </motion.div>
            )}

            {/* STATE B: CONNECTED / FLASHING */}
            {isConnected && !isDone && (
              <motion.div
                key="state-ready"
                initial={{ opacity: 0, y: 8 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -6 }}
                className="space-y-5"
              >
                {/* Clean Connected Status Strip */}
                <div className="flex flex-wrap items-center justify-between gap-2 rounded-lg border border-cyber-emerald/40 bg-cyber-emerald/8 px-4 py-3">
                  <div className="flex items-center gap-2.5">
                    <CheckCircle2 className="h-4 w-4 text-cyber-emerald" />
                    <div>
                      <div className="text-xs font-semibold text-foreground">
                        {t("connectedReady")}
                      </div>
                      <div className="telemetry-mono text-[11px] text-muted-foreground">
                        {chipInfo?.chipDescription}
                        {chipInfo && chipInfo.flashSizeMB > 0 ? ` · ${chipInfo.flashSizeMB} MB` : ""}
                        {chipInfo ? ` · ${chipInfo.macAddress}` : ""}
                      </div>
                    </div>
                  </div>
                  <Button
                    variant="ghost"
                    size="sm"
                    disabled={isBusy}
                    onClick={onDisconnect}
                    className="h-7 gap-1 text-xs text-muted-foreground hover:text-destructive"
                  >
                    <Unplug className="h-3.5 w-3.5" />
                    {t("disconnectBtn")}
                  </Button>
                </div>

                {hasChipMismatch && (
                  <Alert variant="warning">
                    <AlertTriangle className="h-4 w-4" />
                    <AlertTitle className="text-xs">{t("chipMismatchTitle")}</AlertTitle>
                    <AlertDescription className="space-y-2">
                      <div>{t("chipMismatchDesc")}</div>
                      {suggestedCompatibleBoard && (
                        <Button
                          type="button"
                          variant="outline"
                          size="sm"
                          onClick={() => onSelectBoardId(suggestedCompatibleBoard.id)}
                          className="h-7 text-xs"
                        >
                          {t("switchCompatibleBtn")}:{" "}
                          {locale === "zh"
                            ? suggestedCompatibleBoard.boardNameZh
                            : suggestedCompatibleBoard.boardName}
                        </Button>
                      )}
                    </AlertDescription>
                  </Alert>
                )}

                {!hasChipMismatch && hasFlashOverflow && (
                  <Alert variant="destructive">
                    <AlertTriangle className="h-4 w-4" />
                    <AlertTitle className="text-xs">
                      {t("flashOverflowTitle")} ({boardCompat.requiredFlashMB} MB &gt;{" "}
                      {boardCompat.detectedFlashMB} MB)
                    </AlertTitle>
                    <AlertDescription className="space-y-2">
                      <div>{t("flashOverflowDesc")}</div>
                      {suggestedCompatibleBoard && (
                        <Button
                          type="button"
                          variant="outline"
                          size="sm"
                          onClick={() => onSelectBoardId(suggestedCompatibleBoard.id)}
                          className="h-7 text-xs"
                        >
                          {t("switchCompatibleBtn")}:{" "}
                          {locale === "zh"
                            ? suggestedCompatibleBoard.boardNameZh
                            : suggestedCompatibleBoard.boardName}
                        </Button>
                      )}
                    </AlertDescription>
                  </Alert>
                )}

                {/* Simple Clean Install Toggle (hidden while actively flashing) */}
                {!isFlashing && (
                  <div className="flex items-center justify-between gap-4 rounded-lg border border-border/80 bg-muted/30 px-4 py-3">
                    <div>
                      <label
                        htmlFor="simple-erase-toggle"
                        className="cursor-pointer text-xs font-semibold text-foreground"
                      >
                        {t("eraseFlashLabel")}
                      </label>
                      <p className="mt-0.5 text-[11px] text-muted-foreground">
                        {t("eraseFlashDesc")}
                      </p>
                    </div>
                    <Switch
                      id="simple-erase-toggle"
                      checked={eraseAll}
                      disabled={isBusy}
                      onCheckedChange={onChangeEraseAll}
                    />
                  </div>
                )}

                {/* Live Futuristic Progress Display when Flashing */}
                {isFlashing && (
                  <div className="space-y-3 rounded-xl border border-cyber-cyan/40 bg-muted/30 p-5">
                    <div className="flex items-baseline justify-between gap-2">
                      <div>
                        <div className="text-sm font-semibold text-foreground">
                          {statusLabel[progress.phase]}
                        </div>
                        <div className="telemetry-mono mt-0.5 text-[11px] text-muted-foreground">
                          {progress.currentPartName || selectedBoard?.boardName}
                        </div>
                      </div>
                      <span className="telemetry-mono text-3xl font-bold text-cyber-cyan">
                        {progress.overallPercent.toFixed(0)}%
                      </span>
                    </div>

                    <Progress
                      value={progress.overallPercent}
                      active={true}
                      tone={progress.phase === "erasing" ? "amber" : "cyan"}
                      className="h-3"
                    />

                    <div className="flex flex-wrap items-center justify-between gap-2 text-[11px] text-muted-foreground telemetry-mono">
                      <span>
                        {t("progressWritten")}: {formatBytes(progress.writtenBytes)} /{" "}
                        {formatBytes(progress.totalBytes)}
                      </span>
                      <span>
                        {progress.throughputBps > 0
                          ? `${formatBytes(progress.throughputBps)}/s`
                          : "--"}{" "}
                        · {t("progressEta")}: {formatDuration(progress.etaSeconds)}
                      </span>
                    </div>
                  </div>
                )}

                {/* Big Clear Install CTA */}
                <Button
                  variant="cyber"
                  size="lg"
                  disabled={!canFlash || isBusy}
                  onClick={onStartFlash}
                  className="h-12 w-full text-sm font-semibold"
                >
                  {isFlashing ? (
                    <Loader2 className="h-4 w-4 animate-spin" />
                  ) : (
                    <Flame className="h-4 w-4" />
                  )}
                  <span>
                    {isFlashing
                      ? t("flashRunningBtn")
                      : `${t("flashStartBtn")} ${
                          sourceMode === "manifest" && selectedRelease
                            ? selectedRelease.version
                            : `(${segmentCount} .bin)`
                        }`}
                  </span>
                </Button>
              </motion.div>
            )}

            {/* STATE C: COMPLETE */}
            {isDone && (
              <motion.div
                key="state-done"
                initial={{ opacity: 0, scale: 0.98 }}
                animate={{ opacity: 1, scale: 1 }}
                exit={{ opacity: 0 }}
                className="space-y-5 text-center"
              >
                <div className="mx-auto flex h-14 w-14 items-center justify-center rounded-2xl border border-cyber-emerald/45 bg-cyber-emerald/15 text-cyber-emerald">
                  <CheckCircle2 className="h-7 w-7" />
                </div>
                <div>
                  <h3 className="text-lg font-bold text-foreground">{t("statusDone")}</h3>
                  <p className="mt-1 text-xs text-muted-foreground">
                    {selectedBoard
                      ? locale === "zh"
                        ? selectedBoard.boardNameZh
                        : selectedBoard.boardName
                      : "ESP32-S3"}{" "}
                    · {selectedRelease?.version}
                  </p>
                </div>

                <div className="flex flex-wrap items-center justify-center gap-2.5">
                  <Button variant="cyber" size="lg" onClick={onHardReset}>
                    <RotateCcw className="h-4 w-4" />
                    {t("hardResetBtn")}
                  </Button>
                  <Button variant="outline" size="lg" onClick={onOpenUsbFiles}>
                    <FolderSync className="h-4 w-4 text-cyber-cyan" />
                    {t("goToUsbFilesBtn")}
                  </Button>
                  <Button variant="ghost" size="lg" onClick={onOpenConsole}>
                    <Terminal className="h-4 w-4" />
                    {t("openMonitorAfterBtn")}
                  </Button>
                </div>
              </motion.div>
            )}
          </AnimatePresence>

          {/* Error Feedback */}
          {errorMessage && (
            <Alert variant="destructive">
              <AlertCircle className="h-4 w-4" />
              <AlertDescription className="space-y-1">
                <div className="telemetry-mono break-words">{errorMessage}</div>
                <div className="text-[11px] opacity-85">{t("missingArtifactHint")}</div>
              </AlertDescription>
            </Alert>
          )}
        </CardContent>
      </Card>

      {/* Progressive Disclosure: Collapsed Advanced Settings for Pros */}
      <Card className="overflow-hidden border-border/75">
        <button
          type="button"
          onClick={() => setShowAdvanced((v) => !v)}
          className="flex w-full items-center justify-between px-5 py-3.5 text-left transition-colors hover:bg-muted/35"
        >
          <div className="flex items-center gap-2.5">
            <SlidersHorizontal className="h-4 w-4 text-muted-foreground" />
            <div>
              <div className="text-xs font-semibold text-foreground">
                {t("advancedToggleTitle")}
              </div>
              <div className="text-[11px] text-muted-foreground">{t("advancedToggleDesc")}</div>
            </div>
          </div>
          {showAdvanced ? (
            <ChevronUp className="h-4 w-4 text-muted-foreground" />
          ) : (
            <ChevronDown className="h-4 w-4 text-muted-foreground" />
          )}
        </button>

        <AnimatePresence initial={false}>
          {showAdvanced && (
            <motion.div
              initial={{ height: 0, opacity: 0 }}
              animate={{ height: "auto", opacity: 1 }}
              exit={{ height: 0, opacity: 0 }}
              transition={{ duration: 0.22 }}
              className="overflow-hidden border-t border-border/80"
            >
              <div className="space-y-4 p-5">
                <div className="grid gap-3 sm:grid-cols-2">
                  {/* Release Selector */}
                  <div>
                    <label className="mb-1.5 block text-[11px] font-medium text-muted-foreground">
                      {t("releaseVersionLabel")}
                    </label>
                    <Select
                      value={selectedRelease?.version ?? ""}
                      onValueChange={onSelectReleaseVersion}
                      disabled={isBusy || !manifest}
                    >
                      <SelectTrigger className="telemetry-mono text-xs">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        {manifest?.releases.map((rel) => (
                          <SelectItem key={rel.version} value={rel.version}>
                            {rel.version} ·{" "}
                            {rel.channel === "stable"
                              ? t("channelStable")
                              : rel.channel === "beta"
                                ? t("channelBeta")
                                : t("channelArchive")}
                          </SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                  </div>

                  {/* Baud Rate Selector */}
                  <div>
                    <label className="mb-1.5 block text-[11px] font-medium text-muted-foreground">
                      {t("baudRateLabel")}
                    </label>
                    <Select
                      value={String(baudRate)}
                      onValueChange={(v) => onChangeBaudRate(Number(v))}
                      disabled={isBusy || isConnected}
                    >
                      <SelectTrigger className="telemetry-mono text-xs">
                        <SelectValue />
                      </SelectTrigger>
                      <SelectContent>
                        {BAUD_OPTIONS.map((b) => (
                          <SelectItem key={b} value={String(b)} className="telemetry-mono text-xs">
                            {b.toLocaleString()} bps
                          </SelectItem>
                        ))}
                      </SelectContent>
                    </Select>
                    <p className="mt-1 text-[10px] text-muted-foreground">{t("baudFastHint")}</p>
                  </div>
                </div>

                {/* Source Switcher: Manifest vs Local .bin */}
                <Tabs
                  value={sourceMode}
                  onValueChange={(v) => onChangeSourceMode(v as FirmwareSourceMode)}
                >
                  <TabsList className="grid w-full grid-cols-2">
                    <TabsTrigger value="manifest" disabled={isBusy}>
                      {t("sourceManifestTab")}
                    </TabsTrigger>
                    <TabsTrigger value="local" disabled={isBusy}>
                      {t("sourceLocalTab")}
                    </TabsTrigger>
                  </TabsList>

                  <TabsContent value="manifest" className="space-y-3">
                    {selectedBoard?.flashModes.partitioned && (
                      <div className="grid gap-2 sm:grid-cols-2">
                        <button
                          type="button"
                          disabled={isBusy}
                          onClick={() => onChangeFlashLayout("merged")}
                          className={cn(
                            "rounded-md border p-2.5 text-left text-xs transition-colors",
                            flashLayout === "merged"
                              ? "border-cyber-crimson/70 bg-cyber-crimson/8 font-semibold"
                              : "border-border bg-background/50",
                          )}
                        >
                          {locale === "zh"
                            ? selectedBoard.flashModes.merged.labelZh
                            : selectedBoard.flashModes.merged.labelEn}
                        </button>
                        <button
                          type="button"
                          disabled={isBusy}
                          onClick={() => onChangeFlashLayout("partitioned")}
                          className={cn(
                            "rounded-md border p-2.5 text-left text-xs transition-colors",
                            flashLayout === "partitioned"
                              ? "border-cyber-crimson/70 bg-cyber-crimson/8 font-semibold"
                              : "border-border bg-background/50",
                          )}
                        >
                          {locale === "zh"
                            ? selectedBoard.flashModes.partitioned.labelZh
                            : selectedBoard.flashModes.partitioned.labelEn}
                        </button>
                      </div>
                    )}

                    {selectedBoard && (
                      <div className="rounded-lg border border-border/80 bg-muted/25 p-3">
                        <div className="mb-2 flex items-center justify-between text-[10px] text-muted-foreground telemetry-mono">
                          <span>{t("partitionMapTitle")}</span>
                          <span>{selectedBoard.partitionTable}</span>
                        </div>
                        <FlashMemoryMap
                          partitions={selectedBoard.partitions}
                          activeOffset={progress.currentPartOffset}
                          phase={progress.phase}
                        />
                      </div>
                    )}
                  </TabsContent>

                  <TabsContent value="local" className="space-y-3">
                    <div
                      onDragOver={(e) => e.preventDefault()}
                      onDrop={handleDrop}
                      onClick={() => !isBusy && fileInputRef.current?.click()}
                      className="flex cursor-pointer flex-col items-center justify-center rounded-lg border border-dashed border-border bg-muted/25 px-4 py-5 text-center transition-colors hover:border-cyber-crimson/60"
                    >
                      <FolderUp className="mb-1.5 h-6 w-6 text-cyber-crimson" />
                      <div className="text-xs font-semibold text-foreground">
                        {t("localDropTitle")}
                      </div>
                      <p className="mt-1 text-[11px] text-muted-foreground">
                        {t("localDropHint")}
                      </p>
                      <input
                        ref={fileInputRef}
                        type="file"
                        accept=".bin,application/octet-stream"
                        multiple
                        className="hidden"
                        onChange={(e) => {
                          if (e.target.files?.length) {
                            onAddLocalFiles(e.target.files);
                            e.target.value = "";
                          }
                        }}
                      />
                    </div>

                    {localParts.map((part) => (
                      <div
                        key={part.id}
                        className="flex flex-wrap items-center justify-between gap-2 rounded-md border border-border/80 bg-background/60 p-2.5"
                      >
                        <div className="min-w-0 flex-1">
                          <div className="truncate text-xs font-medium">{part.file.name}</div>
                          <div className="telemetry-mono text-[10px] text-muted-foreground">
                            {formatBytes(part.file.size)}
                          </div>
                        </div>
                        <div className="flex items-center gap-1.5">
                          <span className="telemetry-mono text-[10px] text-muted-foreground">
                            {t("localOffsetLabel")}
                          </span>
                          <Input
                            value={part.offset}
                            disabled={isBusy}
                            onChange={(e) => onUpdateLocalPartOffset(part.id, e.target.value)}
                            className="telemetry-mono h-7 w-24 px-2 text-xs"
                          />
                          <Button
                            type="button"
                            variant="ghost"
                            size="icon"
                            disabled={isBusy}
                            onClick={() => onRemoveLocalPart(part.id)}
                            className="h-7 w-7 text-muted-foreground hover:text-destructive"
                          >
                            <Trash2 className="h-3.5 w-3.5" />
                          </Button>
                        </div>
                      </div>
                    ))}

                    {localParts.length > 0 && (
                      <Button
                        type="button"
                        variant="outline"
                        size="sm"
                        disabled={isBusy}
                        onClick={() => fileInputRef.current?.click()}
                        className="h-7 text-[11px]"
                      >
                        <Plus className="h-3 w-3" />
                        {t("localAddPartBtn")}
                      </Button>
                    )}
                  </TabsContent>
                </Tabs>

                <div className="flex flex-wrap items-center justify-between gap-2 border-t border-border/70 pt-3 text-xs">
                  <span className="telemetry-mono text-[11px] text-muted-foreground">
                    USB Ports authorized: {knownPorts.length}
                  </span>
                  <Button
                    type="button"
                    variant="outline"
                    size="sm"
                    disabled={!isSupported || isBusy}
                    onClick={() => onRequestPort(true)}
                    className="h-7 text-[11px]"
                  >
                    {t("requestAllPortsBtn")}
                  </Button>
                </div>
              </div>
            </motion.div>
          )}
        </AnimatePresence>
      </Card>
    </div>
  );
}
