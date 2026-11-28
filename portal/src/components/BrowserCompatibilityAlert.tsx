import { AlertTriangle, Chrome, Globe } from "lucide-react";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { useLocale } from "@/i18n/LocaleContext";

interface BrowserCompatibilityAlertProps {
  isSupported: boolean;
}

export function BrowserCompatibilityAlert({ isSupported }: BrowserCompatibilityAlertProps) {
  const { t } = useLocale();

  if (isSupported) return null;

  return (
    <Alert variant="destructive" className="mb-6 border-destructive/60 bg-destructive/10">
      <AlertTriangle className="h-4 w-4" />
      <AlertTitle className="flex items-center gap-2 text-sm">
        <span>{t("browserUnsupportedTitle")}</span>
      </AlertTitle>
      <AlertDescription className="mt-1.5 flex flex-col gap-2 sm:flex-row sm:items-center sm:justify-between">
        <span>{t("browserUnsupportedDesc")}</span>
        <span className="telemetry-mono inline-flex shrink-0 items-center gap-1.5 rounded border border-destructive/30 bg-background/60 px-2.5 py-1 text-[11px] text-foreground">
          <Chrome className="h-3.5 w-3.5 text-cyber-amber" />
          <Globe className="h-3.5 w-3.5 text-cyber-cyan" />
          Chrome / Edge / Opera
        </span>
      </AlertDescription>
    </Alert>
  );
}
