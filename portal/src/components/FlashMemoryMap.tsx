import { motion } from "framer-motion";
import { parseHexOffset } from "@/lib/utils";
import type { FlashPhase, PartitionDescriptor, PartitionRole } from "@/types/firmware";

interface FlashMemoryMapProps {
  partitions: PartitionDescriptor[];
  activeOffset?: string;
  phase: FlashPhase;
}

const ROLE_STYLES: Record<PartitionRole, { bar: string; dot: string }> = {
  boot: { bar: "bg-cyber-amber", dot: "bg-cyber-amber" },
  table: { bar: "bg-violet-500", dot: "bg-violet-500" },
  nvs: { bar: "bg-slate-500", dot: "bg-slate-500" },
  app: { bar: "bg-cyber-crimson", dot: "bg-cyber-crimson" },
  fs: { bar: "bg-cyber-cyan", dot: "bg-cyber-cyan" },
  dump: { bar: "bg-zinc-500", dot: "bg-zinc-500" },
};

/**
 * Proportional visual layout of the board's real partition table CSV.
 * Highlights the active partition offset during live flashing.
 */
export function FlashMemoryMap({
  partitions,
  activeOffset,
  phase,
}: FlashMemoryMapProps) {
  if (partitions.length === 0) return null;

  const isFlashing = phase === "flashing" || phase === "erasing";
  const parsed = partitions.map((p) => ({
    ...p,
    sizeNum: Math.max(1, parseHexOffset(p.size)),
    offsetNum: parseHexOffset(p.offset),
  }));

  // Log-balanced visual weighting so 32KB bootloader/table segments stay legible next to 9MB LittleFS
  const weights = parsed.map((p) => Math.pow(p.sizeNum, 0.28));
  const totalWeight = weights.reduce((a, b) => a + b, 0);
  const activeNum = activeOffset ? parseHexOffset(activeOffset) : null;

  return (
    <div className="space-y-2.5">
      <div className="relative flex h-3.5 w-full overflow-hidden rounded-md border border-border/80 bg-secondary/70 p-0.5 gap-0.5">
        {parsed.map((part, idx) => {
          const widthPct = (weights[idx] / totalWeight) * 100;
          const style = ROLE_STYLES[part.role];
          const isTargeted =
            isFlashing &&
            activeNum !== null &&
            (activeNum === 0 || activeNum === part.offsetNum);

          return (
            <div
              key={`${part.label}-${part.offset}`}
              style={{ width: `${widthPct}%` }}
              title={`${part.label} @ ${part.offset} (${part.size})`}
              className={`relative h-full rounded-[3px] ${style.bar} transition-opacity ${
                isFlashing && !isTargeted ? "opacity-35" : "opacity-90"
              }`}
            >
              {isTargeted && (
                <motion.span
                  className="absolute inset-0 bg-gradient-to-r from-transparent via-white/55 to-transparent"
                  animate={{ x: ["-100%", "100%"] }}
                  transition={{ duration: 1.1, repeat: Infinity, ease: "linear" }}
                />
              )}
            </div>
          );
        })}
      </div>

      {/* Compact Offset Legend */}
      <div className="grid grid-cols-2 gap-1.5 sm:grid-cols-3">
        {parsed.map((part) => {
          const style = ROLE_STYLES[part.role];
          return (
            <div
              key={`legend-${part.label}-${part.offset}`}
              className="flex items-center justify-between gap-1.5 rounded border border-border/60 bg-background/50 px-2 py-1 text-[11px]"
            >
              <span className="flex items-center gap-1.5 truncate">
                <span className={`h-2 w-2 shrink-0 rounded-full ${style.dot}`} />
                <span className="truncate font-medium text-foreground/90">{part.label}</span>
              </span>
              <span className="telemetry-mono shrink-0 text-[10px] text-muted-foreground">
                {part.offset}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
}
