import { motion } from "framer-motion";
import { Cpu, HardDrive, Monitor, Usb } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { useLocale } from "@/i18n/LocaleContext";
import type { BoardBuild, DetectedChipInfo, FlashPhase } from "@/types/firmware";

interface HardwarePreviewProps {
  board: BoardBuild | null;
  chipInfo: DetectedChipInfo | null;
  phase: FlashPhase;
  percent: number;
}

/**
 * Precision-drafted vector hardware schematic reacting to selected target board
 * (LILYGO T-Deck / T-Deck Plus keyboard+trackball vs compact ESP32 handheld)
 * and live Web Serial link / flashing progress.
 */
export function HardwarePreview({
  board,
  chipInfo,
  phase,
  percent,
}: HardwarePreviewProps) {
  const { locale, t } = useLocale();
  const isTDeck = !board || board.id.includes("t-deck");
  const isFlashing = phase === "flashing" || phase === "erasing" || phase === "downloading";
  const isConnected = chipInfo !== null;
  const isDone = phase === "done";

  return (
    <Card className="overflow-hidden">
      <CardHeader className="pb-3">
        <div className="flex items-center justify-between gap-2">
          <CardTitle className="flex items-center gap-2 text-sm">
            <Cpu className="h-4 w-4 text-cyber-crimson" />
            <span>{t("hardwareSchematicTitle")}</span>
          </CardTitle>
          <Badge
            variant={isFlashing ? "cyan" : isDone ? "emerald" : isConnected ? "amber" : "secondary"}
            className="telemetry-mono text-[10px]"
          >
            {board?.mcu ?? "ESP32-S3"}
          </Badge>
        </div>
      </CardHeader>

      <CardContent className="space-y-4">
        {/* Vector Hardware Silhouette */}
        <div className="relative flex items-center justify-center rounded-lg border border-border/80 bg-muted/35 p-4 dark:bg-[#05070c]">
          <div className="pointer-events-none absolute inset-0 bg-precision-grid opacity-55" />

          <svg
            viewBox="0 0 360 225"
            className="relative z-10 h-auto w-full max-w-[340px]"
            fill="none"
            role="img"
            aria-label={board ? board.boardName : "LILYGO T-Deck"}
          >
            {/* Outer chassis */}
            <rect
              x="24"
              y="14"
              width="312"
              height="182"
              rx="18"
              className="fill-card stroke-border"
              strokeWidth="1.8"
            />
            {/* Inner milled bezel */}
            <rect
              x="31"
              y="21"
              width="298"
              height="168"
              rx="13"
              className="stroke-border/70"
              strokeWidth="1"
              strokeDasharray="3 3"
            />

            {/* TFT Screen Window */}
            <rect
              x="44"
              y="30"
              width={isTDeck ? 210 : 272}
              height={isTDeck ? 94 : 112}
              rx="8"
              className="fill-[#07090f] stroke-border"
              strokeWidth="1.4"
            />

            {/* Screen Live Telemetry HUD */}
            <g>
              <text
                x="56"
                y="50"
                fill="#ef233c"
                fontSize="9"
                fontFamily="JetBrains Mono, monospace"
                fontWeight="700"
                letterSpacing="1.4"
              >
                NEZHA CYBER OS
              </text>
              <text
                x="56"
                y="65"
                fill="#94a3b8"
                fontSize="7.5"
                fontFamily="JetBrains Mono, monospace"
              >
                {isFlashing
                  ? `WRITING FLASH ${percent.toFixed(0)}%`
                  : isDone
                    ? "FLASH COMPLETE · READY"
                    : isConnected
                      ? `SYNC: ${chipInfo.chipName}`
                      : `TARGET: ${board?.env ?? "lilygo-t-deck"}`}
              </text>

              {/* Mini animated waveform / flash progress inside TFT */}
              <rect
                x="56"
                y="76"
                width={isTDeck ? 186 : 244}
                height="6"
                rx="3"
                fill="#151926"
              />
              <motion.rect
                x="56"
                y="76"
                height="6"
                rx="3"
                fill={isDone ? "#10b981" : isFlashing ? "#06b6d4" : "#ef233c"}
                animate={{
                  width: isFlashing
                    ? Math.max(8, ((isTDeck ? 186 : 244) * percent) / 100)
                    : isDone
                      ? isTDeck
                        ? 186
                        : 244
                      : isConnected
                        ? 54
                        : 26,
                }}
                transition={{ duration: 0.25 }}
              />

              {/* Subtle RF / radar grid inside screen */}
              <path
                d={
                  isTDeck
                    ? "M56 106 L82 99 L104 110 L132 95 L158 108 L188 97 L218 105 L242 101"
                    : "M56 116 L90 104 L125 122 L162 100 L198 119 L236 104 L274 115 L300 109"
                }
                stroke={isFlashing ? "#06b6d4" : "#f59e0b"}
                strokeWidth="1.35"
                strokeLinecap="round"
                strokeLinejoin="round"
                opacity="0.85"
              />
            </g>

            {isTDeck ? (
              <>
                {/* Right Side: Trackball (GPIO0 BOOT) + Speaker */}
                <g>
                  <motion.circle
                    cx="290"
                    cy="66"
                    r="21"
                    className="stroke-cyber-crimson"
                    strokeWidth="1.5"
                    strokeDasharray="4 3"
                    animate={
                      !isConnected
                        ? { opacity: [0.35, 1, 0.35], scale: [1, 1.07, 1] }
                        : { opacity: 0.4, scale: 1 }
                    }
                    transition={{ duration: 2.2, repeat: Infinity, ease: "easeInOut" }}
                    style={{ transformOrigin: "290px 66px" }}
                  />
                  <circle
                    cx="290"
                    cy="66"
                    r="14"
                    className="fill-secondary stroke-border"
                    strokeWidth="1.4"
                  />
                  <circle cx="290" cy="66" r="6" className="fill-foreground/80" />
                  <text
                    x="290"
                    y="100"
                    textAnchor="middle"
                    className="fill-cyber-crimson"
                    fontSize="7"
                    fontFamily="JetBrains Mono, monospace"
                    fontWeight="600"
                  >
                    GPIO0 BOOT
                  </text>
                  <text
                    x="290"
                    y="110"
                    textAnchor="middle"
                    className="fill-muted-foreground"
                    fontSize="6.5"
                    fontFamily="JetBrains Mono, monospace"
                  >
                    TRACKBALL
                  </text>
                </g>

                {/* QWERTY Keyboard Matrix */}
                <g opacity="0.85">
                  {[0, 1, 2].map((row) =>
                    Array.from({ length: 10 }).map((_, col) => (
                      <rect
                        key={`${row}-${col}`}
                        x={46 + col * 27.2}
                        y={135 + row * 15}
                        width="22"
                        height="10.5"
                        rx="2.5"
                        className="fill-secondary/90 stroke-border"
                        strokeWidth="0.9"
                      />
                    )),
                  )}
                </g>
              </>
            ) : (
              /* Compact Handheld Buttons */
              <g>
                <rect
                  x="130"
                  y="156"
                  width="100"
                  height="20"
                  rx="6"
                  className="fill-secondary stroke-cyber-crimson/70"
                  strokeWidth="1.2"
                />
                <text
                  x="180"
                  y="169"
                  textAnchor="middle"
                  className="fill-foreground"
                  fontSize="7.5"
                  fontFamily="JetBrains Mono, monospace"
                >
                  MAIN / G0 BOOT
                </text>
              </g>
            )}

            {/* USB-C Port & Animated Data Trace */}
            <rect
              x="160"
              y="194"
              width="40"
              height="10"
              rx="3"
              className="fill-secondary stroke-border"
              strokeWidth="1.2"
            />
            <line
              x1="180"
              y1="204"
              x2="180"
              y2="223"
              className={
                isFlashing
                  ? "stroke-cyber-cyan"
                  : isConnected
                    ? "stroke-cyber-emerald"
                    : "stroke-border"
              }
              strokeWidth="2.5"
              strokeDasharray={isFlashing ? "4 4" : undefined}
            />
            {isFlashing && (
              <motion.circle
                cx="180"
                r="3.2"
                className="fill-cyber-cyan"
                animate={{ cy: [222, 198] }}
                transition={{ duration: 0.7, repeat: Infinity, ease: "linear" }}
              />
            )}
          </svg>
        </div>

        {/* Concise Spec Readout Strip */}
        {board && (
          <div className="grid grid-cols-2 gap-2 text-xs">
            <div className="rounded-md border border-border/75 bg-muted/35 p-2.5">
              <div className="flex items-center gap-1.5 text-[10px] uppercase tracking-wider text-muted-foreground">
                <HardDrive className="h-3 w-3 text-cyber-amber" />
                {t("hardwareFlash")} / {t("hardwarePsram")}
              </div>
              <div className="telemetry-mono mt-1 font-medium text-foreground">
                {board.flashSize} · {board.psram}
              </div>
            </div>

            <div className="rounded-md border border-border/75 bg-muted/35 p-2.5">
              <div className="flex items-center gap-1.5 text-[10px] uppercase tracking-wider text-muted-foreground">
                <Monitor className="h-3 w-3 text-cyber-cyan" />
                {t("hardwareDisplay")}
              </div>
              <div className="telemetry-mono mt-1 truncate font-medium text-foreground" title={board.display}>
                {board.display}
              </div>
            </div>

            <div className="col-span-2 flex items-center justify-between rounded-md border border-border/75 bg-muted/35 px-3 py-2">
              <span className="flex items-center gap-1.5 text-[11px] text-muted-foreground">
                <Usb className="h-3.5 w-3.5 text-cyber-crimson" />
                {locale === "zh" ? board.boardNameZh : board.boardName}
              </span>
              <span className="telemetry-mono text-[11px] text-muted-foreground">
                {board.partitionTable}
              </span>
            </div>
          </div>
        )}
      </CardContent>
    </Card>
  );
}
