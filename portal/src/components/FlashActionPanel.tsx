import { motion } from "framer-motion";
import {
  AlertCircle,
  CheckCircle2,
  Flame,
  Loader2,
  RotateCcw,
  Terminal,
  Trash2,
  Zap,
} from "lucide-react";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { Progress } from "@/components/ui/progress";
import { Switch } from "@/components/ui/switch";
import { useLocale } from "@/i18n/LocaleContext";
import { formatBytes, formatDuration } from "@/lib/utils";
import type { FlashProgressState } from "@/types/firmware";

interface FlashActionPanelProps {
  isConnected: boolean;
  isBusy: boolean;
  canFlash: boolean;
  eraseAll: boolean;
  onChangeEraseAll: (value: boolean) => void;
  progress: FlashProgressState;
  errorMessage: string | null;
  onStartFlash: () => void;
  onHardReset: () => void;
  onOpenMonitor: () => void;
  segmentCount: number;
}

export function FlashActionPanel({
  isConnected,
  isBusy,
  canFlash,
  eraseAll,
  onChangeEraseAll,
  progress,
  errorMessage,
  onStartFlash,
  onHardReset,
  onOpenMonitor,
  segmentCount,
}: FlashActionPanelProps) {
  const { t } = useLocale();

  const statusText: Record<FlashProgressState["phase"], string> = {
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

  const progressTone =
    progress.phase === "done"
      ? "emerald"
      : progress.phase === "erasing"
        ? "amber"
        : progress.phase === "flashing"
          ? "cyan"
          : "crimson";

  const isStreaming =
    progress.phase === "flashing" ||
    progress.phase === "erasing" ||
    progress.phase === "downloading";

  return (
    <Card className="overflow-hidden">
      <CardHeader className="pb-4">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <div>
            <div className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.18em] text-cyber-crimson">
              {t("step3Label")}
            </div>
            <CardTitle className="mt-1 flex items-center gap-2">
              <Zap className="h-4 w-4 text-cyber-crimson" />
              <span>{t("flashPanelTitle")}</span>
            </CardTitle>
          </div>

          <Badge
            variant={
              progress.phase === "done"
                ? "emerald"
                : progress.phase === "error"
                  ? "destructive"
                  : isStreaming
                    ? "cyan"
                    : "secondary"
            }
            className="telemetry-mono text-[10px]"
          >
            {statusText[progress.phase]}
          </Badge>
        </div>
      </CardHeader>

      <CardContent className="space-y-4">
        {/* Erase Flash Toggle */}
        <div className="flex items-start justify-between gap-4 rounded-lg border border-border/80 bg-muted/30 p-3.5">
          <div className="space-y-1">
            <label
              htmlFor="erase-flash-toggle"
              className="flex cursor-pointer items-center gap-1.5 text-xs font-semibold text-foreground"
            >
              <Trash2 className="h-3.5 w-3.5 text-cyber-amber" />
              {t("eraseFlashLabel")}
            </label>
            <p className="text-[11px] leading-relaxed text-muted-foreground">
              {t("eraseFlashDesc")}
            </p>
          </div>
          <Switch
            id="erase-flash-toggle"
            checked={eraseAll}
            disabled={isBusy}
            onCheckedChange={onChangeEraseAll}
          />
        </div>

        {/* Real-time Telemetry Progress Readout */}
        <div className="space-y-3 rounded-lg border border-border/80 bg-background/60 p-4">
          <div className="flex items-baseline justify-between gap-2">
            <div className="min-w-0">
              <div className="text-xs font-medium text-foreground">
                {progress.currentPartName
                  ? `${t("progressSegment")}: ${progress.currentPartName}`
                  : statusText[progress.phase]}
              </div>
              {progress.totalParts > 0 && (
                <div className="telemetry-mono mt-0.5 text-[11px] text-muted-foreground">
                  Part {Math.min(progress.currentPartIndex + 1, progress.totalParts)}/{progress.totalParts} · Offset{" "}
                  <span className="text-cyber-amber">{progress.currentPartOffset}</span>
                </div>
              )}
            </div>
            <motion.span
              key={Math.round(progress.overallPercent)}
              className="telemetry-mono text-2xl font-bold tracking-tight text-foreground"
            >
              {progress.overallPercent.toFixed(1)}%
            </motion.span>
          </div>

          <Progress
            value={progress.overallPercent}
            active={isStreaming}
            tone={progressTone}
          />

          <div className="grid grid-cols-3 gap-2 pt-1 text-[11px]">
            <div className="rounded border border-border/60 bg-muted/30 px-2.5 py-1.5">
              <div className="text-[10px] text-muted-foreground">{t("progressWritten")}</div>
              <div className="telemetry-mono mt-0.5 font-medium text-foreground">
                {formatBytes(progress.writtenBytes)} / {formatBytes(progress.totalBytes)}
              </div>
            </div>
            <div className="rounded border border-border/60 bg-muted/30 px-2.5 py-1.5">
              <div className="text-[10px] text-muted-foreground">{t("progressSpeed")}</div>
              <div className="telemetry-mono mt-0.5 font-medium text-cyber-cyan">
                {progress.throughputBps > 0 ? `${formatBytes(progress.throughputBps)}/s` : "--"}
              </div>
            </div>
            <div className="rounded border border-border/60 bg-muted/30 px-2.5 py-1.5">
              <div className="text-[10px] text-muted-foreground">{t("progressEta")}</div>
              <div className="telemetry-mono mt-0.5 font-medium text-foreground">
                {formatDuration(progress.etaSeconds)}
              </div>
            </div>
          </div>
        </div>

        {/* Error Alert */}
        {errorMessage && (
          <Alert variant="destructive">
            <AlertCircle className="h-4 w-4" />
            <AlertDescription className="space-y-1">
              <div className="telemetry-mono break-words">{errorMessage}</div>
              <div className="text-[11px] opacity-80">{t("missingArtifactHint")}</div>
            </AlertDescription>
          </Alert>
        )}

        {/* Completion Banner */}
        {progress.phase === "done" && (
          <Alert variant="success">
            <CheckCircle2 className="h-4 w-4" />
            <AlertDescription className="flex flex-wrap items-center justify-between gap-2">
              <span className="font-medium">{t("statusDone")}</span>
              <span className="telemetry-mono text-[11px] text-muted-foreground">
                RTS/DTR Hard Reset ready
              </span>
            </AlertDescription>
          </Alert>
        )}

        {/* Primary Flash + Post-Flash Reset / Monitor Actions */}
        <div className="flex flex-wrap items-center gap-2.5">
          <Button
            variant="cyber"
            size="lg"
            disabled={!canFlash || isBusy}
            onClick={onStartFlash}
            className="flex-1 min-w-[210px]"
          >
            {isStreaming ? (
              <Loader2 className="h-4 w-4 animate-spin" />
            ) : (
              <Flame className="h-4 w-4" />
            )}
            <span>{isStreaming ? t("flashRunningBtn") : t("flashStartBtn")}</span>
            {!isStreaming && segmentCount > 0 && (
              <span className="telemetry-mono ml-1 rounded bg-black/25 px-1.5 py-0.5 text-[10px]">
                {segmentCount} BIN
              </span>
            )}
          </Button>

          <Button
            variant={progress.phase === "done" ? "default" : "outline"}
            size="lg"
            disabled={isBusy || (!isConnected && progress.phase !== "done")}
            onClick={onHardReset}
            className="gap-2"
          >
            <RotateCcw className="h-4 w-4" />
            {t("hardResetBtn")}
          </Button>

          {progress.phase === "done" && (
            <Button
              variant="secondary"
              size="lg"
              onClick={onOpenMonitor}
              className="gap-2"
            >
              <Terminal className="h-4 w-4 text-cyber-cyan" />
              {t("openMonitorAfterBtn")}
            </Button>
          )}
        </div>
      </CardContent>
    </Card>
  );
}
