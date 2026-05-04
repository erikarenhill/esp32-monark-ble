// Live dashboard for monark-dev-bridge. Subscribes to /api/stream, renders three tiles,
// a connection status row, a 60s overlaid uPlot chart, and a scan/bind panel.

const WINDOW_S = 60;

const series = {
  t: [],
  real: [],
  esp: [],
};

const chart = new uPlot({
  width: document.getElementById('chart').clientWidth,
  height: 280,
  scales: { x: { time: true }, y: { auto: true } },
  series: [
    {},
    { label: 'Real (W)', stroke: '#58a6ff', width: 2 },
    { label: 'ESP32 (W)', stroke: '#f0883e', width: 2 },
  ],
  axes: [
    { stroke: '#7d8590' },
    { stroke: '#7d8590' },
  ],
}, [series.t, series.real, series.esp], document.getElementById('chart'));

window.addEventListener('resize', () => chart.setSize({
  width: document.getElementById('chart').clientWidth, height: 280
}));

function pushPoint(t, real, esp) {
  series.t.push(t);
  series.real.push(real);
  series.esp.push(esp);
  const cutoff = t - WINDOW_S;
  while (series.t.length && series.t[0] < cutoff) {
    series.t.shift(); series.real.shift(); series.esp.shift();
  }
  chart.setData([series.t, series.real, series.esp]);
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
  const diffEl = document.getElementById('tile-diff');
  diffEl.querySelector('.value').textContent = (s.diff_w === null) ? '—' : (s.diff_w > 0 ? '+' : '') + fmt(s.diff_w);

  const t = s.updated_at;
  const real = s.latest.real_power?.power_w ?? null;
  const esp = s.latest.esp32?.power_w ?? null;
  pushPoint(t, real, esp);

  for (const role of ['real_power', 'hr', 'esp32']) {
    updateConn(role, s.connections[role]);
  }
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
