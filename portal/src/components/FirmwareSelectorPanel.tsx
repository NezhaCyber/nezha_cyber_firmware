import React, { useRef } from "react";
import { FileCode2, FolderUp, Layers, Package, Plus, Sparkles, Trash2 } from "lucide-react";
import { FlashMemoryMap } from "@/components/FlashMemoryMap";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { useLocale } from "@/i18n/LocaleContext";
import { formatBytes } from "@/lib/utils";
import type {
  BoardBuild,
  CustomLocalPart,
  FirmwareManifest,
  FirmwareRelease,
  FlashPhase,
} from "@/types/firmware";

export type FirmwareSourceMode = "manifest" | "local";
export type ManifestFlashLayout = "merged" | "partitioned";

interface FirmwareSelectorPanelProps {
  manifest: FirmwareManifest | null;
  manifestLoading: boolean;
  manifestError: string | null;
  sourceMode: FirmwareSourceMode;
  onChangeSourceMode: (mode: FirmwareSourceMode) => void;
  selectedRelease: FirmwareRelease | null;
  onSelectReleaseVersion: (version: string) => void;
  selectedBoard: BoardBuild | null;
  onSelectBoardId: (boardId: string) => void;
  flashLayout: ManifestFlashLayout;
  onChangeFlashLayout: (layout: ManifestFlashLayout) => void;
  localParts: CustomLocalPart[];
  onAddLocalFiles: (files: FileList | File[]) => void;
  onUpdateLocalPartOffset: (id: string, offset: string) => void;
  onRemoveLocalPart: (id: string) => void;
  activeOffset?: string;
  phase: FlashPhase;
  autoMatchedNotice: boolean;
  isBusy: boolean;
}

export function FirmwareSelectorPanel({
  manifest,
  manifestLoading,
  manifestError,
  sourceMode,
  onChangeSourceMode,
  selectedRelease,
  onSelectReleaseVersion,
  selectedBoard,
  onSelectBoardId,
  flashLayout,
  onChangeFlashLayout,
  localParts,
  onAddLocalFiles,
  onUpdateLocalPartOffset,
  onRemoveLocalPart,
  activeOffset,
  phase,
  autoMatchedNotice,
  isBusy,
}: FirmwareSelectorPanelProps) {
  const { locale, t } = useLocale();
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  const hasPartitionedPreset = Boolean(selectedBoard?.flashModes.partitioned);
  const effectiveLayout: ManifestFlashLayout =
    flashLayout === "partitioned" && hasPartitionedPreset ? "partitioned" : "merged";
  const activeManifestPreset =
    effectiveLayout === "partitioned" && selectedBoard?.flashModes.partitioned
      ? selectedBoard.flashModes.partitioned
      : selectedBoard?.flashModes.merged;

  const handleDrop = (e: React.DragEvent<HTMLDivElement>) => {
    e.preventDefault();
    if (isBusy) return;
    if (e.dataTransfer.files?.length) {
      onAddLocalFiles(e.dataTransfer.files);
    }
  };

  return (
    <Card>
      <CardHeader className="pb-4">
        <div className="flex flex-wrap items-start justify-between gap-2">
          <div>
            <div className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.18em] text-cyber-crimson">
              {t("step2Label")}
            </div>
            <CardTitle className="mt-1 flex items-center gap-2">
              <Layers className="h-4 w-4 text-cyber-amber" />
              <span>{t("firmwarePanelTitle")}</span>
            </CardTitle>
            <CardDescription className="mt-1">{t("firmwarePanelDesc")}</CardDescription>
          </div>

          {autoMatchedNotice && (
            <Badge variant="cyan" className="text-[10px]">
              <Sparkles className="h-3 w-3" />
              {t("autoMatchedBoard")}
            </Badge>
          )}
        </div>
      </CardHeader>

      <CardContent className="space-y-4">
        <Tabs
          value={sourceMode}
          onValueChange={(v) => onChangeSourceMode(v as FirmwareSourceMode)}
        >
          <TabsList className="grid w-full grid-cols-2">
            <TabsTrigger value="manifest" disabled={isBusy} className="gap-1.5">
              <Package className="h-3.5 w-3.5" />
              {t("sourceManifestTab")}
            </TabsTrigger>
            <TabsTrigger value="local" disabled={isBusy} className="gap-1.5">
              <FolderUp className="h-3.5 w-3.5" />
              {t("sourceLocalTab")}
            </TabsTrigger>
          </TabsList>

          {/* TAB 1: Manifest-driven release & board selection */}
          <TabsContent value="manifest" className="space-y-4">
            {manifestError && (
              <div className="rounded-md border border-destructive/40 bg-destructive/10 p-3 text-xs text-destructive">
                {manifestError}
              </div>
            )}

            <div className="grid gap-3 sm:grid-cols-2">
              {/* Release Version */}
              <div>
                <label className="mb-1.5 block text-[11px] font-medium text-muted-foreground">
                  {t("releaseVersionLabel")}
                </label>
                <Select
                  value={selectedRelease?.version ?? ""}
                  onValueChange={onSelectReleaseVersion}
                  disabled={manifestLoading || isBusy || !manifest}
                >
                  <SelectTrigger className="telemetry-mono text-xs">
                    <SelectValue placeholder="Loading manifest..." />
                  </SelectTrigger>
                  <SelectContent>
                    {manifest?.releases.map((rel) => (
                      <SelectItem key={rel.version} value={rel.version}>
                        <div className="flex items-center gap-2">
                          <span className="telemetry-mono font-semibold">{rel.version}</span>
                          <span className="text-[11px] text-muted-foreground">
                            ·{" "}
                            {rel.channel === "stable"
                              ? t("channelStable")
                              : rel.channel === "beta"
                                ? t("channelBeta")
                                : t("channelArchive")}
                          </span>
                        </div>
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              {/* Target Board Variant */}
              <div>
                <label className="mb-1.5 block text-[11px] font-medium text-muted-foreground">
                  {t("boardVariantLabel")}
                </label>
                <Select
                  value={selectedBoard?.id ?? ""}
                  onValueChange={onSelectBoardId}
                  disabled={manifestLoading || isBusy || !selectedRelease}
                >
                  <SelectTrigger className="text-xs">
                    <SelectValue placeholder="Select board..." />
                  </SelectTrigger>
                  <SelectContent>
                    {selectedRelease?.builds.map((b) => (
                      <SelectItem key={b.id} value={b.id}>
                        <div className="flex items-center gap-2">
                          <span className="font-medium">
                            {locale === "zh" ? b.boardNameZh : b.boardName}
                          </span>
                          <span className="telemetry-mono text-[10px] text-muted-foreground">
                            ({b.mcu} · {b.flashSize})
                          </span>
                        </div>
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>
            </div>

            {/* Release Notes Strip */}
            {selectedRelease && (
              <div className="rounded-md border border-border/75 bg-muted/35 px-3 py-2.5 text-xs leading-relaxed text-muted-foreground">
                <span className="telemetry-mono mr-2 font-semibold text-foreground">
                  {selectedRelease.version}
                </span>
                {locale === "zh" ? selectedRelease.notesZh : selectedRelease.notesEn}
              </div>
            )}

            {/* Flash Layout Mode (Merged 0x0 vs Multi-Partition) */}
            {selectedBoard && (
              <div className="space-y-2">
                <div className="flex items-center justify-between">
                  <label className="text-[11px] font-medium text-muted-foreground">
                    {t("flashModeLabel")}
                  </label>
                  <span className="telemetry-mono text-[10px] text-muted-foreground">
                    env: {selectedBoard.env}
                  </span>
                </div>
                <div className="grid gap-2 sm:grid-cols-2">
                  <button
                    type="button"
                    disabled={isBusy}
                    onClick={() => onChangeFlashLayout("merged")}
                    className={`rounded-md border p-2.5 text-left transition-all ${
                      effectiveLayout === "merged"
                        ? "border-cyber-crimson/70 bg-cyber-crimson/8"
                        : "border-border/80 bg-background/50 hover:bg-muted/40"
                    }`}
                  >
                    <div className="text-xs font-semibold text-foreground">
                      {locale === "zh"
                        ? selectedBoard.flashModes.merged.labelZh
                        : selectedBoard.flashModes.merged.labelEn}
                    </div>
                    <div className="telemetry-mono mt-0.5 text-[10px] text-muted-foreground">
                      1 segment · 0x0
                    </div>
                  </button>

                  {selectedBoard.flashModes.partitioned && (
                    <button
                      type="button"
                      disabled={isBusy}
                      onClick={() => onChangeFlashLayout("partitioned")}
                      className={`rounded-md border p-2.5 text-left transition-all ${
                        effectiveLayout === "partitioned"
                          ? "border-cyber-crimson/70 bg-cyber-crimson/8"
                          : "border-border/80 bg-background/50 hover:bg-muted/40"
                      }`}
                    >
                      <div className="text-xs font-semibold text-foreground">
                        {locale === "zh"
                          ? selectedBoard.flashModes.partitioned.labelZh
                          : selectedBoard.flashModes.partitioned.labelEn}
                      </div>
                      <div className="telemetry-mono mt-0.5 text-[10px] text-muted-foreground">
                        {selectedBoard.flashModes.partitioned.parts.length} segments · custom offsets
                      </div>
                    </button>
                  )}
                </div>
              </div>
            )}

            {/* Queued Flash Segments Table */}
            {activeManifestPreset && (
              <div className="space-y-2">
                <div className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
                  {t("filesToFlashTitle")}
                </div>
                <div className="divide-y divide-border/70 overflow-hidden rounded-md border border-border/80 bg-background/50">
                  {activeManifestPreset.parts.map((part) => (
                    <div
                      key={`${part.offset}-${part.name}`}
                      className="flex flex-wrap items-center justify-between gap-2 px-3 py-2 text-xs"
                    >
                      <div className="flex min-w-0 items-center gap-2">
                        <FileCode2 className="h-3.5 w-3.5 shrink-0 text-cyber-cyan" />
                        <span className="truncate font-medium text-foreground">{part.name}</span>
                        <span className="hidden text-[11px] text-muted-foreground sm:inline">
                          ({part.label})
                        </span>
                      </div>
                      <Badge variant="amber" className="telemetry-mono text-[10px]">
                        @ {part.offset}
                      </Badge>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </TabsContent>

          {/* TAB 2: Local Custom .bin Multi-Part Uploader */}
          <TabsContent value="local" className="space-y-3">
            <div
              onDragOver={(e) => e.preventDefault()}
              onDrop={handleDrop}
              onClick={() => !isBusy && fileInputRef.current?.click()}
              className="flex cursor-pointer flex-col items-center justify-center rounded-lg border border-dashed border-border bg-muted/25 px-4 py-6 text-center transition-colors hover:border-cyber-crimson/60 hover:bg-muted/45"
            >
              <FolderUp className="mb-2 h-7 w-7 text-cyber-crimson" />
              <div className="text-xs font-semibold text-foreground">{t("localDropTitle")}</div>
              <p className="mt-1 max-w-md text-[11px] text-muted-foreground">
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

            {localParts.length > 0 && (
              <div className="space-y-2">
                {localParts.map((part) => (
                  <div
                    key={part.id}
                    className="flex flex-wrap items-center justify-between gap-2 rounded-md border border-border/80 bg-background/60 p-2.5"
                  >
                    <div className="min-w-0 flex-1">
                      <div className="truncate text-xs font-medium text-foreground">
                        {part.file.name}
                      </div>
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
                        placeholder="0x0"
                      />
                      <Button
                        type="button"
                        variant="ghost"
                        size="icon"
                        disabled={isBusy}
                        onClick={() => onRemoveLocalPart(part.id)}
                        className="h-7 w-7 text-muted-foreground hover:text-destructive"
                        title={t("localRemoveBtn")}
                      >
                        <Trash2 className="h-3.5 w-3.5" />
                      </Button>
                    </div>
                  </div>
                ))}

                <div className="flex flex-wrap items-center gap-1.5 pt-1">
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
                </div>
              </div>
            )}
          </TabsContent>
        </Tabs>

        {/* Visual Flash Partition Map */}
        {selectedBoard && (
          <div className="rounded-lg border border-border/80 bg-muted/25 p-3.5">
            <div className="mb-2.5 flex items-center justify-between">
              <span className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
                {t("partitionMapTitle")}
              </span>
              <span className="telemetry-mono text-[10px] text-muted-foreground">
                {t("partitionTableSource")}: {selectedBoard.partitionTable}
              </span>
            </div>
            <FlashMemoryMap
              partitions={selectedBoard.partitions}
              activeOffset={activeOffset}
              phase={phase}
            />
          </div>
        )}
      </CardContent>
    </Card>
  );
}
