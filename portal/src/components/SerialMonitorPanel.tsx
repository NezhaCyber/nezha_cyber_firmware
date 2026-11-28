import React, { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "framer-motion";
import {
  Check,
  ChevronDown,
  ChevronUp,
  Copy,
  Download,
  Plug,
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
  open: boolean;
  onToggleOpen: () => void;
  isSupported: boolean;
  monitorConnected: boolean;
  isFlashingBusy: boolean;
  lines: TerminalLine[];
  onAttach: () => void;
  onDetach: () => void;
  onClear: () => void;
  onSendCommand: (cmd: string) => void;
}

const QUICK_COMMANDS = ["info", "sysmon", "wire i2c scan", "help", "reboot"];

export function SerialMonitorPanel({
  open,
  onToggleOpen,
  isSupported,
  monitorConnected,
  isFlashingBusy,
  lines,
  onAttach,
  onDetach,
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
  }, [lines, autoScroll, open]);

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
    <Card className="overflow-hidden">
      {/* Collapsible Header Bar */}
      <div className="flex flex-wrap items-center justify-between gap-3 px-5 py-3.5">
        <button
          type="button"
          onClick={onToggleOpen}
          className="flex items-center gap-3 text-left focus:outline-none"
        >
          <div className="flex h-8 w-8 items-center justify-center rounded-md border border-border bg-muted/50">
            <Terminal className="h-4 w-4 text-cyber-cyan" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <span className="telemetry-mono text-[10px] font-semibold uppercase tracking-[0.16em] text-cyber-crimson">
                {t("step4Label")}
              </span>
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
              <span className="telemetry-mono text-[10px] text-muted-foreground">
                {lines.length} lines
              </span>
            </div>
            <div className="text-sm font-semibold text-foreground">{t("monitorTitle")}</div>
          </div>
        </button>

        <div className="flex flex-wrap items-center gap-2">
          {!monitorConnected ? (
            <Button
              variant="outline"
              size="sm"
              disabled={!isSupported || isFlashingBusy}
              onClick={onAttach}
              className="h-8 gap-1.5 text-xs"
            >
              <Plug className="h-3.5 w-3.5 text-cyber-cyan" />
              {t("monitorConnectBtn")}
            </Button>
          ) : (
            <Button
              variant="outline"
              size="sm"
              onClick={onDetach}
              className="h-8 gap-1.5 border-cyber-emerald/50 text-xs hover:border-destructive/60 hover:text-destructive"
            >
              <Unplug className="h-3.5 w-3.5" />
              {t("monitorDisconnectBtn")}
            </Button>
          )}

          <Button
            variant="ghost"
            size="sm"
            onClick={onToggleOpen}
            className="h-8 gap-1 text-xs text-muted-foreground"
          >
            <span>{open ? t("monitorCollapse") : t("monitorExpand")}</span>
            {open ? <ChevronUp className="h-4 w-4" /> : <ChevronDown className="h-4 w-4" />}
          </Button>
        </div>
      </div>

      {/* Expandable Terminal Body */}
      <AnimatePresence initial={false}>
        {open && (
          <motion.div
            key="terminal-body"
            initial={{ height: 0, opacity: 0 }}
            animate={{ height: "auto", opacity: 1 }}
            exit={{ height: 0, opacity: 0 }}
            transition={{ duration: 0.22, ease: "easeOut" }}
            className="overflow-hidden border-t border-border/80"
          >
            {/* Toolbar */}
            <div className="flex flex-wrap items-center justify-between gap-2 border-b border-border/70 bg-muted/35 px-4 py-2">
              <div className="flex flex-wrap items-center gap-1.5">
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

            {/* Terminal Stream Output */}
            <div
              ref={viewportRef}
              className="terminal-surface telemetry-mono h-72 overflow-y-auto px-4 py-3 text-xs leading-relaxed"
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

            {/* Command TX Input */}
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
          </motion.div>
        )}
      </AnimatePresence>
    </Card>
  );
}
