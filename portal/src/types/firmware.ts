export type ChipFamily = "esp32s3" | "esp32" | "esp32c5" | "esp32c6" | "esp32s2";

export type PartitionRole = "boot" | "table" | "nvs" | "app" | "fs" | "dump";

export interface PartitionDescriptor {
  label: string;
  offset: string;
  size: string;
  role: PartitionRole;
}

export interface FlashPart {
  name: string;
  label: string;
  offset: string;
  path: string;
}

export interface FlashModePreset {
  labelEn: string;
  labelZh: string;
  parts: FlashPart[];
}

export interface UsbHint {
  vendorId: string;
  productId?: string;
}

export interface BoardBuild {
  id: string;
  env: string;
  boardName: string;
  boardNameZh: string;
  mcu: string;
  chipFamily: ChipFamily;
  flashSize: string;
  psram: string;
  display: string;
  partitionTable: string;
  featured?: boolean;
  usbHints: UsbHint[];
  downloadModeHintEn: string;
  downloadModeHintZh: string;
  partitions: PartitionDescriptor[];
  flashModes: {
    merged: FlashModePreset;
    partitioned?: FlashModePreset;
  };
}

export interface FirmwareRelease {
  version: string;
  channel: "stable" | "beta" | "archive";
  releaseDate: string;
  notesEn: string;
  notesZh: string;
  builds: BoardBuild[];
}

export interface FirmwareManifest {
  project: string;
  projectZh: string;
  repository: string;
  schemaVersion: number;
  updatedAt: string;
  releases: FirmwareRelease[];
}

export interface CustomLocalPart {
  id: string;
  file: File;
  offset: string;
  label: string;
}

export interface DetectedChipInfo {
  chipName: string;
  chipDescription: string;
  chipFamily: ChipFamily | "unknown";
  macAddress: string;
  flashSizeMB: number;
  crystalFreqMHz?: number;
  features?: string[];
}

export interface UsbPortInfoSummary {
  id: string;
  port: SerialPort;
  vendorId?: number;
  productId?: number;
  vendorHex: string;
  productHex: string;
  deviceLabel: string;
  deviceLabelZh: string;
  likelyChipFamily: ChipFamily | "unknown";
  suggestedBoardId?: string;
  isAuthorized: boolean;
}

export type FlashPhase =
  | "idle"
  | "connecting"
  | "connected"
  | "downloading"
  | "erasing"
  | "flashing"
  | "verifying"
  | "resetting"
  | "done"
  | "error";

export interface FlashProgressState {
  phase: FlashPhase;
  overallPercent: number;
  writtenBytes: number;
  totalBytes: number;
  currentPartIndex: number;
  totalParts: number;
  currentPartName: string;
  currentPartOffset: string;
  currentPartPercent: number;
  throughputBps: number;
  etaSeconds: number | null;
  message: string;
}

export interface TerminalLine {
  id: number;
  timestamp: string;
  text: string;
  source: "device" | "system" | "tx" | "error";
}
