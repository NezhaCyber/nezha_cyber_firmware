import { type ClassValue, clsx } from "clsx";
import { twMerge } from "tailwind-merge";

export function cn(...inputs: ClassValue[]): string {
  return twMerge(clsx(inputs));
}

/**
 * Resolve a path relative to Vite's configured BASE_URL so deployments under
 * both root domains and GitHub Pages subpaths (/nezha_cyber_firmware/) work.
 */
export function resolveAssetUrl(relativePath: string): string {
  if (/^https?:\/\//i.test(relativePath)) {
    return relativePath;
  }
  const base = import.meta.env.BASE_URL || "./";
  const cleanBase = base.endsWith("/") ? base : `${base}/`;
  const cleanPath = relativePath.replace(/^\/+/, "");
  return `${cleanBase}${cleanPath}`;
}

export function parseHexOffset(offset: string | number): number {
  if (typeof offset === "number") {
    return Number.isFinite(offset) && offset >= 0 ? Math.floor(offset) : 0;
  }
  const trimmed = offset.trim();
  const parsed = Number.parseInt(trimmed, trimmed.toLowerCase().startsWith("0x") ? 16 : 10);
  if (Number.isNaN(parsed) || parsed < 0) {
    throw new Error(`Invalid flash offset: "${offset}"`);
  }
  return parsed;
}

export function formatHexOffset(value: number): string {
  return `0x${value.toString(16).toUpperCase()}`;
}

export function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return "0 B";
  const units = ["B", "KB", "MB", "GB"];
  const idx = Math.min(units.length - 1, Math.floor(Math.log(bytes) / Math.log(1024)));
  const scaled = bytes / Math.pow(1024, idx);
  return `${scaled >= 100 || idx === 0 ? scaled.toFixed(0) : scaled.toFixed(1)} ${units[idx]}`;
}

export function formatDuration(seconds: number | null): string {
  if (seconds === null || !Number.isFinite(seconds) || seconds < 0) return "--";
  if (seconds < 60) return `${Math.ceil(seconds)}s`;
  const m = Math.floor(seconds / 60);
  const s = Math.ceil(seconds % 60);
  return `${m}m ${s.toString().padStart(2, "0")}s`;
}

export function uint8ArrayToBinaryString(bytes: Uint8Array): string {
  // Chunked conversion avoids V8 call-stack overflow on multi-megabyte firmware images.
  const CHUNK_SIZE = 0x8000;
  const parts: string[] = [];
  for (let i = 0; i < bytes.length; i += CHUNK_SIZE) {
    const slice = bytes.subarray(i, Math.min(i + CHUNK_SIZE, bytes.length));
    parts.push(String.fromCharCode(...slice));
  }
  return parts.join("");
}

export function formatTimeStamp(date = new Date()): string {
  return date.toTimeString().slice(0, 8);
}
