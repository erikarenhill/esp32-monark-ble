// Live dashboard for monark-dev-bridge. Subscribes to /api/stream, renders three tiles,
// a connection status row, a 60s overlaid uPlot chart, and a scan/bind panel.

const WINDOW_S = 30;

const series = {
  t: [],
  real: [],
  esp: [],
  realRpm: [],
  espRpm: [],
  hr: [],
};

const chart = new uPlot({
  width: document.getElementById('chart').clientWidth,
  height: 240,
  scales: { x: { time: true }, y: { auto: true } },
  series: [
    {},
    { label: 'Real (W)', stroke: '#58a6ff', width: 2 },
    { label: 'ESP32 (W)', stroke: '#f0883e', width: 2 },
  ],
  axes: [{ stroke: '#7d8590' }, { stroke: '#7d8590' }],
}, [series.t, series.real, series.esp], document.getElementById('chart'));

const rpmChart = new uPlot({
  width: document.getElementById('chart-rpm').clientWidth,
  height: 200,
  scales: { x: { time: true }, y: { auto: true } },
  series: [
    {},
    { label: 'Real (rpm)', stroke: '#56d364', width: 2 },
    { label: 'ESP32 (rpm)', stroke: '#d4a72c', width: 2 },
  ],
  axes: [{ stroke: '#7d8590' }, { stroke: '#7d8590' }],
}, [series.t, series.realRpm, series.espRpm], document.getElementById('chart-rpm'));

const hrChart = new uPlot({
  width: document.getElementById('chart-hr').clientWidth,
  height: 200,
  scales: { x: { time: true }, y: { auto: true } },
  series: [
    {},
    { label: 'HR (bpm)', stroke: '#f85149', width: 2 },
  ],
  axes: [{ stroke: '#7d8590' }, { stroke: '#7d8590' }],
}, [series.t, series.hr], document.getElementById('chart-hr'));

window.addEventListener('resize', () => {
  chart.setSize({ width: document.getElementById('chart').clientWidth, height: 240 });
  rpmChart.setSize({ width: document.getElementById('chart-rpm').clientWidth, height: 200 });
  hrChart.setSize({ width: document.getElementById('chart-hr').clientWidth, height: 200 });
});

function pushPoint(t, real, esp, realRpm, espRpm, hr) {
  series.t.push(t);
  series.real.push(real);
  series.esp.push(esp);
  series.realRpm.push(realRpm);
  series.espRpm.push(espRpm);
  series.hr.push(hr);
  const cutoff = t - WINDOW_S;
  while (series.t.length && series.t[0] < cutoff) {
    series.t.shift();
    series.real.shift(); series.esp.shift();
    series.realRpm.shift(); series.espRpm.shift();
    series.hr.shift();
  }
  chart.setData([series.t, series.real, series.esp]);
  rpmChart.setData([series.t, series.realRpm, series.espRpm]);
  hrChart.setData([series.t, series.hr]);
}

function fmt(v, digits = 0) {
  return (v === null || v === undefined) ? '—' : Number(v).toFixed(digits);
}

function updateTile(id, sample, suffix = 'W') {
  const el = document.getElementById(id);
  const valueEl = el.querySelector('.value');
  const subEl = el.querySelector('.sub');
  if (!sample) { valueEl.textContent = '—'; subEl.textContent = ''; el.classList.remove('stale'); return; }
  el.classList.toggle('stale', !!sample.stale);
  if (sample.power_w !== null && sample.power_w !== undefined) {
    valueEl.textContent = fmt(sample.power_w);
    const cad = sample.cadence_rpm;
    subEl.textContent = `${suffix}${cad !== null && cad !== undefined ? '  ·  ' + fmt(cad) + ' rpm' : ''}  ·  ${fmt(sample.age_s, 1)}s ago`;
  } else if (sample.hr_bpm !== null && sample.hr_bpm !== undefined) {
    valueEl.textContent = sample.hr_bpm;
    subEl.textContent = `bpm  ·  ${fmt(sample.age_s, 1)}s ago`;
  }
}

function updateConn(role, info) {
  const el = document.getElementById(`conn-${role}`);
  if (!el) return;
  const badge = el.querySelector('.badge');
  badge.className = 'badge ' + (info?.state || 'disconnected');
  badge.textContent = info?.state || 'unconfigured';
  el.querySelector('.addr').textContent = info ? ` · ${info.address}` : '';
}

function applyState(s) {
  updateTile('tile-real', s.latest.real_power);
  updateTile('tile-esp', s.latest.esp32);
  updateTile('tile-hr', s.latest.hr);
  updateRpmTile('tile-real-rpm', s.latest.real_power);
  updateRpmTile('tile-esp-rpm', s.latest.esp32);

  const diffEl = document.getElementById('tile-diff');
  diffEl.querySelector('.value').textContent = (s.diff_w === null) ? '—' : (s.diff_w > 0 ? '+' : '') + fmt(s.diff_w);
  const diffRpmEl = document.getElementById('tile-diff-rpm');
  diffRpmEl.querySelector('.value').textContent = (s.diff_rpm === null || s.diff_rpm === undefined)
    ? '—' : (s.diff_rpm > 0 ? '+' : '') + fmt(s.diff_rpm);

  const t = s.updated_at;
  const real = s.latest.real_power?.power_w ?? null;
  const esp = s.latest.esp32?.power_w ?? null;
  const realRpm = s.latest.real_power?.cadence_rpm ?? null;
  const espRpm = s.latest.esp32?.cadence_rpm ?? null;
  const hr = s.latest.hr?.hr_bpm ?? null;
  pushPoint(t, real, esp, realRpm, espRpm, hr);

  for (const role of ['real_power', 'hr', 'esp32']) {
    updateConn(role, s.connections[role]);
  }
}

function updateRpmTile(id, sample) {
  const el = document.getElementById(id);
  const valueEl = el.querySelector('.value');
  const subEl = el.querySelector('.sub');
  if (!sample || sample.cadence_rpm === null || sample.cadence_rpm === undefined) {
    valueEl.textContent = '—';
    subEl.textContent = 'rpm';
    el.classList.remove('stale');
    return;
  }
  el.classList.toggle('stale', !!sample.stale);
  valueEl.textContent = fmt(sample.cadence_rpm);
  subEl.textContent = `rpm  ·  ${fmt(sample.age_s, 1)}s ago`;
}

function connectWS() {
  const ws = new WebSocket(`ws://${location.host}/api/stream`);
  ws.onmessage = ev => applyState(JSON.parse(ev.data));
  ws.onclose = () => setTimeout(connectWS, 1500);
  ws.onerror = () => ws.close();
}
connectWS();

// ---- Scan / bind ----

const scanBtn = document.getElementById('scan-btn');
const scanStatus = document.getElementById('scan-status');
const scanTable = document.getElementById('scan-table');
const scanTbody = scanTable.querySelector('tbody');

scanBtn.onclick = async () => {
  scanBtn.disabled = true;
  scanStatus.textContent = 'Scanning… (10s)';
  scanTable.style.display = 'none';
  try {
    const r = await fetch('/api/scan?duration=10', { method: 'POST' });
    const data = await r.json();
    renderScan(data.results || []);
    scanStatus.textContent = `Found ${data.results.length} device(s)`;
  } catch (e) {
    scanStatus.textContent = 'Scan failed: ' + e;
  } finally {
    scanBtn.disabled = false;
  }
};

function renderScan(results) {
  scanTbody.innerHTML = '';
  for (const d of results) {
    const tr = document.createElement('tr');
    const services = [d.offers_cps && 'CPS', d.offers_hr && 'HR'].filter(Boolean).join(', ') || '(none in adv)';
    tr.innerHTML = `
      <td>${escape(d.name)}</td>
      <td><code>${escape(d.address)}</code></td>
      <td>${d.rssi ?? ''}</td>
      <td>${services}</td>
      <td>
        <button data-role="real_power">real_power</button>
        <button data-role="hr">hr</button>
        <button data-role="esp32">esp32</button>
      </td>`;
    tr.querySelectorAll('button').forEach(btn => {
      btn.onclick = () => bind(btn.dataset.role, d.address, d.name);
    });
    scanTbody.appendChild(tr);
  }
  scanTable.style.display = 'table';
}

async function bind(role, address, name) {
  const r = await fetch('/api/bind', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ role, address, name }),
  });
  if (r.ok) {
    scanStatus.textContent = `Bound ${role} → ${address}`;
  } else {
    scanStatus.textContent = `Bind failed: ${r.status}`;
  }
}

function escape(s) {
  return String(s).replace(/[&<>"']/g, c => ({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;' }[c]));
}

// ---- Cycle-constant tuning ----

const calBtn = document.getElementById('cal-btn');
const calStatus = document.getElementById('cal-status');
const calResult = document.getElementById('cal-result');

calBtn.onclick = async () => {
  const current = parseFloat(document.getElementById('cal-current').value);
  const duration = parseInt(document.getElementById('cal-duration').value, 10);
  if (!isFinite(current) || !isFinite(duration)) {
    calStatus.textContent = 'invalid input';
    return;
  }
  calBtn.disabled = true;
  calResult.style.display = 'none';
  // Live countdown
  let remaining = duration;
  calStatus.textContent = `pedal steady at any load… ${remaining}s remaining`;
  const tick = setInterval(() => {
    remaining -= 1;
    if (remaining > 0) calStatus.textContent = `pedal steady at any load… ${remaining}s remaining`;
  }, 1000);
  try {
    const r = await fetch('/api/calibrate/cycle-constant', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ duration_s: duration, current_constant: current }),
    });
    const data = await r.json();
    clearInterval(tick);
    if (!data.ok) {
      calStatus.textContent = `failed: ${data.error || r.status}`;
      return;
    }
    calStatus.textContent = `done — ${data.real_samples} real / ${data.esp32_samples} esp32 samples`;
    calResult.style.display = 'block';
    calResult.innerHTML = `
      <div style="display:grid; grid-template-columns: 1fr 1fr; gap:6px 14px;">
        <div style="color:#7d8590">Real pedals avg</div><div><b>${data.real_avg_w} W</b></div>
        <div style="color:#7d8590">ESP32 avg</div><div><b>${data.esp32_avg_w} W</b></div>
        <div style="color:#7d8590">Ratio (real / esp32)</div><div>${data.ratio_real_over_esp32}</div>
        <div style="color:#7d8590">Current cycle_constant</div><div>${data.current_constant}</div>
        <div style="color:#7d8590">Suggested cycle_constant</div><div style="color:#56d364; font-size:18px"><b>${data.suggested_constant}</b></div>
      </div>
      <div style="margin-top:10px; color:#7d8590; font-size:12px;">${escape(data.note || '')}</div>
    `;
  } catch (e) {
    clearInterval(tick);
    calStatus.textContent = 'error: ' + e;
  } finally {
    calBtn.disabled = false;
  }
};

// ---- Assistant notes (chat) ------------------------------------------------
// Polls /api/notes every 2s. Newest message on top. New messages flash briefly
// so they catch the eye while pedaling.

const notesList = document.getElementById('notes-list');
const notesEmpty = document.getElementById('notes-empty');
let lastNoteTs = 0;
const seenTs = new Set();

function fmtClock(ts) {
  const d = new Date(ts * 1000);
  return d.toTimeString().slice(0, 8);
}

function renderNote(note, isFresh) {
  const el = document.createElement('div');
  el.className = 'note ' + (note.kind || 'info') + (isFresh ? ' fresh' : '');
  const ts = document.createElement('span'); ts.className = 'ts';   ts.textContent = fmtClock(note.ts);
  const k  = document.createElement('span'); k.className  = 'kind'; k.textContent  = (note.kind || 'info').toUpperCase();
  const t  = document.createElement('span'); t.className  = 'text'; t.textContent  = note.text;
  el.append(ts, k, t);
  return el;
}

async function pollNotes() {
  try {
    const res = await fetch('/api/notes?since=' + lastNoteTs);
    const data = await res.json();
    const fresh = (data.notes || []).filter(n => !seenTs.has(n.ts));
    if (fresh.length === 0) return;
    if (notesEmpty && notesEmpty.parentNode) notesEmpty.remove();
    // Newest on top — sort desc, prepend.
    fresh.sort((a, b) => b.ts - a.ts);
    for (const n of fresh) {
      seenTs.add(n.ts);
      lastNoteTs = Math.max(lastNoteTs, n.ts);
      notesList.prepend(renderNote(n, /*isFresh=*/true));
    }
    // Keep DOM bounded.
    while (notesList.children.length > 60) notesList.lastElementChild.remove();
  } catch (e) {
    // Silent — bridge may be restarting; next poll picks up.
  }
}
pollNotes();
setInterval(pollNotes, 2000);
