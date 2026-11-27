/* Nezha Cyber WebSerial Companion — Zero-build browser controller */

const I18N = {
  en: {
    subtitle: "WebSerial Flasher · Live CLI · Remote Nav · Health HUD",
    connect: "Connect USB-C",
    disconnect: "Disconnect",
    tab_console: "Live CLI & Remote",
    tab_health: "Health HUD (乾坤仪)",
    tab_files: "File & Playbook Mgr",
    tab_flash: "WebSerial Flasher",
    serial_terminal: "USB-C Serial Terminal",
    clear: "Clear",
    send: "Send",
    remote_nav: "Remote Hardware Navigation",
    nav_hint: "Uses native USB-C WebSerial (Chrome / Edge / Chromium). Zero Wi-Fi required.",
    health_title: "Live ESP32 Memory & Radio Telemetry (sysmon)",
    poll_start: "Start Live Poll (2s)",
    poll_stop: "Stop Live Poll",
    poll_once: "Refresh Now",
    storage_browser: "Storage Browser (SD / LittleFS)",
    list_dir: "List",
    playbook_builder: "Playbook JSON Quick Runner (灵珠剧本)",
    run_steps: "Execute Steps via CLI",
    download_json: "Download .json",
    flasher_title: "One-Click WebSerial Firmware Flasher",
  },
  zh: {
    subtitle: "WebSerial 网页烧录 · 实时串口终端 · 远程遥控 · 系统健康监控",
    connect: "连接 USB-C",
    disconnect: "断开连接",
    tab_console: "实时终端与遥控",
    tab_health: "乾坤仪健康监控",
    tab_files: "文件与灵珠剧本",
    tab_flash: "网页烧录器",
    serial_terminal: "USB-C 串口命令行",
    clear: "清屏",
    send: "发送",
    remote_nav: "硬件远程导航",
    nav_hint: "基于浏览器原生 WebSerial（Chrome / Edge），无需 Wi-Fi 即可直连设备。",
    health_title: "ESP32 实时内存与射频健康遥测 (sysmon)",
    poll_start: "开启实时轮询 (2秒)",
    poll_stop: "停止轮询",
    poll_once: "立即刷新",
    storage_browser: "存储浏览器 (SD / LittleFS)",
    list_dir: "列出目录",
    playbook_builder: "灵珠剧本 JSON 快捷运行器",
    run_steps: "通过串口顺序执行",
    download_json: "导出 .json",
    flasher_title: "一键 WebSerial 固件烧录",
  },
};

let lang = "en";
let port = null;
let reader = null;
let writer = null;
let keepReading = false;
let rxLineBuf = "";
let healthTimer = null;
let cmdHistory = [];
let histIdx = -1;
let captureFileList = false;

const $ = (id) => document.getElementById(id);

function applyI18n() {
  const dict = I18N[lang];
  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const key = el.getAttribute("data-i18n");
    if (dict[key]) el.textContent = dict[key];
  });
  $("connectBtn").textContent = port ? dict.disconnect : dict.connect;
  $("pollHealthBtn").textContent = healthTimer ? dict.poll_stop : dict.poll_start;
  $("langBtn").textContent = lang === "en" ? "中文" : "EN";
}

function appendTerm(text) {
  const out = $("termOut");
  out.textContent += text;
  if (out.textContent.length > 64000) {
    out.textContent = out.textContent.slice(-48000);
  }
  out.scrollTop = out.scrollHeight;
}

function handleLine(line) {
  const trimmed = line.trim();
  if (trimmed.startsWith("{") && trimmed.endsWith("}") && trimmed.includes("\"heap_free\"")) {
    try {
      updateHealthHud(JSON.parse(trimmed));
    } catch (_) {
      /* ignore non-telemetry JSON */
    }
  }
  if (captureFileList) {
    const fl = $("fileListOut");
    fl.textContent += line + "\n";
    fl.scrollTop = fl.scrollHeight;
  }
}

function updateHealthHud(d) {
  $("mBoard").textContent = `${d.board || "ESP32"} @ ${d.cpu_mhz || "--"} MHz`;
  $("mUptime").textContent = `Uptime: ${d.uptime_s ?? "--"} s`;

  const dramFreeK = Math.round((d.heap_free || 0) / 1024);
  const dramTotK = Math.max(1, Math.round((d.heap_total || 1) / 1024));
  const dramUsedPct = Math.min(100, Math.round(((dramTotK - dramFreeK) / dramTotK) * 100));
  $("mDram").textContent = `${dramFreeK} / ${dramTotK} KB`;
  $("bDram").style.width = `${dramUsedPct}%`;

  const dmaMaxK = Math.round((d.dma_largest || 0) / 1024);
  const dmaFreeK = Math.round((d.dma_free || 0) / 1024);
  $("mDma").textContent = `${dmaMaxK} KB max (${dmaFreeK} KB free)`;

  const pWifi = $("pWifi");
  pWifi.textContent = d.wifi_ok ? "WIFI READY" : "WIFI LOW";
  pWifi.className = `pill ${d.wifi_ok ? "ok" : "low"}`;

  const pBle = $("pBle");
  pBle.textContent = d.ble_ok ? "BLE READY" : "BLE LOW";
  pBle.className = `pill ${d.ble_ok ? "ok" : "low"}`;

  if (d.psram_total > 0) {
    const pfK = Math.round(d.psram_free / 1024);
    const ptK = Math.round(d.psram_total / 1024);
    const puPct = Math.min(100, Math.round(((ptK - pfK) / ptK) * 100));
    $("mPsram").textContent = `${pfK} / ${ptK} KB`;
    $("bPsram").style.width = `${puPct}%`;
  } else {
    $("mPsram").textContent = "N/A (Internal DRAM)";
    $("bPsram").style.width = "0%";
  }

  const lfsUsedK = Math.round((d.lfs_used || 0) / 1024);
  const lfsTotK = Math.round((d.lfs_total || 0) / 1024);
  $("mStorage").textContent = `LFS ${lfsUsedK}/${lfsTotK} KB`;
  $("mSd").textContent = `SD Card: ${d.sd ? "Mounted" : "Not mounted"}`;

  $("mBat").textContent = d.bat >= 0 ? `${d.bat}%` : "USB / N/A";
  $("mTasks").textContent = `Tasks: ${d.tasks ?? "--"} · MinHeap: ${Math.round((d.heap_min || 0) / 1024)} KB`;
}

async function sendCommand(cmd) {
  if (!writer) {
    appendTerm("[Not connected — click Connect USB-C first]\n");
    return;
  }
  const enc = new TextEncoder();
  await writer.write(enc.encode(cmd + "\n"));
}

async function readLoop() {
  const dec = new TextDecoder();
  while (port && port.readable && keepReading) {
    reader = port.readable.getReader();
    try {
      while (keepReading) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) {
          const chunk = dec.decode(value, { stream: true });
          appendTerm(chunk);
          rxLineBuf += chunk;
          let nl;
          while ((nl = rxLineBuf.indexOf("\n")) >= 0) {
            const line = rxLineBuf.slice(0, nl);
            rxLineBuf = rxLineBuf.slice(nl + 1);
            handleLine(line);
          }
        }
      }
    } catch (err) {
      appendTerm(`\n[Serial read error: ${err.message}]\n`);
    } finally {
      reader.releaseLock();
      reader = null;
    }
  }
}

async function toggleConnect() {
  if (!("serial" in navigator)) {
    alert("WebSerial requires Chrome, Edge, or Chromium over HTTPS/localhost.");
    return;
  }
  if (port) {
    keepReading = false;
    if (healthTimer) {
      clearInterval(healthTimer);
      healthTimer = null;
    }
    try {
      if (reader) await reader.cancel();
      if (writer) writer.releaseLock();
      await port.close();
    } catch (_) {}
    port = null;
    writer = null;
    applyI18n();
    appendTerm("\n[Disconnected]\n");
    return;
  }

  try {
    port = await navigator.serial.requestPort();
    const baudRate = parseInt($("baudSelect").value, 10) || 115200;
    await port.open({ baudRate });
    writer = port.writable.getWriter();
    keepReading = true;
    applyI18n();
    appendTerm(`[Connected @ ${baudRate} bps]\n`);
    readLoop();
    await sendCommand("sysmon");
  } catch (err) {
    port = null;
    writer = null;
    appendTerm(`[Connect failed: ${err.message}]\n`);
  }
}

// Tab switching
document.querySelectorAll(".tab").forEach((btn) => {
  btn.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
    document.querySelectorAll(".panel").forEach((p) => p.classList.remove("active"));
    btn.classList.add("active");
    $(`tab-${btn.dataset.tab}`).classList.add("active");
  });
});

$("connectBtn").addEventListener("click", toggleConnect);
$("langBtn").addEventListener("click", () => {
  lang = lang === "en" ? "zh" : "en";
  applyI18n();
});
$("clearTermBtn").addEventListener("click", () => {
  $("termOut").textContent = "";
});

$("cmdForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const input = $("cmdInput");
  const cmd = input.value.trim();
  if (!cmd) return;
  cmdHistory.unshift(cmd);
  if (cmdHistory.length > 50) cmdHistory.pop();
  histIdx = -1;
  input.value = "";
  await sendCommand(cmd);
});

$("cmdInput").addEventListener("keydown", (e) => {
  if (e.key === "ArrowUp" && cmdHistory.length > 0) {
    histIdx = Math.min(cmdHistory.length - 1, histIdx + 1);
    $("cmdInput").value = cmdHistory[histIdx];
    e.preventDefault();
  } else if (e.key === "ArrowDown") {
    histIdx = Math.max(-1, histIdx - 1);
    $("cmdInput").value = histIdx >= 0 ? cmdHistory[histIdx] : "";
    e.preventDefault();
  }
});

document.querySelectorAll("[data-cmd]").forEach((btn) => {
  btn.addEventListener("click", () => sendCommand(btn.dataset.cmd));
});

document.querySelectorAll("[data-nav]").forEach((btn) => {
  btn.addEventListener("click", () => sendCommand(`nav ${btn.dataset.nav}`));
});

$("onceHealthBtn").addEventListener("click", () => sendCommand("sysmon"));
$("pollHealthBtn").addEventListener("click", () => {
  if (healthTimer) {
    clearInterval(healthTimer);
    healthTimer = null;
  } else {
    sendCommand("sysmon");
    healthTimer = setInterval(() => sendCommand("sysmon"), 2000);
  }
  applyI18n();
});

$("listFilesBtn").addEventListener("click", async () => {
  const path = $("pathInput").value.trim() || "/";
  $("fileListOut").textContent = `$ storage list ${path}\n`;
  captureFileList = true;
  await sendCommand(`storage list ${path}`);
  setTimeout(() => {
    captureFileList = false;
  }, 1200);
});

$("runPlaybookBtn").addEventListener("click", async () => {
  try {
    const pb = JSON.parse($("playbookArea").value);
    const steps = Array.isArray(pb.steps) ? pb.steps : [];
    const repeat = Math.max(1, Math.min(20, pb.repeat || 1));
    appendTerm(`\n[Running Playbook: ${pb.name || "Untitled"} (${steps.length} steps x${repeat})]\n`);
    for (let r = 0; r < repeat; r++) {
      for (const s of steps) {
        if (s.cmd) await sendCommand(s.cmd);
        const ms = Math.max(50, Math.min(10000, s.delay_ms || 250));
        await new Promise((res) => setTimeout(res, ms));
      }
    }
    appendTerm("[Playbook Complete]\n");
  } catch (err) {
    alert("Invalid Playbook JSON: " + err.message);
  }
});

$("downloadPlaybookBtn").addEventListener("click", () => {
  const blob = new Blob([$("playbookArea").value], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "nezha_playbook.json";
  a.click();
  URL.revokeObjectURL(a.href);
});

applyI18n();
