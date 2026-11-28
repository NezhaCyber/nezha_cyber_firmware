import { motion } from "framer-motion";
import type { FlashPhase } from "@/types/firmware";

interface CyberBackdropProps {
  phase: FlashPhase;
}

/**
 * Architectural precision grid & subtle GPU-composited telemetry ambience.
 * Responds calmly to active flashing state without distracting visual noise.
 */
export function CyberBackdrop({ phase }: CyberBackdropProps) {
  const isFlashing = phase === "flashing" || phase === "erasing" || phase === "downloading";

  return (
    <div
      aria-hidden="true"
      className="pointer-events-none fixed inset-0 -z-10 overflow-hidden select-none"
    >
      {/* Architectural coordinate grid */}
      <div className="absolute inset-0 bg-precision-grid opacity-100" />

      {/* Top atmospheric crimson/cyan vignette — restrained, instrument-grade */}
      <div
        className="absolute -top-48 left-1/2 h-[420px] w-[880px] -translate-x-1/2 rounded-full opacity-[0.14] dark:opacity-[0.18] blur-[120px] transition-colors duration-700"
        style={{
          background: isFlashing
            ? "radial-gradient(circle, hsl(var(--cyber-cyan)) 0%, hsl(var(--cyber-crimson)) 48%, transparent 72%)"
            : "radial-gradient(circle, hsl(var(--cyber-crimson)) 0%, hsl(var(--cyber-amber)) 42%, transparent 72%)",
        }}
      />

      {/* Corner calibration hairlines */}
      <svg
        className="absolute right-8 top-24 h-64 w-64 text-foreground/[0.04] dark:text-foreground/[0.055]"
        viewBox="0 0 240 240"
        fill="none"
      >
        <motion.circle
          cx="120"
          cy="120"
          r="98"
          stroke="currentColor"
          strokeWidth="1"
          strokeDasharray="4 6"
          animate={{ rotate: 360 }}
          transition={{
            duration: isFlashing ? 18 : 64,
            repeat: Infinity,
            ease: "linear",
          }}
          style={{ transformOrigin: "120px 120px" }}
        />
        <circle cx="120" cy="120" r="68" stroke="currentColor" strokeWidth="0.75" />
        <line x1="120" y1="12" x2="120" y2="228" stroke="currentColor" strokeWidth="0.75" />
        <line x1="12" y1="120" x2="228" y2="120" stroke="currentColor" strokeWidth="0.75" />
      </svg>

      {/* Subtle horizontal telemetry scanline during active flash */}
      {isFlashing && (
        <motion.div
          className="absolute inset-x-0 h-px bg-gradient-to-r from-transparent via-cyber-cyan/50 to-transparent"
          initial={{ top: "0%", opacity: 0 }}
          animate={{ top: ["0%", "100%"], opacity: [0, 0.7, 0] }}
          transition={{ duration: 3.4, repeat: Infinity, ease: "linear" }}
        />
      )}
    </div>
  );
}
