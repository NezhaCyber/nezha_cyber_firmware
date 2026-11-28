import React, { useEffect, useRef, useState } from "react";
import {
  AlertCircle,
  Check,
  Copy,
  CornerDownLeft,
  Download,
  Loader2,
  Plug,
  RotateCcw,
  Send,
  Terminal,
  Trash2,
  Unplug,
  Usb,
} from "lucide-react";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { useLocale } from "@/i18n/LocaleContext";
import type { LineEndingMode } from "@/lib/serialTerminal";
import { cn, formatBytes } from "@/lib/utils";
import type { TerminalLine, UsbPortInfoSummary } from "@/types/firmware";

interface AttachMonitorOptions {
  forceSelectPort?: boolean;
  unfiltered?: boolean;
  customBaud?: number;
  portOverride?: UsbPortInfoSummary;
}

interface SerialMonitorPanelProps {
  isSupported: boolean;
  monitorConnected: boolean;
  monitorConnecting: boolean;
  isFlashingBusy: boolean;
  selectedPort: UsbPortInfoSummary | null;
  knownPorts: UsbPortInfoSummary[];
  monitorBaudRate: number;
  onChangeMonitorBaudRate: (baud: number) => void;
  lineEnding: LineEndingMode;
  onChangeLineEnding: (ending: LineEndingMode) => void;
  localEcho: boolean;
  onChangeLocalEcho: (echo: boolean) => void;
  monitorError: string | null;
  lines: TerminalLine[];
  pendingLine: string;
  rxBytes: number;
  txBytes: number;
  onAttach: (options?: AttachMonitorOptions) => void;
  onDetach: () => void;
  onHardReset: () => void;
  onClear: () => void;
  onSendCommand: (cmd: string) => void;
  onSendCtrlC: () => void;
}

const QUICK_COMMANDS = ["info", "sysmon", "free", "uptime", "ls /", "i2c", "help", "reboot"];
const MONITOR_BAUD_OPTIONS = [115200, 74880, 921600, 460800, 57600, 9600];

export function SerialMonitorPanel({
  isSupported,
  monitorConnected,
  monitorConnecting,
  isFlashingBusy,
  selectedPort,
  knownPorts,
  monitorBaudRate,
  onChangeMonitorBaudRate,
  lineEnding,
  onChangeLineEnding,
  localEcho,
  onChangeLocalEcho,
  monitorError,
  lines,
  pendingLine,
  rxBytes,
  txBytes,
  onAttach,
  onDetach,
  onHardReset,
  onClear,
  onSendCommand,
  onSendCtrlC,
}: SerialMonitorPanelProps) {
  const { locale, t } = useLocale();
  const [command, setCommand] = useState("");
  const [history, setHistory] = useState<string[]>([]);
  const [historyIndex, setHistoryIndex] = useState<number>(-1);
  const [showTimestamps, setShowTimestamps] = useState<boolean>(true);
  const [autoScroll, setAutoScroll] = useState<boolean>(true);
  const [copied, setCopied] = useState<boolean>(false);

  const viewportRef = useRef<HTMLDivElement | null>(null);
  const inputRef = useRef<HTMLInputElement | null>(null);

  useEffect(() => {
    if (autoScroll && viewportRef.current) {
      viewportRef.current.scrollTop = viewportRef.current.scrollHeight;
    }
  }, [lines, pendingLine, autoScroll]);

  useEffect(() => {
    if (monitorConnected) {
      inputRef.current?.focus();
    }
  }, [monitorConnected]);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!monitorConnected) return;
    const trimmed = command.trim();
    // Allow sending empty Enter to wake up the firmware's "# " CLI prompt
    onSendCommand(trimmed);
    if (trimmed.length > 0) {
      setHistory((prev) => [trimmed, ...prev.filter((c) => c !== trimmed).slice(0, 49)]);
    }
    setHistoryIndex(-1);
    setCommand("");
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.ctrlKey && e.key.toLowerCase() === "c" && command.length === 0) {
      e.preventDefault();
      onSendCtrlC();
      return;
    }
    if (e.key === "ArrowUp") {
      e.preventDefault();
      if (history.length === 0) return;
      const next = Math.min(history.length - 1, historyIndex + 1);
      setHistoryIndex(next);
      setCommand(history[next]);
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      if (historyIndex <= 0) {
        setHistoryIndex(-1);
        setCommand("");
      } else {
        const next = historyIndex - 1;
        setHistoryIndex(next);
        setCommand(history[next]);
      }
    }
  };

  const handleCopy = async () => {
    const content = [
      ...lines.map((l) => (showTimestamps ? `[${l.timestamp}] ${l.text}` : l.text)),
      ...(pendingLine ? [pendingLine] : []),
    ].join("\n");
    try {
      await navigator.clipboard.writeText(content);
      setCopied(true);
      setTimeout(() => setCopied(false), 1800);
    } catch {
      // Clipboard API unavailable
    }
  };

  const handleDownload = () => {
    const content = [
      ...lines.map((l) => `[${l.timestamp}] [${l.source.toUpperCase()}] ${l.text}`),
      ...(pendingLine ? [`[LIVE] [DEVICE] ${pendingLine}`] : []),
    ].join("\n");
    const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `nezha-serial-${new Date().toISOString().replace(/[:.]/g, "-")}.log`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const activePortLabel = selectedPort
    ? `${locale === "zh" ? selectedPort.deviceLabelZh : selectedPort.deviceLabel} (${selectedPort.vendorHex}:${selectedPort.productHex})`
    : null;

  return (
    <div className="mx-auto max-w-4xl space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="text-lg font-bold tracking-tight text-foreground">{t("monitorTitle")}</h2>
          <p className="text-xs text-muted-foreground">
            {activePortLabel ?? t("monitorSubtitle")}
          </p>
        </div>

        <div className="flex flex-wrap items-center gap-2">
          <Badge
            variant={monitorConnected ? "emerald" : "secondary"}
            className="telemetry-mono text-[10px]"
          >
            <span
              className={cn(
                "mr-1 inline-block h-1.5 w-1.5 rounded-full",
                monitorConnected ? "bg-cyber-emerald animate-pulse" : "bg-muted-foreground/50",
              )}
            />
            {monitorConnected ? `${monitorBaudRate} LIVE` : "STANDBY"}
          </Badge>

          {/* Baud Rate Picker */}
          <Select
            value={String(monitorBaudRate)}
            onValueChange={(v) => onChangeMonitorBaudRate(Number(v))}
            disabled={isFlashingBusy || monitorConnecting}
          >
            <SelectTrigger className="telemetry-mono h-8 w-[108px] text-[11px]">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {MONITOR_BAUD_OPTIONS.map((b) => (
                <SelectItem key={b} value={String(b)} className="telemetry-mono text-xs">
                  {b} bps
                </SelectItem>
              ))}
            </SelectContent>
          </Select>

          {!monitorConnected ? (
            <>
              <Button
                variant="cyber"
                size="sm"
                disabled={!isSupported || isFlashingBusy || monitorConnecting}
                onClick={() => onAttach()}
                className="gap-1.5"
              >
                {monitorConnecting ? (
                  <Loader2 className="h-3.5 w-3.5 animate-spin" />
                ) : (
                  <Plug className="h-3.5 w-3.5" />
                )}
                {monitorConnecting ? t("connectingBtn") : t("monitorConnectBtn")}
              </Button>
              {selectedPort && (
                <Button
                  variant="outline"
                  size="sm"
                  disabled={!isSupported || isFlashingBusy || monitorConnecting}
                  onClick={() => onAttach({ forceSelectPort: true })}
                  className="gap-1.5"
                >
                  <Usb className="h-3.5 w-3.5" />
                  {t("chooseAnotherPort")}
                </Button>
              )}
              <Button
                variant="ghost"
                size="sm"
                disabled={!isSupported || isFlashingBusy || monitorConnecting}
                onClick={() => onAttach({ forceSelectPort: true, unfiltered: true })}
                className="text-xs text-muted-foreground hover:text-foreground"
              >
                {t("requestAllPortsBtn")}
              </Button>
            </>
          ) : (
            <>
              <Button variant="outline" size="sm" onClick={onHardReset} className="gap-1.5">
                <RotateCcw className="h-3.5 w-3.5" />
                {t("hardResetBtn")}
              </Button>
              <Button
                variant="outline"
                size="sm"
                onClick={() => onAttach({ forceSelectPort: true })}
                className="gap-1.5"
              >
                <Usb className="h-3.5 w-3.5" />
                {t("chooseAnotherPort")}
              </Button>
              <Button
                variant="outline"
                size="sm"
                onClick={onDetach}
                className="gap-1.5 hover:border-destructive/60 hover:text-destructive"
              >
                <Unplug className="h-3.5 w-3.5" />
                {t("monitorDisconnectBtn")}
              </Button>
            </>
          )}
        </div>
      </div>

      {/* Authorized Ports Quick Switcher when multiple devices are plugged in */}
      {knownPorts.length > 1 && !monitorConnected && (
        <div className="flex flex-wrap items-center gap-1.5 text-[11px]">
          <span className="text-muted-foreground">{t("monitorKnownPorts")}:</span>
          {knownPorts.map((p) => (
            <button
              key={p.id}
              type="button"
              disabled={isFlashingBusy || monitorConnecting}
              onClick={() => onAttach({ portOverride: p })}
              className={cn(
                "telemetry-mono rounded-md border px-2 py-0.5 transition-colors",
                selectedPort?.port === p.port
                  ? "border-cyber-cyan/60 bg-cyber-cyan/10 text-foreground"
                  : "border-border bg-card/60 text-muted-foreground hover:text-foreground",
              )}
            >
              {locale === "zh" ? p.deviceLabelZh : p.deviceLabel} ({p.vendorHex}:{p.productHex})
            </button>
          ))}
        </div>
      )}

      {/* Explicit Connection / Runtime Error Banner */}
      {monitorError && (
        <Alert variant="destructive">
          <AlertCircle className="h-4 w-4" />
          <AlertDescription className="space-y-1">
            <div className="telemetry-mono break-words text-xs">{monitorError}</div>
            <div className="text-[11px] opacity-85">{t("monitorBusyHint")}</div>
          </AlertDescription>
        </Alert>
      )}

      {/* Helpful hint when connected but no bytes received yet */}
      {monitorConnected && rxBytes === 0 && (
        <div className="flex flex-wrap items-center justify-between gap-2 rounded-lg border border-cyber-cyan/35 bg-cyber-cyan/8 px-3.5 py-2 text-[11px] text-foreground">
          <span>{t("monitorNoRxHint")}</span>
          <div className="flex items-center gap-1.5">
            <Button
              type="button"
              variant="outline"
              size="sm"
              onClick={() => onSendCommand("info")}
              className="telemetry-mono h-6 px-2 text-[10px]"
            >
              info
            </Button>
            <Button
              type="button"
              variant="outline"
              size="sm"
              onClick={onHardReset}
              className="h-6 px-2 text-[10px]"
            >
              {t("hardResetBtn")}
            </Button>
          </div>
        </div>
      )}

      <Card className="overflow-hidden">
        {/* Compact Toolbar */}
        <div className="flex flex-wrap items-center justify-between gap-2 border-b border-border/75 bg-muted/35 px-4 py-2">
          <div className="flex flex-wrap items-center gap-1.5">
            <Terminal className="mr-0.5 h-3.5 w-3.5 text-cyber-cyan" />
            <span className="telemetry-mono mr-1 text-[10px] uppercase tracking-wider text-muted-foreground">
              {t("monitorQuickCmds")}:
            </span>
            {QUICK_COMMANDS.map((qCmd) => (
              <button
                key={qCmd}
                type="button"
                disabled={!monitorConnected}
                onClick={() => onSendCommand(qCmd)}
                className="telemetry-mono rounded border border-border/80 bg-background/70 px-2 py-0.5 text-[11px] text-foreground transition-colors hover:border-cyber-cyan/60 hover:text-cyber-cyan disabled:opacity-40"
              >
                {qCmd}
              </button>
            ))}
            <button
              type="button"
              disabled={!monitorConnected}
              onClick={onSendCtrlC}
              title="Send Ctrl+C (0x03)"
              className="telemetry-mono rounded border border-rose-500/40 bg-rose-500/10 px-2 py-0.5 text-[11px] text-rose-400 transition-colors hover:bg-rose-500/20 disabled:opacity-40"
            >
              ^C
            </button>
          </div>

          <div className="flex flex-wrap items-center gap-1.5">
            <span className="telemetry-mono mr-1 text-[10px] text-muted-foreground">
              RX {formatBytes(rxBytes)} · TX {formatBytes(txBytes)}
            </span>
            <button
              type="button"
              onClick={() => onChangeLocalEcho(!localEcho)}
              className={cn(
                "rounded border px-2 py-0.5 text-[11px] transition-colors",
                localEcho
                  ? "border-cyber-cyan/50 bg-cyber-cyan/10 text-cyber-cyan"
                  : "border-border text-muted-foreground",
              )}
            >
              {t("monitorEcho")}
            </button>
            <button
              type="button"
              onClick={() => setShowTimestamps((v) => !v)}
              className={cn(
                "rounded border px-2 py-0.5 text-[11px] transition-colors",
                showTimestamps
                  ? "border-cyber-cyan/50 bg-cyber-cyan/10 text-cyber-cyan"
                  : "border-border text-muted-foreground",
              )}
            >
              {t("monitorTimestamps")}
            </button>
            <button
              type="button"
              onClick={() => setAutoScroll((v) => !v)}
              className={cn(
                "rounded border px-2 py-0.5 text-[11px] transition-colors",
                autoScroll
                  ? "border-cyber-cyan/50 bg-cyber-cyan/10 text-cyber-cyan"
                  : "border-border text-muted-foreground",
              )}
            >
              {t("monitorAutoscroll")}
            </button>
            <Button
              variant="ghost"
              size="sm"
              onClick={handleCopy}
              disabled={lines.length === 0 && pendingLine.length === 0}
              className="h-7 gap-1 px-2 text-[11px]"
            >
              {copied ? (
                <Check className="h-3.5 w-3.5 text-cyber-emerald" />
              ) : (
                <Copy className="h-3.5 w-3.5" />
              )}
              {copied ? t("monitorCopiedBtn") : t("monitorCopyBtn")}
            </Button>
            <Button
              variant="ghost"
              size="sm"
              onClick={handleDownload}
              disabled={lines.length === 0 && pendingLine.length === 0}
              className="h-7 gap-1 px-2 text-[11px]"
            >
              <Download className="h-3.5 w-3.5" />
              {t("monitorDownloadBtn")}
            </Button>
            <Button
              variant="ghost"
              size="sm"
              onClick={onClear}
              disabled={lines.length === 0 && pendingLine.length === 0}
              className="h-7 gap-1 px-2 text-[11px] text-muted-foreground hover:text-destructive"
            >
              <Trash2 className="h-3.5 w-3.5" />
              {t("monitorClearBtn")}
            </Button>
          </div>
        </div>

        {/* Stream Viewport */}
        <div
          ref={viewportRef}
          onClick={() => inputRef.current?.focus()}
          className="terminal-surface telemetry-mono h-[420px] cursor-text overflow-y-auto px-4 py-3 text-xs leading-relaxed"
        >
          {lines.length === 0 && pendingLine.length === 0 ? (
            <div className="flex h-full items-center justify-center text-center text-slate-500">
              {t("monitorEmptyHint")}
            </div>
          ) : (
            <>
              {lines.map((line) => (
                <div
                  key={line.id}
                  className={cn(
                    "whitespace-pre-wrap break-all",
                    line.source === "error" && "text-rose-400",
                    line.source === "system" && "text-cyan-400/90",
                    line.source === "tx" && "text-amber-300 font-semibold",
                    line.source === "device" && "text-slate-200",
                  )}
                >
                  {showTimestamps && (
                    <span className="mr-2 select-none text-[10px] text-slate-500">
                      [{line.timestamp}]
                    </span>
                  )}
                  {line.text}
                </div>
              ))}
              {pendingLine.length > 0 && (
                <div className="whitespace-pre-wrap break-all text-slate-200">
                  {pendingLine}
                  {monitorConnected && (
                    <span className="ml-0.5 inline-block h-3.5 w-1.5 animate-pulse bg-cyber-cyan align-middle" />
                  )}
                </div>
              )}
            </>
          )}
        </div>

        {/* Command Input + Line Ending Selector */}
        <form
          onSubmit={handleSubmit}
          className="flex flex-wrap items-center gap-2 border-t border-border/80 bg-muted/25 px-4 py-2.5"
        >
          <span className="telemetry-mono text-xs font-bold text-cyber-crimson">&gt;_</span>
          <Input
            ref={inputRef}
            value={command}
            onChange={(e) => setCommand(e.target.value)}
            onKeyDown={handleKeyDown}
            disabled={!monitorConnected}
            placeholder={t("monitorPlaceholder")}
            className="telemetry-mono h-8 min-w-[180px] flex-1 text-xs"
          />
          <Select
            value={lineEnding}
            onValueChange={(v) => onChangeLineEnding(v as LineEndingMode)}
          >
            <SelectTrigger className="telemetry-mono h-8 w-[84px] text-[11px]">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="lf" className="telemetry-mono text-xs">
                LF (\n)
              </SelectItem>
              <SelectItem value="crlf" className="telemetry-mono text-xs">
                CRLF
              </SelectItem>
              <SelectItem value="cr" className="telemetry-mono text-xs">
                CR (\r)
              </SelectItem>
              <SelectItem value="none" className="telemetry-mono text-xs">
                None
              </SelectItem>
            </SelectContent>
          </Select>
          <Button
            type="submit"
            size="sm"
            variant="default"
            disabled={!monitorConnected}
            className="h-8 gap-1.5 text-xs"
          >
            {command.trim().length > 0 ? (
              <Send className="h-3.5 w-3.5" />
            ) : (
              <CornerDownLeft className="h-3.5 w-3.5" />
            )}
            {t("monitorSendBtn")}
          </Button>
        </form>
      </Card>
    </div>
  );
}
