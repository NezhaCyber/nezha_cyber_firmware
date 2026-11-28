import { motion, AnimatePresence } from "framer-motion";
import {
  AlertTriangle,
  Cable,
  CheckCircle2,
  Cpu,
  HardDrive,
  Loader2,
  PlugZap,
  Radio,
  RefreshCw,
  Unplug,
  Usb,
} from "lucide-react";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { useLocale } from "@/i18n/LocaleContext";
import { cn } from "@/lib/utils";
import type {
  BoardBuild,
  DetectedChipInfo,
  FlashPhase,
  UsbPortInfoSummary,
} from "@/types/firmware";

interface DeviceConnectionPanelProps {
  isSupported: boolean;
  knownPorts: UsbPortInfoSummary[];
  selectedPort: UsbPortInfoSummary | null;
  onSelectPort: (summary: UsbPortInfoSummary) => void;
  baudRate: number;
  onChangeBaudRate: (baud: number) => void;
  chipInfo: DetectedChipInfo | null;
  selectedBoard: BoardBuild | null;
  phase: FlashPhase;
  isBusy: boolean;
  onRequestPort: (unfiltered?: boolean) => void;
  onConnectPort: (summary: UsbPortInfoSummary) => void;
  onDisconnect: () => void;
  onRefreshPorts: () => void;
}

const BAUD_OPTIONS = [
  { value: 921600, label: "921,600 bps (Fast)" },
  { value: 460800, label: "460,800 bps" },
  { value: 230400, label: "230,400 bps" },
  { value: 115200, label: "115,200 bps (Fallback)" },
];

export function DeviceConnectionPanel({
  isSupported,
  knownPorts,
  selectedPort,
  onSelectPort,
  baudRate,
  onChangeBaudRate,
  chipInfo,
  selectedBoard,
  phase,
  isBusy,
  onRequestPort,
  onConnectPort,
  onDisconnect,
  onRefreshPorts,
}: DeviceConnectionPanelProps) {
  const { locale, t } = useLocale();
  const isConnected = chipInfo !== null;
  const isConnecting = phase === "connecting";

  const hasChipMismatch =
    chipInfo !== null &&
    selectedBoard !== null &&
    chipInfo.chipFamily !== "unknown" &&
    chipInfo.chipFamily !== selectedBoard.chipFamily;

  return (
    <Card>
      <CardHeader className="pb-4">
        <div className="flex flex-wrap items-start justify-between gap-2">
          <div>
            <div className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.18em] text-cyber-crimson">
              {t("step1Label")}
            </div>
            <CardTitle className="mt-1 flex items-center gap-2">
              <Usb className="h-4 w-4 text-cyber-cyan" />
              <span>{t("devicePanelTitle")}</span>
            </CardTitle>
            <CardDescription className="mt-1">{t("devicePanelDesc")}</CardDescription>
          </div>

          <Badge
            variant={isConnected ? "emerald" : knownPorts.length > 0 ? "cyan" : "secondary"}
            className="telemetry-mono text-[10px]"
          >
            <Radio
              className={cn(
                "h-3 w-3",
                (isConnected || knownPorts.length > 0) && "animate-pulse",
              )}
            />
            {isConnected
              ? chipInfo.chipName
              : knownPorts.length > 0
                ? t("autoDetectFound")
                : t("autoDetectScanning")}
          </Badge>
        </div>
      </CardHeader>

      <CardContent className="space-y-4">
        {/* Auto-detected Authorized USB Serial Ports */}
        {isSupported && knownPorts.length > 0 && (
          <div className="space-y-2">
            <div className="flex items-center justify-between text-[11px] text-muted-foreground">
              <span className="telemetry-mono uppercase tracking-wider">
                {t("autoDetectFound")} ({knownPorts.length})
              </span>
              <button
                type="button"
                onClick={onRefreshPorts}
                disabled={isBusy}
                className="inline-flex items-center gap-1 text-[11px] text-muted-foreground transition-colors hover:text-foreground disabled:opacity-50"
              >
                <RefreshCw className="h-3 w-3" />
                Scan
              </button>
            </div>
            <div className="grid gap-2">
              {knownPorts.map((item) => {
                const isSelected = selectedPort?.port === item.port;
                return (
                  <button
                    key={item.id}
                    type="button"
                    disabled={isBusy}
                    onClick={() => onSelectPort(item)}
                    className={cn(
                      "flex w-full items-center justify-between gap-3 rounded-md border px-3 py-2 text-left transition-all",
                      isSelected
                        ? "border-cyber-cyan/70 bg-cyber-cyan/8 shadow-sm"
                        : "border-border/80 bg-background/50 hover:border-border hover:bg-muted/50",
                    )}
                  >
                    <div className="flex min-w-0 items-center gap-2.5">
                      <span
                        className={cn(
                          "h-2 w-2 shrink-0 rounded-full",
                          isSelected && isConnected
                            ? "bg-cyber-emerald"
                            : isSelected
                              ? "bg-cyber-cyan"
                              : "bg-muted-foreground/50",
                        )}
                      />
                      <div className="min-w-0">
                        <div className="truncate text-xs font-medium text-foreground">
                          {locale === "zh" ? item.deviceLabelZh : item.deviceLabel}
                        </div>
                        <div className="telemetry-mono text-[10px] text-muted-foreground">
                          VID {item.vendorHex} · PID {item.productHex}
                        </div>
                      </div>
                    </div>
                    <Badge variant="outline" className="telemetry-mono shrink-0 text-[10px]">
                      {item.likelyChipFamily.toUpperCase()}
                    </Badge>
                  </button>
                );
              })}
            </div>
          </div>
        )}

        {/* Controls Row: Baud Rate + Port Selection + Connect / Disconnect */}
        <div className="grid gap-3 sm:grid-cols-12">
          <div className="sm:col-span-5">
            <label className="mb-1.5 block text-[11px] font-medium text-muted-foreground">
              {t("baudRateLabel")}
            </label>
            <Select
              value={String(baudRate)}
              onValueChange={(val) => onChangeBaudRate(Number(val))}
              disabled={isBusy || isConnected}
            >
              <SelectTrigger className="telemetry-mono text-xs">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {BAUD_OPTIONS.map((opt) => (
                  <SelectItem
                    key={opt.value}
                    value={String(opt.value)}
                    className="telemetry-mono text-xs"
                  >
                    {opt.label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
            <p className="mt-1 text-[10px] text-muted-foreground">{t("baudFastHint")}</p>
          </div>

          <div className="flex flex-col justify-end gap-2 sm:col-span-7">
            <div className="flex flex-wrap items-center gap-2">
              {!isConnected ? (
                <>
                  {selectedPort ? (
                    <Button
                      variant="cyber"
                      disabled={!isSupported || isBusy}
                      onClick={() => onConnectPort(selectedPort)}
                      className="flex-1"
                    >
                      {isConnecting ? (
                        <Loader2 className="h-4 w-4 animate-spin" />
                      ) : (
                        <PlugZap className="h-4 w-4" />
                      )}
                      {isConnecting ? t("connectingBtn") : t("reconnectBtn")}
                    </Button>
                  ) : (
                    <Button
                      variant="cyber"
                      disabled={!isSupported || isBusy}
                      onClick={() => onRequestPort(false)}
                      className="flex-1"
                    >
                      {isConnecting ? (
                        <Loader2 className="h-4 w-4 animate-spin" />
                      ) : (
                        <Cable className="h-4 w-4" />
                      )}
                      {isConnecting ? t("connectingBtn") : t("requestPortBtn")}
                    </Button>
                  )}

                  {selectedPort && (
                    <Button
                      variant="outline"
                      size="default"
                      disabled={!isSupported || isBusy}
                      onClick={() => onRequestPort(false)}
                    >
                      <Usb className="h-4 w-4" />
                      {t("requestPortBtn")}
                    </Button>
                  )}

                  <Button
                    variant="ghost"
                    size="sm"
                    disabled={!isSupported || isBusy}
                    onClick={() => onRequestPort(true)}
                    className="text-xs text-muted-foreground"
                  >
                    {t("requestAllPortsBtn")}
                  </Button>
                </>
              ) : (
                <Button
                  variant="outline"
                  disabled={isBusy}
                  onClick={onDisconnect}
                  className="w-full border-cyber-emerald/50 text-foreground hover:border-destructive/60 hover:text-destructive"
                >
                  <Unplug className="h-4 w-4" />
                  {t("disconnectBtn")}
                </Button>
              )}
            </div>
          </div>
        </div>

        {/* Detected Silicon Telemetry Readout */}
        <div className="rounded-lg border border-border/80 bg-muted/30 p-3.5">
          <div className="mb-2.5 flex items-center justify-between">
            <span className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
              {t("chipTelemetryTitle")}
            </span>
            {isConnected && (
              <span className="inline-flex items-center gap-1 text-[11px] font-medium text-cyber-emerald">
                <CheckCircle2 className="h-3.5 w-3.5" />
                ROM Synced
              </span>
            )}
          </div>

          <AnimatePresence mode="wait">
            {chipInfo ? (
              <motion.div
                key="chip-online"
                initial={{ opacity: 0, y: 6 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -4 }}
                transition={{ duration: 0.2 }}
                className="grid grid-cols-2 gap-2.5 sm:grid-cols-4"
              >
                <div className="rounded-md border border-border/70 bg-background/70 p-2.5">
                  <div className="flex items-center gap-1 text-[10px] text-muted-foreground">
                    <Cpu className="h-3 w-3 text-cyber-crimson" />
                    {t("chipModel")}
                  </div>
                  <div className="telemetry-mono mt-1 truncate text-xs font-semibold text-foreground" title={chipInfo.chipDescription}>
                    {chipInfo.chipDescription}
                  </div>
                </div>

                <div className="rounded-md border border-border/70 bg-background/70 p-2.5">
                  <div className="text-[10px] text-muted-foreground">{t("chipMac")}</div>
                  <div className="telemetry-mono mt-1 truncate text-xs font-semibold text-cyber-cyan">
                    {chipInfo.macAddress}
                  </div>
                </div>

                <div className="rounded-md border border-border/70 bg-background/70 p-2.5">
                  <div className="flex items-center gap-1 text-[10px] text-muted-foreground">
                    <HardDrive className="h-3 w-3 text-cyber-amber" />
                    {t("chipFlashSize")}
                  </div>
                  <div className="telemetry-mono mt-1 text-xs font-semibold text-foreground">
                    {chipInfo.flashSizeMB > 0 ? `${chipInfo.flashSizeMB} MB` : "Auto"}
                  </div>
                </div>

                <div className="rounded-md border border-border/70 bg-background/70 p-2.5">
                  <div className="text-[10px] text-muted-foreground">{t("chipCrystal")}</div>
                  <div className="telemetry-mono mt-1 text-xs font-semibold text-foreground">
                    {chipInfo.crystalFreqMHz ? `${chipInfo.crystalFreqMHz} MHz` : chipInfo.chipFamily.toUpperCase()}
                  </div>
                </div>
              </motion.div>
            ) : (
              <motion.p
                key="chip-idle"
                initial={{ opacity: 0 }}
                animate={{ opacity: 1 }}
                exit={{ opacity: 0 }}
                className="text-xs leading-relaxed text-muted-foreground"
              >
                {t("chipAwaiting")}
              </motion.p>
            )}
          </AnimatePresence>
        </div>

        {/* Chip Mismatch Safety Guard */}
        {hasChipMismatch && (
          <Alert variant="warning">
            <AlertTriangle className="h-4 w-4" />
            <AlertTitle className="text-xs">{t("chipMismatchTitle")}</AlertTitle>
            <AlertDescription>
              {t("chipMismatchDesc")} ({chipInfo.chipName} vs {selectedBoard.mcu})
            </AlertDescription>
          </Alert>
        )}
      </CardContent>
    </Card>
  );
}
