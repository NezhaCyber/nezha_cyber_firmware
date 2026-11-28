import type {
  BoardBuild,
  DetectedChipInfo,
  FirmwareEdition,
  FirmwareManifest,
  FirmwareRelease,
  ReleaseChannel,
} from "../types/firmware.js";

export interface GitHubReleaseAssetApi {
  name: string;
  size: number;
  browser_download_url: string;
}

export interface GitHubReleaseApiItem {
  tag_name: string;
  name?: string | null;
  prerelease: boolean;
  draft: boolean;
  published_at?: string | null;
  created_at?: string | null;
  html_url?: string | null;
  body?: string | null;
  assets: GitHubReleaseAssetApi[];
}

export interface BoardCompatibilityResult {
  compatible: boolean;
  chipMismatch: boolean;
  flashOverflow: boolean;
  requiredFlashMB: number;
  detectedFlashMB: number;
  binarySizeBytes: number | null;
}

export function parseFlashSizeMB(flashSize: string, explicitMB?: number): number {
  if (typeof explicitMB === "number" && Number.isFinite(explicitMB) && explicitMB > 0) {
    return explicitMB;
  }
  const m = String(flashSize || "").match(/(\d+)/);
  return m ? Number.parseInt(m[1], 10) : 4;
}

export function inferEdition(board: Pick<BoardBuild, "env" | "flashSize" | "flashSizeMB" | "edition">): FirmwareEdition {
  if (board.edition) return board.edition;
  if (board.env.startsWith("LAUNCHER_")) return "lite";
  const mb = parseFlashSizeMB(board.flashSize, board.flashSizeMB);
  if (mb <= 4) return "compact";
  if (mb <= 8) return "standard";
  return "full";
}

export function computeFlashUsagePct(board: BoardBuild): number | null {
  if (typeof board.flashUsagePct === "number" && Number.isFinite(board.flashUsagePct)) {
    return board.flashUsagePct;
  }
  const appBytes = board.otaSizeBytes ?? board.binarySizeBytes ?? null;
  if (!appBytes || appBytes <= 0) return null;
  const limit =
    board.maxAppBytes && board.maxAppBytes > 0
      ? board.maxAppBytes
      : parseFlashSizeMB(board.flashSize, board.flashSizeMB) * 1024 * 1024;
  if (limit <= 0) return null;
  return Math.round((appBytes / limit) * 1000) / 10;
}

/**
 * Validates both MCU architecture AND flash memory capacity so a 4 MB device
 * (e.g. M5StickC Plus 1.1) can never be flashed with an 8 MB / 16 MB firmware.
 */
export function checkBoardFlashCompatibility(
  board: BoardBuild | null,
  chipInfo: DetectedChipInfo | null,
): BoardCompatibilityResult {
  if (!board) {
    return {
      compatible: true,
      chipMismatch: false,
      flashOverflow: false,
      requiredFlashMB: 0,
      detectedFlashMB: chipInfo?.flashSizeMB ?? 0,
      binarySizeBytes: null,
    };
  }
  const requiredFlashMB = parseFlashSizeMB(board.flashSize, board.flashSizeMB);
  const detectedFlashMB = chipInfo?.flashSizeMB ?? 0;
  const binarySizeBytes = board.binarySizeBytes ?? board.flashModes.merged.parts[0]?.sizeBytes ?? null;

  if (!chipInfo) {
    return {
      compatible: true,
      chipMismatch: false,
      flashOverflow: false,
      requiredFlashMB,
      detectedFlashMB: 0,
      binarySizeBytes,
    };
  }

  const chipMismatch =
    chipInfo.chipFamily !== "unknown" && chipInfo.chipFamily !== board.chipFamily;

  const detectedFlashBytes = detectedFlashMB > 0 ? detectedFlashMB * 1024 * 1024 : 0;
  const flashOverflow =
    detectedFlashMB > 0 &&
    (requiredFlashMB > detectedFlashMB ||
      (binarySizeBytes !== null && binarySizeBytes > detectedFlashBytes));

  return {
    compatible: !chipMismatch && !flashOverflow,
    chipMismatch,
    flashOverflow,
    requiredFlashMB,
    detectedFlashMB,
    binarySizeBytes,
  };
}

/**
 * Picks the best compatible board for the connected chip + flash capacity.
 * Example: ESP32 with 4 MB flash automatically selects m5stack-cplus1_1 (<4 MB),
 * never an 8 MB or 16 MB ESP32 image.
 */
export function pickBestCompatibleBoard(
  builds: BoardBuild[],
  chipInfo: DetectedChipInfo,
  preferredBoardId?: string,
): BoardBuild | null {
  if (builds.length === 0) return null;
  const compatible = builds.filter((b) => checkBoardFlashCompatibility(b, chipInfo).compatible);
  if (compatible.length === 0) return null;

  if (preferredBoardId) {
    const exact = compatible.find((b) => b.id === preferredBoardId);
    if (exact) return exact;
  }

  // Prefer non-lite featured build matching chip + flash tier, then compact <4MB, then lite
  const featured = compatible.find((b) => b.featured && inferEdition(b) !== "lite");
  if (featured) return featured;

  const nonLite = compatible.find((b) => inferEdition(b) !== "lite");
  return nonLite ?? compatible[0];
}

function summarizeReleaseBody(body: string | null | undefined, fallback: string): string {
  if (!body) return fallback;
  for (const raw of body.split(/\r?\n/)) {
    const clean = raw.trim().replace(/^[#>*\-\s]+/, "").trim();
    if (clean.length > 0 && !clean.startsWith("|") && !clean.startsWith("```")) {
      return clean.slice(0, 180);
    }
  }
  return fallback;
}

/**
 * Merges live GitHub Releases (Stable, Beta/Pre-release, Archive) into the bundled
 * portal manifest so users can browse every GitHub Release version and inspect real
 * per-board memory sizes right in the browser.
 */
export function mergeGitHubReleasesIntoManifest(
  base: FirmwareManifest,
  ghReleases: GitHubReleaseApiItem[],
): FirmwareManifest {
  const published = ghReleases.filter((r) => r && !r.draft && Boolean(r.tag_name));
  if (published.length === 0) {
    return base;
  }

  // Build reusable board template lookup from all bundled releases
  const templateByEnv = new Map<string, BoardBuild>();
  const existingReleaseByTag = new Map<string, FirmwareRelease>();
  for (const rel of base.releases) {
    existingReleaseByTag.set(rel.version, rel);
    for (const b of rel.builds) {
      if (!templateByEnv.has(b.env)) {
        templateByEnv.set(b.env, b);
      }
    }
  }
  const orderedTemplates = Array.from(templateByEnv.values());

  let seenFirstStable = false;
  const mergedReleases: FirmwareRelease[] = [];

  for (const gh of published) {
    const tag = gh.tag_name.trim();
    const isBeta = Boolean(gh.prerelease) || tag.includes("-");
    let channel: ReleaseChannel;
    if (isBeta) {
      channel = "beta";
    } else if (!seenFirstStable) {
      channel = "stable";
      seenFirstStable = true;
    } else {
      channel = "archive";
    }

    const existing = existingReleaseByTag.get(tag);
    const existingByEnv = new Map<string, BoardBuild>(
      (existing?.builds ?? []).map((b) => [b.env, b]),
    );

    const assetsByName = new Map<string, GitHubReleaseAssetApi>(
      (gh.assets ?? []).map((a) => [a.name, a]),
    );
    const hasBinaryAssets = Array.from(assetsByName.keys()).some(
      (n) => n.startsWith("NezhaCyber-") && n.endsWith(".bin") && !n.endsWith("-ota.bin"),
    );

    const builds: BoardBuild[] = [];
    for (const tpl of orderedTemplates) {
      const factoryName = `NezhaCyber-${tpl.env}.bin`;
      const otaName = `NezhaCyber-${tpl.env}-ota.bin`;
      const factoryAsset = assetsByName.get(factoryName);
      const otaAsset = assetsByName.get(otaName);
      const ex = existingByEnv.get(tpl.env);

      // When the GitHub release contains .bin assets, only expose boards present in that release
      if (hasBinaryAssets && !factoryAsset && !ex) {
        continue;
      }

      const sourceBoard = ex ?? tpl;
      const factorySize = factoryAsset?.size ?? sourceBoard.binarySizeBytes ?? null;
      const otaSize = otaAsset?.size ?? sourceBoard.otaSizeBytes ?? null;
      const maxApp = sourceBoard.maxAppBytes ?? parseFlashSizeMB(sourceBoard.flashSize, sourceBoard.flashSizeMB) * 1024 * 1024;
      const effectiveApp = otaSize ?? factorySize;
      const flashUsagePct =
        sourceBoard.flashUsagePct ??
        (effectiveApp && maxApp > 0 ? Math.round((effectiveApp / maxApp) * 1000) / 10 : null);

      const downloadUrl = factoryAsset?.browser_download_url ?? sourceBoard.downloadUrl ?? null;
      const staticPath =
        ex?.flashModes.merged.parts[0]?.path ?? `firmware/releases/${tag}/${factoryName}`;

      builds.push({
        ...sourceBoard,
        flashSizeMB: parseFlashSizeMB(sourceBoard.flashSize, sourceBoard.flashSizeMB),
        edition: inferEdition(sourceBoard),
        binarySizeBytes: factorySize,
        otaSizeBytes: otaSize,
        flashUsagePct,
        downloadUrl,
        staticReady: ex?.staticReady ?? false,
        flashModes: {
          ...sourceBoard.flashModes,
          merged: {
            ...sourceBoard.flashModes.merged,
            parts: [
              {
                name: factoryName,
                label: "Merged Factory Image",
                offset: "0x0",
                path: staticPath,
                sizeBytes: factorySize,
                downloadUrl,
              },
            ],
          },
        },
      });
    }

    if (builds.length === 0) continue;

    const releaseDate = (gh.published_at || gh.created_at || existing?.releaseDate || "").slice(0, 10);
    const defaultEn = `GitHub Release ${tag} (${channel}).`;
    const defaultZh = `GitHub 发行版 ${tag}（${channel === "beta" ? "测试版" : "正式版"}）。`;

    mergedReleases.push({
      version: tag,
      channel,
      releaseDate,
      releaseUrl: gh.html_url ?? existing?.releaseUrl ?? `https://github.com/${base.repository}/releases/tag/${tag}`,
      source: "github",
      notesEn: summarizeReleaseBody(gh.body, existing?.notesEn ?? defaultEn),
      notesZh: summarizeReleaseBody(gh.body, existing?.notesZh ?? defaultZh),
      builds,
    });
  }

  if (mergedReleases.length === 0) {
    return base;
  }

  return {
    ...base,
    updatedAt: mergedReleases[0].releaseDate || base.updatedAt,
    releases: mergedReleases,
  };
}

export async function fetchLiveGitHubReleases(
  repository: string,
  signal?: AbortSignal,
): Promise<GitHubReleaseApiItem[]> {
  const url = `https://api.github.com/repos/${repository}/releases?per_page=15`;
  const res = await fetch(url, {
    headers: { Accept: "application/vnd.github+json" },
    cache: "no-store",
    signal,
  });
  if (!res.ok) {
    throw new Error(`GitHub Releases API returned HTTP ${res.status}`);
  }
  const data = (await res.json()) as unknown;
  return Array.isArray(data) ? (data as GitHubReleaseApiItem[]) : [];
}
