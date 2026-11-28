import * as React from "react";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const badgeVariants = cva(
  "inline-flex items-center gap-1 rounded-md border px-2 py-0.5 text-[11px] font-medium tracking-wide transition-colors focus:outline-none focus:ring-2 focus:ring-ring focus:ring-offset-2",
  {
    variants: {
      variant: {
        default: "border-transparent bg-primary text-primary-foreground",
        secondary: "border-border/80 bg-secondary text-secondary-foreground",
        destructive: "border-destructive/40 bg-destructive/15 text-destructive",
        outline: "border-border text-foreground",
        crimson: "border-cyber-crimson/40 bg-cyber-crimson/12 text-cyber-crimson",
        cyan: "border-cyber-cyan/40 bg-cyber-cyan/12 text-cyber-cyan",
        amber: "border-cyber-amber/40 bg-cyber-amber/12 text-cyber-amber",
        emerald: "border-cyber-emerald/40 bg-cyber-emerald/12 text-cyber-emerald",
      },
    },
    defaultVariants: {
      variant: "default",
    },
  },
);

export interface BadgeProps
  extends React.HTMLAttributes<HTMLDivElement>,
    VariantProps<typeof badgeVariants> {}

function Badge({ className, variant, ...props }: BadgeProps) {
  return <div className={cn(badgeVariants({ variant }), className)} {...props} />;
}

export { Badge, badgeVariants };
