import * as React from "react";
import * as ProgressPrimitive from "@radix-ui/react-progress";
import { cn } from "@/lib/utils";

interface ProgressProps extends React.ComponentPropsWithoutRef<typeof ProgressPrimitive.Root> {
  active?: boolean;
  tone?: "crimson" | "cyan" | "amber" | "emerald";
}

const toneClasses: Record<NonNullable<ProgressProps["tone"]>, string> = {
  crimson: "bg-cyber-crimson",
  cyan: "bg-cyber-cyan",
  amber: "bg-cyber-amber",
  emerald: "bg-cyber-emerald",
};

const Progress = React.forwardRef<React.ComponentRef<typeof ProgressPrimitive.Root>, ProgressProps>(
  ({ className, value, active = false, tone = "crimson", ...props }, ref) => (
    <ProgressPrimitive.Root
      ref={ref}
      className={cn("relative h-2.5 w-full overflow-hidden rounded-full bg-secondary", className)}
      {...props}
    >
      <ProgressPrimitive.Indicator
        className={cn(
          "relative h-full w-full flex-1 transition-transform duration-200 ease-out",
          toneClasses[tone],
        )}
        style={{ transform: `translateX(-${100 - Math.min(100, Math.max(0, value || 0))}%)` }}
      >
        {active && (
          <span className="absolute inset-0 bg-gradient-to-r from-transparent via-white/35 to-transparent animate-scan-sweep" />
        )}
      </ProgressPrimitive.Indicator>
    </ProgressPrimitive.Root>
  ),
);
Progress.displayName = ProgressPrimitive.Root.displayName;

export { Progress };
