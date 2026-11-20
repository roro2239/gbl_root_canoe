const IMAGE_NAMES = ["abl"];

const state = {
  confirmStep: 0,
  pendingAction: null,
  moduleDir: "",
  scriptPath: "",
  status: null,
  pollTimer: null,
  prevStatusRaw: "",
  lang: "zh"
};

const i18n = {
  zh: {
    pageTitle: "假回锁 - BL Flasher",
    ksuWebUI: "KernelSU 模块",
    slotStatus: "槽位概览",
    refresh: "刷新状态",
    settings: "操作设置",
    currentSlot: "当前槽位",
    targetSlot: "目标槽位",
    imageCount: "镜像数量",
    taskStatus: "任务状态",
    flash: "刷写到另一槽位",
    bdsTools: "仅更新 BDS/Tools",
    confirmBdsTools: "确认更新 BDS/Tools",
    modalBdsStep1: "将把 BDS.efi 刷入 efisp 分区，复制模块自带的 efisp 文件夹和 ENTRIES.CFG 到 persist。不会改动 ABL 与 boot.efi。",
    modalBdsStep2: "第二次确认: 这是高风险写入操作，错误的 BDS 或 efisp 写入可能导致无法进入启动菜单。确认后将立即开始。",
    toastStartBdsTools: "BDS/Tools 更新任务已启动",
    toastBdsToolsDone: "BDS/Tools 更新完成",
    clearLog: "清空日志",
    updateEfisp: "更新 efisp",
    efispHint: "默认开启",
    debugMode: "调试模式",
    debugHint: "仅处理文件，不写入分区。efisp 文件保存在模块的 tmp/efisp 目录。",
    warning: "刷写会修改引导分区。请先确认镜像与机型匹配。",
    imageMap: "镜像映射",
    partition: "分区",
    source: "源分区（当前）",
    target: "目标分区",
    action: "操作",
    waiting: "等待读取槽位信息",
    log: "实时日志",
    autoPoll: "最近 200 行",
    project: "当前项目",
    upstream: "上游项目",
    risk: "高风险操作",
    confirmFlash: "确认刷写",
    cancel: "取消",
    continue: "继续",
    states: { idle: "空闲", success: "已完成", warning: "有警告", error: "失败" },
    updated: "更新于",
    noTimestamp: "暂无更新时间",
    disconnected: "模块未连接",
    openInManager: "请从模块管理器打开此页。",
    initFailed: "模块初始化失败",
    missingInfo: "未读取到模块信息",
    logWaiting: "等待日志输出…",
    toastRunning: "已有刷写任务在运行",
    toastStartDebug: "调试任务已启动",
    toastStartFlash: "刷写任务已启动",
    toastDebugDone: "调试完成",
    toastFlashDone: "刷写已完成",
    toastBlDone: "BL 刷写完成，但 efisp 未更新",
    toastFailed: "任务已结束（失败）",
    toastStartError: "任务启动失败",
    toastLogBusy: "任务运行中，暂时不能清空日志",
    toastLogCleared: "日志已清空",
    modalStep1Debug: "调试模式：将执行所有处理流程但不刷写分区，生成的文件保存在 tmp 目录。",
    modalStep1Normal: (slot) => `第一次确认: 将把当前槽位的 BL 分区拷贝到槽位 ${slot}`,
    modalStep2: "第二次确认: 这是高风险写入操作，错误操作可能导致目标槽位无法启动。确认后将立即开始刷写。",
    withEfisp: "，并更新 efisp。",
    noEfisp: "，不更新 efisp。",
    confirmSlot: "请确认槽位无误。",
    taskRunning:"任务运行中",
    waitOperate:"等待操作",
    copyPart:"复制",
    statusReadFail:"状态读取失败",
    startFail:"启动失败"
  },
  en: {
    pageTitle: "Fake Lock - BL Flasher",
    ksuWebUI: "KernelSU module",
    slotStatus: "Slot overview",
    refresh: "Refresh status",
    settings: "Task settings",
    currentSlot: "Current Slot",
    targetSlot: "Target Slot",
    imageCount: "Image Count",
    taskStatus: "Task Status",
    flash: "Flash To Other Slot",
    bdsTools: "Update BDS & Tools Only",
    confirmBdsTools: "Confirm BDS/Tools Update",
    modalBdsStep1: "Will flash BDS.efi to the efisp partition and copy the bundled efisp folder and ENTRIES.CFG to persist. The ABL and boot.efi are not touched.",
    modalBdsStep2: "2nd Confirm: This is a high-risk write. A wrong BDS or efisp write may prevent the boot menu from loading. It starts immediately after confirm.",
    toastStartBdsTools: "BDS/Tools update started",
    toastBdsToolsDone: "BDS/Tools update completed",
    clearLog: "Clear Log",
    updateEfisp: "Update efisp",
    efispHint: "On by default",
    debugMode: "Debug mode",
    debugHint: "Process files without writing to partitions. The module saves efisp files in tmp/efisp.",
    warning: "Flashing bootloader partitions is high risk. Verify images match your device before starting.",
    imageMap: "Image Mapping",
    partition: "Partition",
    source: "Source (Current)",
    target: "Target",
    action: "Action",
    waiting: "Waiting for slot information",
    log: "Live Log",
    autoPoll: "Last 200 lines",
    project: "Current project",
    upstream: "Upstream project",
    risk: "HIGH RISK",
    confirmFlash: "Confirm Flash",
    cancel: "Cancel",
    continue: "Continue",
    states: { idle: "Idle", success: "Completed", warning: "Warning", error: "Failed" },
    updated: "Updated",
    noTimestamp: "No update time",
    disconnected: "Module disconnected",
    openInManager: "Open this page in the module manager.",
    initFailed: "Module initialization failed",
    missingInfo: "Module information unavailable",
    logWaiting: "Waiting for log output…",
    toastRunning: "Flash task is already running",
    toastStartDebug: "Debug task started",
    toastStartFlash: "Flash task started",
    toastDebugDone: "Debug completed",
    toastFlashDone: "Flash completed",
    toastBlDone: "BL flashed, but efisp not updated",
    toastFailed: "Task finished (failed)",
    toastStartError: "Failed to start task",
    toastLogBusy: "Cannot clear log while task is running",
    toastLogCleared: "Log cleared",
    modalStep1Debug: "Debug Mode: All processes run without flashing partitions. Files saved to tmp directory.",
    modalStep1Normal: (slot) => `1st Confirm: Copy BL partition from current slot to ${slot}`,
    modalStep2: "2nd Confirm: This is a high-risk write operation. Wrong action may brick the target slot. Flash will start immediately after confirm.",
    withEfisp: ", and update efisp.",
    noEfisp: ", efisp not updated.",
    confirmSlot: "Please confirm slot is correct.",
    taskRunning:"Task Running",
    waitOperate:"Waiting",
    copyPart:"Copy",
    statusReadFail:"Status Read Failed",
    startFail:"Start Failed"
  }
};

const elements = {
  stateChip: document.getElementById("stateChip"),
  currentSlot: document.getElementById("currentSlot"),
  targetSlot: document.getElementById("targetSlot"),
  imageCount: document.getElementById("imageCount"),
  taskMessage: document.getElementById("taskMessage"),
  updatedAt: document.getElementById("updatedAt"),
  imageTableBody: document.getElementById("imageTableBody"),
  logOutput: document.getElementById("logOutput"),
  flashButton: document.getElementById("flashButton"),
  bdsToolsButton: document.getElementById("bdsToolsButton"),
  clearLogButton: document.getElementById("clearLogButton"),
  refreshButton: document.getElementById("refreshButton"),
  confirmModal: document.getElementById("confirmModal"),
  confirmText: document.getElementById("confirmText"),
  nextConfirmButton: document.getElementById("nextConfirmButton"),
  cancelConfirmButton: document.getElementById("cancelConfirmButton"),
  updateEfispCheckbox: document.getElementById("updateEfispCheckbox"),
  debugModeCheckbox: document.getElementById("debugModeCheckbox")
};

function applyLanguage(lang) {
  state.lang = lang;
  const t = i18n[lang];
  document.documentElement.lang = lang === "zh" ? "zh-CN" : "en";
  if ([i18n.zh.logWaiting, i18n.en.logWaiting].includes(elements.logOutput.textContent)) {
    elements.logOutput.textContent = t.logWaiting;
  }
  document.querySelectorAll("[data-i18n]").forEach(el => {
    const key = el.dataset.i18n;
    if (t[key]) el.textContent = t[key];
  });
}

function getKsuBridge() {
  return globalThis.ksu || window.ksu || null;
}

function shellQuote(value) {
  return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function toast(message) {
  getKsuBridge()?.toast?.(message);
}

function moduleInfo() {
  const bridge = getKsuBridge();
  if (!bridge) throw new Error("No Webui");

  if (bridge.moduleInfo) {
    const raw = bridge.moduleInfo();
    return typeof raw === "string" ? JSON.parse(raw) : raw;
  }

  const found = extractStdout(
    bridge.exec(
      'for d in /data/adb/modules/*/; do [ -f "${d}bin/bl_flasher.sh" ] && printf "%s" "${d%/}" && break; done'
    )
  ).trim();
  if (!found) throw new Error("Module Not Found");
  return { moduleDir: found };
}

function extractStdout(raw) {
  if (raw == null) return "";
  if (typeof raw === "string") {
    try {
      const obj = JSON.parse(raw);
      if (typeof obj?.stdout === "string") return obj.stdout;
      if (typeof obj?.out === "string") return obj.out;
    } catch {}
    return raw;
  }
  if (typeof raw?.stdout === "string") return raw.stdout;
  if (typeof raw?.out === "string") return raw.out;
  return String(raw);
}

function exec(command) {
  const bridge = getKsuBridge();
  if (!bridge?.exec) return "";
  return extractStdout(bridge.exec(command)).replace(/\t/g, "\n");
}

function runScript(action, arg) {
  const parts = [`MODDIR=${shellQuote(state.moduleDir)}`, "sh", shellQuote(state.scriptPath), action];
  if (arg) parts.push(shellQuote(arg));
  return exec(parts.join(" "));
}

function parseKeyValueOutput(output) {
  const info = {};
  for (const line of output.split(/\r?\n/)) {
    if (!line) continue;
    const eq = line.indexOf("=");
    if (eq > 0) info[line.slice(0, eq)] = line.slice(eq + 1);
  }
  return info;
}

function escapeHtml(str) {
  return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function renderTable(currentSlot, targetSlot) {
  const t = i18n[state.lang];
  if (currentSlot === "-" || targetSlot === "-") {
    elements.imageTableBody.innerHTML = `<tr><td colspan="4" class="empty-row">${t.waiting}</td></tr>`;
    return;
  }
  elements.imageTableBody.innerHTML = IMAGE_NAMES.map(name => {
    const src = `/dev/block/by-name/${name}${currentSlot}`;
    const dst = `/dev/block/by-name/${name}${targetSlot}`;
    return `<tr><td>${escapeHtml(name)}</td><td class="caption">${escapeHtml(src)}</td><td>${escapeHtml(dst)}</td><td><span class="status-pill ok">${t.copyPart}</span></td></tr>`;
  }).join("");
}

function renderStatus(status) {
  state.status = status;
  const t = i18n[state.lang];
  const cur = status.CURRENT_SLOT || "-";
  const tar = status.TARGET_SLOT || "-";
  const run = status.RUNNING === "1";
  const st = status.STATE || "idle";
  const msg = status.MESSAGE || t.waitOperate;
  elements.currentSlot.textContent = cur === "-" ? "—" : cur;
  elements.targetSlot.textContent = tar === "-" ? "—" : tar;
  elements.imageCount.textContent = IMAGE_NAMES.length;
  elements.taskMessage.textContent = msg;
  elements.updatedAt.textContent = status.UPDATED_AT ? `${t.updated} ${status.UPDATED_AT}` : t.noTimestamp;
  elements.stateChip.textContent = run ? t.taskRunning : (t.states[st] || st);
  elements.stateChip.className = "chip";
  if (st === "success") elements.stateChip.classList.add("chip-success");
  else if (st === "error") elements.stateChip.classList.add("chip-danger");
  else if (st === "warning" || run) elements.stateChip.classList.add("chip-warn");
  elements.flashButton.disabled = run || cur === "-" || tar === "-";
  elements.bdsToolsButton.disabled = run;
  elements.clearLogButton.disabled = run;
  renderTable(cur, tar);
}

function refreshStatus() {
  try {
    const raw = runScript("status");
    if (raw === state.prevStatusRaw) return state.status;
    state.prevStatusRaw = raw;
    const s = parseKeyValueOutput(raw);
    if (s.USER_LANG !== state.lang && (s.USER_LANG === "zh" || s.USER_LANG === "en")) {
      applyLanguage(s.USER_LANG);
    }
    renderStatus(s);
    return s;
  } catch (e) {
    const t = i18n[state.lang];
    elements.stateChip.textContent = t.statusReadFail;
    elements.stateChip.className = "chip chip-danger";
    elements.taskMessage.textContent = e.message;
    return null;
  }
}

function refreshLog() {
  try {
    const log = runScript("tail", "200").trim();
    const output = elements.logOutput;
    const content = log || i18n[state.lang].logWaiting;
    if (output.textContent !== content) {
      const atBottom = output.scrollHeight - output.scrollTop - output.clientHeight <= 2;
      output.textContent = content;
      if (atBottom) output.scrollTop = output.scrollHeight;
    }
  } catch (e) {
    elements.logOutput.textContent = `${state.lang === "zh" ? "日志读取失败" : "Log Read Failed"}: ${e.message}`;
  }
}

function closeConfirmModal() {
  state.confirmStep = 0;
  state.pendingAction = null;
  elements.confirmModal.close();
  elements.nextConfirmButton.textContent = i18n[state.lang].continue;
}

function openConfirmModal(action) {
  const t = i18n[state.lang];
  state.pendingAction = action;
  state.confirmStep = 1;
  if (action === "bds-tools") {
    document.querySelector("#modalTitle").textContent = t.confirmBdsTools;
    elements.confirmText.textContent = t.modalBdsStep1;
    elements.nextConfirmButton.textContent = t.continue;
  } else {
    document.querySelector("#modalTitle").textContent = t.confirmFlash;
    const tar = state.status?.TARGET_SLOT || "?";
    const efisp = elements.updateEfispCheckbox?.checked;
    const dbg = elements.debugModeCheckbox?.checked;
    let msg = dbg ? t.modalStep1Debug : t.modalStep1Normal(tar);
    if (!dbg) {
      msg += efisp ? t.withEfisp : t.noEfisp;
      msg += t.confirmSlot;
    }
    elements.confirmText.textContent = msg;
    elements.nextConfirmButton.textContent = dbg ? (state.lang === "zh" ? "开始调试" : "Start Debug") : t.continue;
  }
  elements.confirmModal.showModal();
}

function handleConfirmProgress() {
  const t = i18n[state.lang];
  if (state.confirmStep === 1) {
    if (state.pendingAction === "bds-tools") {
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalBdsStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "开始更新" : "Start Update";
      return;
    }
    const dbg = elements.debugModeCheckbox?.checked;
    if (!dbg) {
      state.confirmStep = 2;
      elements.confirmText.textContent = t.modalStep2;
      elements.nextConfirmButton.textContent = state.lang === "zh" ? "确认刷写" : "Confirm Flash";
      return;
    }
    // debug: single-step confirm, proceed directly
    closeConfirmModal();
    startFlash();
    return;
  }
  const action = state.pendingAction;
  closeConfirmModal();
  if (action === "bds-tools") startBdsTools();
  else startFlash();
}

function startFlash() {
  const t = i18n[state.lang];
  const efisp = elements.updateEfispCheckbox?.checked;
  const dbg = elements.debugModeCheckbox?.checked;
  let mode = dbg ? "debug" : (efisp ? "update-efisp" : "skip-efisp");
  try {
    const out = parseKeyValueOutput(runScript("start", mode));
    if (out.ALREADY_RUNNING) toast(t.toastRunning);
    else if (out.STARTED === "1") toast(dbg ? t.toastStartDebug : t.toastStartFlash);
    else if (out.FINISHED === "success") toast(dbg ? t.toastDebugDone : t.toastFlashDone);
    else if (out.FINISHED === "warning") toast(t.toastBlDone);
    else if (out.FINISHED === "error") toast(t.toastFailed);
    else toast(t.toastStartError);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  manualRefresh();
}

function startBdsTools() {
  const t = i18n[state.lang];
  try {
    const out = parseKeyValueOutput(runScript("start", "update-bds-tools"));
    if (out.ALREADY_RUNNING) toast(t.toastRunning);
    else if (out.STARTED === "1") toast(t.toastStartBdsTools);
    else if (out.FINISHED === "success") toast(t.toastBdsToolsDone);
    else if (out.FINISHED === "error") toast(t.toastFailed);
    else toast(t.toastStartError);
  } catch (e) { toast(`${t.startFail}: ${e.message}`); }
  manualRefresh();
}

function clearLog() {
  const t = i18n[state.lang];
  try {
    const out = parseKeyValueOutput(runScript("clear-log"));
    if (out.BUSY === "1") { toast(t.toastLogBusy); return; }
    toast(t.toastLogCleared);
  } catch (e) { toast(`${state.lang === "zh" ? "清空失败" : "Clear Failed"}: ${e.message}`); }
  manualRefresh();
}

function poll() {
  const s = refreshStatus();
  if (s?.RUNNING === "1") refreshLog();
  schedulePoll(s?.RUNNING === "1" ? 3000 : 8000);
}

function schedulePoll(ms) {
  clearTimeout(state.pollTimer);
  state.pollTimer = setTimeout(poll, ms);
}

function manualRefresh() {
  clearTimeout(state.pollTimer);
  state.prevStatusRaw = "";
  refreshStatus();
  refreshLog();
  schedulePoll(state.status?.RUNNING === "1" ? 3000 : 8000);
}

async function init() {
  try {
    const info = moduleInfo();
    if (!info) throw new Error(i18n[state.lang].missingInfo);
    state.moduleDir = info.moduleDir;
    state.scriptPath = `${state.moduleDir}/bin/bl_flasher.sh`;
    refreshStatus();
  } catch (e) {
    const t = i18n[state.lang];
    const hasBridge = Boolean(getKsuBridge());
    elements.stateChip.textContent = hasBridge ? t.initFailed : t.disconnected;
    elements.stateChip.className = hasBridge ? "chip chip-danger" : "chip";
    elements.taskMessage.textContent = hasBridge ? e.message : t.openInManager;
    elements.flashButton.disabled = true;
    elements.bdsToolsButton.disabled = true;
    elements.clearLogButton.disabled = true;
    return;
  }

  elements.refreshButton.disabled = false;
  elements.refreshButton.addEventListener("click", manualRefresh);
  elements.flashButton.addEventListener("click", () => openConfirmModal("flash"));
  elements.bdsToolsButton.addEventListener("click", () => openConfirmModal("bds-tools"));
  elements.clearLogButton.addEventListener("click", clearLog);
  elements.cancelConfirmButton.addEventListener("click", closeConfirmModal);
  elements.nextConfirmButton.addEventListener("click", handleConfirmProgress);
  elements.confirmModal.addEventListener("click", e => e.target === elements.confirmModal && closeConfirmModal());
  elements.confirmModal.addEventListener("cancel", e => {
    e.preventDefault();
    closeConfirmModal();
  });

  schedulePoll(3000);
}

init();
getKsuBridge()?.fullScreen?.(true);
