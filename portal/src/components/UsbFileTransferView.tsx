import React, { useRef, useState } from "react";
import { AnimatePresence, motion } from "framer-motion";
import {
  AlertCircle,
  ArrowLeft,
  ChevronRight,
  Download,
  Eye,
  FileCode2,
  FileText,
  Folder,
  FolderPlus,
  FolderSync,
  HardDrive,
  Info,
  Loader2,
  RefreshCw,
  Trash2,
  Unplug,
  Upload,
  Usb,
  X,
} from "lucide-react";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { useLocale } from "@/i18n/LocaleContext";
import { cn, formatBytes } from "@/lib/utils";
import type {
  DeviceFileEntry,
  DeviceStorageInfo,
  UsbTransferProgress,
} from "@/types/firmware";

interface UsbFileTransferViewProps {
  isSupported: boolean;
  connected: boolean;
  connecting: boolean;
  loadingList: boolean;
  currentPath: string;
  entries: DeviceFileEntry[];
  storageInfo: DeviceStorageInfo;
  deviceBanner: string;
  transfer: UsbTransferProgress;
  error: string | null;
  previewFile: { name: string; content: string } | null;
  onClosePreview: () => void;
  onConnect: (forceChooser?: boolean) => void;
  onDisconnect: () => void;
  onNavigate: (path: string) => void;
  onUploadFiles: (files: FileList | File[]) => void;
  onCancelTransfer: () => void;
  onCreateFolder: (name: string) => void;
  onDeleteEntry: (entry: DeviceFileEntry) => void;
  onDownloadOrPreview: (entry: DeviceFileEntry, triggerDownload: boolean) => void;
}

function getParentPath(path: string): string {
  if (!path || path === "/") return "/";
  const trimmed = path.endsWith("/") ? path.slice(0, -1) : path;
  const idx = trimmed.lastIndexOf("/");
  return idx <= 0 ? "/" : trimmed.slice(0, idx);
}

function isPreviewable(name: string): boolean {
  return /\.(txt|json|js|ir|sub|conf|log|csv|md|rfid|nfc|ibtn|key|cfg|py|sh|ini|html|css)$/i.test(
    name,
  );
}

export function UsbFileTransferView({
  isSupported,
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
  onClosePreview,
  onConnect,
  onDisconnect,
  onNavigate,
  onUploadFiles,
  onCancelTransfer,
  onCreateFolder,
  onDeleteEntry,
  onDownloadOrPreview,
}: UsbFileTransferViewProps) {
  const { t } = useLocale();
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const [dragOver, setDragOver] = useState(false);
  const [creatingFolder, setCreatingFolder] = useState(false);
  const [newFolderName, setNewFolderName] = useState("");
  const [confirmDeletePath, setConfirmDeletePath] = useState<string | null>(null);

  const breadcrumbs = currentPath
    .split("/")
    .filter(Boolean)
    .reduce<{ label: string; path: string }[]>((acc, segment) => {
      const prevPath = acc.length > 0 ? acc[acc.length - 1].path : "";
      acc.push({ label: segment, path: `${prevPath}/${segment}` });
      return acc;
    }, []);

  const handleDrop = (e: React.DragEvent<HTMLDivElement>) => {
    e.preventDefault();
    setDragOver(false);
    if (!connected || transfer.active) return;
    if (e.dataTransfer.files?.length) {
      onUploadFiles(e.dataTransfer.files);
    }
  };

  const handleCreateFolderSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!newFolderName.trim()) return;
    onCreateFolder(newFolderName.trim());
    setNewFolderName("");
    setCreatingFolder(false);
  };

  const sdPct =
    storageInfo.sd.available && storageInfo.sd.totalBytes > 0
      ? Math.min(100, (storageInfo.sd.usedBytes / storageInfo.sd.totalBytes) * 100)
      : 0;
  const lfsPct =
    storageInfo.littlefs.available && storageInfo.littlefs.totalBytes > 0
      ? Math.min(100, (storageInfo.littlefs.usedBytes / storageInfo.littlefs.totalBytes) * 100)
      : 0;

  return (
    <div className="mx-auto max-w-3xl space-y-5">
      {/* Header */}
      <div className="space-y-1">
        <h2 className="text-lg font-bold tracking-tight text-foreground">{t("usbFilesTitle")}</h2>
        <p className="text-xs leading-relaxed text-muted-foreground">{t("usbFilesSubtitle")}</p>
      </div>

      {!connected ? (
        /* Clean Beginner Onboarding Card for USB File Mode */
        <Card className="overflow-hidden">
          <CardContent className="space-y-5 p-6 text-center">
            <div className="mx-auto flex h-16 w-16 items-center justify-center rounded-2xl border border-cyber-cyan/40 bg-cyber-cyan/10 text-cyber-cyan">
              {connecting ? (
                <Loader2 className="h-7 w-7 animate-spin" />
              ) : (
                <FolderSync className="h-7 w-7" />
              )}
            </div>

            <div className="mx-auto max-w-md space-y-1.5">
              <h3 className="text-base font-semibold text-foreground">
                {t("usbFilesConnectTitle")}
              </h3>
              <p className="text-xs leading-relaxed text-muted-foreground">
                {t("usbFilesConnectDesc")}
              </p>
            </div>

            <div className="flex flex-col items-center justify-center gap-2.5 sm:flex-row">
              <Button
                variant="cyber"
                size="lg"
                disabled={!isSupported || connecting}
                onClick={() => onConnect(false)}
                className="w-full sm:w-auto sm:min-w-[240px]"
              >
                {connecting ? (
                  <Loader2 className="h-4 w-4 animate-spin" />
                ) : (
                  <Usb className="h-4 w-4" />
                )}
                {connecting ? t("usbFilesConnectingBtn") : t("usbFilesConnectBtn")}
              </Button>
              <Button
                variant="outline"
                size="lg"
                disabled={!isSupported || connecting}
                onClick={() => onConnect(true)}
                className="w-full sm:w-auto"
              >
                {t("usbFilesSwitchPortBtn")}
              </Button>
            </div>

            {error && (
              <Alert variant="destructive" className="text-left">
                <AlertCircle className="h-4 w-4" />
                <AlertDescription className="telemetry-mono">{error}</AlertDescription>
              </Alert>
            )}
          </CardContent>
        </Card>
      ) : (
        /* Connected USB File Manager */
        <div className="space-y-4">
          {/* Device & Storage Capacity Strip */}
          <Card>
            <CardContent className="space-y-3 p-4">
              <div className="flex flex-wrap items-center justify-between gap-2">
                <div className="flex items-center gap-2">
                  <span className="h-2 w-2 rounded-full bg-cyber-emerald animate-pulse" />
                  <span className="text-xs font-semibold text-foreground">
                    {deviceBanner || "Nezha Cyber USB"}
                  </span>
                  <Badge variant="cyan" className="telemetry-mono text-[10px]">
                    115200 USB
                  </Badge>
                </div>
                <Button
                  variant="ghost"
                  size="sm"
                  disabled={transfer.active}
                  onClick={onDisconnect}
                  className="h-7 gap-1 text-xs text-muted-foreground hover:text-destructive"
                >
                  <Unplug className="h-3.5 w-3.5" />
                  {t("disconnectBtn")}
                </Button>
              </div>

              <div className="grid gap-3 sm:grid-cols-2">
                {/* SD Card */}
                <div className="rounded-lg border border-border/75 bg-muted/30 p-3">
                  <div className="flex items-center justify-between text-[11px]">
                    <span className="flex items-center gap-1.5 font-medium text-foreground">
                      <HardDrive className="h-3.5 w-3.5 text-cyber-cyan" />
                      {t("usbFilesSdCard")}
                    </span>
                    <span className="telemetry-mono text-[10px] text-muted-foreground">
                      {storageInfo.sd.available
                        ? `${formatBytes(storageInfo.sd.freeBytes)} ${t("usbFilesFree")}`
                        : t("usbFilesNotMounted")}
                    </span>
                  </div>
                  <Progress value={sdPct} tone="cyan" className="mt-2 h-1.5" />
                </div>

                {/* LittleFS */}
                <div className="rounded-lg border border-border/75 bg-muted/30 p-3">
                  <div className="flex items-center justify-between text-[11px]">
                    <span className="flex items-center gap-1.5 font-medium text-foreground">
                      <HardDrive className="h-3.5 w-3.5 text-cyber-amber" />
                      {t("usbFilesLittleFs")}
                    </span>
                    <span className="telemetry-mono text-[10px] text-muted-foreground">
                      {storageInfo.littlefs.available
                        ? `${formatBytes(storageInfo.littlefs.freeBytes)} ${t("usbFilesFree")}`
                        : t("usbFilesNotMounted")}
                    </span>
                  </div>
                  <Progress value={lfsPct} tone="amber" className="mt-2 h-1.5" />
                </div>
              </div>
            </CardContent>
          </Card>

          {/* Drag & Drop Upload Zone */}
          <div
            onDragOver={(e) => {
              e.preventDefault();
              setDragOver(true);
            }}
            onDragLeave={() => setDragOver(false)}
            onDrop={handleDrop}
            onClick={() => !transfer.active && fileInputRef.current?.click()}
            className={cn(
              "cursor-pointer rounded-xl border-2 border-dashed p-5 text-center transition-all",
              dragOver
                ? "border-cyber-cyan bg-cyber-cyan/10"
                : "border-border/90 bg-card/65 hover:border-cyber-cyan/60 hover:bg-card",
              transfer.active && "pointer-events-none opacity-65",
            )}
          >
            <Upload className="mx-auto mb-2 h-6 w-6 text-cyber-cyan" />
            <div className="text-xs font-semibold text-foreground">{t("usbFilesDropTitle")}</div>
            <div className="telemetry-mono mt-1 inline-flex items-center gap-1 rounded bg-muted/70 px-2 py-0.5 text-[10px] text-cyber-cyan">
              → {currentPath}
            </div>
            <p className="mt-1.5 text-[11px] text-muted-foreground">{t("usbFilesDropHint")}</p>
            <input
              ref={fileInputRef}
              type="file"
              multiple
              className="hidden"
              onChange={(e) => {
                if (e.target.files?.length) {
                  onUploadFiles(e.target.files);
                  e.target.value = "";
                }
              }}
            />
          </div>

          {/* Active Transfer Progress Card */}
          <AnimatePresence>
            {transfer.active && (
              <motion.div
                initial={{ opacity: 0, y: -6 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -6 }}
              >
                <Card className="border-cyber-cyan/50 bg-cyber-cyan/5">
                  <CardContent className="space-y-2.5 p-4">
                    <div className="flex items-center justify-between gap-2">
                      <div className="min-w-0">
                        <div className="truncate text-xs font-semibold text-foreground">
                          {t("usbFilesTransferring")} {transfer.fileName}
                        </div>
                        <div className="telemetry-mono text-[10px] text-muted-foreground">
                          {formatBytes(transfer.transferredBytes)} / {formatBytes(transfer.totalBytes)}
                          {transfer.speedBps > 0 ? ` · ${formatBytes(transfer.speedBps)}/s` : ""}
                        </div>
                      </div>
                      <div className="flex items-center gap-2">
                        <span className="telemetry-mono text-sm font-bold text-cyber-cyan">
                          {transfer.percent.toFixed(0)}%
                        </span>
                        <Button
                          variant="ghost"
                          size="sm"
                          onClick={onCancelTransfer}
                          className="h-7 px-2 text-xs text-destructive"
                        >
                          {t("usbFilesCancelBtn")}
                        </Button>
                      </div>
                    </div>
                    <Progress value={transfer.percent} active={true} tone="cyan" />
                  </CardContent>
                </Card>
              </motion.div>
            )}
          </AnimatePresence>

          {error && (
            <Alert variant="destructive">
              <AlertCircle className="h-4 w-4" />
              <AlertDescription className="telemetry-mono">{error}</AlertDescription>
            </Alert>
          )}

          {/* Explorer Toolbar + File List */}
          <Card className="overflow-hidden">
            <div className="flex flex-wrap items-center justify-between gap-2 border-b border-border/80 bg-muted/30 px-4 py-2.5">
              {/* Breadcrumb path */}
              <div className="flex flex-wrap items-center gap-1 text-xs">
                {currentPath !== "/" && (
                  <Button
                    variant="ghost"
                    size="icon"
                    disabled={loadingList || transfer.active}
                    onClick={() => onNavigate(getParentPath(currentPath))}
                    className="mr-1 h-7 w-7"
                    title={t("usbFilesUpBtn")}
                  >
                    <ArrowLeft className="h-3.5 w-3.5" />
                  </Button>
                )}
                <button
                  type="button"
                  onClick={() => onNavigate("/")}
                  className="telemetry-mono rounded px-1.5 py-0.5 font-semibold text-foreground hover:bg-accent"
                >
                  /
                </button>
                {breadcrumbs.map((crumb) => (
                  <React.Fragment key={crumb.path}>
                    <ChevronRight className="h-3.5 w-3.5 text-muted-foreground" />
                    <button
                      type="button"
                      onClick={() => onNavigate(crumb.path)}
                      className="telemetry-mono rounded px-1.5 py-0.5 text-foreground hover:bg-accent"
                    >
                      {crumb.label}
                    </button>
                  </React.Fragment>
                ))}
              </div>

              {/* Quick Actions */}
              <div className="flex items-center gap-1.5">
                <Button
                  variant="outline"
                  size="sm"
                  disabled={transfer.active}
                  onClick={() => fileInputRef.current?.click()}
                  className="h-7 gap-1 text-xs"
                >
                  <Upload className="h-3 w-3" />
                  {t("usbFilesUploadBtn")}
                </Button>
                <Button
                  variant="outline"
                  size="sm"
                  disabled={transfer.active}
                  onClick={() => setCreatingFolder((v) => !v)}
                  className="h-7 gap-1 text-xs"
                >
                  <FolderPlus className="h-3 w-3" />
                  {t("usbFilesNewFolderBtn")}
                </Button>
                <Button
                  variant="ghost"
                  size="icon"
                  disabled={loadingList || transfer.active}
                  onClick={() => onNavigate(currentPath)}
                  className="h-7 w-7"
                  title={t("usbFilesRefreshBtn")}
                >
                  <RefreshCw className={cn("h-3.5 w-3.5", loadingList && "animate-spin")} />
                </Button>
              </div>
            </div>

            {/* Inline New Folder Bar */}
            {creatingFolder && (
              <form
                onSubmit={handleCreateFolderSubmit}
                className="flex items-center gap-2 border-b border-border/80 bg-muted/20 px-4 py-2.5"
              >
                <Input
                  value={newFolderName}
                  onChange={(e) => setNewFolderName(e.target.value)}
                  placeholder={t("usbFilesNewFolderPlaceholder")}
                  className="h-8 text-xs"
                  autoFocus
                />
                <Button type="submit" size="sm" className="h-8 text-xs">
                  {t("usbFilesCreateBtn")}
                </Button>
                <Button
                  type="button"
                  variant="ghost"
                  size="sm"
                  onClick={() => setCreatingFolder(false)}
                  className="h-8 text-xs"
                >
                  {t("usbFilesCancelBtn")}
                </Button>
              </form>
            )}

            {/* File Entries List */}
            <div className="divide-y divide-border/70">
              {loadingList ? (
                <div className="flex items-center justify-center gap-2 py-12 text-xs text-muted-foreground">
                  <Loader2 className="h-4 w-4 animate-spin text-cyber-cyan" />
                  <span>Loading {currentPath}...</span>
                </div>
              ) : entries.length === 0 ? (
                <div className="py-12 text-center text-xs text-muted-foreground">
                  {t("usbFilesEmptyDir")}
                </div>
              ) : (
                entries.map((entry) => {
                  const confirming = confirmDeletePath === entry.path;
                  return (
                    <div
                      key={entry.path}
                      className="flex items-center justify-between gap-3 px-4 py-2.5 transition-colors hover:bg-muted/35"
                    >
                      <button
                        type="button"
                        disabled={transfer.active}
                        onClick={() => {
                          if (entry.isDir) {
                            onNavigate(entry.path);
                          } else if (isPreviewable(entry.name)) {
                            onDownloadOrPreview(entry, false);
                          }
                        }}
                        className="flex min-w-0 flex-1 items-center gap-2.5 text-left"
                      >
                        {entry.isDir ? (
                          <Folder className="h-4 w-4 shrink-0 text-cyber-amber" />
                        ) : isPreviewable(entry.name) ? (
                          <FileCode2 className="h-4 w-4 shrink-0 text-cyber-cyan" />
                        ) : (
                          <FileText className="h-4 w-4 shrink-0 text-muted-foreground" />
                        )}
                        <span className="truncate text-xs font-medium text-foreground">
                          {entry.name}
                        </span>
                      </button>

                      <div className="flex shrink-0 items-center gap-1.5">
                        {!entry.isDir && (
                          <span className="telemetry-mono mr-1 text-[11px] text-muted-foreground">
                            {formatBytes(entry.size)}
                          </span>
                        )}

                        {!entry.isDir && isPreviewable(entry.name) && (
                          <>
                            <Button
                              variant="ghost"
                              size="icon"
                              disabled={transfer.active}
                              onClick={() => onDownloadOrPreview(entry, false)}
                              className="h-7 w-7 text-muted-foreground hover:text-foreground"
                              title={t("usbFilesPreviewBtn")}
                            >
                              <Eye className="h-3.5 w-3.5" />
                            </Button>
                            <Button
                              variant="ghost"
                              size="icon"
                              disabled={transfer.active}
                              onClick={() => onDownloadOrPreview(entry, true)}
                              className="h-7 w-7 text-muted-foreground hover:text-cyber-cyan"
                              title={t("usbFilesDownloadBtn")}
                            >
                              <Download className="h-3.5 w-3.5" />
                            </Button>
                          </>
                        )}

                        {confirming ? (
                          <div className="flex items-center gap-1">
                            <Button
                              variant="destructive"
                              size="sm"
                              onClick={() => {
                                setConfirmDeletePath(null);
                                onDeleteEntry(entry);
                              }}
                              className="h-7 px-2 text-[11px]"
                            >
                              {t("usbFilesDeleteConfirm")}
                            </Button>
                            <Button
                              variant="ghost"
                              size="icon"
                              onClick={() => setConfirmDeletePath(null)}
                              className="h-7 w-7"
                            >
                              <X className="h-3.5 w-3.5" />
                            </Button>
                          </div>
                        ) : (
                          <Button
                            variant="ghost"
                            size="icon"
                            disabled={transfer.active}
                            onClick={() => setConfirmDeletePath(entry.path)}
                            className="h-7 w-7 text-muted-foreground hover:text-destructive"
                            title={t("usbFilesDeleteBtn")}
                          >
                            <Trash2 className="h-3.5 w-3.5" />
                          </Button>
                        )}
                      </div>
                    </div>
                  );
                })
              )}
            </div>
          </Card>

          {/* File Preview Card */}
          <AnimatePresence>
            {previewFile && (
              <motion.div
                initial={{ opacity: 0, y: 8 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: 6 }}
              >
                <Card className="overflow-hidden">
                  <div className="flex items-center justify-between border-b border-border/80 bg-muted/35 px-4 py-2.5">
                    <span className="telemetry-mono text-xs font-semibold text-foreground">
                      {t("usbFilesPreviewTitle")}: {previewFile.name}
                    </span>
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={onClosePreview}
                      className="h-7 gap-1 text-xs"
                    >
                      <X className="h-3.5 w-3.5" />
                      {t("usbFilesCloseBtn")}
                    </Button>
                  </div>
                  <pre className="terminal-surface telemetry-mono max-h-64 overflow-auto p-4 text-xs leading-relaxed">
                    {previewFile.content || "(empty file)"}
                  </pre>
                </Card>
              </motion.div>
            )}
          </AnimatePresence>
        </div>
      )}

      {/* Helpful Tip about Native ESP32-S3 USB Mass Storage (U盘模式) */}
      <Alert variant="info">
        <Info className="h-4 w-4" />
        <AlertDescription className="space-y-0.5">
          <div className="font-semibold text-foreground">{t("usbFilesMscTipTitle")}</div>
          <div className="text-muted-foreground">{t("usbFilesMscTipDesc")}</div>
        </AlertDescription>
      </Alert>
    </div>
  );
}
