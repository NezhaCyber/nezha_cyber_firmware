import type { ChipFamily, UsbPortInfoSummary } from "@/types/firmware";

interface KnownUsbSignature {
  vendorId: number;
  productId?: number;
  labelEn: string;
  labelZh: string;
  chipFamily: ChipFamily | "unknown";
  suggestedBoardId?: string;
}

/**
 * Curated USB-Serial VID:PID registry covering Espressif native USB-JTAG/CDC
 * (LILYGO T-Deck / T-Deck Plus / Cardputer / T-Embed) and common UART bridges
 * (M5StickC Plus 1.1, CYD, DevKits).
 */
export const KNOWN_USB_SIGNATURES: KnownUsbSignature[] = [
  {
    vendorId: 0x303a,
    productId: 0x1001,
    labelEn: "Espressif ESP32-S3 USB-JTAG / CDC (T-Deck / S3)",
    labelZh: "乐鑫 ESP32-S3 原生 USB-JTAG/串口 (T-Deck / S3)",
    chipFamily: "esp32s3",
    suggestedBoardId: "lilygo-t-deck",
  },
  {
    vendorId: 0x303a,
    productId: 0x0002,
    labelEn: "Espressif ESP32-S2 Native USB",
    labelZh: "乐鑫 ESP32-S2 原生 USB",
    chipFamily: "esp32s2",
  },
  {
    vendorId: 0x303a,
    labelEn: "Espressif Native USB Serial",
    labelZh: "乐鑫原生 USB 串口设备",
    chipFamily: "esp32s3",
    suggestedBoardId: "lilygo-t-deck",
  },
  {
    vendorId: 0x1a86,
    productId: 0x55d4,
    labelEn: "WCH CH9102F USB-UART Bridge",
    labelZh: "沁恒 CH9102F USB 转串口桥接芯片",
    chipFamily: "esp32",
    suggestedBoardId: "m5stack-cplus1_1",
  },
  {
    vendorId: 0x1a86,
    productId: 0x7523,
    labelEn: "WCH CH340 USB-UART Bridge",
    labelZh: "沁恒 CH340 USB 转串口桥接芯片",
    chipFamily: "esp32",
  },
  {
    vendorId: 0x10c4,
    productId: 0xea60,
    labelEn: "Silicon Labs CP210x USB-UART",
    labelZh: "芯科 CP210x USB 转串口",
    chipFamily: "esp32",
  },
  {
    vendorId: 0x0403,
    productId: 0x6001,
    labelEn: "FTDI FT232R USB-UART (M5StickC Plus)",
    labelZh: "FTDI FT232R USB 转串口 (M5StickC Plus)",
    chipFamily: "esp32",
    suggestedBoardId: "m5stack-cplus1_1",
  },
  {
    vendorId: 0x0403,
    productId: 0x6015,
    labelEn: "FTDI FT-X Series USB-UART",
    labelZh: "FTDI FT-X 系列 USB 转串口",
    chipFamily: "esp32",
    suggestedBoardId: "m5stack-cplus1_1",
  },
];

export const ESP_SERIAL_FILTERS: SerialPortFilter[] = [
  { usbVendorId: 0x303a }, // Espressif Systems (ESP32-S3 / S2 / C3 / C5 / C6)
  { usbVendorId: 0x1a86 }, // WCH (CH9102 / CH340 / CH343)
  { usbVendorId: 0x10c4 }, // Silicon Labs (CP2102 / CP2104)
  { usbVendorId: 0x0403 }, // FTDI (FT232R / FT2232)
];

function formatHex16(value?: number): string {
  if (typeof value !== "number") return "----";
  return `0x${value.toString(16).toUpperCase().padStart(4, "0")}`;
}

export function describeSerialPort(port: SerialPort, index: number): UsbPortInfoSummary {
  const info = port.getInfo();
  const vendorId = info.usbVendorId;
  const productId = info.usbProductId;

  const exactMatch = KNOWN_USB_SIGNATURES.find(
    (sig) => sig.vendorId === vendorId && sig.productId !== undefined && sig.productId === productId,
  );
  const vendorMatch =
    exactMatch ?? KNOWN_USB_SIGNATURES.find((sig) => sig.vendorId === vendorId && sig.productId === undefined);

  const vendorHex = formatHex16(vendorId);
  const productHex = formatHex16(productId);

  return {
    id: `${vendorHex}:${productHex}#${index}`,
    port,
    vendorId,
    productId,
    vendorHex,
    productHex,
    deviceLabel: vendorMatch?.labelEn ?? `USB Serial Device (${vendorHex}:${productHex})`,
    deviceLabelZh: vendorMatch?.labelZh ?? `USB 串口设备 (${vendorHex}:${productHex})`,
    likelyChipFamily: vendorMatch?.chipFamily ?? "unknown",
    suggestedBoardId: vendorMatch?.suggestedBoardId,
    isAuthorized: true,
  };
}

export function normalizeChipFamily(chipName: string): ChipFamily | "unknown" {
  const normalized = chipName.toLowerCase().replace(/[^a-z0-9]/g, "");
  if (normalized.includes("esp32s3")) return "esp32s3";
  if (normalized.includes("esp32c5")) return "esp32c5";
  if (normalized.includes("esp32c6")) return "esp32c6";
  if (normalized.includes("esp32s2")) return "esp32s2";
  if (normalized.includes("esp32")) return "esp32";
  return "unknown";
}
