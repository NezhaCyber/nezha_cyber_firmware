import { motion } from "framer-motion";
import {
  FolderSync,
  Github,
  Laptop,
  Moon,
  Sun,
  Terminal,
  Zap,
} from "lucide-react";
import { NezhaEmblem } from "@/components/NezhaEmblem";
import { Button } from "@/components/ui/button";
import { useTheme, type ThemeMode } from "@/context/ThemeContext";
import { useLocale } from "@/i18n/LocaleContext";
import type { LocaleMode } from "@/i18n/translations";
import { cn } from "@/lib/utils";
import type { FlashPhase } from "@/types/firmware";

export type PortalTab = "flash" | "files" | "console";

interface TopBarProps {
  repository: string;
  activeTab: PortalTab;
  onChangeTab: (tab: PortalTab) => void;
  phase: FlashPhase;
  usbTransferActive: boolean;
}

export function TopBar({
  repository,
  activeTab,
  onChangeTab,
  phase,
  usbTransferActive,
}: TopBarProps) {
  const { mode: themeMode, setMode: setThemeMode } = useTheme();
  const { mode: localeMode, setMode: setLocaleMode, t } = useLocale();

  const isBusy =
    usbTransferActive ||
    phase === "connecting" ||
    phase === "downloading" ||
    phase === "erasing" ||
    phase === "flashing" ||
    phase === "resetting";

  const navTabs: { id: PortalTab; label: string; icon: typeof Zap }[] = [
    { id: "flash", label: t("tabFlash"), icon: Zap },
    { id: "files", label: t("tabFiles"), icon: FolderSync },
    { id: "console", label: t("tabConsole"), icon: Terminal },
  ];

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
    <header className="sticky top-0 z-40 border-b border-border/75 bg-background/80 backdrop-blur-xl">
      <div className="mx-auto flex max-w-5xl flex-wrap items-center justify-between gap-3 px-4 py-3 sm:px-6">
        {/* Brand */}
        <div className="flex items-center gap-2.5">
          <NezhaEmblem active={isBusy} className="h-9 w-9" />
          <div className="leading-tight">
            <div className="text-sm font-bold tracking-[0.1em] text-foreground">
              {t("brandTitle")}
            </div>
            <div className="telemetry-mono text-[10px] text-muted-foreground">
              {t("brandSubtitle")}
            </div>
          </div>
        </div>

        {/* Center Primary Mode Switcher (3 Clear Tasks) */}
        <nav
          aria-label="Workspace mode"
          className="order-3 flex w-full items-center justify-center rounded-lg border border-border/80 bg-muted/60 p-1 sm:order-2 sm:w-auto"
        >
          {navTabs.map((tab) => {
            const active = activeTab === tab.id;
            const Icon = tab.icon;
            return (
              <button
                key={tab.id}
                type="button"
                onClick={() => onChangeTab(tab.id)}
                className={cn(
                  "relative z-10 flex flex-1 items-center justify-center gap-1.5 rounded-md px-3.5 py-1.5 text-xs font-semibold transition-colors sm:flex-initial",
                  active ? "text-foreground" : "text-muted-foreground hover:text-foreground",
                )}
              >
                {active && (
                  <motion.span
                    layoutId="workspace-nav-pill"
                    transition={{ type: "spring", stiffness: 420, damping: 32 }}
                    className="absolute inset-0 -z-10 rounded-md border border-border/80 bg-background shadow-sm"
                  />
                )}
                <Icon
                  className={cn(
                    "h-3.5 w-3.5",
                    active && tab.id === "flash" && "text-cyber-crimson",
                    active && tab.id === "files" && "text-cyber-cyan",
                    active && tab.id === "console" && "text-cyber-amber",
                  )}
                />
                <span>{tab.label}</span>
              </button>
            );
          })}
        </nav>

        {/* Compact Locale + Theme + GitHub */}
        <div className="order-2 flex items-center gap-1.5 sm:order-3">
          <div
            role="group"
            aria-label="Language"
            className="inline-flex items-center rounded-md border border-border/80 bg-muted/60 p-0.5"
          >
            {localeOptions.map((opt) => {
              const active = localeMode === opt.value;
              return (
                <button
                  key={opt.value}
                  type="button"
                  onClick={() => setLocaleMode(opt.value)}
                  className={cn(
                    "relative z-10 rounded-[5px] px-2 py-1 text-[11px] font-medium transition-colors",
                    active ? "text-foreground" : "text-muted-foreground hover:text-foreground",
                  )}
                >
                  {active && (
                    <motion.span
                      layoutId="locale-pill"
                      transition={{ type: "spring", stiffness: 460, damping: 34 }}
                      className="absolute inset-0 -z-10 rounded-[5px] border border-border/70 bg-background shadow-sm"
                    />
                  )}
                  {opt.label}
                </button>
              );
            })}
          </div>

          <div
            role="group"
            aria-label="Theme"
            className="inline-flex items-center rounded-md border border-border/80 bg-muted/60 p-0.5"
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
                    "relative z-10 rounded-[5px] p-1.5 transition-colors",
                    active ? "text-foreground" : "text-muted-foreground hover:text-foreground",
                  )}
                >
                  {active && (
                    <motion.span
                      layoutId="theme-pill"
                      transition={{ type: "spring", stiffness: 460, damping: 34 }}
                      className="absolute inset-0 -z-10 rounded-[5px] border border-border/70 bg-background shadow-sm"
                    />
                  )}
                  <Icon className="h-3.5 w-3.5" />
                </button>
              );
            })}
          </div>

          <Button variant="ghost" size="icon" asChild className="h-8 w-8">
            <a
              href={`https://github.com/${repository}`}
              target="_blank"
              rel="noreferrer"
              title={t("navGithub")}
            >
              <Github className="h-4 w-4" />
            </a>
          </Button>
        </div>
      </div>
    </header>
  );
}
