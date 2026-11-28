import React, { createContext, useContext, useEffect, useMemo, useState } from "react";
import {
  type LocaleMode,
  type ResolvedLocale,
  type TranslationKey,
  translations,
} from "@/i18n/translations";

interface LocaleContextValue {
  mode: LocaleMode;
  locale: ResolvedLocale;
  setMode: (mode: LocaleMode) => void;
  t: (key: TranslationKey) => string;
}

const STORAGE_KEY = "nezha-portal-locale";

const LocaleContext = createContext<LocaleContextValue | null>(null);

function detectBrowserLocale(): ResolvedLocale {
  if (typeof navigator === "undefined") return "zh";
  const langs = navigator.languages?.length ? navigator.languages : [navigator.language];
  for (const lang of langs) {
    if (lang && lang.toLowerCase().startsWith("zh")) {
      return "zh";
    }
  }
  return "en";
}

function readStoredLocale(): LocaleMode {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw === "auto" || raw === "zh" || raw === "en") return raw;
  } catch {
    // Ignore storage access restrictions
  }
  return "auto";
}

export function LocaleProvider({ children }: { children: React.ReactNode }) {
  const [mode, setModeState] = useState<LocaleMode>(readStoredLocale);
  const [browserLocale, setBrowserLocale] = useState<ResolvedLocale>(detectBrowserLocale);

  useEffect(() => {
    const onLanguageChange = () => setBrowserLocale(detectBrowserLocale());
    window.addEventListener("languagechange", onLanguageChange);
    return () => window.removeEventListener("languagechange", onLanguageChange);
  }, []);

  const locale: ResolvedLocale = mode === "auto" ? browserLocale : mode;

  useEffect(() => {
    document.documentElement.lang = locale === "zh" ? "zh-CN" : "en";
  }, [locale]);

  const setMode = (next: LocaleMode) => {
    setModeState(next);
    try {
      localStorage.setItem(STORAGE_KEY, next);
    } catch {
      // Ignore storage access restrictions
    }
  };

  const value = useMemo<LocaleContextValue>(
    () => ({
      mode,
      locale,
      setMode,
      t: (key: TranslationKey) => translations[locale][key],
    }),
    [mode, locale],
  );

  return <LocaleContext.Provider value={value}>{children}</LocaleContext.Provider>;
}

export function useLocale(): LocaleContextValue {
  const ctx = useContext(LocaleContext);
  if (!ctx) {
    throw new Error("useLocale must be used inside LocaleProvider");
  }
  return ctx;
}
