import { motion } from "framer-motion";
import { cn } from "@/lib/utils";

interface NezhaEmblemProps {
  active?: boolean;
  className?: string;
}

/**
 * Geometric Nezha Qiankun Ring (乾坤圈) + Fire-Tipped Spear Lotus Crest.
 * Rotates its calibrated outer ring smoothly when a flash or sync operation is live.
 */
export function NezhaEmblem({ active = false, className }: NezhaEmblemProps) {
  return (
    <div className={cn("relative flex h-10 w-10 items-center justify-center", className)}>
      <svg viewBox="0 0 48 48" fill="none" className="h-full w-full">
        {/* Outer Qiankun calibration ring */}
        <motion.g
          animate={{ rotate: active ? 360 : 0 }}
          transition={
            active
              ? { duration: 4.5, repeat: Infinity, ease: "linear" }
              : { duration: 0.6, ease: "easeOut" }
          }
          style={{ transformOrigin: "24px 24px" }}
        >
          <circle
            cx="24"
            cy="24"
            r="20.5"
            className="stroke-cyber-amber/75"
            strokeWidth="1.5"
            strokeDasharray="10 5 3 5"
          />
          <circle cx="24" cy="3.5" r="1.6" className="fill-cyber-crimson" />
          <circle cx="24" cy="44.5" r="1.6" className="fill-cyber-cyan" />
        </motion.g>

        {/* Inner obsidian / porcelain core housing */}
        <circle
          cx="24"
          cy="24"
          r="15.5"
          className="fill-card stroke-border"
          strokeWidth="1.2"
        />

        {/* Stylized Nezha flame spear / demon mark */}
        <motion.path
          d="M24 10.5L30.5 22.5L24 37.5L17.5 22.5L24 10.5Z"
          className="fill-cyber-crimson"
          animate={active ? { scale: [1, 1.06, 1] } : { scale: 1 }}
          transition={{ duration: 1.4, repeat: active ? Infinity : 0, ease: "easeInOut" }}
          style={{ transformOrigin: "24px 24px" }}
        />
        <path
          d="M24 15.5L27.4 22.8L24 31.5L20.6 22.8L24 15.5Z"
          className="fill-cyber-amber"
        />
        <circle cx="24" cy="22.8" r="2.1" className="fill-background" />
      </svg>
    </div>
  );
}
