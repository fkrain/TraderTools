const state = {
  page: "follow",
  source: "eastmoney",
  rank: "daily",
  selected: null,
  follow: null,
  targetDetail: null,
  session: "closed",
};

const $ = (id) => document.getElementById(id);

async function api(path, options = {}) {
  const resp = await fetch(path, {
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
    ...options,
  });
  const data = await resp.json().catch(() => ({}));
  if (!resp.ok) {
    throw new Error(data.detail || data.message || `请求失败 ${resp.status}`);
  }
  return data;
}

function toast(message) {
  const el = $("toast");
  el.textContent = message;
  el.hidden = false;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => {
    el.hidden = true;
  }, 3200);
}

function pct(value) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return "-";
  const n = Number(value);
  const sign = n > 0 ? "+" : "";
  return `${sign}${n.toFixed(2)}%`;
}

function money(value) {
  if (value === null || value === undefined) return "-";
  return Number(value).toLocaleString("zh-CN", { maximumFractionDigits: 2 });
}

function tone(value) {
  if (value > 0) return "up";
  if (value < 0) return "down";
  return "";
}

const PRICE_TYPE_OPTIONS = [
  { value: "market", label: "市价（即时成交）" },
  { value: "last", label: "最新价" },
  { value: "ask1", label: "卖一" },
  { value: "ask2", label: "卖二" },
  { value: "ask3", label: "卖三" },
  { value: "ask4", label: "卖四" },
  { value: "ask5", label: "卖五" },
  { value: "bid1", label: "买一" },
  { value: "bid2", label: "买二" },
  { value: "bid3", label: "买三" },
  { value: "bid4", label: "买四" },
  { value: "bid5", label: "买五" },
];

function fillPxTypeSelects(root = document) {
  root.querySelectorAll("select.js-px-type").forEach((sel) => {
    const cur = sel.value || sel.dataset.pxDefault || "ask1";
    sel.innerHTML = PRICE_TYPE_OPTIONS.map(
      (o) => `<option value="${o.value}">${o.label}</option>`
    ).join("");
    sel.value = PRICE_TYPE_OPTIONS.some((o) => o.value === cur) ? cur : sel.dataset.pxDefault || "ask1";
  });
}

function setPxType(id, value, fallback) {
  const sel = $(id);
  if (!sel) return;
  const v = value || fallback || sel.dataset.pxDefault || "ask1";
  sel.value = PRICE_TYPE_OPTIONS.some((o) => o.value === v) ? v : fallback || "ask1";
}

const GRID_TPL_SKIP = new Set([
  "id",
  "code",
  "name",
  "status",
  "position_shares",
  "match_buy_prices",
  "buy_streak",
  "range_pnl",
  "range_save",
  "last_price",
  "last_qty",
  "low_pivot",
  "high_pivot",
  "pending_order_id",
  "pending_side",
  "pending_qty",
  "pending_price",
  "pending_ref_price",
  "pending_grid_match_pop",
  "pending_grid_match_push",
  "pivot_touch_ms",
  "sell_armed",
  "buy_armed",
]);

async function loadGridTemplates() {
  try {
    const list = await api("/api/templates");
    state.gridTemplates = (list || []).filter((t) => (t.kind || "grid") === "grid");
  } catch (_) {
    state.gridTemplates = [];
  }
  const sel = $("g-tpl-select");
  if (!sel) return;
  const cur = sel.value;
  sel.innerHTML =
    `<option value="">选择模板…</option>` +
    state.gridTemplates
      .map(
        (t) =>
          `<option value="${escapeHtml(t.id)}">${escapeHtml(t.name || t.id)}${
            t.is_default ? "（默认）" : ""
          }</option>`
      )
      .join("");
  if (cur && state.gridTemplates.some((t) => t.id === cur)) sel.value = cur;
}

function applyGridTemplatePayload(payload, { keepCode = true } = {}) {
  if (!payload || typeof payload !== "object") return;
  const code = keepCode ? $("g-code")?.value : "";
  const name = keepCode ? $("g-name")?.value : "";
  openGridDialog(
    {
      ...payload,
      id: $("grid-id").value || "",
      code: code || "",
      name: name || "",
      status: "paused",
    },
    { skipDefaultTpl: true }
  );
  if (keepCode && code) fillStockCode("g-code", code);
  if (keepCode && name) {
    $("g-name").value = name;
    $("g-name").dataset.autofilled = "0";
  }
}

function gridTemplatePayloadFromForm() {
  const body = collectGridBody({ requireCode: false });
  if (!body) return null;
  const payload = {};
  Object.keys(body).forEach((k) => {
    if (!GRID_TPL_SKIP.has(k)) payload[k] = body[k];
  });
  delete payload.code;
  delete payload.name;
  return payload;
}

/** A-share code: digits only, max 6. Display 600616; persist 600616.SH */
function stockCodeDigits(raw) {
  const s = String(raw ?? "");
  const m = s.match(/(\d{6})/);
  if (m) return m[1];
  return s.replace(/\D/g, "").slice(0, 6);
}

function marketSuffixForNum(d) {
  // BJ: 43/82/83/87/88/92；SH: 6*/5* ETF·基金·11 可转债；SZ: 00/30/12/15/16 LOF·ETF 等
  if (/^(92|83|87|43|82|88)/.test(d)) return ".BJ";
  if (d[0] === "6" || d[0] === "5" || d.startsWith("11")) return ".SH";
  return ".SZ";
}

function toFullStockCode(digitsOrRaw) {
  const d = stockCodeDigits(digitsOrRaw);
  if (d.length !== 6) return "";
  return `${d}${marketSuffixForNum(d)}`;
}

function candidateStockCodes(digitsOrRaw) {
  const d = stockCodeDigits(digitsOrRaw);
  if (d.length !== 6) return [];
  const primary = toFullStockCode(d);
  const alts = [`${d}.SH`, `${d}.SZ`, `${d}.BJ`].filter((c) => c !== primary);
  return [primary, ...alts];
}

function fillStockCode(id, raw) {
  const el = $(id);
  if (!el) return;
  el.value = stockCodeDigits(raw);
  el.dispatchEvent(new Event("input", { bubbles: true }));
}

function readStockCode(id) {
  return toFullStockCode($(id)?.value || "");
}

function requireStockCode(id) {
  const full = readStockCode(id);
  if (!full) {
    toast("请输入6位数字股票代码");
    $(id)?.focus();
    return null;
  }
  return full;
}

function stockCodeHintEl(input) {
  return input.closest(".code-with-name")?.querySelector(".code-name-hint") || null;
}

function setStockCodeHint(input, text, miss = false) {
  const hint = stockCodeHintEl(input);
  if (!hint) return;
  hint.textContent = text || "";
  hint.classList.toggle("is-miss", !!miss && !!text);
}

async function lookupStockQuote(digits) {
  const candidates = candidateStockCodes(digits);
  if (!candidates.length) return null;
  // 一次请求多市场候选，兼容基金/LOF/北交所后缀误判
  const data = await api(`/api/quotes?codes=${encodeURIComponent(candidates.join(","))}`);
  const items = data.items || [];
  const hit =
    items.find((x) => x.code === candidates[0] && (x.name || x.price > 0)) ||
    items.find((x) => stockCodeDigits(x.code) === digits && x.name) ||
    items.find((x) => stockCodeDigits(x.code) === digits && x.price > 0) ||
    null;
  return hit;
}

function bindStockCodeInputs() {
  document.querySelectorAll(".js-stock-code").forEach((input) => {
    if (input.dataset.codeBound === "1") return;
    input.dataset.codeBound = "1";
    input.setAttribute("maxlength", "6");
    input.setAttribute("inputmode", "numeric");
    input.setAttribute("pattern", "\\d{6}");
    input.setAttribute("autocomplete", "off");
    if (!input.placeholder) input.placeholder = "600616";

    const nameId = input.dataset.nameTarget;
    const nameEl = nameId ? $(nameId) : null;
    if (nameEl && nameEl.dataset.nameBound !== "1") {
      nameEl.dataset.nameBound = "1";
      nameEl.addEventListener("input", () => {
        nameEl.dataset.autofilled = "0";
      });
    }
    const priceId = input.dataset.fillPrice;
    const priceEl = priceId ? $(priceId) : null;
    if (priceEl && priceEl.dataset.priceBound !== "1") {
      priceEl.dataset.priceBound = "1";
      priceEl.addEventListener("input", () => {
        priceEl.dataset.autofilled = "0";
      });
    }

    let timer = 0;
    const sanitize = () => {
      const next = stockCodeDigits(input.value);
      if (input.value !== next) input.value = next;
      if (next.length < 6) {
        setStockCodeHint(input, "");
        return;
      }
      clearTimeout(timer);
      timer = setTimeout(async () => {
        try {
          const q = await lookupStockQuote(next);
          if (stockCodeDigits(input.value) !== next) return;
          if (q?.name) {
            setStockCodeHint(input, q.name, false);
            if (nameEl && (!nameEl.value.trim() || nameEl.dataset.autofilled === "1")) {
              nameEl.value = q.name;
              nameEl.dataset.autofilled = "1";
            }
            if (priceEl && q.price > 0 && (!Number(priceEl.value) || priceEl.dataset.autofilled === "1")) {
              priceEl.value = q.price;
              priceEl.dataset.autofilled = "1";
            }
          } else {
            setStockCodeHint(input, "未找到", true);
          }
        } catch (_) {
          if (stockCodeDigits(input.value) === next) setStockCodeHint(input, "查询失败", true);
        }
      }, 160);
    };

    input.addEventListener("beforeinput", (e) => {
      if (e.inputType === "insertText" && e.data && /\D/.test(e.data)) e.preventDefault();
    });
    input.addEventListener("keydown", (e) => {
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      const allow = ["Backspace", "Delete", "Tab", "Enter", "Escape", "ArrowLeft", "ArrowRight", "Home", "End"];
      if (allow.includes(e.key)) return;
      if (e.key.length === 1 && /\D/.test(e.key)) e.preventDefault();
    });
    input.addEventListener("paste", (e) => {
      e.preventDefault();
      const text = (e.clipboardData || window.clipboardData)?.getData("text") || "";
      input.value = stockCodeDigits(text);
      sanitize();
    });
    input.addEventListener("input", sanitize);
  });
}

function sourceLabel(source) {
  return source === "xueqiu" ? "雪球" : "东财";
}

function escapeHtml(text) {
  return String(text ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function rankReturn(item) {
  const map = {
    daily: item.daily_return,
    "5d": item.return_5d,
    "20d": item.return_20d,
    "250d": item.return_250d,
    total: item.total_return,
  };
  return map[state.rank] ?? item.daily_return ?? item.total_return;
}

function marketSession() {
  const now = new Date();
  const day = now.getDay();
  if (day === 0 || day === 6) return "closed";
  const mins = now.getHours() * 60 + now.getMinutes();
  if ((mins >= 9 * 60 + 30 && mins <= 11 * 60 + 30) || (mins >= 13 * 60 && mins <= 15 * 60)) {
    return "open";
  }
  return "closed";
}

function updateTopMeta() {
  state.session = marketSession();
  const badge = $("session-badge");
  badge.textContent = state.session === "open" ? "盘中" : "已收盘";
  badge.className = state.session === "open" ? "is-session" : "";
}

function showPage(page) {
  state.page = page;
  document.querySelectorAll(".page").forEach((el) => {
    el.classList.toggle("is-active", el.id === `page-${page}`);
  });
  document.querySelectorAll(".nav-item").forEach((el) => {
    el.classList.toggle("is-active", el.dataset.page === page);
  });
  if (page === "picker") loadList();
  if (page === "follow") renderFollowHome();
  if (page === "account") renderAccount();
  if (page === "logs") renderProductLogs();
  if (page === "alert") loadAlerts();
  if (page === "stoploss") loadStops();
  if (page === "condition") loadConds();
  if (page === "pool") loadWatch();
  if (page === "t0") loadT0();
  if (page === "formula") loadFormulas();
  if (page === "basket") loadBaskets();
  if (page === "board") loadBoards();
  if (page === "accounts") loadAccounts();
  if (page === "curve") loadCurve();
  if (page === "workflow") showWorkflowList();
  if (page === "grid") {
    loadGrid();
    if (gridEngineTimer) clearInterval(gridEngineTimer);
    gridEngineTimer = setInterval(() => {
      if (state.page === "grid") loadGrid();
    }, 2000);
  } else if (gridEngineTimer) {
    clearInterval(gridEngineTimer);
    gridEngineTimer = null;
  }
  if (page === "workflow") {
    if (wfRuntimeTimer) clearInterval(wfRuntimeTimer);
    wfRuntimeTimer = setInterval(() => {
      if (state.page === "workflow" && wfDraft?.id) refreshWfRuntime();
    }, 4000);
  } else if (wfRuntimeTimer) {
    clearInterval(wfRuntimeTimer);
    wfRuntimeTimer = null;
  }
}

/* —— 交易程序（工作流画布） —— */
let wfDraft = null;
let wfSelected = null;
let wfLinkFrom = null;
let wfRuntimeTimer = null;
let wfDrag = null;

function wfNewId() {
  return Math.random().toString(16).slice(2, 12);
}

function defaultNodeConfig(type) {
  if (type === "selector")
    return { label: "选股器", source: "file", sign_name: "买入信号", compare: "gte", trigger_price: 0, trigger_pct: 0, schedule: "every_poll", run_at: "09:25:00" };
  if (type === "pool") return { label: "股票池", codes: [], seed_from_watchlist: false };
  return {
    label: "策略单",
    side: "buy",
    kind: "price",
    compare: "gte",
    trigger_price: 0,
    trigger_pct: 0,
    trigger_time: "09:30:00",
    qty: 100,
    price_type: "ask1",
    limit_price: 0,
    once_per_code_day: true,
  };
}

function showWorkflowList() {
  $("wf-list-wrap")?.classList.remove("is-hidden");
  $("wf-editor")?.classList.add("is-hidden");
  loadWorkflows();
}

async function loadWorkflows() {
  const rows = $("wf-rows");
  if (!rows) return;
  try {
    const list = await api("/api/workflows");
    rows.innerHTML =
      (list || [])
        .map(
          (p) => `<tr>
          <td>${escapeHtml(p.name)}</td>
          <td class="num">${p.nodes || 0}/${p.edges || 0}</td>
          <td>${p.enabled ? "是" : "否"}</td>
          <td>${p.pre_auth ? "是" : "否"}</td>
          <td class="mono">${escapeHtml(p.updated_at || "—")}</td>
          <td>
            <button class="btn linkish" data-act="edit" data-id="${p.id}">编辑</button>
            <button class="btn linkish" data-act="toggle" data-id="${p.id}">启停</button>
            <button class="btn linkish" data-act="del" data-id="${p.id}">删</button>
          </td>
        </tr>`
        )
        .join("") || `<tr><td colspan="6" class="muted">暂无交易程序，可「新建」或加载示例</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          if (btn.dataset.act === "del") {
            await api(`/api/workflows/${btn.dataset.id}`, { method: "DELETE" });
            loadWorkflows();
          } else if (btn.dataset.act === "toggle") {
            await api(`/api/workflows/${btn.dataset.id}/toggle`, { method: "POST", body: "{}" });
            loadWorkflows();
          } else if (btn.dataset.act === "edit") {
            const full = await api(`/api/workflows/${btn.dataset.id}`);
            openWfEditor(full);
          }
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (e) {
    toast(e.message);
  }
}

function openWfEditor(program) {
  wfDraft = {
    id: program?.id || "",
    name: program?.name || "未命名程序",
    enabled: !!program?.enabled,
    pre_auth: !!program?.pre_auth,
    nodes: (program?.nodes || []).map((n) => ({ ...n, config: { ...defaultNodeConfig(n.type), ...(n.config || {}) } })),
    edges: (program?.edges || []).map((e) => ({ ...e })),
    note: program?.note || "",
  };
  wfSelected = null;
  wfLinkFrom = null;
  $("wf-list-wrap")?.classList.add("is-hidden");
  $("wf-editor")?.classList.remove("is-hidden");
  $("wf-name").value = wfDraft.name;
  $("wf-enabled").checked = wfDraft.enabled;
  $("wf-preauth").checked = wfDraft.pre_auth;
  renderWfCanvas();
  renderWfProps();
  refreshWfRuntime();
}

function renderWfCanvas() {
  const canvas = $("wf-canvas");
  const svg = $("wf-edges");
  if (!canvas || !svg || !wfDraft) return;
  canvas.innerHTML = "";
  svg.innerHTML = "";
  const typeLabel = { selector: "选股器", pool: "股票池", strategy: "策略单" };
  wfDraft.nodes.forEach((n) => {
    const el = document.createElement("div");
    el.className = "wf-node" + (wfSelected === n.id ? " is-selected" : "");
    el.dataset.id = n.id;
    el.dataset.type = n.type;
    el.style.left = `${n.x}px`;
    el.style.top = `${n.y}px`;
    const sub =
      n.type === "selector"
        ? n.config.source + (n.config.sign_name ? ` · ${n.config.sign_name}` : "")
        : n.type === "strategy"
          ? `${n.config.side} ${n.config.kind}`
          : `${(n.config.codes || []).length} 种子码`;
    el.innerHTML = `<div class="wf-node-title">${escapeHtml(n.config.label || typeLabel[n.type])}</div>
      <div class="wf-node-sub">${escapeHtml(sub)}</div>
      <span class="wf-port in" data-port="in"></span>
      <span class="wf-port out" data-port="out"></span>`;
    el.addEventListener("mousedown", (ev) => {
      if (ev.target.classList.contains("wf-port")) return;
      wfSelected = n.id;
      wfDrag = { id: n.id, ox: ev.clientX - n.x, oy: ev.clientY - n.y };
      renderWfCanvas();
      renderWfProps();
    });
    el.querySelector(".wf-port.out").addEventListener("mousedown", (ev) => {
      ev.stopPropagation();
      wfLinkFrom = n.id;
      toast("从输出点连到目标输入点");
    });
    el.querySelector(".wf-port.in").addEventListener("mouseup", (ev) => {
      ev.stopPropagation();
      if (!wfLinkFrom || wfLinkFrom === n.id) return;
      if (!wfDraft.edges.some((e) => e.from === wfLinkFrom && e.to === n.id)) {
        wfDraft.edges.push({ from: wfLinkFrom, to: n.id });
      }
      wfLinkFrom = null;
      renderWfCanvas();
    });
    canvas.appendChild(el);
  });
  const nodeMap = Object.fromEntries(wfDraft.nodes.map((n) => [n.id, n]));
  wfDraft.edges.forEach((e, idx) => {
    const a = nodeMap[e.from];
    const b = nodeMap[e.to];
    if (!a || !b) return;
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", a.x + 140);
    line.setAttribute("y1", a.y + 28);
    line.setAttribute("x2", b.x);
    line.setAttribute("y2", b.y + 28);
    line.style.pointerEvents = "stroke";
    line.addEventListener("dblclick", () => {
      wfDraft.edges.splice(idx, 1);
      renderWfCanvas();
    });
    svg.appendChild(line);
  });
}

document.addEventListener("mousemove", (ev) => {
  if (!wfDrag || !wfDraft) return;
  const n = wfDraft.nodes.find((x) => x.id === wfDrag.id);
  if (!n) return;
  n.x = Math.max(0, ev.clientX - wfDrag.ox);
  n.y = Math.max(0, ev.clientY - wfDrag.oy);
  const el = document.querySelector(`.wf-node[data-id="${n.id}"]`);
  if (el) {
    el.style.left = `${n.x}px`;
    el.style.top = `${n.y}px`;
  }
  renderWfEdgesOnly();
});
document.addEventListener("mouseup", () => {
  wfDrag = null;
});

function renderWfEdgesOnly() {
  const svg = $("wf-edges");
  if (!svg || !wfDraft) return;
  svg.innerHTML = "";
  const nodeMap = Object.fromEntries(wfDraft.nodes.map((n) => [n.id, n]));
  wfDraft.edges.forEach((e, idx) => {
    const a = nodeMap[e.from];
    const b = nodeMap[e.to];
    if (!a || !b) return;
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", a.x + 140);
    line.setAttribute("y1", a.y + 28);
    line.setAttribute("x2", b.x);
    line.setAttribute("y2", b.y + 28);
    line.style.pointerEvents = "stroke";
    line.addEventListener("dblclick", () => {
      wfDraft.edges.splice(idx, 1);
      renderWfCanvas();
    });
    svg.appendChild(line);
  });
}

function renderWfProps() {
  const empty = $("wf-props-empty");
  const form = $("wf-props-form");
  if (!empty || !form) return;
  const n = wfDraft?.nodes.find((x) => x.id === wfSelected);
  if (!n) {
    empty.classList.remove("is-hidden");
    form.classList.add("is-hidden");
    form.innerHTML = "";
    return;
  }
  empty.classList.add("is-hidden");
  form.classList.remove("is-hidden");
  const c = n.config;
  let html = `<label>显示名 <input data-k="label" value="${escapeHtml(c.label || "")}" /></label>`;
  if (n.type === "selector") {
    html += `<label>来源
      <select data-k="source">
        <option value="file"${c.source === "file" ? " selected" : ""}>通达信文件</option>
        <option value="watchlist"${c.source === "watchlist" ? " selected" : ""}>自选池</option>
        <option value="quote"${c.source === "quote" ? " selected" : ""}>行情筛选</option>
      </select></label>
      <label>信号名 <input data-k="sign_name" value="${escapeHtml(c.sign_name || "")}" /></label>
      <label>比较 <select data-k="compare"><option value="gte"${c.compare === "gte" ? " selected" : ""}>≥</option><option value="lte"${c.compare === "lte" ? " selected" : ""}>≤</option></select></label>
      <label>触发价 <input data-k="trigger_price" type="number" step="0.01" value="${c.trigger_price || 0}" /></label>
      <label>涨跌幅% <input data-k="trigger_pct" type="number" step="0.1" value="${c.trigger_pct || 0}" /></label>
      <label>调度
        <select data-k="schedule">
          <option value="every_poll"${c.schedule === "every_poll" ? " selected" : ""}>每轮扫描</option>
          <option value="once_daily"${c.schedule === "once_daily" ? " selected" : ""}>每日一次</option>
        </select></label>
      <label>每日时刻 <input data-k="run_at" value="${escapeHtml(c.run_at || "09:25:00")}" /></label>`;
  } else if (n.type === "pool") {
    html += `<label class="check"><input data-k="seed_from_watchlist" type="checkbox"${c.seed_from_watchlist ? " checked" : ""} /> 种子自全局自选</label>
      <label>初始代码(逗号分隔) <input data-k="codes_text" value="${escapeHtml((c.codes || []).join(","))}" /></label>`;
  } else {
    html += `<label>方向 <select data-k="side"><option value="buy"${c.side === "buy" ? " selected" : ""}>买</option><option value="sell"${c.side === "sell" ? " selected" : ""}>卖</option></select></label>
      <label>类型 <select data-k="kind"><option value="price"${c.kind === "price" ? " selected" : ""}>价格</option><option value="time"${c.kind === "time" ? " selected" : ""}>时间</option><option value="pct"${c.kind === "pct" ? " selected" : ""}>涨跌幅</option></select></label>
      <label>比较 <select data-k="compare"><option value="gte"${c.compare === "gte" ? " selected" : ""}>≥</option><option value="lte"${c.compare === "lte" ? " selected" : ""}>≤</option></select></label>
      <label>触发价 <input data-k="trigger_price" type="number" step="0.01" value="${c.trigger_price || 0}" /></label>
      <label>涨跌幅% <input data-k="trigger_pct" type="number" step="0.1" value="${c.trigger_pct || 0}" /></label>
      <label>触发时间 <input data-k="trigger_time" value="${escapeHtml(c.trigger_time || "")}" /></label>
      <label>数量 <input data-k="qty" type="number" value="${c.qty || 100}" /></label>
      <label>报价方式 <input data-k="price_type" value="${escapeHtml(c.price_type || "ask1")}" /></label>
      <label>限价覆盖 <input data-k="limit_price" type="number" step="0.01" value="${c.limit_price || 0}" /></label>
      <label class="check"><input data-k="once_per_code_day" type="checkbox"${c.once_per_code_day !== false ? " checked" : ""} /> 每码每日一次</label>`;
  }
  html += `<button type="button" class="btn ghost" id="wf-del-node">删除节点</button>`;
  form.innerHTML = html;
  form.querySelectorAll("[data-k]").forEach((el) => {
    const apply = () => {
      const k = el.dataset.k;
      if (k === "codes_text") {
        c.codes = el.value
          .split(/[,，\s]+/)
          .map((s) => s.trim())
          .filter(Boolean);
      } else if (el.type === "checkbox") c[k] = el.checked;
      else if (el.type === "number") c[k] = Number(el.value) || 0;
      else c[k] = el.value;
      renderWfCanvas();
    };
    el.addEventListener("change", apply);
    el.addEventListener("blur", apply);
  });
  form.querySelector("#wf-del-node")?.addEventListener("click", () => {
    wfDraft.nodes = wfDraft.nodes.filter((x) => x.id !== n.id);
    wfDraft.edges = wfDraft.edges.filter((e) => e.from !== n.id && e.to !== n.id);
    wfSelected = null;
    renderWfCanvas();
    renderWfProps();
  });
}

async function refreshWfRuntime() {
  const box = $("wf-runtime");
  if (!box || !wfDraft?.id) {
    if (box) box.textContent = "保存后可查看运行态";
    return;
  }
  try {
    const st = await api(`/api/workflows/${wfDraft.id}/state`);
    const pools = st.pools || {};
    const parts = Object.keys(pools).map((id) => {
      const p = pools[id];
      const codes = (p.codes || []).join(", ") || "（空）";
      return `<div class="pool-block"><strong>${escapeHtml(p.label || id)}</strong><br/>${escapeHtml(codes)}</div>`;
    });
    const recent = (st.recent || [])
      .slice()
      .reverse()
      .slice(0, 5)
      .map((r) => `${r.ts || ""} ${r.side || ""} ${r.code || ""}`)
      .join("<br/>");
    box.innerHTML = (parts.join("") || "<div class='muted'>暂无池</div>") + (recent ? `<div class="muted" style="margin-top:8px">最近<br/>${recent}</div>` : "");
  } catch (_) {
    box.textContent = "运行态暂不可用";
  }
}

async function saveWfDraft() {
  if (!wfDraft) return;
  wfDraft.name = $("wf-name").value.trim() || "未命名程序";
  wfDraft.enabled = $("wf-enabled").checked;
  wfDraft.pre_auth = $("wf-preauth").checked;
  const body = { ...wfDraft };
  const v = await api("/api/workflows/validate", { method: "POST", body: JSON.stringify(body) });
  if (!v.ok) {
    toast((v.errors || []).join("；") || "校验失败");
    return;
  }
  const saved = await api("/api/workflows", { method: "POST", body: JSON.stringify(body) });
  wfDraft.id = saved.id;
  toast("交易程序已保存");
  refreshWfRuntime();
}

function renderMetrics(el, pairs) {
  el.innerHTML = pairs
    .map(([label, value, cls = ""]) => `<div><dt>${label}</dt><dd class="${cls}">${value}</dd></div>`)
    .join("");
}

function positionWeight(account) {
  if (!account) return 0;
  const nav = Number(account.nav) || 0;
  if (!nav) return 0;
  return ((nav - Number(account.cash || 0)) / nav) * 100;
}

function alignmentText(account, targetDetail) {
  if (!account) return "-";
  const mine = positionWeight(account);
  const theirs =
    targetDetail?.position_weight != null
      ? Number(targetDetail.position_weight)
      : (account.target_holdings || []).reduce((s, h) => s + Number(h.weight || 0), 0);
  const diff = theirs - mine;
  if (Math.abs(diff) < 0.5) return "已对齐";
  const sign = diff > 0 ? "+" : "";
  return `${sign}${diff.toFixed(1)}%`;
}

function statusLabel(status) {
  if (status === "paused") return { cls: "paused", text: "已暂停" };
  if (status === "failed") return { cls: "failed", text: "失败" };
  return { cls: "running", text: "监控中" };
}

function renderFollowHome() {
  const account = state.follow;
  const empty = $("follow-empty");
  const wrap = $("follow-table-wrap");
  if (!account) {
    empty.classList.remove("is-hidden");
    wrap.classList.add("is-hidden");
    return;
  }
  empty.classList.add("is-hidden");
  wrap.classList.remove("is-hidden");
  const st = statusLabel(account.status);
  const targetRet = state.targetDetail?.daily_return;
  const myRet = account.pnl_pct;
  const align = alignmentText(account, state.targetDetail);
  $("follow-rows").innerHTML = `
    <tr data-id="${escapeHtml(account.portfolio_id)}">
      <td><span class="status ${st.cls}">${st.text}</span></td>
      <td>${sourceLabel(account.source)}</td>
      <td>
        <div class="cell-name">
          <strong>${escapeHtml(account.target_name || account.portfolio_id)}</strong>
          <small>${escapeHtml(account.portfolio_id)}</small>
        </div>
      </td>
      <td class="num ${tone(targetRet)}">${pct(targetRet)}</td>
      <td class="num ${tone(myRet)}">${pct(myRet)}</td>
      <td class="num">${align}</td>
      <td class="mono">${escapeHtml(account.last_sync_at || "-")}</td>
      <td>
        <div class="row-actions">
          <button class="btn linkish" type="button" data-act="open">对照</button>
          <button class="btn linkish" type="button" data-act="sync">同步</button>
          <button class="btn linkish" type="button" data-act="toggle">
            ${account.status === "paused" ? "继续" : "暂停"}
          </button>
          <button class="btn linkish" type="button" data-act="stop">结束</button>
        </div>
      </td>
    </tr>`;
  const row = $("follow-rows").querySelector("tr");
  row.addEventListener("click", (event) => {
    const btn = event.target.closest("[data-act]");
    if (!btn) {
      openCompare();
      return;
    }
    event.stopPropagation();
    const act = btn.dataset.act;
    if (act === "open") openCompare();
    if (act === "sync") syncFollow();
    if (act === "toggle") togglePause();
    if (act === "stop") stopFollow();
  });
}

function alignedHoldings(targetHoldings, positions) {
  const map = new Map();
  for (const h of targetHoldings || []) {
    map.set(h.code, {
      code: h.code,
      name: h.name,
      targetWeight: Number(h.weight || 0),
      shares: null,
      mineWeight: null,
    });
  }
  for (const p of positions || []) {
    const row = map.get(p.code) || {
      code: p.code,
      name: p.name,
      targetWeight: null,
      shares: null,
      mineWeight: null,
    };
    row.name = p.name || row.name;
    row.shares = p.shares;
    row.mineWeight = Number(p.weight || 0);
    map.set(p.code, row);
  }
  return [...map.values()].sort(
    (a, b) => (b.targetWeight ?? b.mineWeight ?? 0) - (a.targetWeight ?? a.mineWeight ?? 0)
  );
}

function renderCompare() {
  const account = state.follow;
  const detail = state.targetDetail;
  if (!account || !detail) return;
  $("compare-title").textContent = account.target_name || detail.name;
  $("compare-sub").textContent = `${sourceLabel(account.source)} · ${account.portfolio_id}`;
  renderMetrics($("compare-metrics"), [
    ["日收益", pct(detail.daily_return), tone(detail.daily_return)],
    ["20日", pct(detail.return_20d), tone(detail.return_20d)],
    ["总收益", pct(detail.total_return), tone(detail.total_return)],
    [
      "对方仓位 / 现金",
      `${(detail.position_weight ?? 0).toFixed(1)}% / ${(detail.cash_weight ?? 0).toFixed(1)}%`,
    ],
  ]);
  $("compare-warn").hidden = !detail.warning;
  $("compare-warn").textContent = detail.warning || "";

  const targetHoldings = (detail.holdings || account.target_holdings || []).filter(
    (h) => Number(h.weight || 0) > 0
  );
  const rows = alignedHoldings(targetHoldings, account.positions);
  $("compare-target-rows").innerHTML = rows
    .map((r) => {
      const flag = r.targetWeight == null ? "我有" : r.mineWeight == null ? "对方有" : "";
      return `<tr>
        <td class="mono">${escapeHtml(r.code)}</td>
        <td>${escapeHtml(r.name || "")}</td>
        <td class="num">${r.targetWeight == null ? "-" : `${r.targetWeight.toFixed(2)}%`}</td>
        <td class="num muted">${flag}</td>
      </tr>`;
    })
    .join("");
  $("compare-mine-rows").innerHTML = rows
    .map((r) => {
      const flag = r.mineWeight == null ? "对方有" : r.targetWeight == null ? "我有" : "";
      const diff =
        r.targetWeight != null && r.mineWeight != null ? r.targetWeight - r.mineWeight : null;
      return `<tr>
        <td class="num">${r.shares ?? "-"}</td>
        <td class="num">${r.mineWeight == null ? "-" : `${r.mineWeight.toFixed(2)}%`}</td>
        <td class="num ${tone(diff)}">${diff == null ? "-" : `${diff > 0 ? "+" : ""}${diff.toFixed(2)}%`}</td>
        <td class="num muted">${flag}</td>
      </tr>`;
    })
    .join("");

  const rebalances = detail.rebalances || [];
  $("compare-rebalance-rows").innerHTML = rebalances.length
    ? rebalances
        .map(
          (r) => `<tr>
            <td class="mono">${escapeHtml(r.time || "")}</td>
            <td>${escapeHtml(r.side || "")}</td>
            <td>${escapeHtml(r.name || "")} <span class="muted mono">${escapeHtml(r.code || "")}</span></td>
            <td class="num">${r.prev_weight ?? "-"}% → ${r.weight ?? "-"}%</td>
            <td class="num">${r.price ?? "-"}</td>
          </tr>`
        )
        .join("")
    : `<tr><td colspan="5" class="muted">暂无调仓记录</td></tr>`;
}

async function openCompare() {
  if (!state.follow) return;
  try {
    await refreshTargetDetail();
    renderCompare();
    showPage("compare");
  } catch (err) {
    toast(err.message);
  }
}

function renderAccount() {
  const account = state.follow;
  const empty = $("account-empty");
  const body = $("account-body");
  if (!account) {
    empty.classList.remove("is-hidden");
    body.classList.add("is-hidden");
    $("account-target").textContent = "还没有跟单";
    return;
  }
  empty.classList.add("is-hidden");
  body.classList.remove("is-hidden");
  $("account-target").textContent = `正在跟 ${sourceLabel(account.source)} · ${account.target_name} · ${account.portfolio_id}`;
  renderMetrics($("account-metrics"), [
    ["总资产", money(account.nav)],
    ["现金", money(account.cash)],
    ["浮动盈亏", `${money(account.pnl)} (${pct(account.pnl_pct)})`, tone(account.pnl)],
  ]);
  $("account-rows").innerHTML = (account.positions || [])
    .map(
      (p) => `<tr>
        <td class="mono">${escapeHtml(p.code)}</td>
        <td>${escapeHtml(p.name)}</td>
        <td class="num">${p.shares}</td>
        <td class="num">${Number(p.cost || 0).toFixed(2)}</td>
        <td class="num">${p.price ?? "-"}</td>
        <td class="num ${tone(p.pnl)}">${money(p.pnl)} / ${pct(p.pnl_pct)}</td>
        <td class="num">${(p.weight ?? 0).toFixed(2)}%</td>
      </tr>`
    )
    .join("") || `<tr><td colspan="7" class="muted">暂无持仓</td></tr>`;
  $("trade-rows").innerHTML = (account.trades || [])
    .slice(0, 50)
    .map(
      (t) => `<tr>
        <td class="mono">${escapeHtml(t.time)}</td>
        <td>${escapeHtml(t.side)}</td>
        <td>${escapeHtml(t.name)}</td>
        <td class="num">${t.shares}</td>
        <td class="num">${t.price}</td>
        <td class="num">${money(t.amount)}</td>
      </tr>`
    )
    .join("") || `<tr><td colspan="6" class="muted">还没有成交</td></tr>`;
}

state.logTab = "events";

async function renderProductLogs() {
  const empty = $("logs-empty");
  const wrap = $("logs-table-wrap");
  const rows = $("log-rows");
  if (!rows) return;
  try {
    let html = "";
    if (state.logTab === "orders") {
      const list = await api("/api/orders?limit=100");
      html = (list || [])
        .map(
          (o) => `<tr>
          <td class="mono">${escapeHtml(o.created_at || "")}</td>
          <td>${escapeHtml(o.folder || "")}</td>
          <td>${escapeHtml(o.side || "")} ${escapeHtml(o.code || "")} x${o.qty || 0} @${o.price || 0} · ${escapeHtml(o.reason || "")}</td>
        </tr>`
        )
        .join("");
    } else if (state.logTab === "engine") {
      const st = await api("/api/grid/engine");
      const tail = st.log_tail || [];
      html = tail
        .slice()
        .reverse()
        .map((line) => `<tr><td class="mono">—</td><td>engine</td><td>${escapeHtml(line)}</td></tr>`)
        .join("");
    } else {
      const list = await api("/api/events?limit=100");
      html = (list || [])
        .map(
          (ev) => `<tr>
          <td class="mono">${escapeHtml(ev.ts || "")}</td>
          <td>${escapeHtml(ev.kind || "")}</td>
          <td>${escapeHtml(ev.msg || "")}</td>
        </tr>`
        )
        .join("");
    }
    if (!html) {
      empty?.classList.remove("is-hidden");
      wrap?.classList.add("is-hidden");
      rows.innerHTML = "";
      return;
    }
    empty?.classList.add("is-hidden");
    wrap?.classList.remove("is-hidden");
    rows.innerHTML = html;
  } catch (err) {
    toast(err.message);
  }
}

function renderLogs() {
  renderProductLogs();
}

async function loadAlerts() {
  const rows = $("alert-rows");
  if (!rows) return;
  try {
    const list = await api("/api/alerts");
    rows.innerHTML =
      (list || [])
        .map((a) => {
          const cond =
            a.source === "pct"
              ? `<span class="${a.compare === "lte" ? "down" : "up"}">${a.compare === "lte" ? "跌" : "涨"}${a.trigger_pct}%</span>`
              : a.source === "file"
                ? escapeHtml(a.sign_name || "—")
                : `${a.compare === "lte" ? "≤" : "≥"}${a.trigger_price}`;
          return `<tr>
            <td>${escapeHtml(a.source)}</td>
            <td>${escapeHtml(a.name)}</td>
            <td class="mono">${escapeHtml(a.code || a.sign_name || "")}</td>
            <td>${escapeHtml(a.side)}</td>
            <td class="num">${cond}</td>
            <td class="num">${a.qty}${a.qty_unit === "amount" ? "元" : "股"}</td>
            <td>${a.auto_trade ? "是" : "否"}</td>
            <td><span class="status ${a.enabled ? "running" : "paused"}">${escapeHtml(a.status)}</span></td>
            <td class="mono">${escapeHtml(a.last_trigger_at || "—")}</td>
            <td>
              <button class="btn linkish" data-act="edit" data-id="${a.id}">编辑</button>
              <button class="btn linkish" data-act="toggle" data-id="${a.id}">启停</button>
              <button class="btn linkish" data-act="del" data-id="${a.id}">删</button>
            </td>
          </tr>`;
        })
        .join("") || `<tr><td colspan="10" class="muted">暂无预警，点右上角添加</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.dataset.id;
        const act = btn.dataset.act;
        try {
          if (act === "del") {
            await api(`/api/alerts/${id}`, { method: "DELETE" });
            toast("已删除");
          } else if (act === "toggle") {
            await api(`/api/alerts/${id}/toggle`, { method: "POST" });
          } else if (act === "edit") {
            const item = (list || []).find((x) => x.id === id);
            openAlertDialog(item);
            return;
          }
          loadAlerts();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (err) {
    toast(err.message);
  }
}

function openAlertDialog(item) {
  $("a-id").value = item?.id || "";
  $("a-name").value = item?.name || "";
  $("a-source").value = item?.source || "price";
  fillStockCode("a-code", item?.code || "");
  $("a-sign").value = item?.sign_name || "";
  $("a-side").value = item?.side || "buy";
  $("a-compare").value = item?.compare || "gte";
  $("a-price").value = item?.trigger_price ?? 0;
  $("a-pct").value = item?.trigger_pct ?? 3;
  $("a-qty").value = item?.qty ?? 100;
  $("a-unit").value = item?.qty_unit || "shares";
  $("a-cd").value = item?.cooldown_sec ?? 60;
  $("a-auto").checked = item?.auto_trade !== false;
  $("a-once").checked = !!item?.once;
  $("alert-dialog").showModal();
}

function stopCondLabel(s) {
  const parts = [];
  if (s.stop_price > 0) parts.push(`价≤${s.stop_price}`);
  else if (s.stop_pct > 0) parts.push(`-${s.stop_pct}%`);
  else parts.push("—");
  return parts.join(" ");
}
function takeCondLabel(s) {
  const parts = [];
  if (s.take_price > 0) parts.push(`价≥${s.take_price}`);
  else if (s.take_pct > 0) parts.push(`+${s.take_pct}%`);
  else parts.push("—");
  return parts.join(" ");
}
function condKindLabel(c) {
  const kind = c.kind || "price";
  if (kind === "time") return `时间 ${c.trigger_time || ""}`;
  if (kind === "pct") return `涨跌幅 ${c.compare === "lte" ? "≤-" : "≥+"}${c.trigger_pct || 0}%`;
  return `价格 ${c.compare === "lte" ? "≤" : "≥"}${c.trigger_price || 0}`;
}

async function loadStops() {
  const rows = $("sl-rows");
  if (!rows) return;
  try {
    const list = await api("/api/stoploss");
    rows.innerHTML =
      (list || [])
        .map(
          (s) => `<tr>
          <td class="mono">${escapeHtml(s.code)}</td>
          <td>${escapeHtml(s.name || "")}</td>
          <td class="num">${s.qty}</td>
          <td class="num">${s.cost_price}</td>
          <td class="num down">${escapeHtml(stopCondLabel(s))}</td>
          <td class="num up">${escapeHtml(takeCondLabel(s))}</td>
          <td>${s.trail_enabled ? `回落${s.trail_pct}%` : "—"}</td>
          <td><span class="status ${s.enabled ? "running" : "paused"}">${escapeHtml(s.status)}</span></td>
          <td class="mono">${escapeHtml(s.last_trigger_at || "—")}</td>
          <td>
            <button class="btn linkish" data-act="edit" data-id="${s.id}">编辑</button>
            <button class="btn linkish" data-act="toggle" data-id="${s.id}">启停</button>
            <button class="btn linkish" data-act="del" data-id="${s.id}">删</button>
          </td>
        </tr>`
        )
        .join("") || `<tr><td colspan="10" class="muted">暂无规则</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.dataset.id;
        const act = btn.dataset.act;
        try {
          if (act === "del") await api(`/api/stoploss/${id}`, { method: "DELETE" });
          else if (act === "toggle") await api(`/api/stoploss/${id}/toggle`, { method: "POST" });
          else if (act === "edit") {
            const item = (list || []).find((x) => x.id === id);
            openSlDialog(item);
            return;
          }
          loadStops();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (err) {
    toast(err.message);
  }
}

function openSlDialog(item) {
  $("sl-id").value = item?.id || "";
  if ($("sl-name")) {
    $("sl-name").value = item?.name || "";
    $("sl-name").dataset.autofilled = item?.name ? "0" : "1";
  }
  fillStockCode("sl-code", item?.code || "");
  $("sl-cost").value = item?.cost_price ?? 0;
  $("sl-qty").value = item?.qty ?? 100;
  $("sl-stop").value = item?.stop_pct ?? 5;
  $("sl-take").value = item?.take_pct ?? 10;
  if ($("sl-stop-px")) $("sl-stop-px").value = item?.stop_price ?? 0;
  if ($("sl-take-px")) $("sl-take-px").value = item?.take_price ?? 0;
  if ($("sl-trail")) $("sl-trail").value = item?.trail_pct ?? 0;
  if ($("sl-trail-on")) $("sl-trail-on").checked = !!item?.trail_enabled;
  $("sl-dialog").showModal();
}

async function loadConds() {
  const rows = $("cond-rows");
  if (!rows) return;
  try {
    const list = await api("/api/conditions");
    rows.innerHTML =
      (list || [])
        .map(
          (c) => `<tr>
          <td class="mono">${escapeHtml(c.code)}</td>
          <td>${escapeHtml(c.side)}</td>
          <td>${escapeHtml(c.kind || "price")}</td>
          <td>${escapeHtml(condKindLabel(c))}</td>
          <td class="num">${c.qty}</td>
          <td><span class="status ${c.status === "armed" ? "running" : "paused"}">${escapeHtml(c.status)}</span></td>
          <td class="mono">${escapeHtml(c.last_trigger_at || "—")}</td>
          <td>
            <button class="btn linkish" data-act="edit" data-id="${c.id}">编辑</button>
            <button class="btn linkish" data-act="toggle" data-id="${c.id}">启停</button>
            <button class="btn linkish" data-act="del" data-id="${c.id}">删</button>
          </td>
        </tr>`
        )
        .join("") || `<tr><td colspan="8" class="muted">暂无条件单</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.dataset.id;
        const act = btn.dataset.act;
        try {
          if (act === "del") await api(`/api/conditions/${id}`, { method: "DELETE" });
          else if (act === "toggle") await api(`/api/conditions/${id}/toggle`, { method: "POST" });
          else if (act === "edit") {
            openCondDialog((list || []).find((x) => x.id === id));
            return;
          }
          loadConds();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (err) {
    toast(err.message);
  }
}

function openCondDialog(item) {
  fillPxTypeSelects($("cond-dialog") || document);
  $("c-id").value = item?.id || "";
  if ($("c-name")) {
    $("c-name").value = item?.name || "";
    $("c-name").dataset.autofilled = item?.name ? "0" : "1";
  }
  fillStockCode("c-code", item?.code || "");
  $("c-side").value = item?.side || "buy";
  if ($("c-kind")) $("c-kind").value = item?.kind || "price";
  $("c-compare").value = item?.compare || "gte";
  $("c-trigger").value = item?.trigger_price ?? 0;
  if ($("c-pct")) $("c-pct").value = item?.trigger_pct ?? 0;
  if ($("c-time")) {
    const t = item?.trigger_time || "09:30:00";
    $("c-time").value = t.length >= 8 ? t.slice(0, 8) : t;
  }
  setPxType("c-px", item?.price_type, item?.side === "sell" ? "bid1" : "ask1");
  $("c-limit").value = item?.limit_price ?? 0;
  $("c-qty").value = item?.qty ?? 100;
  $("cond-dialog").showModal();
}

async function loadWatch() {
  const rows = $("watch-rows");
  if (!rows) return;
  try {
    const list = await api("/api/watchlist");
    rows.innerHTML =
      (list || [])
        .map(
          (w) => `<tr>
          <td class="mono">${escapeHtml(w.code)}</td>
          <td>${escapeHtml(w.name || "")}</td>
          <td>${escapeHtml(w.tag || "")}</td>
          <td class="num">${w.alert_high || "—"}</td>
          <td class="num">${w.alert_low || "—"}</td>
          <td>${escapeHtml(w.note || "")}</td>
          <td>
            <button class="btn linkish" data-act="edit" data-id="${w.id}">编辑</button>
            <button class="btn linkish" data-act="del" data-id="${w.id}">删</button>
          </td>
        </tr>`
        )
        .join("") || `<tr><td colspan="7" class="muted">暂无自选</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        const id = btn.dataset.id;
        try {
          if (btn.dataset.act === "del") {
            await api(`/api/watchlist/${id}`, { method: "DELETE" });
            loadWatch();
          } else {
            openWatchDialog((list || []).find((x) => x.id === id));
          }
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (err) {
    toast(err.message);
  }
}

function openWatchDialog(item) {
  $("w-id").value = item?.id || "";
  if ($("w-name")) {
    $("w-name").value = item?.name || "";
    $("w-name").dataset.autofilled = item?.name ? "0" : "1";
  }
  fillStockCode("w-code", item?.code || "");
  $("w-tag").value = item?.tag || "";
  $("w-high").value = item?.alert_high ?? 0;
  $("w-low").value = item?.alert_low ?? 0;
  $("w-note").value = item?.note || "";
  $("watch-dialog").showModal();
}

async function loadRiskIntoSettings() {
  try {
    const r = await api("/api/risk");
    if ($("risk-sign-file")) $("risk-sign-file").value = r.tdx_sign_file || "";
    if ($("risk-max-orders")) $("risk-max-orders").value = r.max_orders_per_day ?? 20;
    if ($("risk-max-amount")) $("risk-max-amount").value = r.max_order_amount ?? 500000;
    if ($("risk-max-loss")) $("risk-max-loss").value = r.max_daily_loss ?? 50000;
    if ($("risk-code-cd")) $("risk-code-cd").value = r.code_cooldown_sec ?? 60;
    if ($("risk-sponsor")) $("risk-sponsor").value = r.sponsor_equity_limit ?? 1000000;
    if ($("risk-session-start")) $("risk-session-start").value = (r.session_start || "09:25:00").slice(0, 8);
    if ($("risk-session-end")) $("risk-session-end").value = (r.session_end || "15:00:00").slice(0, 8);
    if ($("risk-trade-days")) $("risk-trade-days").checked = r.trade_days_only !== false;
    if ($("risk-session")) $("risk-session").checked = r.session_only !== false;
    if ($("risk-sound")) $("risk-sound").checked = r.sound_on_trigger !== false;
    if ($("risk-confirm")) $("risk-confirm").checked = r.confirm_before_trade !== false;
  } catch (_) {}
}

function t0StatusLabel(r) {
  if (!r.enabled) return { cls: "paused", text: "暂停" };
  const map = {
    watching: { cls: "running", text: "盯盘" },
    long: { cls: "running", text: "持仓中" },
    done_today: { cls: "paused", text: "今日完成" },
    session_wait: { cls: "paused", text: "等开盘" },
  };
  return map[r.status] || { cls: "running", text: r.status || "监控" };
}

function closeT0Dialog() {
  const dlg = $("t0-dialog");
  if (dlg?.open) dlg.close();
}

function openT0Dialog(item) {
  fillPxTypeSelects($("t0-dialog") || document);
  $("t0-id").value = item?.id || "";
  if ($("t0-name")) {
    $("t0-name").value = item?.name || "";
    $("t0-name").dataset.autofilled = item?.name ? "0" : "1";
  }
  if ($("t0-anchor")) {
    $("t0-anchor").value = item?.base_price ?? item?.day_anchor ?? 0;
    $("t0-anchor").dataset.autofilled =
      item?.base_price || item?.day_anchor ? "0" : "1";
  }
  fillStockCode("t0-code", item?.code || "");
  setPxType("t0-px", item?.order_type, "market");
  $("t0-buy").value = item?.buy_down_pct ?? 1;
  $("t0-sell").value = item?.sell_up_pct ?? 1;
  $("t0-buy-qty").value = item?.buy_qty ?? item?.qty ?? 100;
  $("t0-sell-qty").value = item?.sell_qty ?? 0;
  $("t0-max-rounds").value = item?.max_rounds ?? 0;
  $("t0-cooldown").value = item?.cooldown_sec ?? 3;
  $("t0-session").checked = item?.session_only !== false;
  $("t0-note").value = item?.note || "";
  $("t0-dialog").showModal();
}

async function loadT0() {
  const rows = $("t0-rows");
  if (!rows) return;
  try {
    const list = await api("/api/t0");
    state.t0List = list || [];
    rows.innerHTML =
      state.t0List
        .map((r) => {
          const st = t0StatusLabel(r);
          const bq = r.buy_qty ?? r.qty ?? 0;
          const sq = r.sell_qty > 0 ? r.sell_qty : bq;
          return `<tr>
          <td class="mono">${escapeHtml(r.code)}</td><td>${escapeHtml(r.name || "")}</td>
          <td class="num down">-${Number(r.buy_down_pct).toFixed(1)}%</td>
          <td class="num up">+${Number(r.sell_up_pct).toFixed(1)}%</td>
          <td class="num">${bq}/${sq}</td>
          <td class="num">${
            r.day_anchor || r.base_price
              ? Number(r.day_anchor || r.base_price).toFixed(3)
              : "自动"
          }</td>
          <td class="num">${r.rounds_today || 0}${r.max_rounds ? "/" + r.max_rounds : ""}</td>
          <td>${r.long_today ? "是" : "否"}</td>
          <td><span class="status ${st.cls}">${st.text}</span></td>
          <td>
            <button class="btn linkish" data-act="edit" data-id="${r.id}">编辑</button>
            <button class="btn linkish" data-act="toggle" data-id="${r.id}">启停</button>
            <button class="btn linkish" data-act="del" data-id="${r.id}">删</button>
          </td></tr>`;
        })
        .join("") || `<tr><td colspan="10" class="muted">暂无</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          if (btn.dataset.act === "edit") {
            const item = state.t0List.find((x) => x.id === btn.dataset.id);
            openT0Dialog(item);
            return;
          }
          if (btn.dataset.act === "del") await api(`/api/t0/${btn.dataset.id}`, { method: "DELETE" });
          else await api(`/api/t0/${btn.dataset.id}/toggle`, { method: "POST" });
          loadT0();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (e) {
    toast(e.message);
  }
}

async function loadFormulas() {
  const rows = $("formula-rows");
  if (!rows) return;
  try {
    const list = await api("/api/formulas");
    rows.innerHTML =
      (list || [])
        .map(
          (f) => `<tr>
          <td>${escapeHtml(f.name)}</td><td>${escapeHtml(f.side)}</td><td class="num">${f.qty}</td>
          <td><span class="status ${f.enabled ? "running" : "paused"}">${f.enabled ? "监控" : "暂停"}</span></td>
          <td class="num">${f.trigger_count || 0}</td><td class="mono">${escapeHtml(f.last_trigger_at || "—")}</td>
          <td><button class="btn linkish" data-act="del" data-id="${f.id}">删</button></td></tr>`
        )
        .join("") || `<tr><td colspan="7" class="muted">暂无</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          await api(`/api/formulas/${btn.dataset.id}`, { method: "DELETE" });
          loadFormulas();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (e) {
    toast(e.message);
  }
}

async function loadBaskets() {
  const rows = $("basket-rows");
  if (!rows) return;
  try {
    const list = await api("/api/baskets");
    rows.innerHTML =
      (list || [])
        .map(
          (b) => `<tr>
          <td>${escapeHtml(b.name)}</td><td class="num">${money(b.capital)}</td>
          <td class="num">${(b.legs || []).length}</td><td>${escapeHtml(b.note || "")}</td>
          <td>
            <button class="btn linkish" data-act="run" data-id="${b.id}">执行</button>
            <button class="btn linkish" data-act="del" data-id="${b.id}">删</button>
          </td></tr>`
        )
        .join("") || `<tr><td colspan="5" class="muted">暂无</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          if (btn.dataset.act === "del") await api(`/api/baskets/${btn.dataset.id}`, { method: "DELETE" });
          else {
            const out = await api(`/api/baskets/${btn.dataset.id}/run`, { method: "POST" });
            toast(`篮子执行 ${(out.legs || []).length} 腿`);
          }
          loadBaskets();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (e) {
    toast(e.message);
  }
}

async function loadBoards() {
  const rows = $("board-rows");
  if (!rows) return;
  try {
    const list = await api("/api/board");
    rows.innerHTML =
      (list || [])
        .map(
          (b) => `<tr>
          <td class="mono">${escapeHtml(b.code)}</td><td>${escapeHtml(b.name || "")}</td>
          <td class="num up">+${b.seal_pct}%</td><td class="num">${b.qty}</td>
          <td><span class="status ${b.enabled ? "running" : "paused"}">${escapeHtml(b.status)}</span></td>
          <td class="mono">${escapeHtml(b.last_trigger_at || "—")}</td>
          <td>
            <button class="btn linkish" data-act="toggle" data-id="${b.id}">启停</button>
            <button class="btn linkish" data-act="del" data-id="${b.id}">删</button>
          </td></tr>`
        )
        .join("") || `<tr><td colspan="7" class="muted">暂无</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          if (btn.dataset.act === "del") await api(`/api/board/${btn.dataset.id}`, { method: "DELETE" });
          else await api(`/api/board/${btn.dataset.id}/toggle`, { method: "POST" });
          loadBoards();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (e) {
    toast(e.message);
  }
}

async function loadAccounts() {
  const rows = $("acct-rows");
  if (!rows) return;
  try {
    const list = await api("/api/accounts");
    rows.innerHTML =
      (list || [])
        .map(
          (a) => `<tr>
          <td>${escapeHtml(a.label)}</td><td>${escapeHtml(a.broker_channel)}</td>
          <td class="num">${money(a.capital)}</td><td class="num">${money(a.cash)}</td>
          <td class="num">${money(a.equity)}</td><td>${a.enabled ? "是" : "否"}</td>
          <td><button class="btn linkish" data-act="del" data-id="${a.id}">删</button></td></tr>`
        )
        .join("") || `<tr><td colspan="7" class="muted">暂无</td></tr>`;
    rows.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          await api(`/api/accounts/${btn.dataset.id}`, { method: "DELETE" });
          loadAccounts();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (e) {
    toast(e.message);
  }
}

async function loadCurve() {
  const rows = $("curve-rows");
  if (!rows) return;
  try {
    const data = await api("/api/curve");
    if ($("curve-summary")) {
      $("curve-summary").textContent = `当前权益 ${money(data.equity)} · 账户数 ${data.accounts || 0} · 点位 ${(data.points || []).length}`;
    }
    const pts = (data.points || []).slice().reverse();
    rows.innerHTML =
      pts.map((p) => `<tr><td class="mono">${escapeHtml(p.day)}</td><td class="num">${money(p.equity)}</td><td class="num">${money(p.cash)}</td></tr>`).join("") ||
      `<tr><td colspan="3" class="muted">尚无快照（交易日自动写入）</td></tr>`;
  } catch (e) {
    toast(e.message);
  }
}

async function saveRiskFromSettings() {
  if (!$("risk-max-orders")) return;
  const norm = (v, fb) => {
    if (!v) return fb;
    return v.length === 5 ? `${v}:00` : v;
  };
  await api("/api/risk", {
    method: "PUT",
    body: JSON.stringify({
      tdx_sign_file: $("risk-sign-file").value.trim(),
      max_orders_per_day: Number($("risk-max-orders").value) || 20,
      max_order_amount: Number($("risk-max-amount")?.value) || 500000,
      max_daily_loss: Number($("risk-max-loss")?.value) || 50000,
      code_cooldown_sec: Number($("risk-code-cd")?.value) || 60,
      sponsor_equity_limit: Number($("risk-sponsor")?.value) || 1000000,
      session_start: norm($("risk-session-start").value, "09:25:00"),
      session_end: norm($("risk-session-end").value, "15:00:00"),
      trade_days_only: $("risk-trade-days").checked,
      session_only: $("risk-session").checked,
      sound_on_trigger: $("risk-sound").checked,
      confirm_before_trade: $("risk-confirm") ? $("risk-confirm").checked : true,
      notify_log: true,
    }),
  });
}

async function ensureDisclaimer() {
  try {
    const d = await api("/api/disclaimer");
    if (d.accepted) return true;
    if ($("disclaimer-title")) $("disclaimer-title").textContent = d.title || "使用协议与风险揭示";
    if ($("disclaimer-text")) $("disclaimer-text").textContent = d.text || "";
    const dlg = $("disclaimer-dialog");
    const agree = $("disclaimer-agree");
    const btn = $("disclaimer-accept");
    if (!dlg || !agree || !btn) return true;
    agree.checked = false;
    btn.disabled = true;
    agree.onchange = () => {
      btn.disabled = !agree.checked;
    };
    return await new Promise((resolve) => {
      btn.onclick = async () => {
        if (!agree.checked) return;
        try {
          await api("/api/disclaimer/accept", { method: "POST", body: "{}" });
          dlg.close();
          resolve(true);
        } catch (e) {
          toast(e.message);
        }
      };
      dlg.showModal();
    });
  } catch (_) {
    return true;
  }
}

let sponsorShownToday = false;
async function pollRiskUi() {
  try {
    const r = await api("/api/risk");
    const rs = $("risk-status");
    if (rs) {
      if (r.paused_loss) {
        rs.textContent = "风控 · 日亏暂停";
        rs.className = "is-off";
      } else if (!r.in_session && r.session_only) {
        rs.textContent = `风控 · 今日${r.orders_today || 0}笔`;
        rs.className = "";
      } else {
        rs.textContent = `风控 · 今日${r.orders_today || 0}笔`;
        rs.className = "is-ok";
      }
    }
    if (r.sponsor_due && !sponsorShownToday) {
      sponsorShownToday = true;
      $("sponsor-dialog")?.showModal();
    }
  } catch (_) {}
  try {
    const list = await api("/api/confirms");
    const bar = $("confirm-bar");
    const box = $("confirm-list");
    const badge = $("confirm-status");
    const n = (list || []).length;
    if (badge) {
      badge.hidden = n === 0;
      badge.textContent = `待确认 · ${n}`;
      badge.className = n ? "is-off" : "";
    }
    if (!bar || !box) return;
    if (!n) {
      bar.hidden = true;
      box.innerHTML = "";
      return;
    }
    bar.hidden = false;
    box.innerHTML = list
      .map(
        (p) => `<div class="confirm-item">
        <span class="mono">${escapeHtml(p.created_at || "")}</span>
        <span>${escapeHtml(p.side)} ${escapeHtml(p.code)} x${p.qty} @${p.price}</span>
        <span class="muted">${escapeHtml(p.reason || "")}</span>
        <button class="btn primary" data-act="ok" data-id="${p.id}">确认下单</button>
        <button class="btn ghost" data-act="no" data-id="${p.id}">拒绝</button>
      </div>`
      )
      .join("");
    box.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          const act = btn.dataset.act === "ok" ? "accept" : "reject";
          await api(`/api/confirms/${btn.dataset.id}/${act}`, { method: "POST", body: "{}" });
          toast(act === "accept" ? "已确认下单" : "已拒绝");
          pollRiskUi();
        } catch (e) {
          toast(e.message);
        }
      });
    });
  } catch (_) {}
}

function renderPickerSkeleton() {
  $("picker-rows").innerHTML = Array.from({ length: 8 }, () => `<tr><td colspan="4" class="muted">加载中…</td></tr>`).join("");
}

function renderPickerList(items) {
  const box = $("picker-rows");
  if (!items.length) {
    box.innerHTML = `<tr><td colspan="4" class="muted">这一页没有组合</td></tr>`;
    return;
  }
  box.innerHTML = items
    .map((item) => {
      const ret = rankReturn(item);
      const active =
        state.selected && state.selected.source === item.source && state.selected.id === item.id;
      const action = [item.last_action, item.last_stock].filter(Boolean).join(" ") || "-";
      return `<tr class="${active ? "is-active" : ""}" data-id="${escapeHtml(item.id)}">
        <td>
          <div class="cell-name">
            <strong>${escapeHtml(item.name)}</strong>
            <small>${escapeHtml(item.id)}</small>
          </div>
        </td>
        <td>${escapeHtml(item.owner || "-")}</td>
        <td class="num ${tone(ret)}">${pct(ret)}</td>
        <td>${escapeHtml(action)}</td>
      </tr>`;
    })
    .join("");
  box.querySelectorAll("tr[data-id]").forEach((tr) => {
    tr.addEventListener("click", () => openPortfolio(state.source, tr.dataset.id));
  });
}

function renderPreview(detail) {
  state.selected = detail;
  $("preview-empty").classList.add("is-hidden");
  $("preview-body").classList.remove("is-hidden");
  $("preview-name").textContent = detail.name;
  $("preview-meta").textContent = `${sourceLabel(detail.source)} · ${detail.id}${detail.owner ? ` · ${detail.owner}` : ""}`;
  renderMetrics($("preview-metrics"), [
    ["日收益", pct(detail.daily_return), tone(detail.daily_return)],
    ["仓位 / 现金", `${(detail.position_weight ?? 0).toFixed(1)}% / ${(detail.cash_weight ?? 0).toFixed(1)}%`],
  ]);
  const holdings = (detail.holdings || []).slice(0, 8);
  $("preview-holdings").innerHTML = holdings.length
    ? holdings
        .map(
          (h) => `<tr>
            <td class="mono">${escapeHtml(h.code)}</td>
            <td>${escapeHtml(h.name)}</td>
            <td class="num">${(h.weight ?? 0).toFixed(2)}%</td>
          </tr>`
        )
        .join("")
    : `<tr><td colspan="3" class="muted">暂无持仓</td></tr>`;
  updateFollowPreviewText();
}

function updateFollowPreviewText() {
  const detail = state.selected;
  if (!detail) return;
  const count = (detail.holdings || []).filter((h) => h.weight > 0).length;
  const cash = detail.cash_weight ?? 0;
  const ratio = Number($("ratio-input").value) || 1;
  $("follow-preview-text").textContent = `按对方仓位建仓，约 ${count} 只，现金约 ${(cash * ratio).toFixed(0)}%（比例 ${ratio}）`;
}

async function loadList() {
  renderPickerSkeleton();
  $("picker-note").textContent = "";
  try {
    const data = await api(
      `/api/portfolios?source=${state.source}&rank=${state.rank}&page=1&size=30`
    );
    renderPickerList(data.items || []);
    if (state.source === "xueqiu" && !data.items?.length) {
      $("picker-note").textContent = "没有组合。可输入 ZH 代码，或到设置更新 Cookie。";
    }
  } catch (err) {
    $("picker-rows").innerHTML = `<tr><td colspan="4" class="muted">${escapeHtml(err.message)}</td></tr>`;
    if (state.source === "xueqiu") {
      $("picker-note").textContent = "打开右上角设置，粘贴雪球 Cookie。";
    }
  }
}

async function openPortfolio(source, id) {
  try {
    const detail = await api(`/api/portfolios/${source}/${encodeURIComponent(id)}`);
    renderPreview(detail);
    document.querySelectorAll("#picker-rows tr").forEach((el) => {
      el.classList.toggle("is-active", el.dataset.id === id);
    });
  } catch (err) {
    toast(err.message);
  }
}

async function refreshTargetDetail() {
  if (!state.follow) {
    state.targetDetail = null;
    return null;
  }
  const detail = await api(
    `/api/portfolios/${state.follow.source}/${encodeURIComponent(state.follow.portfolio_id)}`
  );
  state.targetDetail = detail;
  return detail;
}

async function loadFollow() {
  try {
    const data = await api("/api/follow");
    state.follow = data.account || null;
    if (state.follow) {
      try {
        await refreshTargetDetail();
      } catch (_) {
        state.targetDetail = null;
      }
    } else {
      state.targetDetail = null;
    }
  } catch (_) {
    state.follow = null;
    state.targetDetail = null;
  }
  if (state.page === "follow") renderFollowHome();
  if (state.page === "account") renderAccount();
  if (state.page === "logs") renderLogs();
  if (state.page === "compare" && state.follow) renderCompare();
}

async function loadSettingsStatus() {
  const data = await api("/api/settings");
  const xq = $("xq-status");
  if (data.xueqiu_cookie_set) {
    xq.textContent = "雪球 · Cookie 已配置";
    xq.className = "is-ok";
  } else {
    xq.textContent = "雪球 · 未配置 Cookie";
    xq.className = "is-off";
  }
  $("cookie-status").textContent = data.xueqiu_cookie_set
    ? `已保存 Cookie：${data.xueqiu_cookie_preview}`
    : "还没有雪球 Cookie。";

  fillSelect($("quote-source"), data.quote_sources || [], data.quote_source);
  fillSelect($("broker-channel"), data.broker_channels || [], data.broker_channel);
  $("orders-dir").value = data.orders_dir || "";
  if ($("easyths-host")) $("easyths-host").value = data.easyths_host || "127.0.0.1";
  if ($("easyths-port")) $("easyths-port").value = data.easyths_port ?? 7648;
  if ($("easyths-api-key")) {
    $("easyths-api-key").value = "";
    $("easyths-api-key").placeholder = data.easyths_api_key_set ? "已保存，留空不修改" : "无密钥可留空";
  }
  if ($("easyths-account")) $("easyths-account").value = data.easyths_account || "";

  const qLabel = (data.quote_sources || []).find((x) => x.id === data.quote_source)?.label || data.quote_source;
  const qs = $("quote-status");
  qs.textContent = `行情 · ${qLabel || "—"}`;
  qs.className = "is-ok";

  await refreshBrokerStatus();
  return data;
}

function fillSelect(el, items, current) {
  if (!el) return;
  el.innerHTML = (items || [])
    .map((it) => `<option value="${escapeHtml(it.id)}" ${it.id === current ? "selected" : ""}>${escapeHtml(it.label)}</option>`)
    .join("");
}

async function refreshBrokerStatus() {
  const el = $("broker-status");
  try {
    const st = await api("/api/broker/status");
    el.textContent = `交易 · ${st.label}${st.detail ? " · " + st.detail : ""}`;
    el.className = st.connected ? "is-ok" : "is-off";
    el.title = st.detail || "";
  } catch (err) {
    el.textContent = "交易 · 异常";
    el.className = "is-off";
  }
}

async function probeEastmoney() {
  const el = $("em-status");
  try {
    await api("/api/portfolios?source=eastmoney&rank=daily&page=1&size=1");
    el.textContent = "东财 · 可用";
    el.className = "is-ok";
  } catch (_) {
    el.textContent = "东财 · 异常";
    el.className = "is-off";
  }
}

async function syncFollow() {
  try {
    const data = await api("/api/follow/sync", { method: "POST" });
    state.follow = data.account;
    await refreshTargetDetail().catch(() => null);
    renderFollowHome();
    if (state.page === "compare") renderCompare();
    if (state.page === "account") renderAccount();
    if (state.page === "logs") renderLogs();
    const n = data.account.last_actions?.length || 0;
    toast(n ? `同步完成，成交 ${n} 笔` : "仓位已对齐，无需调仓");
  } catch (err) {
    toast(err.message);
  }
}

async function togglePause() {
  if (!state.follow) return;
  try {
    const path = state.follow.status === "paused" ? "/api/follow/resume" : "/api/follow/pause";
    const data = await api(path, { method: "POST" });
    state.follow = data.account;
    renderFollowHome();
    if (state.page === "logs") renderLogs();
    toast(state.follow.status === "paused" ? "已暂停" : "已继续");
  } catch (err) {
    toast(err.message);
  }
}

async function stopFollow() {
  await api("/api/follow", { method: "DELETE" });
  state.follow = null;
  state.targetDetail = null;
  renderFollowHome();
  if (state.page === "account") renderAccount();
  if (state.page === "logs") renderLogs();
  if (state.page === "compare") showPage("follow");
  toast("已结束本次跟单");
}

function openFollowDialog() {
  if (!state.selected) return toast("先选一个组合");
  $("follow-target-label").textContent = `${state.selected.name} / ${state.selected.id}`;
  updateFollowPreviewText();
  $("follow-dialog").showModal();
}

let gridCache = [];
let gridEngineTimer = null;

async function loadGridEngineStatus() {
  try {
    const st = await api("/api/grid/engine");
    const badge = $("grid-engine-badge");
    const eng = $("grid-m-engine");
    const logEl = $("grid-engine-log");
    if (st.running) {
      badge.textContent = st.dry_run ? "试跑中" : "运行中";
      const q = st.quote_source || "";
      eng.textContent = `内嵌${st.dry_run ? " · dry" : ""}${q ? " · " + q : ""}`;
    } else if (!st.exe_found) {
      badge.textContent = "未编译";
      eng.textContent = "无 exe";
    } else {
      badge.textContent = "已停止";
      eng.textContent = "未启动";
    }
    if (logEl) {
      logEl.textContent = (st.log_tail || []).join("\n");
      logEl.scrollTop = logEl.scrollHeight;
    }
  } catch (err) {
    const eng = $("grid-m-engine");
    if (eng) eng.textContent = err.message;
  }
}

async function loadGrid() {
  try {
    const data = await api("/api/grid");
    gridCache = data.items || [];
    const s = data.summary || {};
    $("grid-m-count").textContent = s.count ?? gridCache.length;
    $("grid-m-running").textContent = s.running ?? 0;
    $("grid-m-pnl").textContent = money(s.range_pnl ?? 0);
    $("grid-m-pnl").className = tone(s.range_pnl);
    $("grid-m-save").textContent = money(s.range_save ?? 0);
    renderGridRows(gridCache);
    await loadGridEngineStatus();
  } catch (err) {
    $("grid-rows").innerHTML = `<tr><td colspan="12" class="muted">${escapeHtml(err.message)}</td></tr>`;
  }
}

function renderGridRows(items) {
  if (!items.length) {
    $("grid-rows").innerHTML = `<tr><td colspan="12" class="muted">还没有区间策略，点「加入区间」</td></tr>`;
    return;
  }
  $("grid-rows").innerHTML = items
    .map((g) => {
      const st =
        g.status === "running"
          ? { cls: "running", text: "监控中" }
          : g.status === "paused"
            ? { cls: "paused", text: "已暂停" }
            : { cls: "failed", text: "已停止" };
      const actShort = (a) =>
        a === "clear_pause" ? "清停" : a === "clear_stop" ? "清终" : a === "pause" ? "暂停" : "";
      const range =
        g.price_floor || g.price_ceil
          ? `${g.price_floor || 0}~${g.price_ceil || "∞"}${
              g.ceil_action || g.floor_action
                ? ` (${actShort(g.floor_action || "clear_stop")}/${actShort(g.ceil_action || "pause")})`
                : ""
            }`
          : "不限";
      const flags = [
        g.preorder_sell ? "挂卖" : "",
        g.preorder_buy ? "挂买" : "",
        g.grid_match ? "匹配" : "",
        g.check_fill ? "查成" : "",
        g.by_price ? "按价" : "",
      ]
        .filter(Boolean)
        .join(" ") || "—";
      const buyLabel =
        g.buy_unit === "amount" ? `${money(g.buy_qty)}元` : `${g.buy_qty}股`;
      return `<tr data-id="${escapeHtml(g.id)}">
        <td><span class="status ${st.cls}">${st.text}</span></td>
        <td class="mono">${escapeHtml(g.code)}</td>
        <td>${escapeHtml(g.name || "")}</td>
        <td class="num">${g.last_price || "-"}</td>
        <td class="num up">+${Number(g.sell_up_pct).toFixed(1)}%</td>
        <td class="num down">-${Number(g.buy_down_pct).toFixed(1)}%</td>
        <td class="num">${buyLabel}</td>
        <td class="num">${range}</td>
        <td class="num ${tone(g.range_pnl)}">${money(g.range_pnl)}</td>
        <td class="num">${money(g.range_save)}</td>
        <td class="muted">${flags}</td>
        <td>
          <div class="row-actions">
            <button class="btn linkish" type="button" data-act="edit">设置</button>
            <button class="btn linkish" type="button" data-act="toggle">${g.status === "running" ? "暂停" : "启动"}</button>
            <button class="btn linkish" type="button" data-act="del">删除</button>
          </div>
        </td>
      </tr>`;
    })
    .join("");
  $("grid-rows").querySelectorAll("tr[data-id]").forEach((tr) => {
    tr.addEventListener("click", async (event) => {
      const btn = event.target.closest("[data-act]");
      if (!btn) return;
      event.stopPropagation();
      const id = tr.dataset.id;
      const item = gridCache.find((x) => x.id === id);
      if (!item) return;
      const act = btn.dataset.act;
      if (act === "edit") openGridDialog(item);
      if (act === "toggle") {
        try {
          await api(`/api/grid/${id}/toggle`, { method: "POST" });
          toast(item.status === "running" ? "已暂停该策略" : "已启动该策略");
          loadGrid();
        } catch (err) {
          toast(err.message);
        }
      }
      if (act === "del") {
        try {
          await api(`/api/grid/${id}`, { method: "DELETE" });
          toast("已删除");
          loadGrid();
        } catch (err) {
          toast(err.message);
        }
      }
    });
  });
}

function normTime(v, fallback) {
  if (!v) return fallback;
  return v.length === 5 ? `${v}:00` : v;
}

function fmtPx(n) {
  if (!n || Number(n) <= 0) return "不限";
  return Number(n).toFixed(2);
}

function syncGridModeUI() {
  const byPrice = !!$("g-by-price")?.checked;
  document.querySelectorAll('#gs-mode input[name="gs-mode"]').forEach((el) => {
    el.checked = byPrice ? el.value === "price" : el.value === "pct";
  });
  const unit = byPrice ? "元" : "%";
  if ($("gs-unit-buy")) $("gs-unit-buy").textContent = unit;
  if ($("gs-unit-sell")) $("gs-unit-sell").textContent = unit;
}

function refreshGridVisual() {
  if (!$("gs-story")) return;
  const byPrice = !!$("g-by-price")?.checked;
  const base = Number($("g-last-price").value) || 0;
  const buyDown = Number($("g-buy-down").value) || 0;
  const sellUp = Number($("g-sell-up").value) || 0;
  const buyQty = Number($("g-buy-qty").value) || 0;
  const sellQty = Number($("g-sell-qty").value) || 0;
  const buyUnit = $("g-buy-unit")?.value === "amount" ? "元" : "股";
  const sellUnit = $("g-sell-unit")?.value === "amount" ? "元" : "股";

  $("gs-v-ceil").textContent = fmtPx($("g-ceil").value);
  $("gs-v-floor").textContent = fmtPx($("g-floor").value);
  $("gs-v-base").textContent = base > 0 ? base.toFixed(2) : "待定（首次行情写入）";

  if (byPrice) {
    $("gs-v-sell").textContent = `+${sellUp}`;
    $("gs-v-buy").textContent = `-${buyDown}`;
    $("gs-v-sell-px").textContent = base > 0 ? `约 ${(base + sellUp).toFixed(2)}` : "相对基准价";
    $("gs-v-buy-px").textContent = base > 0 ? `约 ${(base - buyDown).toFixed(2)}` : "相对基准价";
  } else {
    $("gs-v-sell").textContent = `+${sellUp}%`;
    $("gs-v-buy").textContent = `-${buyDown}%`;
    $("gs-v-sell-px").textContent = base > 0 ? `约 ${(base * (1 + sellUp / 100)).toFixed(2)}` : "相对基准价";
    $("gs-v-buy-px").textContent = base > 0 ? `约 ${(base * (1 - buyDown / 100)).toFixed(2)}` : "相对基准价";
  }

  const sellText = sellQty > 0 ? `卖 ${sellQty}${sellUnit}` : "卖上次成交量";
  const buyText = `买 ${buyQty}${buyUnit}`;
  const trig = byPrice
    ? `跌 ${buyDown} 元 ${buyText} · 涨 ${sellUp} 元 ${sellText}`
    : `跌 ${buyDown}% ${buyText} · 涨 ${sellUp}% ${sellText}`;
  $("gs-story").textContent = trig;
  if ($("gs-foot-story")) $("gs-foot-story").textContent = trig;
}

function applyGridPreset(key) {
  const map = { safe: 1, std: 2, agg: 3 };
  const pct = map[key] ?? 2;
  $("g-by-price").checked = false;
  $("g-buy-down").value = pct;
  $("g-sell-up").value = pct;
  if (!$("g-buy-qty").value || Number($("g-buy-qty").value) <= 0) $("g-buy-qty").value = 100;
  document.querySelectorAll(".gs-chip").forEach((el) => el.classList.toggle("is-active", el.dataset.preset === key));
  syncGridModeUI();
  refreshGridVisual();
}

function bindGridSetupUI() {
  if (bindGridSetupUI._done) return;
  bindGridSetupUI._done = true;
  document.querySelectorAll(".gs-chip").forEach((btn) => {
    btn.addEventListener("click", () => applyGridPreset(btn.dataset.preset));
  });
  document.querySelectorAll('#gs-mode input[name="gs-mode"]').forEach((el) => {
    el.addEventListener("change", () => {
      $("g-by-price").checked = el.value === "price";
      syncGridModeUI();
      refreshGridVisual();
    });
  });
  [
    "g-code",
    "g-ceil",
    "g-floor",
    "g-last-price",
    "g-buy-down",
    "g-sell-up",
    "g-buy-qty",
    "g-sell-qty",
    "g-buy-unit",
    "g-sell-unit",
  ].forEach((id) => {
    $(id)?.addEventListener("input", refreshGridVisual);
    $(id)?.addEventListener("change", refreshGridVisual);
  });
}

async function openGridDialog(item, opts = {}) {
  bindGridSetupUI();
  fillPxTypeSelects($("grid-dialog") || document);
  await loadGridTemplates();
  if (!item?.id && !opts.skipDefaultTpl) {
    const def = (state.gridTemplates || []).find((t) => t.is_default);
    if (def?.payload) item = { ...(item || {}), ...def.payload, id: "" };
  }
  $("grid-dialog-title").textContent = item?.id ? "编辑区间交易" : "加入区间交易";
  $("grid-id").value = item?.id || "";
  if ($("g-name")) {
    $("g-name").value = item?.name || "";
    $("g-name").dataset.autofilled = item?.name ? "0" : "1";
  }
  fillStockCode("g-code", item?.code || "");
  $("g-ceil").value = item?.price_ceil ?? 0;
  $("g-floor").value = item?.price_floor ?? 0;
  if ($("g-ceil-action")) $("g-ceil-action").value = item?.ceil_action || "pause";
  if ($("g-floor-action")) $("g-floor-action").value = item?.floor_action || "clear_stop";
  $("g-buy-qty").value = item?.buy_qty ?? 100;
  $("g-buy-unit").value = item?.buy_unit || "shares";
  $("g-sell-qty").value = item?.sell_qty ?? 0;
  $("g-sell-unit").value = item?.sell_unit || "shares";
  $("g-last-price").value = item?.last_price ?? 0;
  $("g-last-qty").value = item?.last_qty ?? 0;
  $("g-sell-up").value = item?.sell_up_pct ?? 2;
  $("g-buy-down").value = item?.buy_down_pct ?? 2;
  $("g-session-start").value = (item?.session_start || "09:25:00").slice(0, 8);
  $("g-session-end").value = (item?.session_end || "15:00:00").slice(0, 8);
  $("g-max-pos").value = item?.max_position_amount ?? 0;
  $("g-reserve").value = item?.reserve_base_shares ?? 0;
  setPxType("g-buy-px", item?.buy_price_type, "ask1");
  setPxType("g-sell-px", item?.sell_price_type, "bid1");
  if ($("g-tpl-default")) $("g-tpl-default").checked = false;
  if ($("g-tpl-select") && !opts.skipDefaultTpl && !item?.id) {
    const def = (state.gridTemplates || []).find((t) => t.is_default);
    if (def) $("g-tpl-select").value = def.id;
  }
  $("g-offset").value = item?.price_offset_cents ?? 0;
  $("g-low-pivot").value = item?.low_pivot ?? 0;
  $("g-rebound").value = item?.rebound_pct ?? 0;
  $("g-high-pivot").value = item?.high_pivot ?? 0;
  $("g-pullback").value = item?.pullback_pct ?? 0;
  $("g-max-streak").value = item?.max_buy_streak ?? 0;
  $("g-buy-mult").value = item?.buy_qty_multiple ?? 0;
  $("g-delay").value = item?.delay_confirm_sec ?? 0;
  $("g-session").checked = item?.session_enabled !== false;
  $("g-preorder-sell").checked = !!item?.preorder_sell;
  $("g-preorder-buy").checked = !!item?.preorder_buy;
  $("g-match").checked = !!item?.grid_match;
  $("g-check-fill").checked = !!item?.check_fill;
  $("g-by-price").checked = !!item?.by_price;
  $("g-cost").checked = !!item?.use_cost_basis;
  $("g-cross-buy").checked = !!item?.cross_buy_multiply;
  $("g-cross-sell").checked = !!item?.cross_sell_multiply;
  $("g-sell-all").checked = !!item?.sell_all_after_multi_buy;
  $("g-margin").checked = !!item?.margin_buy;
  $("g-xrxd").checked = item?.auto_xrxd !== false;
  $("g-check-buy-down").checked = !!item?.check_buy_with_down;
  $("g-check-sell-up").checked = !!item?.check_sell_with_up;
  if ($("g-cap-shares")) $("g-cap-shares").value = item?.position_cap_shares ?? 0;
  if ($("g-stop-price")) $("g-stop-price").value = item?.stop_price ?? 0;
  if ($("g-max-sell-streak")) $("g-max-sell-streak").value = item?.max_sell_streak ?? 0;
  if ($("g-pause-buy")) $("g-pause-buy").checked = !!item?.pause_buy;
  if ($("g-pause-sell")) $("g-pause-sell").checked = !!item?.pause_sell;
  if ($("g-stop-flat")) $("g-stop-flat").checked = !!item?.stop_when_flat;
  if ($("g-gap")) $("g-gap").checked = !!item?.gap_open_lots;
  if ($("g-drive")) $("g-drive").value = item?.drive || (item?.check_fill ? "fill" : "touch");
  if ($("g-pair")) $("g-pair").value = item?.pair_mode || (item?.grid_match ? "strict" : "loose");
  if ($("g-unpaired")) $("g-unpaired").value = item?.unpaired_limit ?? 0;
  if ($("g-tp-mode")) $("g-tp-mode").value = item?.take_profit_mode || "none";
  if ($("g-tp-value")) $("g-tp-value").value = item?.take_profit_value ?? 0;
  if ($("g-rebuild")) $("g-rebuild").value = item?.rebuild_price ?? 0;
  const locked = !!item?.id;
  if ($("g-drive")) $("g-drive").disabled = locked;
  if ($("g-pair")) $("g-pair").disabled = locked;
  $("g-note").value = item?.note || "";
  document.querySelectorAll(".gs-chip").forEach((el) => {
    const v = Number(item?.buy_down_pct ?? 2);
    const key = v <= 1.2 ? "safe" : v >= 2.8 ? "agg" : "std";
    el.classList.toggle("is-active", el.dataset.preset === key);
  });
  syncGridModeUI();
  refreshGridVisual();
  $("grid-dialog").showModal();
}

function collectGridBody({ requireCode = true } = {}) {
  const code = requireCode ? requireStockCode("g-code") : readStockCode("g-code");
  if (requireCode && !code) return null;
  return {
    code: code || "",
    name: $("g-name").value.trim(),
    price_ceil: Number($("g-ceil").value) || 0,
    price_floor: Number($("g-floor").value) || 0,
    ceil_action: $("g-ceil-action")?.value || "pause",
    floor_action: $("g-floor-action")?.value || "clear_stop",
    buy_qty: Number($("g-buy-qty").value) || 0,
    buy_unit: $("g-buy-unit").value,
    sell_qty: Number($("g-sell-qty").value) || 0,
    sell_unit: $("g-sell-unit").value,
    last_price: Number($("g-last-price").value) || 0,
    last_qty: Number($("g-last-qty").value) || 0,
    sell_up_pct: Number($("g-sell-up").value) || 0,
    buy_down_pct: Number($("g-buy-down").value) || 0,
    session_enabled: $("g-session").checked,
    session_start: normTime($("g-session-start").value, "09:25:00"),
    session_end: normTime($("g-session-end").value, "15:00:00"),
    max_position_amount: Number($("g-max-pos").value) || 0,
    reserve_base_shares: Number($("g-reserve").value) || 0,
    buy_price_type: $("g-buy-px").value,
    sell_price_type: $("g-sell-px").value,
    price_offset_cents: Number($("g-offset").value) || 0,
    low_pivot: Number($("g-low-pivot").value) || 0,
    rebound_pct: Number($("g-rebound").value) || 0,
    high_pivot: Number($("g-high-pivot").value) || 0,
    pullback_pct: Number($("g-pullback").value) || 0,
    max_buy_streak: Number($("g-max-streak").value) || 0,
    buy_qty_multiple: Number($("g-buy-mult").value) || 0,
    delay_confirm_sec: Number($("g-delay").value) || 0,
    preorder_sell: $("g-preorder-sell").checked,
    preorder_buy: $("g-preorder-buy").checked,
    grid_match: ($("g-pair")?.value || "") === "strict" || $("g-match").checked,
    check_fill: ($("g-drive")?.value || "") === "fill" || $("g-check-fill").checked,
    drive: $("g-drive")?.value || "touch",
    pair_mode: $("g-pair")?.value || "loose",
    unpaired_limit: Number($("g-unpaired")?.value) || 0,
    pause_buy: !!$("g-pause-buy")?.checked,
    pause_sell: !!$("g-pause-sell")?.checked,
    gap_open_lots: !!$("g-gap")?.checked,
    max_sell_streak: Number($("g-max-sell-streak")?.value) || 0,
    position_cap_shares: Number($("g-cap-shares")?.value) || 0,
    stop_when_flat: !!$("g-stop-flat")?.checked,
    stop_price: Number($("g-stop-price")?.value) || 0,
    take_profit_mode: $("g-tp-mode")?.value || "none",
    take_profit_value: Number($("g-tp-value")?.value) || 0,
    rebuild_price: Number($("g-rebuild")?.value) || 0,
    by_price: $("g-by-price").checked,
    use_cost_basis: $("g-cost").checked,
    cross_buy_multiply: $("g-cross-buy").checked,
    cross_sell_multiply: $("g-cross-sell").checked,
    sell_all_after_multi_buy: $("g-sell-all").checked,
    margin_buy: $("g-margin").checked,
    auto_xrxd: $("g-xrxd").checked,
    check_buy_with_down: $("g-check-buy-down").checked,
    check_sell_with_up: $("g-check-sell-up").checked,
    note: $("g-note").value.trim(),
  };
}

function bind() {
  fillPxTypeSelects();
  bindStockCodeInputs();
  document.querySelectorAll(".nav-item").forEach((btn) => {
    btn.addEventListener("click", () => showPage(btn.dataset.page));
  });
  document.querySelectorAll(".shell-action").forEach((btn) => {
    btn.addEventListener("click", () => toast(btn.dataset.msg || "功能待接入"));
  });
  document.querySelectorAll(".page-tabs").forEach((tabs) => {
    tabs.querySelectorAll("button").forEach((btn) => {
      btn.addEventListener("click", () => {
        tabs.querySelectorAll("button").forEach((el) => el.classList.toggle("is-active", el === btn));
      });
    });
  });
  $("goto-picker-btn").addEventListener("click", () => showPage("picker"));
  $("follow-empty-add").addEventListener("click", () => showPage("picker"));
  $("account-empty-add").addEventListener("click", () => showPage("picker"));
  $("compare-back").addEventListener("click", () => showPage("follow"));
  $("compare-sync").addEventListener("click", syncFollow);

  document.querySelectorAll("#source-tabs button").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.source = btn.dataset.source;
      document.querySelectorAll("#source-tabs button").forEach((el) => {
        el.classList.toggle("is-active", el === btn);
      });
      loadList();
    });
  });
  document.querySelectorAll("#rank-tabs button").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.rank = btn.dataset.rank;
      document.querySelectorAll("#rank-tabs button").forEach((el) => {
        el.classList.toggle("is-active", el === btn);
      });
      loadList();
    });
  });

  $("lookup-form").addEventListener("submit", (event) => {
    event.preventDefault();
    const q = $("lookup-input").value.trim();
    if (!q) return;
    openPortfolio(state.source, q);
  });

  $("preview-follow-btn").addEventListener("click", openFollowDialog);
  $("capital-input").addEventListener("input", updateFollowPreviewText);
  $("ratio-input").addEventListener("input", updateFollowPreviewText);

  $("follow-form").addEventListener("submit", (event) => event.preventDefault());
  $("follow-confirm").addEventListener("click", async (event) => {
    event.preventDefault();
    if (!state.selected) return toast("先选一个组合");
    const btn = $("follow-confirm");
    btn.disabled = true;
    try {
      const data = await api("/api/follow", {
        method: "POST",
        body: JSON.stringify({
          source: state.selected.source,
          portfolio_id: state.selected.id,
          capital: Number($("capital-input").value),
          follow_ratio: Number($("ratio-input").value),
        }),
      });
      state.follow = data.account;
      await refreshTargetDetail().catch(() => null);
      $("follow-dialog").close();
      showPage("follow");
      toast("已按当前持仓建仓");
    } catch (err) {
      toast(err.message);
    } finally {
      btn.disabled = false;
    }
  });
  $("follow-cancel").addEventListener("click", () => $("follow-dialog").close());

  $("grid-add-btn").addEventListener("click", () => openGridDialog(null));
  $("g-tpl-apply")?.addEventListener("click", () => {
    const id = $("g-tpl-select")?.value;
    const tpl = (state.gridTemplates || []).find((t) => t.id === id);
    if (!tpl?.payload) return toast("请先选择模板");
    applyGridTemplatePayload(tpl.payload);
    toast(`已应用模板「${tpl.name}」`);
  });
  $("g-tpl-save")?.addEventListener("click", async () => {
    try {
      const payload = gridTemplatePayloadFromForm();
      if (!payload) return;
      const curId = $("g-tpl-select")?.value || "";
      const cur = (state.gridTemplates || []).find((t) => t.id === curId);
      const name = window.prompt("模板名称", cur?.name || "我的网格模板");
      if (!name || !name.trim()) return;
      const saved = await api("/api/templates", {
        method: "POST",
        body: JSON.stringify({
          id: curId || undefined,
          name: name.trim(),
          kind: "grid",
          is_default: !!$("g-tpl-default")?.checked,
          payload,
        }),
      });
      await loadGridTemplates();
      if ($("g-tpl-select") && saved?.id) $("g-tpl-select").value = saved.id;
      toast("模板已保存");
    } catch (e) {
      toast(e.message);
    }
  });
  $("g-tpl-del")?.addEventListener("click", async () => {
    const id = $("g-tpl-select")?.value;
    if (!id) return toast("请先选择模板");
    if (!window.confirm("确定删除该模板？")) return;
    try {
      await api(`/api/templates/${id}`, { method: "DELETE" });
      await loadGridTemplates();
      toast("模板已删除");
    } catch (e) {
      toast(e.message);
    }
  });
  $("flash-side")?.addEventListener("change", () => {
    const side = $("flash-side").value;
    setPxType("flash-px", side === "sell" ? "bid1" : "ask1");
  });
  $("grid-engine-start").addEventListener("click", async () => {
    try {
      await api("/api/grid/engine/start?dry_run=false&poll_ms=3000", { method: "POST" });
      toast("C++ 区间引擎已启动");
      loadGridEngineStatus();
    } catch (err) {
      toast(err.message);
    }
  });
  $("grid-engine-dry").addEventListener("click", async () => {
    try {
      await api("/api/grid/engine/start?dry_run=true&poll_ms=3000", { method: "POST" });
      toast("试跑已启动（不下单）");
      loadGridEngineStatus();
    } catch (err) {
      toast(err.message);
    }
  });
  $("grid-engine-stop").addEventListener("click", async () => {
    try {
      await api("/api/grid/engine/stop", { method: "POST" });
      toast("引擎已停止");
      loadGridEngineStatus();
    } catch (err) {
      toast(err.message);
    }
  });
  $("grid-cancel").addEventListener("click", () => $("grid-dialog").close());
  $("grid-form").addEventListener("submit", (event) => event.preventDefault());
  $("grid-save").addEventListener("click", async () => {
    const body = collectGridBody();
    if (!body) return;
    const id = $("grid-id").value;
    try {
      if (id) {
        await api(`/api/grid/${id}`, { method: "PUT", body: JSON.stringify(body) });
        toast("区间策略已更新");
      } else {
        await api("/api/grid", { method: "POST", body: JSON.stringify(body) });
        toast("区间策略已添加");
      }
      $("grid-dialog").close();
      loadGrid();
    } catch (err) {
      toast(err.message);
    }
  });

  $("settings-btn").addEventListener("click", async () => {
    await loadSettingsStatus();
    await loadRiskIntoSettings();
    $("settings-dialog").showModal();
  });

  $("wf-new-btn")?.addEventListener("click", () => {
    openWfEditor({
      name: "未命名程序",
      enabled: false,
      pre_auth: false,
      nodes: [],
      edges: [],
    });
  });
  $("wf-template-btn")?.addEventListener("click", async () => {
    try {
      const sign = window.prompt("通达信信号名", "买入信号") || "买入信号";
      const px = Number(window.prompt("买入触发价（可先填 0 后在策略单里改）", "0")) || 0;
      const p = await api("/api/workflows/template/file-signal", {
        method: "POST",
        body: JSON.stringify({ sign_name: sign, trigger_price: px }),
      });
      openWfEditor(p);
      toast("已生成示例程序，请检查参数后保存/启用");
    } catch (e) {
      toast(e.message);
    }
  });
  $("wf-back-btn")?.addEventListener("click", () => showWorkflowList());
  $("wf-save-btn")?.addEventListener("click", async () => {
    try {
      await saveWfDraft();
    } catch (e) {
      toast(e.message);
    }
  });
  $("wf-validate-btn")?.addEventListener("click", async () => {
    if (!wfDraft) return;
    try {
      wfDraft.name = $("wf-name").value.trim() || "未命名程序";
      const v = await api("/api/workflows/validate", { method: "POST", body: JSON.stringify(wfDraft) });
      toast(v.ok ? "校验通过" : (v.errors || []).join("；"));
    } catch (e) {
      toast(e.message);
    }
  });
  document.querySelectorAll(".wf-palette-item").forEach((btn) => {
    btn.addEventListener("click", () => {
      if (!wfDraft) return;
      const type = btn.dataset.type;
      const n = {
        id: wfNewId(),
        type,
        x: 80 + wfDraft.nodes.length * 30,
        y: 80 + wfDraft.nodes.length * 24,
        config: defaultNodeConfig(type),
      };
      wfDraft.nodes.push(n);
      wfSelected = n.id;
      renderWfCanvas();
      renderWfProps();
    });
  });

  $("alert-add-btn")?.addEventListener("click", () => openAlertDialog(null));
  $("a-cancel")?.addEventListener("click", () => $("alert-dialog").close());
  $("a-save")?.addEventListener("click", async () => {
    try {
      const src = $("a-source").value;
      let code = "";
      if (src !== "file") {
        code = requireStockCode("a-code");
        if (!code) return;
      } else {
        code = readStockCode("a-code");
      }
      await api("/api/alerts", {
        method: "POST",
        body: JSON.stringify({
          id: $("a-id").value || undefined,
          name: $("a-name").value.trim(),
          source: src,
          code,
          sign_name: $("a-sign").value.trim(),
          side: $("a-side").value,
          compare: $("a-compare").value,
          trigger_price: Number($("a-price").value) || 0,
          trigger_pct: Number($("a-pct").value) || 0,
          qty: Number($("a-qty").value) || 0,
          qty_unit: $("a-unit").value,
          cooldown_sec: Number($("a-cd").value) || 0,
          auto_trade: $("a-auto").checked,
          once: $("a-once").checked,
          enabled: true,
          status: "watching",
        }),
      });
      $("alert-dialog").close();
      toast("预警已保存");
      loadAlerts();
    } catch (e) {
      toast(e.message);
    }
  });

  $("sl-add-btn")?.addEventListener("click", () => openSlDialog(null));
  $("sl-cancel")?.addEventListener("click", () => $("sl-dialog").close());
  $("sl-save")?.addEventListener("click", async () => {
    try {
      const code = requireStockCode("sl-code");
      if (!code) return;
      await api("/api/stoploss", {
        method: "POST",
        body: JSON.stringify({
          id: $("sl-id").value || undefined,
          code,
          name: $("sl-name").value.trim(),
          cost_price: Number($("sl-cost").value) || 0,
          qty: Number($("sl-qty").value) || 0,
          stop_pct: Number($("sl-stop").value) || 0,
          take_pct: Number($("sl-take").value) || 0,
          stop_price: Number($("sl-stop-px")?.value) || 0,
          take_price: Number($("sl-take-px")?.value) || 0,
          trail_pct: Number($("sl-trail")?.value) || 0,
          trail_enabled: !!$("sl-trail-on")?.checked,
          enabled: true,
          status: "watching",
        }),
      });
      $("sl-dialog").close();
      toast("止损规则已保存");
      loadStops();
    } catch (e) {
      toast(e.message);
    }
  });

  $("cond-add-btn")?.addEventListener("click", () => openCondDialog(null));
  $("c-cancel")?.addEventListener("click", () => $("cond-dialog").close());
  $("c-save")?.addEventListener("click", async () => {
    try {
      const code = requireStockCode("c-code");
      if (!code) return;
      const normTime = (v) => {
        if (!v) return "";
        return v.length === 5 ? `${v}:00` : v;
      };
      await api("/api/conditions", {
        method: "POST",
        body: JSON.stringify({
          id: $("c-id").value || undefined,
          code,
          name: $("c-name").value.trim(),
          side: $("c-side").value,
          kind: $("c-kind")?.value || "price",
          compare: $("c-compare").value,
          trigger_price: Number($("c-trigger").value) || 0,
          trigger_pct: Number($("c-pct")?.value) || 0,
          trigger_time: normTime($("c-time")?.value || ""),
          price_type: $("c-px")?.value || "last",
          limit_price: Number($("c-limit").value) || 0,
          qty: Number($("c-qty").value) || 0,
          enabled: true,
          status: "armed",
        }),
      });
      $("cond-dialog").close();
      toast("条件单已保存");
      loadConds();
    } catch (e) {
      toast(e.message);
    }
  });

  $("watch-add-btn")?.addEventListener("click", () => openWatchDialog(null));
  $("w-cancel")?.addEventListener("click", () => $("watch-dialog").close());
  $("w-save")?.addEventListener("click", async () => {
    try {
      const code = requireStockCode("w-code");
      if (!code) return;
      await api("/api/watchlist", {
        method: "POST",
        body: JSON.stringify({
          id: $("w-id").value || undefined,
          code,
          name: $("w-name").value.trim(),
          tag: $("w-tag").value.trim(),
          alert_high: Number($("w-high").value) || 0,
          alert_low: Number($("w-low").value) || 0,
          note: $("w-note").value.trim(),
        }),
      });
      $("watch-dialog").close();
      toast("自选已保存");
      loadWatch();
    } catch (e) {
      toast(e.message);
    }
  });

  $("flash-submit")?.addEventListener("click", async () => {
    try {
      const code = requireStockCode("flash-code");
      if (!code) return;
      const side = $("flash-side").value;
      const out = await api("/api/flash", {
        method: "POST",
        body: JSON.stringify({
          code,
          name: $("flash-name").value.trim(),
          side,
          qty: Number($("flash-qty").value) || 0,
          price: Number($("flash-price").value) || 0,
          price_type: $("flash-px")?.value || (side === "sell" ? "bid1" : "ask1"),
        }),
      });
      $("flash-hint").textContent = `已提交 ${out.side} ${out.code} x${out.qty} @${out.price}`;
      toast("闪电下单已提交");
    } catch (e) {
      toast(e.message);
    }
  });

  $("logs-refresh")?.addEventListener("click", () => renderProductLogs());
  $("logs-tabs")?.querySelectorAll("button").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.logTab = btn.dataset.logtab || "events";
      $("logs-tabs").querySelectorAll("button").forEach((b) => b.classList.toggle("is-active", b === btn));
      renderProductLogs();
    });
  });

  $("t0-add-btn")?.addEventListener("click", () => openT0Dialog(null));
  const closeT0 = () => closeT0Dialog();
  $("t0-close")?.addEventListener("click", closeT0);
  $("t0-cancel")?.addEventListener("click", closeT0);
  $("t0-dialog")?.addEventListener("cancel", (e) => {
    e.preventDefault();
    closeT0();
  });
  $("t0-save")?.addEventListener("click", async () => {
    try {
      const code = requireStockCode("t0-code");
      if (!code) return;
      const anchor = Number($("t0-anchor").value) || 0;
      await api("/api/t0", {
        method: "POST",
        body: JSON.stringify({
          id: $("t0-id").value || undefined,
          code,
          name: $("t0-name").value.trim(),
          base_price: anchor,
          day_anchor: anchor,
          order_type: $("t0-px")?.value || "market",
          buy_down_pct: Number($("t0-buy").value) || 1,
          sell_up_pct: Number($("t0-sell").value) || 1,
          buy_qty: Number($("t0-buy-qty").value) || 100,
          sell_qty: Number($("t0-sell-qty").value) || 0,
          qty: Number($("t0-buy-qty").value) || 100,
          max_rounds: Number($("t0-max-rounds").value) || 0,
          cooldown_sec: Number($("t0-cooldown").value) || 0,
          session_only: $("t0-session").checked,
          note: $("t0-note").value.trim(),
          enabled: true,
        }),
      });
      closeT0Dialog();
      loadT0();
      toast("T+0 已保存");
    } catch (e) {
      toast(e.message);
    }
  });

  $("formula-add-btn")?.addEventListener("click", () => {
    $("f-id").value = "";
    $("f-name").value = "";
    $("f-side").value = "buy";
    $("f-qty").value = 100;
    $("formula-dialog").showModal();
  });
  $("f-cancel")?.addEventListener("click", () => $("formula-dialog").close());
  $("f-save")?.addEventListener("click", async () => {
    try {
      await api("/api/formulas", {
        method: "POST",
        body: JSON.stringify({
          id: $("f-id").value || undefined,
          name: $("f-name").value.trim(),
          side: $("f-side").value,
          qty: Number($("f-qty").value) || 100,
          enabled: true,
        }),
      });
      $("formula-dialog").close();
      loadFormulas();
      toast("公式已保存");
    } catch (e) {
      toast(e.message);
    }
  });

  $("basket-add-btn")?.addEventListener("click", () => {
    $("bsk-id").value = "";
    $("bsk-name").value = "篮子1";
    $("bsk-capital").value = 100000;
    $("bsk-legs").value = '[{"code":"510300.SH","side":"buy","qty":1000,"weight":1}]';
    $("basket-dialog").showModal();
  });
  $("bsk-cancel")?.addEventListener("click", () => $("basket-dialog").close());
  $("bsk-save")?.addEventListener("click", async () => {
    try {
      const legs = JSON.parse($("bsk-legs").value || "[]");
      await api("/api/baskets", {
        method: "POST",
        body: JSON.stringify({
          id: $("bsk-id").value || undefined,
          name: $("bsk-name").value.trim(),
          capital: Number($("bsk-capital").value) || 0,
          legs,
        }),
      });
      $("basket-dialog").close();
      loadBaskets();
      toast("篮子已保存");
    } catch (e) {
      toast(e.message);
    }
  });

  $("board-add-btn")?.addEventListener("click", () => {
    $("bd-id").value = "";
    if ($("bd-name")) {
      $("bd-name").value = "";
      $("bd-name").dataset.autofilled = "1";
    }
    fillStockCode("bd-code", "");
    $("bd-pct").value = 9.5;
    $("bd-qty").value = 100;
    $("board-dialog").showModal();
  });
  $("bd-cancel")?.addEventListener("click", () => $("board-dialog").close());
  $("bd-save")?.addEventListener("click", async () => {
    try {
      const code = requireStockCode("bd-code");
      if (!code) return;
      await api("/api/board", {
        method: "POST",
        body: JSON.stringify({
          id: $("bd-id").value || undefined,
          code,
          name: $("bd-name").value.trim(),
          seal_pct: Number($("bd-pct").value) || 9.5,
          qty: Number($("bd-qty").value) || 100,
          enabled: true,
        }),
      });
      $("board-dialog").close();
      loadBoards();
      toast("打板规则已保存");
    } catch (e) {
      toast(e.message);
    }
  });

  $("acct-add-btn")?.addEventListener("click", () => {
    $("acct-id").value = "";
    $("acct-label").value = "";
    $("acct-broker").value = "paper";
    $("acct-capital").value = 1000000;
    $("acct-cash").value = 1000000;
    $("acct-dialog").showModal();
  });
  $("acct-cancel")?.addEventListener("click", () => $("acct-dialog").close());
  $("acct-save")?.addEventListener("click", async () => {
    try {
      const capital = Number($("acct-capital").value) || 0;
      const cash = Number($("acct-cash").value) || capital;
      await api("/api/accounts", {
        method: "POST",
        body: JSON.stringify({
          id: $("acct-id").value || undefined,
          label: $("acct-label").value.trim(),
          broker_channel: $("acct-broker").value.trim() || "paper",
          capital,
          cash,
          equity: cash,
          enabled: true,
        }),
      });
      $("acct-dialog").close();
      loadAccounts();
      toast("账户已保存");
    } catch (e) {
      toast(e.message);
    }
  });

  $("curve-refresh")?.addEventListener("click", () => loadCurve());
  $("settings-form").addEventListener("submit", (event) => event.preventDefault());
  $("settings-cancel").addEventListener("click", () => $("settings-dialog").close());
  $("broker-connect-btn").addEventListener("click", async () => {
    try {
      const st = await api("/api/broker/connect", { method: "POST" });
      toast(st.connected ? `已连接：${st.label}` : st.detail || "未连接");
      await refreshBrokerStatus();
    } catch (err) {
      toast(err.message);
      await refreshBrokerStatus();
    }
  });
  $("settings-save").addEventListener("click", async (event) => {
    event.preventDefault();
    const body = {
      quote_source: $("quote-source").value,
      broker_channel: $("broker-channel").value,
      orders_dir: $("orders-dir").value.trim(),
      easyths_host: $("easyths-host")?.value.trim() || "127.0.0.1",
      easyths_port: Number($("easyths-port")?.value) || 7648,
      easyths_account: $("easyths-account")?.value.trim() || "",
    };
    const cookie = $("cookie-input").value.trim();
    if (cookie) body.xueqiu_cookie = cookie;
    const ek = $("easyths-api-key")?.value;
    if (ek) body.easyths_api_key = ek;
    await api("/api/settings", {
      method: "PUT",
      body: JSON.stringify(body),
    });
    await saveRiskFromSettings();
    await loadSettingsStatus();
    $("settings-dialog").close();
    toast("设置已保存");
    if (state.source === "xueqiu" && state.page === "picker") loadList();
  });
}

bind();
$("sponsor-ok")?.addEventListener("click", () => $("sponsor-dialog")?.close());
(async () => {
  await ensureDisclaimer();
  updateTopMeta();
  showPage("follow");
  loadFollow();
  loadSettingsStatus();
  probeEastmoney();
  pollRiskUi();
})();
setInterval(updateTopMeta, 60000);
setInterval(pollRiskUi, 5000);
setInterval(() => {
  if (state.follow && state.follow.status !== "paused") loadFollow();
}, 20000);
