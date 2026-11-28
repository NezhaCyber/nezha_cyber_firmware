import { BookOpenCheck, Disc3, Power, ShieldAlert, Usb } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { useLocale } from "@/i18n/LocaleContext";
import type { BoardBuild } from "@/types/firmware";

interface DownloadModeGuideProps {
  board: BoardBuild | null;
}

export function DownloadModeGuide({ board }: DownloadModeGuideProps) {
  const { locale, t } = useLocale();

  const steps = [
    {
      num: "01",
      icon: Power,
      title: t("downloadStep1Title"),
      desc: t("downloadStep1Desc"),
    },
    {
      num: "02",
      icon: Disc3,
      title: t("downloadStep2Title"),
      desc: t("downloadStep2Desc"),
    },
    {
      num: "03",
      icon: ShieldAlert,
      title: t("downloadStep3Title"),
      desc: t("downloadStep3Desc"),
    },
    {
      num: "04",
      icon: Usb,
      title: t("downloadStep4Title"),
      desc: t("downloadStep4Desc"),
    },
  ];

  return (
    <Card>
      <CardHeader className="pb-3">
        <div className="flex items-center justify-between gap-2">
          <CardTitle className="flex items-center gap-2 text-sm">
            <BookOpenCheck className="h-4 w-4 text-cyber-amber" />
            <span>{t("downloadGuideTitle")}</span>
          </CardTitle>
          <Badge variant="amber" className="telemetry-mono text-[10px]">
            GPIO0
          </Badge>
        </div>
        <CardDescription>{t("downloadGuideSubtitle")}</CardDescription>
      </CardHeader>

      <CardContent className="space-y-3">
        {board && (
          <div className="rounded-md border border-cyber-crimson/35 bg-cyber-crimson/8 px-3 py-2 text-xs leading-relaxed text-foreground">
            <span className="telemetry-mono mr-1.5 font-semibold text-cyber-crimson">
              [{locale === "zh" ? board.boardNameZh : board.boardName}]
            </span>
            {locale === "zh" ? board.downloadModeHintZh : board.downloadModeHintEn}
          </div>
        )}

        <ol className="space-y-2">
          {steps.map((step) => {
            const Icon = step.icon;
            return (
              <li
                key={step.num}
                className="flex items-start gap-2.5 rounded-md border border-border/70 bg-muted/25 p-2.5"
              >
                <span className="telemetry-mono flex h-6 w-6 shrink-0 items-center justify-center rounded border border-border bg-background text-[10px] font-bold text-cyber-crimson">
                  {step.num}
                </span>
                <div className="min-w-0 space-y-0.5">
                  <div className="flex items-center gap-1.5 text-xs font-semibold text-foreground">
                    <Icon className="h-3.5 w-3.5 text-muted-foreground" />
                    <span>{step.title}</span>
                  </div>
                  <p className="text-[11px] leading-relaxed text-muted-foreground">{step.desc}</p>
                </div>
              </li>
            );
          })}
        </ol>
      </CardContent>
    </Card>
  );
}
