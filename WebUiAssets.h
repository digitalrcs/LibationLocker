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
  input, select{
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
  input::placeholder{ color: rgba(233,243,255,.55); }

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

    /* Switch schedule view: cards instead of table */
    .tablewrap{ display:none; }
    .cards{ display:block; }
    table{ display:none; }

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
.modalBackdrop{ position:fixed; inset:0; background:rgba(0,0,0,.78); display:none; align-items:center; justify-content:center; padding:16px; }
.modalBackdrop.show{ display:flex; }
.modal{ width:min(720px, 100%); }
.card.modal{ flex:0 0 auto; }
  .card.modal{ background:rgba(7,26,51,.96); border:1px solid rgba(255,255,255,.20); }

.grid2{ display:grid; grid-template-columns: 1fr 1fr; gap:12px; }
@media (max-width: 720px){ .grid2{ grid-template-columns:1fr; } }

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
              <th>Type</th><th>Brand</th><th>Name</th><th>Size</th><th>ABV</th><th>Qty</th>
              <th>Remain%</th><th>Need</th><th>Rating</th><th>Tags</th><th></th>
            </tr>
          </thead>
          <tbody id="tbody"></tbody>
        </table>
      </div>

      <div style="height:8px"></div>
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

      <div style="height:12px"></div>
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
        <div>
        </div>
        <div>
          <div class="sub">SSID</div>
          <input class="in" id="wifiSsid" placeholder="Your Wi‑Fi name" />
        </div>
        <div>
        </div>
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

<script>
'use strict';

const $ = (id)=>document.getElementById(id);

let cfg = { types:[], sizesMl:[], abvPresets:[], remainingPresets:[] };
let items = [];
let editingId = null;

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

function render(){
  const tbody = $('tbody');
  tbody.innerHTML='';
  const filtered = items.filter(itemMatchesFilters);

  for(const it of filtered){
    const tr=document.createElement('tr');
    const tags = (it.tags||[]).join(', ');
    tr.innerHTML = `
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
    tbody.appendChild(tr);
  }

  $('tbl').onclick = async (ev)=>{
    const b = ev.target.closest('button');
    if(!b) return;
    const id = b.getAttribute('data-id');
    const act = b.getAttribute('data-act');
    if(act==='edit') openModal(id);
    if(act==='del') {
      if(!confirm('Delete this item?')) return;
      await apiDel('/api/item?id='+encodeURIComponent(id));
      await loadItems();
    }
  };

  toast(`Showing ${filtered.length} of ${items.length} items`);
}

function openModal(id=null){
  editingId = id;
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
    // remove null abv (server treats missing)
    if(it.abv===null) delete it.abv;

    if(!it.brand || !it.name) { alert('Brand and Name are required'); return; }

    if(editingId) await apiPut('/api/item', it);
    else await apiPost('/api/item', it);

    // UX: close modal immediately, switch to inventory view, then refresh the grid.
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

async function loadConfig(){
  cfg = await apiGet('/api/config');
  $('cfgTypes').value = (cfg.types||[]).join('\n');
  $('cfgSizes').value = (cfg.sizesMl||[]).join('\n');
  $('cfgAbv').value = (cfg.abvPresets||[]).join('\n');
  $('cfgRemain').value = (cfg.remainingPresets||[]).join('\n');
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
  };
  await apiPut('/api/config', newCfg);
  $('cfgMsg').textContent='Saved.';
  await loadConfig();
}

async function loadItems(){
  items = await apiGet('/api/items');
  render();
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
    $('fsBadge').innerHTML = `Storage: <span>${kb} KB free</span> <small>(~${st.estItemsLeft} items)</small>`;
  } catch(e){}
}

function wire(){
  $('tabInv').onclick = ()=>setTab('inv');
  $('tabCfg').onclick = ()=>setTab('cfg');
  $('tabImp').onclick = ()=>setTab('imp');

  $('btnAdd').onclick = ()=>openModal(null);
  $('btnRefresh').onclick = ()=>loadItems();

  $('q').oninput = render;
  $('filterType').onchange = render;
  $('filterNeed').onchange = render;

  $('btnCancel').onclick = closeModal;
  $('modalBg').onclick = (e)=>{ if(e.target === $('modalBg')) closeModal(); };
  $('btnSaveItem').onclick = saveModal;

  $('btnSaveCfg').onclick = saveConfig;

  $('btnExportJson').onclick = ()=>download('/api/export?format=json');
  $('btnExportCsv').onclick = ()=>download('/api/export?format=csv');
  $('btnExportNeed').onclick = ()=>download('/api/export?format=txt&filter=need');
  $('btnImport').onclick = importJson;

  $('btnSaveWifi').onclick = saveWifi;
}

async function init(){
  wire();
  await loadConfig();
  await loadItems();
  await loadWifi();
  await refreshBadges();
  setInterval(refreshBadges, 3000);
}

init().catch(err=>{ console.error(err); });
</script>
</body></html>
)LL";
