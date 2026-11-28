import { useEffect, useState } from "react";
import { resolveAssetUrl } from "@/lib/utils";
import {
  fetchLiveGitHubReleases,
  mergeGitHubReleasesIntoManifest,
} from "@/services/githubReleases";
import type { FirmwareManifest } from "@/types/firmware";

export type ManifestSource = "github" | "bundled";

interface UseManifestResult {
  manifest: FirmwareManifest | null;
  loading: boolean;
  syncingGitHub: boolean;
  source: ManifestSource;
  error: string | null;
  reload: () => void;
}

export function useManifest(): UseManifestResult {
  const [manifest, setManifest] = useState<FirmwareManifest | null>(null);
  const [loading, setLoading] = useState(true);
  const [syncingGitHub, setSyncingGitHub] = useState(false);
  const [source, setSource] = useState<ManifestSource>("bundled");
  const [error, setError] = useState<string | null>(null);
  const [nonce, setNonce] = useState(0);

  useEffect(() => {
    let cancelled = false;
    const controller = new AbortController();
    const url = resolveAssetUrl("firmware/manifest.json");

    setLoading(true);
    setError(null);

    fetch(url, { cache: "no-store", signal: controller.signal })
      .then(async (res) => {
        if (!res.ok) {
          throw new Error(`HTTP ${res.status} while loading ${url}`);
        }
        return (await res.json()) as FirmwareManifest;
      })
      .then(async (bundled) => {
        if (cancelled) return;
        setManifest(bundled);
        setSource("bundled");
        setLoading(false);

        // Live-link to GitHub Releases so Stable, Beta, and Archive versions + exact asset sizes stay current
        const repo = bundled.repository || "NezhaCyber/nezha_cyber_firmware";
        setSyncingGitHub(true);
        try {
          const liveReleases = await fetchLiveGitHubReleases(repo, controller.signal);
          if (!cancelled && liveReleases.length > 0) {
            const merged = mergeGitHubReleasesIntoManifest(bundled, liveReleases);
            setManifest(merged);
            setSource("github");
          }
        } catch {
          // Offline / rate-limited: gracefully keep bundled static manifest
        } finally {
          if (!cancelled) setSyncingGitHub(false);
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setError(err instanceof Error ? err.message : String(err));
          setLoading(false);
          setSyncingGitHub(false);
        }
      });

    return () => {
      cancelled = true;
      controller.abort();
    };
  }, [nonce]);

  return {
    manifest,
    loading,
    syncingGitHub,
    source,
    error,
    reload: () => setNonce((n) => n + 1),
  };
}
