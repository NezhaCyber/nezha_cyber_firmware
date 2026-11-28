import React, { useEffect, useRef, useState } from "react";
import {
  Check,
  Copy,
  Download,
  Plug,
  RotateCcw,
  Send,
  Terminal,
  Trash2,
  Unplug,
} from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { useLocale } from "@/i18n/LocaleContext";
import { cn } from "@/lib/utils";
import type { TerminalLine } from "@/types/firmware";

interface SerialMonitorPanelProps {
  isSupported: boolean;
  monitorConnected: boolean;
  isFlashingBusy: boolean;
  lines: TerminalLine[];
  onAttach: () => void;
  onDetach: () => void;
  onHardReset: () => void;
  onClear: () => void;
  onSendCommand: (cmd: string) => void;
}

const QUICK_COMMANDS = ["info", "sysmon", "ls /", "help", "reboot"];

export function SerialMonitorPanel({
  isSupported,
  monitorConnected,
  isFlashingBusy,
  lines,
  onAttach,
  onDetach,
  onHardReset,
  onClear,
  onSendCommand,
}: SerialMonitorPanelProps) {
  const { t } = useLocale();
  const [command, setCommand] = useState("");
  const [history, setHistory] = useState<string[]>([]);
  const [historyIndex, setHistoryIndex] = useState<number>(-1);
  const [showTimestamps, setShowTimestamps] = useState<boolean>(true);
  const [autoScroll, setAutoScroll] = useState<boolean>(true);
  const [copied, setCopied] = useState<boolean>(false);

  const viewportRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    if (autoScroll && viewportRef.current) {
      viewportRef.current.scrollTop = viewportRef.current.scrollHeight;
    }
  }, [lines, autoScroll]);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const trimmed = command.trim();
    if (!trimmed) return;
    onSendCommand(trimmed);
    setHistory((prev) => [trimmed, ...prev.slice(0, 49)]);
    setHistoryIndex(-1);
    setCommand("");
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
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
    const content = lines
      .map((l) => (showTimestamps ? `[${l.timestamp}] ${l.text}` : l.text))
      .join("\n");
    try {
      await navigator.clipboard.writeText(content);
      setCopied(true);
      setTimeout(() => setCopied(false), 1800);
    } catch {
      // Clipboard API unavailable
    }
  };

  const handleDownload = () => {
    const content = lines
      .map((l) => `[${l.timestamp}] [${l.source.toUpperCase()}] ${l.text}`)
      .join("\n");
    const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `nezha-serial-${new Date().toISOString().replace(/[:.]/g, "-")}.log`;
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="mx-auto max-w-4xl space-y-4">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h2 className="text-lg font-bold tracking-tight text-foreground">{t("monitorTitle")}</h2>
          <p className="text-xs text-muted-foreground">{t("monitorSubtitle")}</p>
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
            {monitorConnected ? "115200 LIVE" : "STANDBY"}
          </Badge>

          {!monitorConnected ? (
            <Button
              variant="cyber"
              size="sm"
              disabled={!isSupported || isFlashingBusy}
              onClick={onAttach}
              className="gap-1.5"
            >
              <Plug className="h-3.5 w-3.5" />
              {t("monitorConnectBtn")}
            </Button>
          ) : (
            <>
              <Button variant="outline" size="sm" onClick={onHardReset} className="gap-1.5">
                <RotateCcw className="h-3.5 w-3.5" />
                {t("hardResetBtn")}
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
          </div>

          <div className="flex flex-wrap items-center gap-1.5">
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
              disabled={lines.length === 0}
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
              disabled={lines.length === 0}
              className="h-7 gap-1 px-2 text-[11px]"
            >
              <Download className="h-3.5 w-3.5" />
              {t("monitorDownloadBtn")}
            </Button>
            <Button
              variant="ghost"
              size="sm"
              onClick={onClear}
              disabled={lines.length === 0}
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
          className="terminal-surface telemetry-mono h-[420px] overflow-y-auto px-4 py-3 text-xs leading-relaxed"
        >
          {lines.length === 0 ? (
            <div className="flex h-full items-center justify-center text-center text-slate-500">
              {t("monitorEmptyHint")}
            </div>
          ) : (
            lines.map((line) => (
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
            ))
          )}
        </div>

        {/* Command Input */}
        <form
          onSubmit={handleSubmit}
          className="flex items-center gap-2 border-t border-border/80 bg-muted/25 px-4 py-2.5"
        >
          <span className="telemetry-mono text-xs font-bold text-cyber-crimson">&gt;_</span>
          <Input
            value={command}
            onChange={(e) => setCommand(e.target.value)}
            onKeyDown={handleKeyDown}
            disabled={!monitorConnected}
            placeholder={t("monitorPlaceholder")}
            className="telemetry-mono h-8 flex-1 text-xs"
          />
          <Button
            type="submit"
            size="sm"
            variant="default"
            disabled={!monitorConnected || command.trim().length === 0}
            className="h-8 gap-1.5 text-xs"
          >
            <Send className="h-3.5 w-3.5" />
            {t("monitorSendBtn")}
          </Button>
        </form>
      </Card>
    </div>
  );
}
