/*
 * Libation Locker by Dan Roberts
 *
 * Local, self-hosted ESP32 web app for tracking spirits / bottle inventory.
 * - AP-first with optional STA join
 * - LittleFS persistence
 * - JSON REST API + single-page UI
 *
 * Creator: Dan Roberts
 * License: GPL-3.0
 */

#pragma once
#include <Arduino.h>

// Embedded UI assets (single-page app, inline CSS/JS)

static const char LL_INDEX_HTML[] PROGMEM = R"LL(
<!doctype html><html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>LibationLocker</title>
<link rel="icon" href="/favicon.ico" type="image/x-icon">
<style>
:root{
    --bg1:#071a33; --bg2:#053a5a; --card:rgba(255,255,255,.08);
    --border:rgba(255,255,255,.14); --text:#e9f3ff; --muted:rgba(233,243,255,.72);
    --red:#ff3b4d; --green:#31d27c;
    --inputBg: rgba(0,0,0,.28);
    --inputBorder: rgba(255,255,255,.18);
    --shadow: 0 10px 26px rgba(0,0,0,.25);
  }

  html { color-scheme: dark; }
  *{ box-sizing:border-box; }
  body{
    margin:0;
    font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;
    color:var(--text);
    background:linear-gradient(180deg,var(--bg1),var(--bg2));
    min-height:100vh;
    -webkit-text-size-adjust:100%;
  }

  .wrap{ max-width:1080px; margin:0 auto; padding:16px; padding-bottom:28px; }
  .top{ display:flex; align-items:flex-start; justify-content:space-between; gap:12px; flex-wrap:wrap; margin-bottom:12px; }
  .title{ font-weight:900; letter-spacing:.4px; font-size:22px; }
  .sub{ color:var(--muted); font-size:13px; margin-top:3px; line-height:1.35; }

  .btn{
    padding:10px 14px;
    border-radius:12px;
    border:1px solid var(--border);
    background:rgba(255,255,255,.10);
    color:var(--text);
    font-weight:800;
    cursor:pointer;
    touch-action:manipulation;
  }
  .btn.primary{ background:rgba(168,85,247,.18); border-color: rgba(168,85,247,.38); }


  .btn.pushed{
    transform: translateY(2px);
    filter: brightness(0.96);
  }
  .btn:disabled{
    opacity:0.55;
    cursor:not-allowed;
  }

  .row{ display:flex; gap:12px; flex-wrap:wrap; }
  .card{
    flex:1;
    min-width:300px;
    border:1px solid var(--border);
    background:var(--card);
    border-radius:16px;
    padding:12px;
    box-shadow:var(--shadow);
  }

  .kv{ display:grid; grid-template-columns:170px 1fr; gap:6px 10px; font-size:14px; }
  .k{ color:var(--muted); }
  .pill{
    display:inline-block; padding:3px 10px; border-radius:999px;
    border:1px solid var(--border); font-size:12px; font-weight:900;
  }
  .pill.ok{ color:var(--green); }
  .pill.bad{ color:var(--red); }

  .inline{ display:flex; gap:10px; align-items:center; flex-wrap:wrap; }

  /* Inputs / Selects (fix dropdown background/text) */
  input, select, textarea{
    width:100%;
    padding:10px;
    border-radius:12px;
    border:1px solid var(--inputBorder);
    background:var(--inputBg);
    color:var(--text);
    font-size:16px; /* prevents iOS zoom */
    line-height:1.2;
    outline:none;
  }
  input::placeholder, textarea::placeholder{ color: rgba(233,243,255,.55); }

  select{
    -webkit-appearance:none; -moz-appearance:none; appearance:none;
    padding-right:36px;
    background-image:
      linear-gradient(45deg, transparent 50%, rgba(233,243,255,.85) 50%),
      linear-gradient(135deg, rgba(233,243,255,.85) 50%, transparent 50%),
      linear-gradient(to right, transparent, transparent);
    background-position:
      calc(100% - 20px) calc(50% - 3px),
      calc(100% - 14px) calc(50% - 3px),
      0 0;
    background-size:6px 6px, 6px 6px, 100% 100%;
    background-repeat:no-repeat;
  }
  select option{ background:#0b2443; color:var(--text); }

  input:focus, select:focus{
    border-color: rgba(168,85,247,.45);
    box-shadow: 0 0 0 3px rgba(168,85,247,.10);
  }

  .iconbtn{
    width:38px; height:38px; border-radius:12px;
    border:1px solid var(--border);
    background:rgba(255,255,255,.08);
    color:var(--text);
    cursor:pointer;
    display:inline-flex; align-items:center; justify-content:center;
    touch-action: manipulation;
    font-weight:900;
  }
  .iconbtn.danger{ background:rgba(255,59,77,.14); border-color:rgba(255,59,77,.32); }
  .iconrow{ display:flex; gap:10px; justify-content:flex-end; }

  dialog{
    width:min(560px,92vw);
    border-radius:16px;
    border:1px solid var(--border);
    background:linear-gradient(180deg, rgba(7,26,51,.96), rgba(5,58,90,.96));
    color:var(--text);
    padding:14px;
  }
  label{ display:block; font-size:13px; color:var(--muted); margin-bottom:6px; }

  .formrow{ display:flex; gap:10px; flex-wrap:wrap; margin:10px 0; }

  /* Two-column form rows: keep side-by-side on desktop, stack on mobile */
  .formrow.twocol{ flex-wrap:nowrap; }
  .formrow.twocol .half{ min-width:0; }
.half{ flex:1; min-width:240px; }
  .spin{ width:84px; }
  select.qcSelect{ width:5.4em; min-width:0; }
  .mini{ font-size:12px; color:var(--muted); margin-top:6px; }
  .timegrid{ display:grid; grid-template-columns:1fr 1fr 1fr; gap:10px; }

  /* Desktop schedule table */
  .tablewrap{ overflow-x:auto; -webkit-overflow-scrolling:touch; border-radius:12px; }
  table{ width:100%; border-collapse:collapse; margin-top:8px; font-size:14px; min-width:760px; }
  th,td{ padding:10px 8px; border-bottom:1px solid rgba(255,255,255,.10); text-align:left; white-space:nowrap; }
  th{ color:var(--muted); font-weight:900; }

  /* Phone "app-like" schedule cards */
  .cards{ display:none; margin-top:10px; }
  .scard{
    border:1px solid rgba(255,255,255,.14);
    background:rgba(255,255,255,.06);
    border-radius:16px;
    padding:12px;
    box-shadow: 0 8px 22px rgba(0,0,0,.22);
    margin-bottom:10px;
  }
  .scardTop{ display:flex; align-items:center; justify-content:space-between; gap:10px; }
  .scardTitle{ font-weight:950; letter-spacing:.2px; }
  .badge{
    display:inline-flex; align-items:center; gap:8px;
    padding:6px 10px; border-radius:999px;
    border:1px solid rgba(255,255,255,.14);
    background:rgba(0,0,0,.18);
    font-size:12px; font-weight:900;
    color:var(--muted);
  }
  .badge.on{ color: var(--green); }
  .scardGrid{
    display:grid;
    grid-template-columns: 1fr 1fr;
    gap:8px 12px;
    margin-top:10px;
    font-size:14px;
  }
  .scK{ color:var(--muted); font-size:12px; margin-bottom:2px; }
  .scV{ font-weight:850; }
  .scActions{ display:flex; gap:10px; justify-content:flex-end; margin-top:10px; }

  /* Mobile tweaks */
  @media (max-width: 720px){
    .formrow.twocol{ flex-wrap:wrap; }
    .wrap{ padding:12px; padding-bottom:22px; }
    .title{ font-size:20px; }
    .sub{ font-size:12.5px; }
    .kv{ grid-template-columns:130px 1fr; font-size:13px; }
    .card{ min-width:100%; border-radius:18px; }
    .btn{ padding:10px 12px; border-radius:14px; }
    dialog{ width:min(560px,96vw); }

    /* On phones: keep the inventory grid visible and horizontally scrollable.
       (If you later add a schedule pane with cards, scope that behavior to that pane.) */
    #paneInv .tablewrap{ display:block; overflow-x:auto; -webkit-overflow-scrolling:touch; }
    #paneInv table{ display:table; }

    /* Make top buttons easier */
    .iconbtn{ width:42px; height:42px; border-radius:14px; }
    select.qcSelect{ width:5.0em; }
  }

  /* Smaller phones */
  @media (max-width: 390px){
    .scardGrid{ grid-template-columns: 1fr; }
    .spin{ width:74px; }
    select.qcSelect{ width:4.6em; }
  }

  /* Connect modal */
  #connDlg{ width:min(420px,92vw); }
  .spinner{
    width:20px; height:20px; border-radius:50%;
    border:3px solid rgba(233,243,255,.22);
    border-top-color: rgba(233,243,255,.95);
    animation: spin .9s linear infinite;
  }
  @keyframes spin{ to{ transform: rotate(360deg);} }

  /* Fullscreen busy overlay */
  #busyOverlay{
    position:fixed; inset:0; z-index:9999;
    background:rgba(0,0,0,.82);
    display:none; align-items:center; justify-content:center;
    flex-direction:column; gap:16px;
  }
  #busyOverlay.show{ display:flex; }
  .busySpinner{
    width:44px; height:44px; border-radius:50%;
    border:4px solid rgba(168,85,247,.25);
    border-top-color:rgba(168,85,247,.9);
    animation: spin 0.8s linear infinite;
  }
  #busyMsg{
    font-size:16px; font-weight:700; color:var(--text);
    text-align:center; max-width:320px; line-height:1.4;
  }
  #busyElapsed{
    font-size:13px; color:var(--muted); font-variant-numeric:tabular-nums;
  }

  /* AI Modes editor */
  .modeCard{
    border:1px solid var(--border); background:rgba(255,255,255,.05);
    border-radius:12px; padding:10px 12px; margin-bottom:8px;
  }
  .modeCard .modeHead{
    display:flex; align-items:center; justify-content:space-between; gap:8px;
  }
  .modeCard .modeLabel{ font-weight:800; font-size:14px; }
  .modeCard .modeId{ font-size:12px; color:var(--muted); font-family:monospace; }
  .modeCard .modeMeta{ font-size:12px; color:var(--muted); margin-top:4px; }
  .modeCard .modeActions{ display:flex; gap:6px; }
  .modeEdit{ display:none; margin-top:8px; }
  .modeEdit.open{ display:block; }
  .modeEdit .grid2{ gap:8px; }
  .modeEdit label{ margin-bottom:2px; }
  .modeEdit textarea{ font-size:13px; }
select option{ background:#071a33; color:var(--text);} 

/* LibationLocker additions */
.badges{ display:flex; gap:10px; align-items:center; justify-content:flex-end; flex-wrap:wrap; }
.badge{ padding:6px 10px; border-radius:999px; border:1px solid var(--border); background:rgba(255,255,255,.08); color:var(--text); font-weight:800; font-size:12px; }
.badge small{ font-weight:700; opacity:.75; }
.tabs{ display:flex; gap:10px; justify-content:flex-end; }
.tabbtn{ padding:10px 14px; border-radius:999px; border:1px solid var(--border); background:rgba(255,255,255,.08); color:var(--text); font-weight:900; cursor:pointer; }
.tabbtn[aria-pressed="true"]{ background:rgba(168,85,247,.18); border-color: rgba(168,85,247,.45); box-shadow: inset 0 0 0 2px rgba(168,85,247,.12); }
.pane{ display:none; }
.pane.active{ display:block; }
.row{ display:flex; gap:12px; align-items:flex-end; flex-wrap:wrap; }
.row > div{ min-width:160px; flex:1; }
.tablewrap{ overflow:auto; border-radius:16px; border:1px solid var(--border); }
table{ width:100%; border-collapse:collapse; }
th,td{ padding:10px 10px; border-bottom:1px solid rgba(255,255,255,.10); text-align:left; white-space:nowrap; }
th{ color:var(--muted); font-weight:900; }
tr:hover td{ background:rgba(255,255,255,.04); }
.modalBackdrop{ position:fixed; inset:0; background:rgba(0,0,0,.78); display:none; align-items:center; justify-content:center; padding:16px; overflow-y:auto; }
.modalBackdrop.show{ display:flex; }
@media (max-width: 720px){ .modalBackdrop{ align-items:flex-start; } }
.modal{ width:min(720px, 100%); 
  /* Mobile: allow scrolling inside the modal so the Save button is reachable */
  max-height:calc(100vh - 32px);
  max-height:calc(100dvh - 32px);
  overflow-y:auto;
  overflow-x:hidden;
  -webkit-overflow-scrolling:touch;
}
.card.modal{ flex:0 0 auto; }
  .card.modal{ background:rgba(7,26,51,.96); border:1px solid rgba(255,255,255,.20); }

.grid2{ display:grid; grid-template-columns: 1fr 1fr; gap:12px; }
@media (max-width: 720px){ .grid2{ grid-template-columns:1fr; } }

/* Pagination */
.pager{ display:flex; align-items:center; justify-content:center; gap:10px; margin-top:10px; }
.pager button{ padding:6px 14px; border-radius:10px; border:1px solid var(--border); background:rgba(255,255,255,.08); color:var(--text); font-weight:800; cursor:pointer; }
.pager button:disabled{ opacity:.4; cursor:not-allowed; }
.pager span{ font-size:13px; color:var(--muted); min-width:100px; text-align:center; }

/* Sortable column headers */
th[data-sort]{ cursor:pointer; user-select:none; }
th[data-sort]:hover{ color:var(--text); }
th .sortarrow{ font-size:11px; margin-left:3px; opacity:.6; }

/* Add/Edit modal form layout (matches the "ADD popup" sketch)
   Desktop: 6-col grid ->
     Row1: Type (3) | Brand (3)
     Row2: Name (6)
     Row3: Size (2) | ABV (2) | Qty (2)
     Row4: Remaining (2) | Need to buy (2) | Rating (2)
     Row5: Tags (6)
     Row6: Notes (6)
*/
.addGrid{ display:grid; grid-template-columns: repeat(6, minmax(0, 1fr)); gap:12px; }
.addGrid .span2{ grid-column: span 2; }
.addGrid .span3{ grid-column: span 3; }
.addGrid .span6{ grid-column: 1 / -1; }
@media (max-width: 720px){
  .addGrid{ grid-template-columns: 1fr; }
  .addGrid .span2,
  .addGrid .span3,
  .addGrid .span6{ grid-column: 1 / -1; }
}

.aiAskBar{ display:flex; gap:10px; align-items:flex-end; flex-wrap:wrap; }
.aiAskBar .grow{ flex:1; min-width:260px; }
.chkrow{ display:flex; align-items:center; gap:8px; min-width:auto !important; flex:0 0 auto !important; }
.chkrow input{ width:auto; }
.responseBox{
  width:100%; min-height:280px; max-height:60vh; overflow-y:auto;
  padding:12px 14px; font-size:14px; line-height:1.55; color:var(--text);
  border:1px solid var(--border); border-radius:12px;
  background:rgba(0,0,0,.18);
}
.responseBox h1,.responseBox h2,.responseBox h3,.responseBox h4{
  margin:14px 0 6px 0; font-weight:800; color:var(--text);
}
.responseBox h1{ font-size:18px; }
.responseBox h2{ font-size:16px; }
.responseBox h3{ font-size:15px; }
.responseBox h4{ font-size:14px; }
.responseBox p{ margin:6px 0; }
.responseBox ul,.responseBox ol{ margin:6px 0 6px 20px; padding:0; }
.responseBox li{ margin:3px 0; }
.responseBox code{
  background:rgba(168,85,247,.12); padding:1px 5px; border-radius:4px;
  font-family:monospace; font-size:13px;
}
.responseBox pre{
  background:rgba(0,0,0,.3); border:1px solid var(--border); border-radius:8px;
  padding:10px; overflow-x:auto; margin:8px 0;
}
.responseBox pre code{ background:none; padding:0; font-size:13px; }
.responseBox strong{ font-weight:800; }
.responseBox em{ font-style:italic; }
.responseBox hr{ border:none; border-top:1px solid var(--border); margin:10px 0; }
.responseBox table{ border-collapse:collapse; margin:8px 0; font-size:13px; }
.responseBox th,.responseBox td{
  border:1px solid var(--border); padding:5px 8px; text-align:left;
}
.responseBox th{ background:rgba(255,255,255,.06); font-weight:800; }
.responseBox blockquote{
  border-left:3px solid rgba(168,85,247,.4); margin:8px 0; padding:4px 12px;
  color:var(--muted); font-style:italic;
}
.respMeta{
  font-size:12px; color:var(--muted); margin-bottom:8px;
  padding:6px 10px; border-radius:8px; background:rgba(0,0,0,.15);
  font-family:monospace;
}

</style>
</head>
<body>
<div class="wrap">
  <div class="top">
    <div>
      <div class="title">Libation Locker</div>
      <div class="sub">by Dan • Personal bar inventory</div>
    </div>
    <div style="display:flex; flex-direction:column; gap:10px; align-items:flex-end;">
      <div class="badges">
        <div class="badge" id="ipBadge">IP: <span>-</span></div>
        <div class="badge" id="fsBadge">Storage: <span>-</span></div>
      </div>
      <div class="tabs">
        <button class="tabbtn" id="tabInv" aria-pressed="true">Inventory</button>
        <button class="tabbtn" id="tabCfg" aria-pressed="false">Config</button>
        <button class="tabbtn" id="tabImp" aria-pressed="false">Import/Export</button>
      </div>
    </div>
  </div>

  <div class="row">
    <div class="card pane active" id="paneInv">
      <div class="aiAskBar">
        <div class="grow">
          <div class="sub">Ask AI about this inventory</div>
          <input class="in" id="aiQuestion" placeholder="What cocktails can I make, what am I missing, what should I buy next..." />
        </div>
        <div>
          <div class="sub">Mode</div>
          <select class="in" id="aiMode">
            <!-- populated from config modes -->
          </select>
        </div>
        <label class="chkrow"><input type="checkbox" id="aiIncludeInventory" checked /> <span class="sub">Include inventory snapshot</span></label>
        <div style="flex:0 0 auto; min-width:auto;">
          <button class="btn primary" id="btnAskAi">Ask AI</button>
        </div>
      </div>

      <div style="height:12px"></div>

      <div class="row">
        <div>
          <div class="sub">Search</div>
          <input class="in" id="q" placeholder="brand, name, tags..." />
        </div>
        <div>
          <div class="sub">Type</div>
          <select class="in" id="filterType"></select>
        </div>
        <div>
          <div class="sub">Need to buy</div>
          <select class="in" id="filterNeed">
            <option value="all">All</option>
            <option value="yes">Need to buy</option>
            <option value="no">In stock</option>
          </select>
        </div>
        <div style="flex:0 0 auto; min-width:auto;">
          <button class="btn primary" id="btnAdd">+ Add</button>
          <button class="btn" id="btnRefresh">Refresh</button>
        </div>
      </div>

      <div style="height:12px"></div>

      <div class="tablewrap">
        <table id="tbl">
          <thead>
            <tr>
              <th></th>
              <th data-sort="type">Type</th>
              <th data-sort="brand">Brand</th>
              <th data-sort="name">Name</th>
              <th data-sort="sizeMl">Size</th>
              <th data-sort="abv">ABV</th>
              <th data-sort="qty">Qty</th>
              <th data-sort="remainingPct">Remain%</th>
              <th data-sort="needToBuy">Need</th>
              <th data-sort="rating">Rating</th>
              <th>Tags</th>
              <th></th>
            </tr>
          </thead>
          <tbody id="tbody"></tbody>
        </table>
      </div>

      <div style="height:8px"></div>
      <div class="pager" id="pager">
        <button id="pgPrev">&laquo; Prev</button>
        <span id="pgInfo">-</span>
        <button id="pgNext">Next &raquo;</button>
      </div>
      <div style="height:4px"></div>
      <div class="sub" id="statusLine">Ready.</div>
    </div>

    <div class="card pane" id="paneCfg">
      <div class="title" style="font-size:18px;">Config</div>
      <div class="sub">Dropdown values are editable. Changes apply immediately to the UI.</div>
      <div style="height:12px"></div>

      <div class="grid2">
        <div>
          <div class="sub">Types (one per line)</div>
          <textarea class="in" id="cfgTypes" rows="8" placeholder="Bourbon&#10;Vodka&#10;Gin"></textarea>
        </div>
        <div>
          <div class="sub">Sizes mL (one per line)</div>
          <textarea class="in" id="cfgSizes" rows="8" placeholder="375&#10;750&#10;1000"></textarea>
        </div>
        <div>
          <div class="sub">ABV presets (one per line)</div>
          <textarea class="in" id="cfgAbv" rows="8" placeholder="40&#10;45&#10;50"></textarea>
        </div>
        <div>
          <div class="sub">Remaining % presets (one per line)</div>
          <textarea class="in" id="cfgRemain" rows="8" placeholder="0&#10;25&#10;50&#10;75&#10;100"></textarea>
        </div>
      </div>

      <div style="height:20px"></div>
      <div class="title" style="font-size:18px;">LM Studio AI</div>
      <div class="sub">Connect to an OpenAI-compatible LM Studio server running on your LAN.</div>
      <div style="height:12px"></div>

      <div class="grid2">
        <div>
          <div class="sub">Enable AI</div>
          <select class="in" id="aiEnabled">
            <option value="0">Disabled</option>
            <option value="1">Enabled</option>
          </select>
        </div>
        <div>
          <div class="sub">Temperature</div>
          <input class="in" id="aiTemperature" type="number" min="0" max="2" step="0.1" placeholder="0.2" />
        </div>
        <div>
          <div class="sub">Base URL</div>
          <input class="in" id="aiBaseUrl" placeholder="http://192.168.5.250:1234" />
        </div>
        <div>
          <div class="sub">Model</div>
          <select class="in" id="aiModelSelect">
            <option value="">Manual entry or refresh models...</option>
          </select>
        </div>
        <div>
          <div class="sub">Manual model override</div>
          <input class="in" id="aiModel" placeholder="qwen3-coder / mistral / etc." />
        </div>
        <div>
          <div class="sub">API Key (optional)</div>
          <input class="in" id="aiApiKey" type="password" placeholder="Leave blank for local LM Studio" />
        </div>
        <div>
          <div class="sub">Max Tokens</div>
          <input class="in" id="aiMaxTokens" type="number" min="64" max="16384" step="1" placeholder="8192" />
        </div>
        <div>
          <div class="sub">Timeout (seconds)</div>
          <input class="in" id="aiTimeoutSec" type="number" min="30" max="600" step="10" placeholder="180" />
        </div>
        <div>
          <div class="sub">Disable Thinking</div>
          <select class="in" id="aiDisableThinking">
            <option value="1">Yes (recommended)</option>
            <option value="0">No &mdash; allow reasoning phase</option>
          </select>
          <div class="mini">Appends /no_think for Qwen 3.x / DeepSeek models. Saves tokens and speeds up responses. Non-thinking models ignore this.</div>
        </div>
        <div style="grid-column:1 / -1;">
          <div class="sub">Default System Prompt</div>
          <textarea class="in" id="aiSystemPrompt" rows="4" placeholder="Global system prompt (used when a mode has no override)"></textarea>
          <div class="mini">Used as fallback when a mode does not define its own system prompt.</div>
        </div>
      </div>

      <div style="height:16px"></div>
      <div class="title" style="font-size:16px;">AI Modes</div>
      <div class="sub">Modes control how the AI interprets your question. Edit existing modes or add new ones.</div>
      <div style="height:8px"></div>
      <div id="modesContainer"></div>
      <button class="btn" id="btnAddMode" style="margin-top:6px;">+ Add Mode</button>

      <div style="height:12px"></div>
      <button class="btn" id="btnRefreshAiModels">Refresh Models</button>
      <button class="btn primary" id="btnTestAi">Test AI Connection</button>
      <span class="sub" id="aiMsg"></span>

      <div style="height:16px"></div>
      <button class="btn primary" id="btnSaveCfg">Save Config</button>
      <span class="sub" id="cfgMsg"></span>

      <div style="height:20px"></div>
      <div class="title" style="font-size:18px;">Wi‑Fi</div>
      <div class="sub">AP stays on forever. Optionally connect STA to your home network.</div>
      <div style="height:12px"></div>

      <div class="grid2">
        <div>
          <div class="sub">Enable STA</div>
          <select class="in" id="wifiEnabled">
            <option value="0">Disabled</option>
            <option value="1">Enabled</option>
          </select>
        </div>
        <div></div>
        <div>
          <div class="sub">SSID</div>
          <input class="in" id="wifiSsid" placeholder="Your Wi‑Fi name" />
        </div>
        <div></div>
        <div>
          <div class="sub">Password</div>
          <input class="in" id="wifiPass" type="password" placeholder="Your Wi‑Fi password" />
        </div>
        <div>
          <div class="sub">Status</div>
          <div class="badge" id="wifiStatus" style="display:inline-block;">-</div>
        </div>
      </div>

      <div style="height:12px"></div>
      <button class="btn primary" id="btnSaveWifi">Save & Connect</button>
      <span class="sub" id="wifiMsg"></span>
    </div>

    <div class="card pane" id="paneImp">
      <div class="title" style="font-size:18px;">Import / Export</div>
      <div class="sub">Export full list as JSON or CSV. Import expects JSON export format.</div>
      <div style="height:12px"></div>
      <button class="btn primary" id="btnExportJson">Export JSON</button>
      <button class="btn" id="btnExportCsv">Export CSV</button>
      <button class="btn" id="btnExportNeed">Shopping List (Need=YES)</button>

      <div style="height:16px"></div>
      <div class="sub">Import JSON</div>
      <input class="in" type="file" id="importFile" accept=".json,application/json" />
      <div style="height:12px"></div>
      <button class="btn primary" id="btnImport">Import</button>
      <span class="sub" id="impMsg"></span>
    </div>
  </div>
</div>

<!-- Modal -->
<div class="modalBackdrop" id="modalBg">
  <div class="card modal">
    <div class="title" style="font-size:18px;" id="modalTitle">Add Item</div>
    <div style="height:12px"></div>
    <div class="addGrid">
      <!-- Row 1 -->
      <div class="span3"><div class="sub">Type</div><select class="in" id="itType"></select></div>
      <div class="span3"><div class="sub">Brand</div><input class="in" id="itBrand"/></div>

      <!-- Row 2 -->
      <div class="span6"><div class="sub">Name</div><input class="in" id="itName"/></div>

      <!-- Row 3 -->
      <div class="span2"><div class="sub">Size mL</div><select class="in" id="itSize"></select></div>
      <div class="span2"><div class="sub">ABV</div><select class="in" id="itAbv"></select></div>
      <div class="span2"><div class="sub">Qty</div><input class="in" id="itQty" type="number" min="0" step="1"/></div>

      <!-- Row 4 -->
      <div class="span2"><div class="sub">Remaining %</div><select class="in" id="itRemain"></select></div>
      <div class="span2"><div class="sub">Need to buy</div>
        <select class="in" id="itNeed"><option value="0">No</option><option value="1">Yes</option></select></div>
      <div class="span2"><div class="sub">Rating (0-10)</div><input class="in" id="itRating" type="number" min="0" max="10" step="1"/></div>

      <!-- Row 5 -->
      <div class="span6"><div class="sub">Tags (comma separated)</div><input class="in" id="itTags" placeholder="smooth, spicy"/></div>

      <!-- Row 6 -->
      <div class="span6"><div class="sub">Notes</div><textarea class="in" id="itNotes" style="width: 100%"  rows="4"></textarea></div>
    </div>
    <div style="height:12px"></div>
    <div class="row" style="justify-content:flex-end;">
      <button class="btn" id="btnCancel">Cancel</button>
      <button class="btn primary" id="btnSaveItem">Save</button>
    </div>
  </div>
</div>

<div class="modalBackdrop" id="viewBg">
  <div class="card modal">
    <div class="title" style="font-size:18px;" id="viewTitle">View Item</div>
    <div style="height:12px"></div>

    <div class="kv" style="grid-template-columns:140px 1fr; gap:8px 12px;">
      <div class="k">Type</div><div id="vType"></div>
      <div class="k">Brand</div><div id="vBrand"></div>
      <div class="k">Name</div><div id="vName"></div>
      <div class="k">Size (mL)</div><div id="vSize"></div>
      <div class="k">ABV</div><div id="vAbv"></div>
      <div class="k">Qty</div><div id="vQty"></div>
      <div class="k">Remaining %</div><div id="vRemain"></div>
      <div class="k">Need to buy</div><div id="vNeed"></div>
      <div class="k">Rating</div><div id="vRating"></div>
      <div class="k">Tags</div><div id="vTags"></div>
      <div class="k">Notes</div><div id="vNotes" style="white-space:pre-wrap; word-break:break-word;"></div>
    </div>

    <div style="height:12px"></div>
    <div class="row" style="justify-content:flex-end;">
      <button class="btn primary" id="btnViewClose">Close</button>
    </div>
  </div>
</div>

<div class="modalBackdrop" id="aiRespBg">
  <div class="card modal">
    <div class="title" style="font-size:18px;" id="aiRespTitle">AI Response</div>
    <div class="respMeta" id="aiRespMeta"></div>
    <div class="responseBox" id="aiRespRendered"></div>
    <div style="height:12px"></div>
    <div class="row" style="justify-content:flex-end;">
      <button class="btn" id="btnToggleRaw" style="font-size:12px;">View Raw</button>
      <button class="btn" id="btnCopyAiResp">Copy</button>
      <button class="btn" id="btnExportAiResp">Export TXT</button>
      <button class="btn primary" id="btnCloseAiResp">Close</button>
    </div>
  </div>
</div>

<!-- Busy overlay (AI thinking / inventory loading) -->
<div id="busyOverlay">
  <div class="busySpinner"></div>
  <div id="busyMsg">Please wait...</div>
  <div id="busyElapsed"></div>
</div>

<script>

'use strict';

const $ = (id)=>document.getElementById(id);

let cfg = { types:[], sizesMl:[], abvPresets:[], remainingPresets:[] };
let items = [];
let editingId = null;
let editingVersion = 0;
let lastAiResponse = '';
let aiModels = [];
let cfgModes = [];

// Pagination
const PAGE_SIZE = 50;
let currentPage = 0;

// Sort state
let sortCol = 'brand';
let sortAsc = true;

// Debounce helper
function debounce(fn, ms) {
  let t; return function(...a) { clearTimeout(t); t = setTimeout(()=>fn.apply(this,a), ms); };
}

function setTab(which){
  const tabs = {
    inv: ['tabInv','paneInv'],
    cfg: ['tabCfg','paneCfg'],
    imp: ['tabImp','paneImp'],
  };
  for (const k in tabs){
    const [tb,pn] = tabs[k];
    $(tb).setAttribute('aria-pressed', k===which ? 'true':'false');
    $(pn).classList.toggle('active', k===which);
  }
}

function toast(msg){ $('statusLine').textContent = msg; }

let _busyTimer = null;
function showBusy(msg){
  $('busyMsg').textContent = msg || 'Please wait...';
  $('busyElapsed').textContent = '';
  $('busyOverlay').classList.add('show');
  const t0 = Date.now();
  clearInterval(_busyTimer);
  _busyTimer = setInterval(()=>{
    const sec = Math.floor((Date.now() - t0) / 1000);
    $('busyElapsed').textContent = sec > 0 ? sec + 's elapsed' : '';
  }, 1000);
}
function hideBusy(){
  clearInterval(_busyTimer);
  _busyTimer = null;
  $('busyOverlay').classList.remove('show');
}

// ---- AI Modes management ----

function populateAiModeDropdown(){
  const sel = $('aiMode');
  const prev = sel.value;
  sel.innerHTML = '';
  cfgModes.forEach(m => {
    const o = document.createElement('option');
    o.value = m.id;
    o.textContent = m.label;
    sel.appendChild(o);
  });
  // Restore previous selection if still exists
  if (prev) { for (const o of sel.options) { if (o.value === prev) { sel.value = prev; return; } } }
  if (sel.options.length) sel.selectedIndex = 0;
}

function renderModes(){
  const c = $('modesContainer');
  c.innerHTML = '';
  cfgModes.forEach((m, idx) => {
    const card = document.createElement('div');
    card.className = 'modeCard';
    card.innerHTML = `
      <div class="modeHead">
        <div>
          <span class="modeLabel">${esc(m.label)}</span>
          <span class="modeId">${esc(m.id)}</span>
          <span class="pill" style="font-size:11px;margin-left:6px;">${m.restrictToInventory ? 'Inventory only' : 'Unrestricted'}</span>
        </div>
        <div class="modeActions">
          <button class="iconbtn" data-midx="${idx}" data-mact="toggle" title="Edit">&#9998;</button>
          <button class="iconbtn danger" data-midx="${idx}" data-mact="del" title="Delete">&times;</button>
        </div>
      </div>
      <div class="modeMeta">${esc((m.instruction||'').substring(0,120))}${(m.instruction||'').length>120?'...':''}</div>
      <div class="modeEdit" id="modeEdit${idx}">
        <div style="height:6px"></div>
        <div class="grid2" style="gap:8px;">
          <div>
            <label>ID (machine name)</label>
            <input class="in" id="mId${idx}" value="${esc(m.id)}" placeholder="e.g. my_custom_mode" />
          </div>
          <div>
            <label>Display Label</label>
            <input class="in" id="mLabel${idx}" value="${esc(m.label)}" />
          </div>
          <div>
            <label>Restrict to Inventory</label>
            <select class="in" id="mRestrict${idx}">
              <option value="1" ${m.restrictToInventory?'selected':''}>Yes &mdash; inventory only</option>
              <option value="0" ${!m.restrictToInventory?'selected':''}>No &mdash; unrestricted</option>
            </select>
          </div>
          <div></div>
          <div style="grid-column:1 / -1;">
            <label>Instruction</label>
            <textarea class="in" id="mInst${idx}" rows="3" placeholder="Instruction injected into the prompt for this mode">${esc(m.instruction||'')}</textarea>
          </div>
          <div style="grid-column:1 / -1;">
            <label>System Prompt Override (optional)</label>
            <textarea class="in" id="mSys${idx}" rows="3" placeholder="Leave blank to use the global default system prompt">${esc(m.systemPrompt||'')}</textarea>
          </div>
        </div>
        <div style="height:6px"></div>
        <button class="btn primary" data-midx="${idx}" data-mact="apply" style="font-size:13px;padding:6px 12px;">Apply Changes</button>
      </div>
    `;
    c.appendChild(card);
  });

  // Delegate clicks on mode cards
  c.onclick = (ev) => {
    const btn = ev.target.closest('button[data-midx]');
    if (!btn) return;
    const idx = parseInt(btn.dataset.midx, 10);
    const act = btn.dataset.mact;
    if (act === 'toggle') {
      const ed = $('modeEdit' + idx);
      ed.classList.toggle('open');
    } else if (act === 'del') {
      if (cfgModes.length <= 1) { alert('You need at least one mode.'); return; }
      if (!confirm('Delete mode "' + cfgModes[idx].label + '"?')) return;
      cfgModes.splice(idx, 1);
      renderModes();
      populateAiModeDropdown();
    } else if (act === 'apply') {
      const newId = $('mId' + idx).value.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
      const newLabel = $('mLabel' + idx).value.trim();
      if (!newId || !newLabel) { alert('ID and Label are required.'); return; }
      // Check duplicate ID
      for (let i = 0; i < cfgModes.length; i++) {
        if (i !== idx && cfgModes[i].id === newId) { alert('Duplicate mode ID: ' + newId); return; }
      }
      cfgModes[idx].id = newId;
      cfgModes[idx].label = newLabel;
      cfgModes[idx].instruction = $('mInst' + idx).value.trim();
      cfgModes[idx].systemPrompt = $('mSys' + idx).value.trim();
      cfgModes[idx].restrictToInventory = $('mRestrict' + idx).value === '1';
      renderModes();
      populateAiModeDropdown();
      toast('Mode "' + newLabel + '" updated. Save Config to persist.');
    }
  };
}

function addNewMode(){
  const newId = 'custom_' + Date.now().toString(36);
  cfgModes.push({
    id: newId,
    label: 'New Mode',
    instruction: '',
    systemPrompt: '',
    restrictToInventory: false
  });
  renderModes();
  populateAiModeDropdown();
  // Auto-open the editor for the new mode
  const ed = $('modeEdit' + (cfgModes.length - 1));
  if (ed) ed.classList.add('open');
  toast('New mode added. Edit it below, then Save Config.');
}

async function apiGet(url){ const r=await fetch(url,{cache:'no-store'}); if(!r.ok) throw new Error(await r.text()); return await r.json(); }
async function apiPut(url,obj){ const r=await fetch(url,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)}); if(!r.ok) throw new Error(await r.text()); return await r.json(); }
async function apiPost(url,obj){ const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)}); if(!r.ok) throw new Error(await r.text()); return await r.json(); }
async function apiDel(url){ const r=await fetch(url,{method:'DELETE'}); if(!r.ok) throw new Error(await r.text()); return await r.json(); }

function fillSelect(el, values, includeAll=false){
  el.innerHTML='';
  if(includeAll){ const o=document.createElement('option'); o.value=''; o.textContent='All'; el.appendChild(o); }
  for(const v of values){
    const o=document.createElement('option');
    o.value=String(v);
    o.textContent=String(v);
    el.appendChild(o);
  }
}

function applyConfigToForm(){
  fillSelect($('filterType'), [''].concat(cfg.types), false);
  // filterType first option is "All"
  $('filterType').innerHTML = '<option value="">All</option>' + cfg.types.map(t=>`<option value="${esc(t)}">${esc(t)}</option>`).join('');
  $('itType').innerHTML = cfg.types.map(t=>`<option value="${esc(t)}">${esc(t)}</option>`).join('');

  $('itSize').innerHTML = cfg.sizesMl.map(v=>`<option value="${v}">${v}</option>`).join('');

  $('itRemain').innerHTML = cfg.remainingPresets.map(v=>`<option value="${v}">${v}</option>`).join('');
  $('itAbv').innerHTML = '<option value="">-</option>' + (cfg.abvPresets||[]).map(v=>`<option value="${v}">${v}</option>`).join('');
}

function esc(s){ return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

// Simple Markdown → HTML renderer (no external dependencies)
function mdToHtml(md){
  const lines = md.split('\n');
  let html = '';
  let inCode = false, codeLang = '', codeLines = [];
  let inUl = false, inOl = false, inTable = false, tableRows = [];
  let inBlockquote = false, bqLines = [];

  function closeList(){
    if(inUl){ html += '</ul>'; inUl = false; }
    if(inOl){ html += '</ol>'; inOl = false; }
  }
  function closeTable(){
    if(!inTable) return;
    // First row = header
    html += '<table>';
    tableRows.forEach((r,i) => {
      const tag = i === 0 ? 'th' : 'td';
      html += '<tr>' + r.map(c => `<${tag}>${inlineFormat(c.trim())}</${tag}>`).join('') + '</tr>';
    });
    html += '</table>';
    inTable = false; tableRows = [];
  }
  function closeBq(){
    if(!inBlockquote) return;
    html += '<blockquote>' + mdToHtml(bqLines.join('\n')) + '</blockquote>';
    inBlockquote = false; bqLines = [];
  }

  function inlineFormat(s){
    s = esc(s);
    // code spans first (protect from further formatting)
    s = s.replace(/`([^`]+)`/g, '<code>$1</code>');
    // bold+italic
    s = s.replace(/\*\*\*(.+?)\*\*\*/g, '<strong><em>$1</em></strong>');
    // bold
    s = s.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
    // italic
    s = s.replace(/\*(.+?)\*/g, '<em>$1</em>');
    // links [text](url)
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" style="color:rgba(168,85,247,.9)">$1</a>');
    return s;
  }

  for(let i = 0; i < lines.length; i++){
    const raw = lines[i];

    // Fenced code block toggle
    if(raw.trimStart().startsWith('```')){
      if(!inCode){
        closeList(); closeTable(); closeBq();
        inCode = true;
        codeLang = raw.trim().slice(3).trim();
        codeLines = [];
        continue;
      } else {
        html += '<pre><code>' + esc(codeLines.join('\n')) + '</code></pre>';
        inCode = false;
        continue;
      }
    }
    if(inCode){ codeLines.push(raw); continue; }

    const trimmed = raw.trim();

    // Blank line closes open blocks
    if(!trimmed){
      closeList(); closeTable(); closeBq();
      continue;
    }

    // Blockquote
    if(trimmed.startsWith('> ')){
      if(!inBlockquote){ closeList(); closeTable(); inBlockquote = true; bqLines = []; }
      bqLines.push(trimmed.slice(2));
      continue;
    } else if(inBlockquote){ closeBq(); }

    // Table row (contains |)
    if(trimmed.includes('|') && !trimmed.startsWith('#')){
      const cells = trimmed.replace(/^\|/,'').replace(/\|$/,'').split('|');
      // Skip separator rows (---|----|---)
      if(cells.every(c => /^[\s:_-]+$/.test(c))){ continue; }
      if(!inTable){ closeList(); closeBq(); inTable = true; tableRows = []; }
      tableRows.push(cells);
      continue;
    } else if(inTable){ closeTable(); }

    // Horizontal rule
    if(/^[-*_]{3,}$/.test(trimmed)){
      closeList(); closeTable(); closeBq();
      html += '<hr>';
      continue;
    }

    // Headers
    const hMatch = trimmed.match(/^(#{1,4})\s+(.+)/);
    if(hMatch){
      closeList(); closeTable(); closeBq();
      const level = hMatch[1].length;
      html += `<h${level}>${inlineFormat(hMatch[2])}</h${level}>`;
      continue;
    }

    // Unordered list
    if(/^[-*+]\s+/.test(trimmed)){
      closeTable(); closeBq();
      if(!inUl){ closeList(); html += '<ul>'; inUl = true; }
      html += '<li>' + inlineFormat(trimmed.replace(/^[-*+]\s+/, '')) + '</li>';
      continue;
    }

    // Ordered list
    if(/^\d+[.)]\s+/.test(trimmed)){
      closeTable(); closeBq();
      if(!inOl){ closeList(); html += '<ol>'; inOl = true; }
      html += '<li>' + inlineFormat(trimmed.replace(/^\d+[.)]\s+/, '')) + '</li>';
      continue;
    }

    // Paragraph
    closeList(); closeTable(); closeBq();
    html += '<p>' + inlineFormat(trimmed) + '</p>';
  }

  // Close any remaining open blocks
  if(inCode) html += '<pre><code>' + esc(codeLines.join('\n')) + '</code></pre>';
  closeList(); closeTable(); closeBq();
  return html;
}

function ensureSelectValue(sel, val){
  const v = (val===null || val===undefined) ? '' : String(val);
  if(!v){ sel.value=''; return; }
  for(const opt of sel.options){ if(opt.value===v){ sel.value=v; return; } }
  const o = document.createElement('option');
  o.value = v;
  o.textContent = v;
  sel.appendChild(o);
  sel.value = v;
}

function itemMatchesFilters(it){
  const q = $('q').value.trim().toLowerCase();
  const t = $('filterType').value;
  const need = $('filterNeed').value;

  if(t && it.type !== t) return false;
  if(need==='yes' && !it.needToBuy) return false;
  if(need==='no' && it.needToBuy) return false;

  if(q){
    const hay = `${it.brand} ${it.name} ${(it.tags||[]).join(' ')}`.toLowerCase();
    if(!hay.includes(q)) return false;
  }
  return true;
}

function sortItems(arr) {
  return arr.sort((a, b) => {
    let va = a[sortCol], vb = b[sortCol];
    if (sortCol === 'needToBuy') { va = va ? 1 : 0; vb = vb ? 1 : 0; }
    if (typeof va === 'string') va = va.toLowerCase();
    if (typeof vb === 'string') vb = vb.toLowerCase();
    if (va == null) va = '';
    if (vb == null) vb = '';
    if (va < vb) return sortAsc ? -1 : 1;
    if (va > vb) return sortAsc ? 1 : -1;
    return 0;
  });
}

function updateSortHeaders() {
  document.querySelectorAll('th[data-sort]').forEach(th => {
    const col = th.dataset.sort;
    const existing = th.querySelector('.sortarrow');
    if (existing) existing.remove();
    if (col === sortCol) {
      const span = document.createElement('span');
      span.className = 'sortarrow';
      span.textContent = sortAsc ? '\u25B2' : '\u25BC';
      th.appendChild(span);
    }
  });
}

function sortBy(col) {
  if (sortCol === col) { sortAsc = !sortAsc; }
  else { sortCol = col; sortAsc = true; }
  currentPage = 0;
  render();
}

function render(){
  const tbody = $('tbody');
  tbody.innerHTML = '';

  const filtered = sortItems(items.filter(itemMatchesFilters));
  const totalPages = Math.max(1, Math.ceil(filtered.length / PAGE_SIZE));
  if (currentPage >= totalPages) currentPage = totalPages - 1;
  if (currentPage < 0) currentPage = 0;

  const start = currentPage * PAGE_SIZE;
  const end = Math.min(start + PAGE_SIZE, filtered.length);
  const page = filtered.slice(start, end);

  // Build rows only for the current page
  const frag = document.createDocumentFragment();
  for (const it of page) {
    const tr = document.createElement('tr');
    const tags = (it.tags||[]).join(', ');
    tr.innerHTML = `
      <td><button class="btn" data-act="view" data-id="${esc(it.id)}">View</button></td>
      <td>${esc(it.type||'')}</td>
      <td>${esc(it.brand||'')}</td>
      <td>${esc(it.name||'')}</td>
      <td>${it.sizeMl||''}</td>
      <td>${(it.abv??'')}</td>
      <td>${it.qty??0}</td>
      <td>${it.remainingPct??''}</td>
      <td>${it.needToBuy ? 'Yes':'No'}</td>
      <td>${it.rating??0}</td>
      <td>${esc(tags)}</td>
      <td>
        <button class="btn" data-act="edit" data-id="${esc(it.id)}">Edit</button>
        <button class="btn" data-act="del" data-id="${esc(it.id)}">Del</button>
      </td>
    `;
    frag.appendChild(tr);
  }
  tbody.appendChild(frag);

  // Update pagination controls
  $('pgPrev').disabled = (currentPage === 0);
  $('pgNext').disabled = (currentPage >= totalPages - 1);
  $('pgInfo').textContent = filtered.length
    ? `${start+1}-${end} of ${filtered.length}`
    : 'No items';

  updateSortHeaders();
  toast(`Showing ${start+1}-${end} of ${filtered.length} items (${items.length} total)`);
}

function openModal(id=null){
  editingId = id;
  editingVersion = 0;
  if(!id){
    $('modalTitle').textContent='Add Item';
    $('itBrand').value='';
    $('itName').value='';
    $('itQty').value='0';
    $('itAbv').value='';
    $('itRating').value='0';
    $('itTags').value='';
    $('itNotes').value='';
    $('itNeed').value='0';
    $('itRemain').value=String(cfg.remainingPresets[0] ?? 100);
    $('itSize').value=String(cfg.sizesMl[0] ?? 750);
  } else {
    const it = items.find(x=>x.id===id);
    if(!it) return;
    editingVersion = it.version || 0;
    $('modalTitle').textContent='Edit Item';
    $('itType').value = it.type||'';
    $('itBrand').value = it.brand||'';
    $('itName').value = it.name||'';
    $('itSize').value = String(it.sizeMl||750);
    $('itAbv').value = (it.abv ?? '');
    ensureSelectValue($('itAbv'), it.abv);
    $('itQty').value = String(it.qty ?? 0);
    $('itRemain').value = String(it.remainingPct ?? 100);
    $('itNeed').value = it.needToBuy ? '1':'0';
    $('itRating').value = String(it.rating ?? 0);
    $('itTags').value = (it.tags||[]).join(', ');
    $('itNotes').value = it.notes||'';
  }
  $('modalBg').classList.add('show');
}

function closeModal(){ $('modalBg').classList.remove('show'); }

function openView(id){
  const it = items.find(x => String(x.id) === String(id));
  if(!it){ toast('Item not found'); return; }
  $('viewTitle').textContent = 'View Item';
  $('vType').textContent = it.type || '';
  $('vBrand').textContent = it.brand || '';
  $('vName').textContent = it.name || '';
  $('vSize').textContent = String(it.sizeMl ?? '');
  $('vAbv').textContent = (it.abv ?? '') === null ? '' : String(it.abv ?? '');
  $('vQty').textContent = String(it.qty ?? 0);
  $('vRemain').textContent = String(it.remainingPct ?? '');
  $('vNeed').textContent = it.needToBuy ? 'Yes' : 'No';
  $('vRating').textContent = String(it.rating ?? 0);
  $('vTags').textContent = (it.tags||[]).join(', ');
  $('vNotes').textContent = it.notes || '';
  $('viewBg').classList.add('show');
}
function closeView(){ $('viewBg').classList.remove('show'); }


async function saveModal(){
  const btn = $('btnSaveItem');
  btn.disabled = true;
  try {
    const it = {
      id: editingId || '',
      type: $('itType').value,
      brand: $('itBrand').value.trim(),
      name: $('itName').value.trim(),
      sizeMl: parseInt($('itSize').value||'750',10),
      abv: $('itAbv').value.trim() ? parseFloat($('itAbv').value) : null,
      qty: parseInt($('itQty').value||'0',10),
      remainingPct: parseInt($('itRemain').value||'100',10),
      needToBuy: $('itNeed').value==='1',
      rating: parseInt($('itRating').value||'0',10),
      tags: $('itTags').value.split(',').map(s=>s.trim()).filter(Boolean),
      notes: $('itNotes').value.trim()
    };
    // Include version for optimistic concurrency on updates
    if (editingId) it.version = editingVersion;
    // remove null abv (server treats missing)
    if(it.abv===null) delete it.abv;

    if(!it.brand || !it.name) { alert('Brand and Name are required'); return; }

    if(editingId) {
      const r = await fetch('/api/item', {method:'PUT', headers:{'Content-Type':'application/json'}, body:JSON.stringify(it)});
      if (r.status === 409) {
        const j = await r.json();
        alert('This item was modified by another session. Your edit was NOT saved.\nPlease close, refresh, and try again.');
        // Reload so the user sees the current version
        await loadItems();
        closeModal();
        return;
      }
      if (!r.ok) throw new Error(await r.text());
    } else {
      await apiPost('/api/item', it);
    }

    closeModal();
    setTab('inv');
    await loadItems();
  } catch (err) {
    console.error(err);
    alert('Save failed: ' + (err?.message || String(err)));
  } finally {
    btn.disabled = false;
  }
}

function populateAiModels(models, selectedId){
  aiModels = Array.isArray(models) ? models.slice() : [];
  const sel = $('aiModelSelect');
  if(!sel) return;
  const currentManual = $('aiModel').value.trim();
  sel.innerHTML = '<option value="">Manual entry or refresh models...</option>';
  aiModels.forEach(m => {
    const opt = document.createElement('option');
    opt.value = m.id;
    opt.textContent = m.id;
    sel.appendChild(opt);
  });
  const target = selectedId || currentManual || '';
  const found = aiModels.some(m => m.id === target);
  sel.value = found ? target : '';
}

async function refreshAiModels(showToastMsg = true){
  $('aiMsg').textContent = 'Refreshing models...';
  try {
    const data = await apiGet('/api/ai/models');
    populateAiModels(data.models || [], $('aiModel').value.trim());
    if(!aiModels.length) {
      $('aiMsg').textContent = 'No models returned.';
      return;
    }
    if(!$('aiModel').value.trim() && $('aiModelSelect').value) {
      $('aiModel').value = $('aiModelSelect').value;
    }
    $('aiMsg').textContent = `Loaded ${aiModels.length} model(s).`;
    if(showToastMsg) toast(`Loaded ${aiModels.length} AI model(s).`);
  } catch(err) {
    console.error(err);
    populateAiModels([], $('aiModel').value.trim());
    $('aiMsg').textContent = 'Model refresh failed.';
    if(showToastMsg) alert('Model refresh failed: ' + (err?.message || String(err)));
  }
}

async function loadConfig(){
  cfg = await apiGet('/api/config');
  cfg.ai = cfg.ai || {};
  $('cfgTypes').value = (cfg.types||[]).join('\n');
  $('cfgSizes').value = (cfg.sizesMl||[]).join('\n');
  $('cfgAbv').value = (cfg.abvPresets||[]).join('\n');
  $('cfgRemain').value = (cfg.remainingPresets||[]).join('\n');
  $('aiEnabled').value = cfg.ai.enabled ? '1' : '0';
  $('aiBaseUrl').value = cfg.ai.baseUrl || '';
  $('aiModel').value = cfg.ai.model || '';
  populateAiModels(cfg.ai.availableModels || [], cfg.ai.model || '');
  $('aiApiKey').value = cfg.ai.apiKey || '';
  $('aiSystemPrompt').value = cfg.ai.systemPrompt || '';
  $('aiTemperature').value = String(cfg.ai.temperature ?? 0.2);
  $('aiMaxTokens').value = String(cfg.ai.maxTokens ?? 8192);
  $('aiTimeoutSec').value = String(cfg.ai.timeoutSec ?? 180);
  $('aiDisableThinking').value = (cfg.ai.disableThinking !== false) ? '1' : '0';

  // Load AI modes
  cfgModes = Array.isArray(cfg.ai.modes) && cfg.ai.modes.length > 0
    ? cfg.ai.modes.map(m => ({
        id: m.id || '',
        label: m.label || '',
        instruction: m.instruction || '',
        systemPrompt: m.systemPrompt || '',
        restrictToInventory: m.restrictToInventory !== false
      }))
    : [
        { id:'general', label:'General', instruction:'Answer the question freely.', systemPrompt:'', restrictToInventory:false },
        { id:'can_make_now', label:'Can Make Now', instruction:'List cocktails that can be made immediately from current inventory only.', systemPrompt:'', restrictToInventory:true },
        { id:'missing_ingredients', label:'Missing Ingredients', instruction:'Focus on drinks that are nearly possible. List what is missing.', systemPrompt:'', restrictToInventory:true },
        { id:'recommend_purchases', label:'Recommend Purchases', instruction:'Recommend highest-value purchases that unlock the most cocktails.', systemPrompt:'', restrictToInventory:true },
        { id:'shopping_list', label:'Shopping List', instruction:'Build a practical shopping list grouped into bottles, mixers, citrus, syrups, extras.', systemPrompt:'', restrictToInventory:true }
      ];
  renderModes();
  populateAiModeDropdown();

  applyConfigToForm();
}

function parseLines(text, kind){
  const lines = text.split(/\r?\n/).map(s=>s.trim()).filter(Boolean);
  if(kind==='int') return lines.map(x=>parseInt(x,10)).filter(x=>Number.isFinite(x));
  if(kind==='float') return lines.map(x=>parseFloat(x)).filter(x=>Number.isFinite(x));
  return lines;
}

async function saveConfig(){
  const newCfg = {
    types: parseLines($('cfgTypes').value,'str'),
    sizesMl: parseLines($('cfgSizes').value,'int'),
    abvPresets: parseLines($('cfgAbv').value,'float'),
    remainingPresets: parseLines($('cfgRemain').value,'int'),
    ai: {
      enabled: $('aiEnabled').value === '1',
      baseUrl: $('aiBaseUrl').value.trim(),
      model: ($('aiModelSelect').value || $('aiModel').value).trim(),
      apiKey: $('aiApiKey').value,
      systemPrompt: $('aiSystemPrompt').value.trim(),
      temperature: parseFloat($('aiTemperature').value || '0.2'),
      maxTokens: parseInt($('aiMaxTokens').value || '8192', 10),
      timeoutSec: parseInt($('aiTimeoutSec').value || '180', 10),
      disableThinking: $('aiDisableThinking').value === '1',
      modes: cfgModes.map(m => ({
        id: m.id,
        label: m.label,
        instruction: m.instruction,
        systemPrompt: m.systemPrompt || '',
        restrictToInventory: !!m.restrictToInventory
      }))
    }
  };
  await apiPut('/api/config', newCfg);
  $('cfgMsg').textContent='Saved.';
  await loadConfig();
}

let _showingRaw = false;

function openAiResponse(title, text, meta){
  lastAiResponse = text || '';
  _showingRaw = false;

  // Build meta line
  let metaText = '';
  if(meta){
    const parts = [];
    if(meta.modeUsed) parts.push(`Mode: ${meta.modeUsed}`);
    if(meta.inventoryIncluded) parts.push(`Inventory: ${meta.inventoryChars||0} chars${meta.inventoryTruncated ? ' (truncated)' : ''}${meta.inventoryCacheHit ? ' / cache hit' : ''}`);
    metaText = parts.join(' | ');
  }
  $('aiRespMeta').textContent = metaText;
  $('aiRespMeta').style.display = metaText ? 'block' : 'none';

  $('aiRespTitle').textContent = title || 'AI Response';
  $('aiRespRendered').innerHTML = mdToHtml(lastAiResponse);
  $('btnToggleRaw').textContent = 'View Raw';
  $('aiRespBg').classList.add('show');
}

function toggleRawView(){
  _showingRaw = !_showingRaw;
  if(_showingRaw){
    $('aiRespRendered').innerHTML = '<pre style="white-space:pre-wrap;word-break:break-word;margin:0;font-size:13px;">' + esc(lastAiResponse) + '</pre>';
    $('btnToggleRaw').textContent = 'View Formatted';
  } else {
    $('aiRespRendered').innerHTML = mdToHtml(lastAiResponse);
    $('btnToggleRaw').textContent = 'View Raw';
  }
}

function closeAiResponse(){ $('aiRespBg').classList.remove('show'); }

async function waitForAiJob(token, label){
  const started = Date.now();
  while((Date.now() - started) < 180000){
    const r = await fetch('/api/ai/status', {cache:'no-store'});
    const j = await r.json();
    if(j.token === token){
      if(j.done && !j.busy) return j;
      if(j.busy || !j.done){
        await new Promise(res=>setTimeout(res, 750));
        continue;
      }
    }
    await new Promise(res=>setTimeout(res, 750));
  }
  throw new Error((label || 'AI request') + ' timed out');
}

async function testAi(){
  if($('aiModelSelect').value) $('aiModel').value = $('aiModel').value || $('aiModelSelect').value;
  $('aiMsg').textContent = 'Testing...';
  $('btnTestAi').disabled = true;
  showBusy('Testing AI connection...');
  try {
    try {
      const net = await apiGet('/api/net');
      if(!net?.sta?.connected){
        hideBusy();
        $('aiMsg').textContent = 'STA not connected.';
        alert('AI test cannot run because STA/Wi-Fi is not connected to your LAN yet. Connect STA first, then retry.');
        return;
      }
    } catch(_) {}
    const r = await fetch('/api/ai/test', {method:'POST'});
    const j = await r.json();
    if(!r.ok || !j.ok || !j.started || !j.token) throw new Error(j.error || 'AI test failed to start');
    $('aiMsg').textContent = 'Testing in background...';
    const done = await waitForAiJob(j.token, 'AI test');
    if(!done.ok) throw new Error(done.error || 'AI test failed');
    hideBusy();
    $('aiMsg').textContent = 'Connected.';
    openAiResponse('LM Studio Test', done.answer || 'Connection OK', done);
  } catch(err) {
    hideBusy();
    $('aiMsg').textContent = 'Failed.';
    alert('AI test failed: ' + (err?.message || String(err)));
  } finally {
    $('btnTestAi').disabled = false;
  }
}

async function askAi(){
  const question = $('aiQuestion').value.trim();
  if(!question){ alert('Enter an AI question first.'); return; }
  const btn = $('btnAskAi');
  btn.disabled = true;
  showBusy('AI is thinking...');
  try {
    toast('Sending question to LM Studio...');
    const r = await fetch('/api/ai/ask', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({
        question,
        includeInventory: $('aiIncludeInventory').checked,
        mode: $('aiMode').value
      })
    });
    const j = await r.json();
    if(!r.ok || !j.ok || !j.started || !j.token) throw new Error(j.error || 'AI request failed to start');
    toast('AI request running...');
    const done = await waitForAiJob(j.token, 'AI request');
    if(!done.ok) throw new Error(done.error || 'AI request failed');
    hideBusy();
    openAiResponse('AI Response', done.answer || '', done);
    toast('AI response received.');
  } catch(err) {
    hideBusy();
    console.error(err);
    alert('AI request failed: ' + (err?.message || String(err)));
    toast('AI request failed.');
  } finally {
    btn.disabled = false;
  }
}

function exportAiResponse(){
  const text = lastAiResponse || '';
  const blob = new Blob([text], {type:'text/plain;charset=utf-8'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'libationlocker-ai-response.txt';
  document.body.appendChild(a);
  a.click();
  setTimeout(()=>{ URL.revokeObjectURL(a.href); a.remove(); }, 100);
}

async function copyAiResponse(){
  const text = lastAiResponse || '';
  try {
    await navigator.clipboard.writeText(text);
    toast('AI response copied.');
  } catch(e) {
    // Fallback: create temporary textarea for execCommand
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    document.execCommand('copy');
    ta.remove();
    toast('AI response copied.');
  }
}

async function loadItems(){
  const tbody = $('tbody');
  if (tbody) tbody.innerHTML = '<tr><td colspan="10" class="sub">Loading inventory...</td></tr>';
  showBusy('Loading inventory...');
  try {
    items = await apiGet('/api/items');
    currentPage = 0;
    render();
  } catch(err) {
    items = [];
    if (tbody) tbody.innerHTML = `<tr><td colspan="10" class="sub">Inventory load failed: ${esc(err?.message || String(err))}</td></tr>`;
    throw err;
  } finally {
    hideBusy();
  }
}

function download(url){
  const a=document.createElement('a');
  a.href=url;
  a.download='';
  document.body.appendChild(a);
  a.click();
  a.remove();
}

async function importJson(){
  const f = $('importFile').files[0];
  if(!f) { $('impMsg').textContent='Choose a JSON file first.'; return; }
  const txt = await f.text();
  const r = await fetch('/api/import', {method:'POST', headers:{'Content-Type':'application/json'}, body:txt});
  if(!r.ok) {
    let msg='Import failed.';
    try{ const t=await r.text(); const j=JSON.parse(t); if(j&&j.error) msg='Import failed: '+j.error; }catch(e){}
    $('impMsg').textContent=msg;
    return;
  }
  $('impMsg').textContent='Imported.';
  await loadItems();
}

async function loadWifi(){
  const net = await apiGet('/api/net');
  $('wifiEnabled').value = net.cfg.enabled ? '1':'0';
  $('wifiSsid').value = net.cfg.ssid || '';
  $('wifiStatus').innerHTML = net.sta.connected ? `STA <small>${esc(net.sta.ip)}</small>` : `AP <small>${esc(net.ap.ip)}</small>`;
}

async function saveWifi(){
  const enabled = $('wifiEnabled').value==='1';
  const ssid = $('wifiSsid').value.trim();
  const pass = $('wifiPass').value;
  await apiPut('/api/net', {enabled, ssid, pass});
  $('wifiMsg').textContent = 'Saved. Connecting...';
  setTimeout(loadWifi, 1200);
}

async function refreshBadges(){
  try {
    const net = await apiGet('/api/net');
    const ip = net.sta.connected ? net.sta.ip : net.ap.ip;
    $('ipBadge').innerHTML = `IP: <span>${esc(ip)}</span>`;

    $('wifiStatus').innerHTML = net.sta.connected ? `STA <small>${esc(net.sta.ip)}</small>` : `AP <small>${esc(net.ap.ip)}</small>`;
  } catch(e){}

  try {
    const st = await apiGet('/api/storage');
    const kb = Math.round(st.freeBytes/1024);
    let badge = `Storage: <span>${kb} KB free</span> <small>(~${st.estItemsLeft} items left)`;
    if (st.psramFreeBytes != null) {
      badge += ` | PSRAM ${Math.round(st.psramFreeBytes/1024)} KB`;
    }
    badge += `</small>`;
    $('fsBadge').innerHTML = badge;
  } catch(e){}
}

function wire(){
  $('tabInv').onclick = ()=>setTab('inv');
  $('tabCfg').onclick = ()=>{ setTab('cfg'); loadWifi().catch(()=>{}); refreshBadges().catch(()=>{}); };
  $('tabImp').onclick = ()=>setTab('imp');

  $('btnAdd').onclick = ()=>openModal(null);
  $('btnRefresh').onclick = ()=>loadItems();

  // Debounced search (150ms) — resets to page 0
  const debouncedRender = debounce(()=>{ currentPage=0; render(); }, 150);
  $('q').oninput = debouncedRender;
  $('filterType').onchange = ()=>{ currentPage=0; render(); };
  $('filterNeed').onchange = ()=>{ currentPage=0; render(); };

  // Pagination buttons
  $('pgPrev').onclick = ()=>{ if(currentPage>0){ currentPage--; render(); } };
  $('pgNext').onclick = ()=>{ currentPage++; render(); };

  // Sortable column headers (delegate from thead)
  $('tbl').querySelector('thead').onclick = (ev)=>{
    const th = ev.target.closest('th[data-sort]');
    if(th) sortBy(th.dataset.sort);
  };

  // Table body click delegation (wired ONCE, not every render)
  $('tbl').onclick = async (ev)=>{
    const b = ev.target.closest('button');
    if(!b) return;
    const id = b.getAttribute('data-id');
    const act = b.getAttribute('data-act');
    if(act==='view') openView(id);
    if(act==='edit') openModal(id);
    if(act==='del') {
      if(!confirm('Delete this item?')) return;
      await apiDel('/api/item?id='+encodeURIComponent(id));
      await loadItems();
    }
  };

  $('btnCancel').onclick = closeModal;
  $('modalBg').onclick = (e)=>{ if(e.target === $('modalBg')) closeModal(); };
  $('btnSaveItem').onclick = saveModal;
  $('btnViewClose').onclick = closeView;
  $('viewBg').onclick = (e)=>{ if(e.target === $('viewBg')) closeView(); };

  $('btnSaveCfg').onclick = saveConfig;
  $('btnAddMode').onclick = addNewMode;
  $('btnRefreshAiModels').onclick = ()=>refreshAiModels(true);
  $('aiModelSelect').onchange = ()=>{ if($('aiModelSelect').value) $('aiModel').value = $('aiModelSelect').value; };
  $('aiBaseUrl').addEventListener('change', ()=>populateAiModels([], $('aiModel').value.trim()));
  $('btnTestAi').onclick = testAi;
  $('btnAskAi').onclick = askAi;
  $('aiQuestion').addEventListener('keydown', (e)=>{ if(e.key==='Enter'){ e.preventDefault(); askAi(); } });

  $('btnExportJson').onclick = ()=>download('/api/export?format=json');
  $('btnExportCsv').onclick = ()=>download('/api/export?format=csv');
  $('btnExportNeed').onclick = ()=>download('/api/export?format=txt&filter=need');
  $('btnImport').onclick = importJson;

  $('btnSaveWifi').onclick = saveWifi;

  $('btnToggleRaw').onclick = toggleRawView;
  $('btnCopyAiResp').onclick = copyAiResponse;
  $('btnExportAiResp').onclick = exportAiResponse;
  $('btnCloseAiResp').onclick = closeAiResponse;
  $('aiRespBg').onclick = (e)=>{ if(e.target === $('aiRespBg')) closeAiResponse(); };
}

async function init(){
  wire();

  // Start lightweight status/UI hydration first so Config and badges populate
  // even if inventory takes longer to arrive.
  const configPromise = loadConfig().catch(err=>{ console.error('loadConfig failed', err); throw err; });
  const wifiPromise = loadWifi().catch(err=>{ console.error('loadWifi failed', err); });
  const badgesPromise = refreshBadges().catch(err=>{ console.error('refreshBadges failed', err); });

  await configPromise;

  if(($('aiEnabled').value === '1') && $('aiBaseUrl').value.trim()) {
    refreshAiModels(false).catch(e=>console.error('refreshAiModels failed', e));
  }

  // Do not block page readiness on the inventory fetch.
  loadItems().catch(err=>{
    console.error('loadItems failed', err);
    toast('Inventory load failed.');
  });

  await Promise.allSettled([wifiPromise, badgesPromise]);

  // Poll every 10s instead of 3s — reduces load on async web server
  setInterval(refreshBadges, 10000);
  setInterval(loadWifi, 15000);
}

init().catch(err=>{ console.error(err); });
</script>
</body></html>
)LL";
