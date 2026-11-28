import { motion } from "framer-motion";
import { Github, Laptop, Moon, Sun, Tag, Usb } from "lucide-react";
import { NezhaEmblem } from "@/components/NezhaEmblem";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { useTheme, type ThemeMode } from "@/context/ThemeContext";
import { useLocale } from "@/i18n/LocaleContext";
import type { LocaleMode } from "@/i18n/translations";
import { cn } from "@/lib/utils";
import type { FlashPhase } from "@/types/firmware";

interface TopBarProps {
  repository: string;
  phase: FlashPhase;
  isConnected: boolean;
  detectedPortsCount: number;
}

export function TopBar({
  repository,
  phase,
  isConnected,
  detectedPortsCount,
}: TopBarProps) {
  const { mode: themeMode, setMode: setThemeMode } = useTheme();
  const { mode: localeMode, setMode: setLocaleMode, t } = useLocale();

  const isBusy =
    phase === "connecting" ||
    phase === "downloading" ||
    phase === "erasing" ||
    phase === "flashing" ||
    phase === "resetting";

  const themeOptions: { value: ThemeMode; label: string; icon: typeof Sun }[] = [
    { value: "auto", label: t("themeAuto"), icon: Laptop },
    { value: "dark", label: t("themeDark"), icon: Moon },
    { value: "light", label: t("themeLight"), icon: Sun },
  ];

  const localeOptions: { value: LocaleMode; label: string }[] = [
    { value: "auto", label: t("localeAuto") },
    { value: "zh", label: t("localeZh") },
    { value: "en", label: t("localeEn") },
  ];

  return (
    <header className="sticky top-0 z-40 border-b border-border/80 bg-background/80 backdrop-blur-xl">
      <div className="mx-auto flex max-w-7xl flex-wrap items-center justify-between gap-3 px-4 py-3 sm:px-6">
        {/* Brand Identity */}
        <div className="flex items-center gap-3">
          <NezhaEmblem active={isBusy} />
          <div className="flex flex-col">
            <div className="flex items-center gap-2">
              <span className="text-sm font-bold tracking-[0.12em] text-foreground sm:text-base">
                {t("brandTitle")}
              </span>
              <Badge variant="crimson" className="telemetry-mono hidden text-[10px] sm:inline-flex">
                ESP32-S3
              </Badge>
              <Badge
                variant={isConnected ? "emerald" : detectedPortsCount > 0 ? "cyan" : "secondary"}
                className="telemetry-mono text-[10px]"
              >
                <span
                  className={cn(
                    "mr-1 inline-block h-1.5 w-1.5 rounded-full",
                    isConnected
                      ? "bg-cyber-emerald animate-pulse"
                      : detectedPortsCount > 0
                        ? "bg-cyber-cyan animate-pulse"
                        : "bg-muted-foreground/60",
                  )}
                />
                <Usb className="h-3 w-3" />
                <span>{isConnected ? "SYNCED" : `${detectedPortsCount} USB`}</span>
              </Badge>
            </div>
            <span className="telemetry-mono text-[11px] text-muted-foreground">
              {t("brandSubtitle")}
            </span>
          </div>
        </div>

        {/* Right Utility Controls */}
        <div className="flex flex-wrap items-center gap-2">
          {/* Segmented Locale Switch (Auto / 中文 / EN) */}
          <div
            role="group"
            aria-label="Language"
            className="relative inline-flex items-center rounded-md border border-border/80 bg-muted/60 p-0.5"
          >
            {localeOptions.map((opt) => {
              const active = localeMode === opt.value;
              return (
                <button
                  key={opt.value}
                  type="button"
                  onClick={() => setLocaleMode(opt.value)}
                  className={cn(
                    "relative z-10 rounded-[5px] px-2.5 py-1 text-[11px] font-medium transition-colors",
                    active ? "text-foreground" : "text-muted-foreground hover:text-foreground",
                  )}
                >
                  {active && (
                    <motion.span
                      layoutId="locale-pill"
                      transition={{ type: "spring", stiffness: 460, damping: 34 }}
                      className="absolute inset-0 -z-10 rounded-[5px] bg-background shadow-sm border border-border/70"
                    />
                  )}
                  {opt.label}
                </button>
              );
            })}
          </div>

          {/* Segmented Theme Switch (Auto / Dark / Light) */}
          <div
            role="group"
            aria-label="Theme"
            className="relative inline-flex items-center rounded-md border border-border/80 bg-muted/60 p-0.5"
          >
            {themeOptions.map((opt) => {
              const active = themeMode === opt.value;
              const Icon = opt.icon;
              return (
                <button
                  key={opt.value}
                  type="button"
                  onClick={() => setThemeMode(opt.value)}
                  title={opt.label}
                  className={cn(
                    "relative z-10 inline-flex items-center gap-1 rounded-[5px] px-2 py-1 text-[11px] font-medium transition-colors",
                    active ? "text-foreground" : "text-muted-foreground hover:text-foreground",
                  )}
                >
                  {active && (
                    <motion.span
                      layoutId="theme-pill"
                      transition={{ type: "spring", stiffness: 460, damping: 34 }}
                      className="absolute inset-0 -z-10 rounded-[5px] bg-background shadow-sm border border-border/70"
                    />
                  )}
                  <Icon className="h-3.5 w-3.5" />
                  <span className="hidden md:inline">{opt.label}</span>
                </button>
              );
            })}
          </div>

          {/* Repo & Releases Links */}
          <Button variant="outline" size="sm" asChild className="h-8 gap-1.5 text-xs">
            <a
              href={`https://github.com/${repository}/releases`}
              target="_blank"
              rel="noreferrer"
            >
              <Tag className="h-3.5 w-3.5" />
              <span className="hidden sm:inline">{t("navReleases")}</span>
            </a>
          </Button>
          <Button variant="outline" size="sm" asChild className="h-8 gap-1.5 text-xs">
            <a
              href={`https://github.com/${repository}`}
              target="_blank"
              rel="noreferrer"
            >
              <Github className="h-3.5 w-3.5" />
              <span className="hidden sm:inline">{t("navGithub")}</span>
            </a>
          </Button>
        </div>
      </div>
    </header>
  );
}
