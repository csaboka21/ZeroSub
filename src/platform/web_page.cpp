/*
 * SPDX-License-Identifier: MIT
 */

#include "web_page.h"

namespace platform {

/*
 * Split into several raw string pieces: MSVC limits a single literal to 16 KB.
 * Names from the device are only ever put into the page with textContent.
 */
const char kWebPageHtml[] =
R"ZS(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ZeroSub Files</title>
<script>
/* Applied before anything is drawn, so a chosen theme never flashes the other one. */
try { const t = localStorage.getItem("zs_theme"); if (t === "dark" || t === "light") document.documentElement.dataset.theme = t; } catch (e) {}
</script>
<style>
:root{color-scheme:light;--bg:#f6f7f9;--panel:#fff;--text:#1d2125;--muted:#6b7280;--line:#e3e6ea;--accent:#e8590c;--accent-text:#fff;--hover:#f0f2f5;--danger:#c92a2a;--ok:#2b8a3e;--shadow:0 1px 3px rgba(0,0,0,.08)}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){color-scheme:dark;--bg:#121417;--panel:#1b1e22;--text:#e6e8eb;--muted:#9097a1;--line:#2b3036;--accent:#ff7a2f;--accent-text:#111;--hover:#23272c;--danger:#ff6b6b;--ok:#51cf66;--shadow:none}}
:root[data-theme=dark]{color-scheme:dark;--bg:#121417;--panel:#1b1e22;--text:#e6e8eb;--muted:#9097a1;--line:#2b3036;--accent:#ff7a2f;--accent-text:#111;--hover:#23272c;--danger:#ff6b6b;--ok:#51cf66;--shadow:none}
body,header,.panel,button,input,dialog{transition:background-color .2s,color .2s,border-color .2s}
#themeBtn{display:flex;align-items:center;gap:6px}
#themeBtn svg{width:16px;height:16px;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:12px 16px;background:var(--panel);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:2}
header h1{font-size:16px;margin:0;font-weight:650;letter-spacing:.2px}
header h1 span{color:var(--accent)}
header .spacer{flex:1}
main{max-width:1000px;margin:0 auto;padding:16px}
button,.btn{font:inherit;border:1px solid var(--line);background:var(--panel);color:var(--text);padding:6px 12px;border-radius:6px;cursor:pointer;white-space:nowrap}
button:hover,.btn:hover{background:var(--hover)}
button.primary{background:var(--accent);border-color:var(--accent);color:var(--accent-text);font-weight:600}
button.link{border:0;background:none;padding:4px 6px;color:var(--muted)}
button.link:hover{color:var(--text);background:var(--hover)}
button.danger:hover{color:var(--danger)}
button.link.install{color:var(--accent);font-weight:600}
.install-status{padding:8px 14px;border-top:1px solid var(--line);font-weight:600}
.install-status.ok{color:var(--ok)}
.install-status.fail{color:var(--danger)}
button:disabled{opacity:.5;cursor:default}
input[type=text],input[type=password]{font:inherit;padding:7px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--text)}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:12px}
.toolbar label{display:flex;align-items:center;gap:6px;color:var(--muted);margin-left:auto}
.crumbs{display:flex;flex-wrap:wrap;align-items:center;gap:2px;margin-bottom:12px;font-size:15px}
.crumbs button{border:0;background:none;padding:2px 6px;color:var(--accent)}
.crumbs button:last-child{color:var(--text);font-weight:600;cursor:default}
.crumbs .sep{color:var(--muted)}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;box-shadow:var(--shadow);overflow:hidden}
table{width:100%;border-collapse:collapse}
th{text-align:left;font-weight:600;color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.4px;padding:8px 12px;border-bottom:1px solid var(--line);cursor:pointer;user-select:none}
td{padding:7px 12px;border-bottom:1px solid var(--line);vertical-align:middle}
tr:last-child td{border-bottom:0}
tbody tr:hover{background:var(--hover)}
td.name{word-break:break-all}
td.name button{border:0;background:none;padding:0;color:inherit;text-align:left;font:inherit;cursor:pointer}
td.name button:hover{color:var(--accent);text-decoration:underline}
td.num{color:var(--muted);white-space:nowrap;font-variant-numeric:tabular-nums}
td.actions{text-align:right;white-space:nowrap}
.icon{display:inline-block;width:1.4em;color:var(--muted)}
.icon.dir{color:var(--accent)}
.empty{padding:32px;text-align:center;color:var(--muted)}
.drop{position:fixed;inset:0;background:rgba(232,89,12,.12);border:3px dashed var(--accent);display:none;align-items:center;justify-content:center;font-size:20px;font-weight:600;z-index:10;pointer-events:none}
.drop.on{display:flex}
.progress{display:none;margin-bottom:12px;padding:10px 12px}
.progress.on{display:block}
.bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin-top:6px}
.bar div{height:100%;width:0;background:var(--accent);transition:width .15s}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--text);color:var(--bg);padding:9px 16px;border-radius:6px;opacity:0;transition:opacity .2s;pointer-events:none;max-width:90vw;z-index:20}
#toast.on{opacity:1}
#toast.err{background:var(--danger);color:#fff}
.login{max-width:340px;margin:12vh auto;padding:28px}
.login h2{margin:0 0 6px;font-size:18px}
.login p{margin:0 0 18px;color:var(--muted)}
.login form{display:flex;gap:8px}
.login input{flex:1;font-size:20px;letter-spacing:6px;text-align:center}
.login .msg{color:var(--danger);min-height:1.4em;margin-top:10px}
dialog{border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--text);padding:0;max-width:min(920px,94vw);width:94vw}
dialog::backdrop{background:rgba(0,0,0,.45)}
.dlg-head{display:flex;align-items:center;gap:8px;padding:10px 14px;border-bottom:1px solid var(--line)}
.dlg-head b{flex:1;word-break:break-all}
.dlg-body{padding:14px;max-height:75vh;overflow:auto}
.dlg-body pre{margin:0;font:12.5px/1.5 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-all}
.dlg-body img{max-width:100%;display:block;margin:0 auto}
.hide{display:none!important}
@media (max-width:640px){th.mtime,td.mtime{display:none}header h1 small{display:none}td.actions button.opt{display:none}}
</style>
</head>
)ZS"
R"ZS(<body>
<header>
  <h1><span>Zero</span>Sub Files <small id="rootLabel" style="color:var(--muted);font-weight:400"></small></h1>
  <div class="spacer"></div>
  <button id="themeBtn" class="link" type="button"></button>
  <button id="logoutBtn" class="link hide">Log out</button>
</header>

<main>
  <section id="loginView" class="panel login hide">
    <h2>Enter PIN</h2>
    <p>The PIN is shown on the device, on the Web Files page.</p>
    <form id="loginForm">
      <input id="pinInput" type="password" inputmode="numeric" autocomplete="off" maxlength="6" autofocus>
      <button class="primary" type="submit">Log in</button>
    </form>
    <div class="msg" id="loginMsg"></div>
  </section>

  <section id="filesView" class="hide">
    <nav class="crumbs" id="crumbs"></nav>
    <div class="toolbar">
      <button class="primary" id="uploadBtn">Upload files</button>
      <button id="uploadDirBtn">Upload folder</button>
      <button id="mkdirBtn">New folder</button>
      <button id="refreshBtn">Refresh</button>
      <label><input type="checkbox" id="hiddenToggle"> Show hidden</label>
      <input type="file" id="fileInput" multiple class="hide">
      <input type="file" id="dirInput" webkitdirectory multiple class="hide">
    </div>
    <div class="panel progress" id="progress">
      <div id="progressText">Uploading</div>
      <div class="bar"><div id="progressBar"></div></div>
    </div>
    <div class="panel">
      <table>
        <thead><tr>
          <th data-sort="name">Name</th>
          <th data-sort="size" style="width:90px">Size</th>
          <th data-sort="mtime" class="mtime" style="width:160px">Modified</th>
          <th style="width:1%"></th>
        </tr></thead>
        <tbody id="rows"></tbody>
      </table>
      <div class="empty hide" id="empty">This folder is empty. Drop files here to upload.</div>
    </div>
  </section>
</main>

<div class="drop" id="drop">Drop to upload here</div>
<div id="toast"></div>

<dialog id="viewer">
  <div class="dlg-head"><b id="viewerTitle"></b><button id="viewerDownload">Download</button><button id="viewerClose">Close</button></div>
  <div class="dlg-body" id="viewerBody"></div>
  <div class="install-status hide" id="installStatus"></div>
</dialog>
)ZS"
R"ZS(<script>
"use strict";
const $ = id => document.getElementById(id);
const TEXT_EXT = ["sub","ir","nfc","rfid","txt","log","conf","cfg","ini","md","json","csv","xml","yml","yaml","sh","py","c","h","cpp","js","html","htm","svg","css","u2f","ibtn","lua","service","rules","list"];
const IMG_EXT = ["png","jpg","jpeg","gif","bmp","webp"];
const MAX_VIEW = 512 * 1024;
let cwd = "";
let entries = [];
let sortKey = "name", sortDir = 1;
let showHidden = false;
try { showHidden = localStorage.getItem("zs_hidden") === "1"; } catch (e) {}
$("hiddenToggle").checked = showHidden;

function toast(text, isError) {
  const t = $("toast");
  t.textContent = text;
  t.className = "on" + (isError ? " err" : "");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => { t.className = ""; }, isError ? 4500 : 2500);
}

async function api(method, url, params) {
  const opts = { method, headers: { "X-ZeroSub": "1" }, credentials: "same-origin" };
  if (params) {
    opts.body = new URLSearchParams(params);
  }
  const res = await fetch(url, opts);
  let data = {};
  try { data = await res.json(); } catch (e) {}
  if (res.status === 401) { showLogin(); throw new Error(data.error || "Log in first"); }
  if (!res.ok) { throw new Error(data.error || ("HTTP " + res.status)); }
  return data;
}

function join(dir, name) { return dir ? dir + "/" + name : name; }
function ext(name) { const i = name.lastIndexOf("."); return i > 0 ? name.slice(i + 1).toLowerCase() : (name.startsWith(".") ? "" : ""); }
function fileUrl(path, inline) { return "/api/file?" + new URLSearchParams({ path, inline: inline ? "1" : "0" }); }

function fmtSize(n) {
  if (n < 1024) return n + " B";
  const u = ["KB", "MB", "GB", "TB"];
  let i = -1;
  do { n /= 1024; i++; } while (n >= 1024 && i < u.length - 1);
  return n.toFixed(n < 10 ? 1 : 0) + " " + u[i];
}
function fmtTime(s) {
  if (!s) return "";
  const d = new Date(s * 1000);
  const pad = v => String(v).padStart(2, "0");
  return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) + " " + pad(d.getHours()) + ":" + pad(d.getMinutes());
}

function showLogin() {
  $("filesView").classList.add("hide");
  $("logoutBtn").classList.add("hide");
  $("loginView").classList.remove("hide");
  $("pinInput").value = "";
  $("pinInput").focus();
}
function showFiles() {
  $("loginView").classList.add("hide");
  $("filesView").classList.remove("hide");
  $("logoutBtn").classList.remove("hide");
}

$("loginForm").addEventListener("submit", async ev => {
  ev.preventDefault();
  $("loginMsg").textContent = "";
  try {
    await api("POST", "/api/login", { pin: $("pinInput").value.trim() });
    showFiles();
    await loadInfo();
    await load(cwd);
  } catch (e) {
    $("loginMsg").textContent = e.message;
    $("pinInput").select();
  }
});

/* Theme: Auto follows the phone or PC; Dark and Light override it. Remembered per browser. */
const THEMES = ["auto", "dark", "light"];
const THEME_ICONS = {
  auto: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 0 0 18z" fill="currentColor"/></svg>',
  dark: '<svg viewBox="0 0 24 24"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>',
  light: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
};
let theme = "auto";
try { theme = localStorage.getItem("zs_theme") || "auto"; } catch (e) {}
if (!THEMES.includes(theme)) theme = "auto";

function applyTheme() {
  if (theme === "auto") delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = theme;
  const label = theme[0].toUpperCase() + theme.slice(1);
  /* The icons are fixed strings above; nothing from the device goes into innerHTML. */
  $("themeBtn").innerHTML = THEME_ICONS[theme];
  $("themeBtn").append(label);
  $("themeBtn").title = "Theme: " + label + " (click to change)";
}
$("themeBtn").addEventListener("click", () => {
  theme = THEMES[(THEMES.indexOf(theme) + 1) % THEMES.length];
  try { localStorage.setItem("zs_theme", theme); } catch (e) {}
  applyTheme();
});
applyTheme();

$("logoutBtn").addEventListener("click", async () => {
  try { await api("POST", "/api/logout"); } catch (e) {}
  showLogin();
});

async function loadInfo() {
  const info = await api("GET", "/api/info");
  $("rootLabel").textContent = info.root;
}
)ZS"
R"ZS(
async function load(path) {
  const data = await api("GET", "/api/list?" + new URLSearchParams({ path }));
  cwd = data.path;
  entries = data.entries;
  try { history.replaceState(null, "", "#" + encodeURI(cwd)); } catch (e) {}
  render();
}

function crumbs() {
  const nav = $("crumbs");
  nav.replaceChildren();
  const parts = cwd ? cwd.split("/") : [];
  const add = (label, path) => {
    const b = document.createElement("button");
    b.textContent = label;
    b.addEventListener("click", () => { if (path !== cwd) go(path); });
    nav.appendChild(b);
  };
  add("Home", "");
  parts.forEach((p, i) => {
    const s = document.createElement("span");
    s.className = "sep";
    s.textContent = "/";
    nav.appendChild(s);
    add(p, parts.slice(0, i + 1).join("/"));
  });
}

function sorted() {
  const list = entries.filter(e => showHidden || !e.name.startsWith("."));
  list.sort((a, b) => {
    if (a.dir !== b.dir) return a.dir ? -1 : 1;
    let r;
    if (sortKey === "name") r = a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" });
    else r = (a[sortKey] - b[sortKey]) || a.name.localeCompare(b.name);
    return r * sortDir;
  });
  return list;
}

function actionButton(label, onClick, extra) {
  const b = document.createElement("button");
  b.className = "link" + (extra ? " " + extra : "");
  b.textContent = label;
  b.addEventListener("click", ev => { ev.stopPropagation(); onClick(); });
  return b;
}

function render() {
  crumbs();
  const body = $("rows");
  body.replaceChildren();
  const list = sorted();
  if (cwd) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.className = "name";
    td.colSpan = 4;
    const b = document.createElement("button");
    b.textContent = "← ..";
    b.addEventListener("click", () => go(cwd.split("/").slice(0, -1).join("/")));
    td.appendChild(b);
    tr.appendChild(td);
    body.appendChild(tr);
  }
  for (const e of list) {
    const path = join(cwd, e.name);
    const tr = document.createElement("tr");

    const name = document.createElement("td");
    name.className = "name";
    const icon = document.createElement("span");
    icon.className = "icon" + (e.dir ? " dir" : "");
    icon.textContent = e.dir ? "\u{1F4C1}" : (e.link ? "\u{1F517}" : "\u{1F4C4}");
    const nb = document.createElement("button");
    nb.textContent = e.name;
    nb.addEventListener("click", () => e.dir ? go(path) : view(path, e));
    name.append(icon, nb);

    const size = document.createElement("td");
    size.className = "num";
    size.textContent = e.dir ? "" : fmtSize(e.size);

    const mtime = document.createElement("td");
    mtime.className = "num mtime";
    mtime.textContent = fmtTime(e.mtime);

    const actions = document.createElement("td");
    actions.className = "actions";
    if (!e.dir && ext(e.name) === "deb") actions.appendChild(actionButton("Install", () => install(path, e.name), "install"));
    if (!e.dir) actions.appendChild(actionButton("Download", () => download(path)));
    actions.appendChild(actionButton("Rename", () => rename(path, e.name), "opt"));
    actions.appendChild(actionButton("Delete", () => remove(path, e), "danger"));

    tr.append(name, size, mtime, actions);
    body.appendChild(tr);
  }
  $("empty").classList.toggle("hide", list.length !== 0);
  document.querySelectorAll("th[data-sort]").forEach(th => {
    const k = th.dataset.sort;
    th.textContent = th.textContent.replace(/ [▲▼]$/, "") + (k === sortKey ? (sortDir > 0 ? " ▲" : " ▼") : "");
  });
}

async function go(path) {
  try { await load(path); } catch (e) { toast(e.message, true); }
}

function download(path) {
  const a = document.createElement("a");
  a.href = fileUrl(path, false);
  a.download = "";
  document.body.appendChild(a);
  a.click();
  a.remove();
}
)ZS"
R"ZS(
async function view(path, e) {
  const x = ext(e.name);
  const isText = TEXT_EXT.includes(x) || (x === "" && e.size <= MAX_VIEW);
  const isImg = IMG_EXT.includes(x);
  if (!isText && !isImg) { download(path); return; }
  if (isText && e.size > MAX_VIEW) { toast("Too big to view: downloading"); download(path); return; }
  const body = $("viewerBody");
  body.replaceChildren();
  $("viewerTitle").textContent = e.name;
  $("viewerDownload").classList.remove("hide");
  $("installStatus").classList.add("hide");
  $("viewerDownload").onclick = () => download(path);
  if (isImg) {
    const img = document.createElement("img");
    img.src = fileUrl(path, true);
    img.alt = e.name;
    body.appendChild(img);
  } else {
    const res = await fetch(fileUrl(path, true), { credentials: "same-origin" });
    if (!res.ok) { toast("Cannot open " + e.name, true); return; }
    const pre = document.createElement("pre");
    pre.textContent = await res.text();
    body.appendChild(pre);
  }
  $("viewer").showModal();
}
$("viewerClose").addEventListener("click", () => $("viewer").close());

/* Installing a .deb: apt-get runs on the device; its log is shown until it ends. */
async function install(path, name) {
  if (!confirm("Install " + name + " on the device?\n\nThis runs apt-get as root. Only install packages you trust.")) return;
  try {
    await api("POST", "/api/install", { path });
  } catch (e) { toast(e.message, true); return; }
  showInstall(name);
}

function showInstall(name) {
  $("viewerTitle").textContent = "Installing " + name;
  $("viewerDownload").classList.add("hide");
  const body = $("viewerBody");
  body.replaceChildren();
  const pre = document.createElement("pre");
  pre.id = "installLog";
  pre.textContent = "Starting apt-get...";
  body.appendChild(pre);
  const status = $("installStatus");
  status.className = "install-status";
  status.textContent = "Running... you can close this, the install keeps going.";
  if (!$("viewer").open) $("viewer").showModal();
  pollInstall();
}

async function pollInstall() {
  clearTimeout(pollInstall.timer);
  let job;
  try { job = await api("GET", "/api/install"); } catch (e) { return; }
  const pre = $("installLog");
  if (pre) {
    const atBottom = $("viewerBody").scrollTop + $("viewerBody").clientHeight >= $("viewerBody").scrollHeight - 8;
    /* apt redraws progress with \r: keep what a terminal would show last on each line. */
    pre.textContent = job.log.split("\n").map(line => line.split("\r").filter(Boolean).pop() || "").join("\n");
    if (atBottom) $("viewerBody").scrollTop = $("viewerBody").scrollHeight;
  }
  if (job.running) {
    pollInstall.timer = setTimeout(pollInstall, 1000);
    return;
  }
  const status = $("installStatus");
  if (pre && job.finished) {
    status.className = "install-status " + (job.exit === 0 ? "ok" : "fail");
    status.textContent = job.exit === 0
      ? "Installed " + job.package + ". If you updated ZeroSub itself, restart it to run the new version."
      : "Install failed (exit code " + job.exit + "). See the log above.";
  }
}
$("viewer").addEventListener("click", ev => { if (ev.target === $("viewer")) $("viewer").close(); });

async function rename(path, oldName) {
  const name = prompt("New name", oldName);
  if (!name || name === oldName) return;
  try {
    await api("POST", "/api/rename", { path, name });
    toast("Renamed");
    await load(cwd);
  } catch (e) { toast(e.message, true); }
}

async function remove(path, e) {
  const what = e.dir ? "the folder \"" + e.name + "\" and everything in it" : "\"" + e.name + "\"";
  if (!confirm("Delete " + what + "? This cannot be undone.")) return;
  try {
    await api("POST", "/api/delete", { path });
    toast("Deleted " + e.name);
    await load(cwd);
  } catch (err) { toast(err.message, true); }
}

$("mkdirBtn").addEventListener("click", async () => {
  const name = prompt("Folder name");
  if (!name) return;
  try {
    await api("POST", "/api/mkdir", { path: cwd, name });
    await load(cwd);
  } catch (e) { toast(e.message, true); }
});

$("refreshBtn").addEventListener("click", () => go(cwd));
$("hiddenToggle").addEventListener("change", ev => {
  showHidden = ev.target.checked;
  try { localStorage.setItem("zs_hidden", showHidden ? "1" : "0"); } catch (e) {}
  render();
});
document.querySelectorAll("th[data-sort]").forEach(th => th.addEventListener("click", () => {
  const k = th.dataset.sort;
  if (sortKey === k) sortDir = -sortDir; else { sortKey = k; sortDir = 1; }
  render();
}));
)ZS"
R"ZS(
/* items: [{file, path}] where path is relative to the current folder. */
function upload(items) {
  if (!items.length) return;
  const existing = new Set(entries.map(e => e.name));
  const clashes = items.filter(i => !i.path.includes("/") && existing.has(i.path)).map(i => i.path);
  if (clashes.length && !confirm("Replace " + (clashes.length === 1 ? "\"" + clashes[0] + "\"" : clashes.length + " existing files") + "?")) return;

  const form = new FormData();
  let total = 0;
  for (const i of items) {
    form.append("path", i.path);
    form.append("file", i.file, i.file.name);
    total += i.file.size;
  }
  const target = cwd;
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/upload?" + new URLSearchParams({ path: target }));
  xhr.setRequestHeader("X-ZeroSub", "1");
  const label = items.length === 1 ? items[0].path : items.length + " files";
  $("progress").classList.add("on");
  $("progressText").textContent = "Uploading " + label + " (" + fmtSize(total) + ")";
  $("progressBar").style.width = "0";
  ["uploadBtn", "uploadDirBtn"].forEach(id => $(id).disabled = true);
  xhr.upload.onprogress = ev => {
    if (ev.lengthComputable) $("progressBar").style.width = (100 * ev.loaded / ev.total).toFixed(1) + "%";
  };
  const done = () => {
    $("progress").classList.remove("on");
    ["uploadBtn", "uploadDirBtn"].forEach(id => $(id).disabled = false);
    if (target === cwd) go(cwd);
  };
  xhr.onload = () => {
    let data = {};
    try { data = JSON.parse(xhr.responseText); } catch (e) {}
    if (xhr.status === 401) { showLogin(); }
    else if (xhr.status >= 200 && xhr.status < 300) toast("Uploaded " + label);
    else toast(data.error || "Upload failed", true);
    done();
  };
  xhr.onerror = () => { toast("Upload failed: connection lost", true); done(); };
  xhr.send(form);
}

$("uploadBtn").addEventListener("click", () => $("fileInput").click());
$("uploadDirBtn").addEventListener("click", () => $("dirInput").click());
$("fileInput").addEventListener("change", ev => {
  upload([...ev.target.files].map(f => ({ file: f, path: f.name })));
  ev.target.value = "";
});
$("dirInput").addEventListener("change", ev => {
  upload([...ev.target.files].map(f => ({ file: f, path: f.webkitRelativePath || f.name })));
  ev.target.value = "";
});

/* Drag and drop, folders included where the browser supports it. */
function readEntry(entry, prefix, out) {
  return new Promise(resolve => {
    if (entry.isFile) {
      entry.file(f => { out.push({ file: f, path: prefix + f.name }); resolve(); }, () => resolve());
    } else if (entry.isDirectory) {
      const reader = entry.createReader();
      const all = [];
      const next = () => reader.readEntries(async batch => {
        if (!batch.length) {
          for (const child of all) await readEntry(child, prefix + entry.name + "/", out);
          resolve();
        } else { all.push(...batch); next(); }
      }, () => resolve());
      next();
    } else resolve();
  });
}

let dragDepth = 0;
const loggedIn = () => !$("filesView").classList.contains("hide");
document.addEventListener("dragenter", ev => { if (!loggedIn()) return; ev.preventDefault(); dragDepth++; $("drop").classList.add("on"); });
document.addEventListener("dragleave", ev => { if (--dragDepth <= 0) { dragDepth = 0; $("drop").classList.remove("on"); } });
document.addEventListener("dragover", ev => { if (loggedIn()) ev.preventDefault(); });
document.addEventListener("drop", async ev => {
  ev.preventDefault();
  dragDepth = 0;
  $("drop").classList.remove("on");
  if (!loggedIn()) return;
  const out = [];
  const items = [...(ev.dataTransfer.items || [])];
  const entriesIn = items.map(i => i.webkitGetAsEntry && i.webkitGetAsEntry()).filter(Boolean);
  if (entriesIn.length) {
    for (const entry of entriesIn) await readEntry(entry, "", out);
  } else {
    for (const f of ev.dataTransfer.files) out.push({ file: f, path: f.name });
  }
  upload(out);
});

(async () => {
  let start = "";
  try { start = decodeURI(location.hash.slice(1)); } catch (e) {}
  try {
    await loadInfo();
    showFiles();
    try { await load(start); } catch (e) { await load(""); }
    try {
      const job = await api("GET", "/api/install");
      if (job.running) showInstall(job.package);
    } catch (e) {}
  } catch (e) {
    showLogin();
  }
})();
</script>
</body>
</html>
)ZS";

} // namespace platform
