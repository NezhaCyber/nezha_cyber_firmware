import { useEffect, useState } from "react";
import { resolveAssetUrl } from "@/lib/utils";
import type { FirmwareManifest } from "@/types/firmware";

interface UseManifestResult {
  manifest: FirmwareManifest | null;
  loading: boolean;
  error: string | null;
  reload: () => void;
}

export function useManifest(): UseManifestResult {
  const [manifest, setManifest] = useState<FirmwareManifest | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [nonce, setNonce] = useState(0);

  useEffect(() => {
    let cancelled = false;
    const url = resolveAssetUrl("firmware/manifest.json");

    setLoading(true);
    setError(null);

    fetch(url, { cache: "no-store" })
      .then(async (res) => {
        if (!res.ok) {
          throw new Error(`HTTP ${res.status} while loading ${url}`);
        }
        return (await res.json()) as FirmwareManifest;
      })
      .then((data) => {
        if (!cancelled) {
          setManifest(data);
          setLoading(false);
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setError(err instanceof Error ? err.message : String(err));
          setLoading(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [nonce]);

  return {
    manifest,
    loading,
    error,
    reload: () => setNonce((n) => n + 1),
  };
}
