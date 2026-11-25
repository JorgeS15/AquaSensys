// History chart on the dashboard.
//
// Drawn from the unit's own logs on the SD card (one CSV a day, written by the
// firmware every log_interval_minutes), so it works without Home Assistant or
// the internet. The day files are fetched once each and kept; today's file is
// fetched again every few minutes while the page is open.

(() => {
  const card = document.getElementById('historyCard');
  if (!card) return;

  const svg = document.getElementById('histSvg');
  const plot = document.getElementById('histPlot');
  const tip = document.getElementById('histTip');
  const empty = document.getElementById('histEmpty');
  const legend = document.getElementById('histLegend');
  const note = document.getElementById('histNote');
  const csvLink = document.getElementById('histCsv');
  const NS = 'http://www.w3.org/2000/svg';
  const REFRESH_MS = 5 * 60 * 1000;

  // What each tab draws. Colours are the --series-N tokens, in this order.
  const METRICS = {
    pressure: { unit: 'bar', decimals: 2, zero: true,
                series: [{ col: 'pressure_in', key: 'beforeFilter', alt: 'pressure' },
                         { col: 'pressure_out', key: 'afterFilter', dualOnly: true }] },
    flow:     { unit: 'L/min', decimals: 1, zero: true, series: [{ col: 'flow_lpm', key: 'flowRate' }] },
    temp:     { unit: '°C', decimals: 1, zero: false, series: [{ col: 'water_temp', key: 'waterTemperature' }] },
    current:  { unit: 'A', decimals: 1, zero: true,
                series: [{ col: 'current_l1', label: 'L1' }, { col: 'current_l2', label: 'L2' },
                         { col: 'current_l3', label: 'L3' }] }
  };

  const view = { hours: 24, metric: 'pressure', loaded: false };
  const days = new Map();   // 'YYYY-MM-DD' -> { rows, fetchedAt } (rows: [] when missing)
  let points = [];          // rows in the current window, oldest first
  let geometry = null;      // what the pointer handler needs to map x back to a row

  const pad2 = n => String(n).padStart(2, '0');
  const dayKey = d => `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`;
  const dayPath = key => `/logs/${key.slice(0, 4)}/${key}.csv`;
  const lang = () => (typeof currentLanguage === 'string' && currentLanguage === 'pt') ? 'pt-PT' : 'en-GB';

  function parseCsv(text) {
    const lines = text.trim().split('\n');
    if (lines.length < 2) return [];
    const cols = lines[0].trim().split(',');
    const at = name => cols.indexOf(name);
    const iEpoch = at('epoch');
    if (iEpoch < 0) return [];
    const rows = [];
    for (let i = 1; i < lines.length; i++) {
      const f = lines[i].split(',');
      const epoch = Number(f[iEpoch]);
      if (!epoch) continue;                    // written before the clock was set
      const row = { t: epoch * 1000 };
      for (const name of cols) {
        const v = f[at(name)];
        row[name] = v === '' || v === undefined ? NaN : Number(v);
      }
      rows.push(row);
    }
    return rows;
  }

  async function loadDay(key, force) {
    const cached = days.get(key);
    if (cached && !force) return cached.rows;
    try {
      const response = await fetch(dayPath(key), { cache: 'no-store' });
      const rows = response.ok ? parseCsv(await response.text()) : [];
      days.set(key, { rows, fetchedAt: Date.now() });
      return rows;
    } catch (err) {
      return cached ? cached.rows : [];
    }
  }

  // The unit names its files by its own local date; one extra day either side
  // covers a browser in another time zone
  function dayKeysFor(hours) {
    const keys = [];
    const now = new Date();
    const first = new Date(now.getTime() - hours * 3600 * 1000 - 24 * 3600 * 1000);
    for (let d = new Date(first.getFullYear(), first.getMonth(), first.getDate());
         d <= now; d.setDate(d.getDate() + 1)) {
      keys.push(dayKey(d));
    }
    const tomorrow = new Date(now.getFullYear(), now.getMonth(), now.getDate() + 1);
    keys.push(dayKey(tomorrow));
    return keys;
  }

  async function load(refreshToday) {
    note.textContent = i18n('historyLoading');
    const keys = dayKeysFor(view.hours);
    const today = dayKey(new Date());
    const all = [];
    for (const key of keys) {   // one at a time: the unit serves one file well, not eight
      const stale = key >= today && refreshToday;
      all.push(...await loadDay(key, stale));
    }
    const from = Date.now() - view.hours * 3600 * 1000;
    points = all.filter(r => r.t >= from).sort((a, b) => a.t - b.t);
    view.loaded = true;
    csvLink.href = dayPath(today);
    csvLink.setAttribute('download', `aquasensys-${today}.csv`);
    draw();
  }

  function activeSeries() {
    const m = METRICS[view.metric];
    const dual = typeof state === 'object' && state.dualPressure;
    return m.series.filter(s => !s.dualOnly || dual).map((s, i) => ({
      ...s,
      color: `var(--series-${i + 1})`,
      name: s.label || i18n(view.metric === 'pressure' && !dual && s.alt ? s.alt : s.key)
    }));
  }

  function el(name, attrs, parent) {
    const node = document.createElementNS(NS, name);
    for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
    if (parent) parent.appendChild(node);
    return node;
  }

  // 1, 2 or 5 times a power of ten, giving about `count` steps across the range
  function niceStep(range, count) {
    const raw = range / count;
    const p = Math.pow(10, Math.floor(Math.log10(raw)));
    const n = raw / p;
    return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10) * p;
  }

  function draw() {
    const m = METRICS[view.metric];
    const series = activeSeries();
    svg.textContent = '';
    hideTip();

    // Legend: only with two or more series; one is named by its tab
    legend.textContent = '';
    if (series.length > 1) {
      for (const s of series) {
        const item = document.createElement('span');
        const swatch = document.createElement('i');
        swatch.style.background = s.color;
        item.append(swatch, document.createTextNode(s.name));
        legend.appendChild(item);
      }
    }

    const values = [];
    for (const r of points) for (const s of series) if (Number.isFinite(r[s.col])) values.push(r[s.col]);
    note.textContent = points.length
      ? i18n('historyNote').replace('{n}', points.length)
      : '';
    if (!values.length) {
      empty.textContent = i18n(view.loaded ? 'historyEmpty' : 'historyLoading');
      empty.classList.remove('hidden');
      geometry = null;
      return;
    }
    empty.classList.add('hidden');

    const width = plot.clientWidth || 600;
    const height = 240;
    const margin = { left: 44, right: 12, top: 18, bottom: 26 };
    const w = width - margin.left - margin.right;
    const h = height - margin.top - margin.bottom;
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);

    const t1 = Date.now();
    const t0 = t1 - view.hours * 3600 * 1000;
    let lo = Math.min(...values), hi = Math.max(...values);
    if (m.zero) lo = Math.min(0, lo);
    if (hi - lo < 1e-6) { hi += 1; lo = m.zero ? lo : lo - 1; }
    const step = niceStep(hi - lo, 4);
    lo = Math.floor(lo / step) * step;
    hi = Math.ceil(hi / step) * step;
    const x = t => margin.left + ((t - t0) / (t1 - t0)) * w;
    const y = v => margin.top + h - ((v - lo) / (hi - lo)) * h;

    // Recessive grid and labels
    for (let v = lo; v <= hi + step / 2; v += step) {
      el('line', { class: 'grid', x1: margin.left, x2: margin.left + w, y1: y(v), y2: y(v) }, svg);
      const label = el('text', { class: 'axis-label', x: margin.left - 6, y: y(v) + 4, 'text-anchor': 'end' }, svg);
      label.textContent = Number(v.toFixed(step < 1 ? (step < 0.1 ? 2 : 1) : 0)).toString();
    }
    // The unit, once, above the axis
    const unitLabel = el('text', { class: 'axis-label', x: margin.left, y: margin.top - 2, 'text-anchor': 'start' }, svg);
    unitLabel.textContent = m.unit;
    const tickHours = view.hours <= 24 ? 4 : 24;
    const firstTick = new Date(t0);
    firstTick.setMinutes(0, 0, 0);
    if (tickHours === 24) firstTick.setHours(0);
    for (let t = firstTick.getTime(); t <= t1; t += tickHours * 3600 * 1000) {
      if (t < t0) continue;
      const d = new Date(t);
      const label = el('text', { class: 'axis-label', x: x(t), y: height - 6, 'text-anchor': 'middle' }, svg);
      label.textContent = tickHours === 24
        ? d.toLocaleDateString(lang(), { weekday: 'short', day: 'numeric' })
        : `${pad2(d.getHours())}:00`;
    }

    // Lines, broken where the unit wasn't logging (more than 3 intervals apart)
    const gaps = [];
    for (let i = 1; i < points.length; i++) gaps.push(points[i].t - points[i - 1].t);
    const typical = gaps.length ? gaps.slice().sort((a, b) => a - b)[Math.floor(gaps.length / 2)] : 0;
    const breakAfter = Math.max(typical * 3, 20 * 60 * 1000);
    for (const s of series) {
      let d = '', prevT = null;
      for (const r of points) {
        const v = r[s.col];
        if (!Number.isFinite(v)) { prevT = null; continue; }
        const move = prevT === null || r.t - prevT > breakAfter;
        d += `${move ? 'M' : 'L'}${x(r.t).toFixed(1)},${y(v).toFixed(1)}`;
        prevT = r.t;
      }
      if (d) el('path', { class: 'series', d, stroke: s.color }, svg);
    }

    geometry = { x, y, margin, w, h, width, series, unit: m.unit, decimals: m.decimals };
  }

  // Crosshair and tooltip at the logged reading nearest the pointer
  function showTipAt(clientX) {
    if (!geometry || !points.length) return;
    const g = geometry;
    const box = svg.getBoundingClientRect();
    const px = ((clientX - box.left) / box.width) * g.width;
    if (px < g.margin.left - 8 || px > g.margin.left + g.w + 8) { hideTip(); return; }
    let best = points[0], bestDist = Infinity;
    for (const r of points) {
      const dist = Math.abs(g.x(r.t) - px);
      if (dist < bestDist) { bestDist = dist; best = r; }
    }
    svg.querySelectorAll('.cross, .dot').forEach(n => n.remove());
    const cx = g.x(best.t);
    el('line', { class: 'cross', x1: cx, x2: cx, y1: g.margin.top, y2: g.margin.top + g.h }, svg);

    tip.textContent = '';
    const when = document.createElement('div');
    when.className = 'tip-time';
    when.textContent = new Date(best.t).toLocaleString(lang(), { weekday: 'short', hour: '2-digit', minute: '2-digit' });
    tip.appendChild(when);
    for (const s of g.series) {
      const v = best[s.col];
      if (Number.isFinite(v)) {
        el('circle', { class: 'dot', cx, cy: g.y(v), r: 4, fill: s.color }, svg);
      }
      const row = document.createElement('div');
      row.className = 'tip-row';
      const swatch = document.createElement('i');
      swatch.style.background = s.color;
      const value = document.createElement('b');
      value.textContent = Number.isFinite(v) ? `${v.toFixed(g.decimals)} ${g.unit}` : '--';
      row.append(swatch, document.createTextNode(s.name), value);
      tip.appendChild(row);
    }
    tip.classList.remove('hidden');
    // Keep the tooltip inside the card, on the side away from the pointer
    const left = (cx / g.width) * box.width;
    const tipWidth = tip.offsetWidth;
    tip.style.left = `${left > box.width / 2 ? Math.max(0, left - tipWidth - 12) : Math.min(box.width - tipWidth, left + 12)}px`;
  }

  function hideTip() {
    tip.classList.add('hidden');
    svg.querySelectorAll('.cross, .dot').forEach(n => n.remove());
  }

  svg.addEventListener('pointermove', e => showTipAt(e.clientX));
  svg.addEventListener('pointerdown', e => showTipAt(e.clientX));
  svg.addEventListener('pointerleave', hideTip);

  function setPressed(group, attr, value) {
    card.querySelectorAll(`[data-${attr}]`).forEach(b =>
      b.setAttribute('aria-pressed', b.dataset[attr] === String(value) ? 'true' : 'false'));
  }

  card.querySelectorAll('[data-range]').forEach(b => b.addEventListener('click', () => {
    view.hours = Number(b.dataset.range);
    setPressed(card, 'range', view.hours);
    load(false);
  }));
  card.querySelectorAll('[data-metric]').forEach(b => b.addEventListener('click', () => {
    view.metric = b.dataset.metric;
    setPressed(card, 'metric', view.metric);
    draw();
  }));

  document.addEventListener('languagechanged', () => { if (view.loaded) draw(); });
  window.addEventListener('resize', () => { if (view.loaded) draw(); });

  // Load once the card is on screen, then keep today's readings current
  const start = () => {
    load(false);
    setInterval(() => { if (!document.hidden) load(true); }, REFRESH_MS);
  };
  if ('IntersectionObserver' in window) {
    const watcher = new IntersectionObserver(entries => {
      if (entries.some(e => e.isIntersecting)) { watcher.disconnect(); start(); }
    });
    watcher.observe(card);
  } else {
    start();
  }
})();
