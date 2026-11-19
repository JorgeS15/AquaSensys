// Dashboard configuration
const config = {
  pressure: {
    gaugeMax: 6,      // bar, full scale of the gauge
    min: 2.5,         // overwritten from /api/config
    max: 3.5
  },
  temperature: {
    max: 50,
    warningThreshold: 40,
    dangerThreshold: 45
  },
  flow: {
    max: 40
  },
  // Clogged-filter alarm, overwritten from /api/config
  filter: {
    enabled: true,
    dpWarning: 0.5,
    minFlow: 5,
    delay: 30
  }
};

const GAUGE_ARC = 335.1; // 240° of a r=80 circle

const $ = (id) => document.getElementById(id);

const elements = {
  wifiStatus: $('wifiStatus'),
  ipAddress: $('ipAddress'),
  lastUpdate: $('lastUpdate'),
  deviceName: $('deviceName'),
  deviceSubtitle: $('deviceSubtitle'),
  deviceModel: $('deviceModel'),
  deviceVersion: $('deviceVersion'),
  connectionSize: $('connectionSize'),

  toggleBtn: $('toggleBtn'),
  pumpLabel: $('pumpLabel'),
  motorTitle: $('motorTitle'),
  motorHint: $('motorHint'),
  modeAutoBtn: $('modeAutoBtn'),
  modeManualBtn: $('modeManualBtn'),
  mainSwitchBtn: $('mainSwitchBtn'),
  mainSwitchText: $('mainSwitchText'),
  systemStatus: $('systemStatus'),
  systemStatusText: $('systemStatusText'),

  pressureTitle: $('pressureTitle'),
  pressureChip: $('pressureChip'),
  pressure: $('pressure'),
  gaugeBand: $('gaugeBand'),
  gaugeNeedle: $('gaugeNeedle'),
  gaugeMax: $('gaugeMax'),
  pressureGauge: $('pressureGauge'),
  minPressureVal: $('minPressureVal'),
  maxPressureVal: $('maxPressureVal'),

  filterCard: $('filterCard'),
  filterChip: $('filterChip'),
  filterNote: $('filterNote'),
  pressureInVal: $('pressureInVal'),
  pressureOutVal: $('pressureOutVal'),
  pressureDiff: $('pressureDiff'),
  dpBlock: $('dpBlock'),
  dpBar: $('dpBar'),
  dpMark: $('dpMark'),
  dpWarnScale: $('dpWarnScale'),
  dpWarnVal: $('dpWarnVal'),

  temperature: $('temperature'),
  tempBar: $('tempBar'),
  flow: $('flow'),
  flowBar: $('flowBar'),

  uptime: $('uptime'),
  wifiRssi: $('wifiRssi'),
  freeHeap: $('freeHeap'),
  motorRuntime: $('motorRuntime'),

  updateBanner: $('updateBanner'),
  updateTitle: $('updateTitle'),
  updateDetail: $('updateDetail'),
  updateMeter: $('updateMeter'),
  updateBar: $('updateBar'),
  updateNotes: $('updateNotes'),
  updateInstallBtn: $('updateInstallBtn'),
  updateCheckBtn: $('updateCheckBtn'),
  updateCheckStatus: $('updateCheckStatus')
};

// Latest values from the device; re-rendered on every update and language change
const state = {
  deviceType: 'C',      // "C" = Controller (drives the pump), "M" = Monitor (readings only)
  connectionSize: null,
  pressure: null,
  pressureOut: null,
  dualPressure: false,
  filterStatus: null,   // "off" | "no_flow" | "no_reading" | "ok" | "clogged"
  temperature: null,
  flow: null,
  motor: false,
  manualOverride: false,
  mainSwitch: false,
  error: false
};

// Identity of the unit this page is talking to, from /api/device
let deviceInfo = null;

let eventSource;
let isConnected = false;

// A sensor that can't be read — a disconnected temperature probe, say — arrives
// as null. Show a dash for it instead of letting toFixed() abort the update.
function isReading(v) {
  return typeof v === 'number' && Number.isFinite(v);
}

function formatReading(v, decimals) {
  return isReading(v) ? v.toFixed(decimals) : '--';
}

function isController() {
  return state.deviceType !== 'M';
}

function connectSSE() {
  if (eventSource) {
    eventSource.close();
  }

  eventSource = new EventSource('/events');

  eventSource.onopen = () => {
    console.log('SSE connection opened');
    isConnected = true;
    updateConnectionStatus(true);
  };

  eventSource.onerror = (e) => {
    console.log('SSE error:', e);
    isConnected = false;
    updateConnectionStatus(false);

    // Close the connection before attempting to reconnect
    eventSource.close();
    setTimeout(connectSSE, 3000);
  };

  eventSource.addEventListener('update', (e) => {
    try {
      const data = JSON.parse(e.data);
      updateDashboard(data);
    } catch (err) {
      console.error('Error parsing SSE data:', err);
    }
  });

  eventSource.addEventListener('diagnostics', (e) => {
    try {
      updateInfoStrip(JSON.parse(e.data));
    } catch (err) {
      console.error('Error parsing diagnostics data:', err);
    }
  });
}

function loadDeviceInfo() {
  fetch('/api/device')
    .then(response => response.ok ? response.json() : Promise.reject(response.status))
    .then(data => {
      deviceInfo = data;
      if (data.device_type) state.deviceType = data.device_type === 'M' ? 'M' : 'C';
      if (data.connection_size) state.connectionSize = data.connection_size;
      render();
    })
    .catch(err => console.error('Error loading device info:', err));
}

function loadConfig() {
  fetch('/api/config')
    .then(response => response.ok ? response.json() : Promise.reject(response.status))
    .then(cfg => {
      if (typeof cfg.min_pressure === 'number') config.pressure.min = cfg.min_pressure;
      if (typeof cfg.max_pressure === 'number') config.pressure.max = cfg.max_pressure;
      if (typeof cfg.dual_pressure_sensors === 'boolean') state.dualPressure = cfg.dual_pressure_sensors;
      if (typeof cfg.filter_alarm_enabled === 'boolean') config.filter.enabled = cfg.filter_alarm_enabled;
      if (typeof cfg.filter_dp_warning === 'number') config.filter.dpWarning = cfg.filter_dp_warning;
      if (typeof cfg.filter_min_flow_lpm === 'number') config.filter.minFlow = cfg.filter_min_flow_lpm;
      if (typeof cfg.filter_alarm_delay_s === 'number') config.filter.delay = cfg.filter_alarm_delay_s;
      render();
    })
    .catch(err => console.error('Error loading config:', err));
}

function updateDashboard(data) {
  if (data.deviceType !== undefined) state.deviceType = data.deviceType === 'M' ? 'M' : 'C';
  if (data.connectionSize !== undefined) state.connectionSize = data.connectionSize;
  if (data.pressure !== undefined) state.pressure = data.pressure;
  if (data.dualPressure !== undefined) state.dualPressure = data.dualPressure;
  if (data.pressureOut !== undefined) state.pressureOut = data.pressureOut;
  if (data.filterStatus !== undefined) state.filterStatus = data.filterStatus;
  if (data.temperature !== undefined) state.temperature = data.temperature;
  if (data.flow !== undefined) state.flow = data.flow;
  if (data.motor !== undefined) state.motor = data.motor;
  if (data.manualOverride !== undefined) state.manualOverride = data.manualOverride;
  if (data.mainSwitch !== undefined) state.mainSwitch = data.mainSwitch;
  if (data.error !== undefined) state.error = data.error;

  elements.lastUpdate.textContent = new Date().toLocaleTimeString();
  render();
}

function render() {
  renderIdentity();
  renderPump();
  renderPressure();
  renderFilter();
  renderTemperature();
  renderFlow();
}

// Header shows which unit this is: "AquaSensys C3" / "AquaSensys M", or the
// name the user gave it. Monitor units hide every motor control.
function renderIdentity() {
  const controller = isController();
  document.body.classList.toggle('monitor', !controller);

  if (deviceInfo) {
    const title = deviceInfo.display_name || deviceInfo.name;
    if (title) {
      elements.deviceName.textContent = title;
      document.title = title;
    }
    elements.deviceModel.textContent = deviceInfo.model || '--';
    elements.deviceVersion.textContent = deviceInfo.version ? 'v' + deviceInfo.version : '--';
  }

  elements.connectionSize.textContent = state.connectionSize ? state.connectionSize + '"' : '--';
  elements.deviceSubtitle.textContent = i18n(controller ? 'controllerSubtitle' : 'monitorSubtitle');
}

function renderPump() {
  const s = state;

  if (!isController()) {
    // Monitor: nothing to drive, the hero just reports what the unit is doing
    elements.pumpLabel.textContent = i18n('monitorLabel');
    elements.motorTitle.textContent = i18n('monitoring');
    elements.motorHint.textContent = i18n('hintMonitor');
  } else {
    elements.pumpLabel.textContent = i18n('pump');
    elements.toggleBtn.classList.toggle('on', s.motor);
    elements.toggleBtn.setAttribute('aria-pressed', s.motor ? 'true' : 'false');
    elements.motorTitle.textContent = i18n(s.motor ? 'pumpRunning' : 'pumpStopped');

    let hint;
    if (!s.mainSwitch) hint = i18n('hintMainOff');
    else if (s.error) hint = i18n('hintError');
    else if (s.manualOverride) hint = i18n('hintManual');
    else hint = i18n('hintAuto')
      .replace('{min}', config.pressure.min.toFixed(1))
      .replace('{max}', config.pressure.max.toFixed(1));
    elements.motorHint.textContent = hint;

    elements.modeAutoBtn.setAttribute('aria-pressed', s.manualOverride ? 'false' : 'true');
    elements.modeManualBtn.setAttribute('aria-pressed', s.manualOverride ? 'true' : 'false');

    elements.mainSwitchBtn.setAttribute('aria-pressed', s.mainSwitch ? 'true' : 'false');
    elements.mainSwitchText.textContent = i18n(s.mainSwitch ? 'switchOn' : 'switchOff');
  }

  elements.systemStatus.classList.toggle('error', s.error);
  elements.systemStatus.querySelector('i').className =
    s.error ? 'fas fa-exclamation-triangle' : 'fas fa-check-circle';
  elements.systemStatusText.textContent = i18n(s.error ? 'statusError' : 'allOk');
}

function renderPressure() {
  const p = state.pressure;
  const { min, max, gaugeMax } = config.pressure;

  elements.pressureTitle.textContent = i18n(state.dualPressure ? 'mainsPressure' : 'pressure');
  elements.minPressureVal.textContent = min.toFixed(1);
  elements.maxPressureVal.textContent = max.toFixed(1);
  elements.gaugeMax.textContent = gaugeMax;

  const bandStart = (Math.max(0, min) / gaugeMax) * GAUGE_ARC;
  const bandLen = (Math.max(0, max - min) / gaugeMax) * GAUGE_ARC;
  elements.gaugeBand.setAttribute('stroke-dasharray', `0 ${bandStart.toFixed(1)} ${bandLen.toFixed(1)} 1000`);

  elements.pressure.textContent = formatReading(p, 2);
  if (!isReading(p)) {
    elements.gaugeNeedle.setAttribute('stroke-dasharray', '0 1000');
    return;
  }

  const needle = (Math.min(Math.max(p, 0), gaugeMax) / gaugeMax) * GAUGE_ARC;
  elements.gaugeNeedle.setAttribute('stroke-dasharray', `${needle.toFixed(1)} 1000`);

  // The start/stop band only means something on a unit that drives the pump
  let chipKey, chipClass;
  if (p < min) { chipKey = 'belowMin'; chipClass = 'warn'; }
  else if (p > max) { chipKey = 'aboveMax'; chipClass = 'warn'; }
  else { chipKey = 'inRange'; chipClass = ''; }
  elements.gaugeNeedle.classList.toggle('warn', isController() && chipClass === 'warn');
  elements.pressureChip.className = 'chip motor-only ' + chipClass;
  elements.pressureChip.textContent = i18n(chipKey);
  elements.pressureGauge.setAttribute('aria-label', `${i18n('pressure')}: ${p.toFixed(2)} bar`);
}

function renderFilter() {
  // Two sensors: gauge beside the filter card, temperature beside flow below
  document.body.classList.toggle('dual', state.dualPressure);
  elements.filterCard.classList.toggle('hidden', !state.dualPressure);
  if (!state.dualPressure) return;

  const pIn = state.pressure;
  const pOut = state.pressureOut;
  const warn = config.filter.dpWarning;
  const alarmOn = config.filter.enabled;

  elements.pressureInVal.textContent = formatReading(pIn, 2);
  elements.pressureOutVal.textContent = formatReading(pOut, 2);
  elements.dpWarnVal.textContent = warn.toFixed(2);
  elements.dpWarnScale.classList.toggle('hidden', !alarmOn);
  elements.dpMark.classList.toggle('hidden', !alarmOn);

  const haveDp = isReading(pIn) && isReading(pOut);
  const dp = haveDp ? pIn - pOut : null;
  elements.pressureDiff.textContent = formatReading(dp, 2);

  // Without the alarm the meter still shows the drop, on a 0–1 bar scale
  const scale = alarmOn ? warn * 2 : 1;
  elements.dpBar.style.width = haveDp ? Math.min(100, Math.max(0, (dp / scale) * 100)) + '%' : '0%';

  // The firmware judges the filter (it knows the flow threshold); fall back to
  // the same rule here for firmware that doesn't send filterStatus.
  let status = state.filterStatus;
  if (!status) {
    if (!alarmOn) status = 'off';
    else if (!haveDp || !isReading(state.flow)) status = 'no_reading';
    else if (state.flow < config.filter.minFlow) status = 'no_flow';
    else status = dp >= warn ? 'clogged' : 'ok';
  }
  if (status === 'ok' && dp >= warn * 0.7) status = 'check';

  const look = {
    off:        { level: 'muted',  key: 'filterAlarmOff' },
    no_flow:    { level: 'muted',  key: 'filterNoFlow' },
    no_reading: { level: 'muted',  key: 'filterNoReading' },
    ok:         { level: 'ok',     key: 'filterOk' },
    check:      { level: 'warn',   key: 'filterCheckSoon' },
    clogged:    { level: 'danger', key: 'filterClogged' }
  }[status] || { level: 'muted', key: 'filterNoReading' };

  const tone = (look.level === 'warn' || look.level === 'danger') ? ' ' + look.level : '';
  elements.dpBlock.className = 'dp-block' + tone;
  elements.dpBar.className = 'meter-fill' + tone;
  elements.filterChip.className = 'chip ' + look.level;
  elements.filterChip.textContent = i18n(look.key);

  elements.filterNote.textContent = alarmOn
    ? i18n('filterNote')
        .replace('{flow}', config.filter.minFlow.toFixed(0))
        .replace('{delay}', config.filter.delay)
    : i18n('filterNoteOff');
}

function renderTemperature() {
  const t = state.temperature;
  const c = config.temperature;
  elements.temperature.textContent = formatReading(t, 1);
  if (!isReading(t)) {
    elements.tempBar.style.width = '0%';
    return;
  }
  elements.tempBar.style.width = Math.min(100, Math.max(0, (t / c.max) * 100)) + '%';
  elements.tempBar.className = 'meter-fill temp' +
    (t >= c.dangerThreshold ? ' danger' : t >= c.warningThreshold ? ' warn' : '');
}

function renderFlow() {
  const f = state.flow;
  elements.flow.textContent = formatReading(f, 1);
  elements.flowBar.style.width = isReading(f)
    ? Math.min(100, Math.max(0, (f / config.flow.max) * 100)) + '%'
    : '0%';
}

function updateInfoStrip(data) {
  if (data.uptime !== undefined) elements.uptime.textContent = formatDuration(data.uptime);
  if (data.wifi_rssi !== undefined) elements.wifiRssi.textContent = data.wifi_rssi;
  if (data.free_heap !== undefined) elements.freeHeap.textContent = (data.free_heap / 1024).toFixed(0);
  if (data.motor_runtime_s !== undefined) elements.motorRuntime.textContent = formatDuration(data.motor_runtime_s);
  if (data.dual_pressure !== undefined && data.dual_pressure !== state.dualPressure) {
    state.dualPressure = data.dual_pressure;
    render();
  }
}

function formatDuration(seconds) {
  const d = Math.floor(seconds / 86400);
  const h = Math.floor((seconds % 86400) / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  if (d > 0) return `${d}d ${String(h).padStart(2, '0')}h ${String(m).padStart(2, '0')}m`;
  if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m`;
  return `${m}m ${String(seconds % 60).padStart(2, '0')}s`;
}

function updateConnectionStatus(connected) {
  elements.wifiStatus.classList.toggle('offline', !connected);
  elements.wifiStatus.querySelector('i').className =
    connected ? 'fas fa-wifi' : 'fas fa-exclamation-circle';
  elements.ipAddress.textContent = connected ? window.location.hostname : i18n('disconnected');

  [elements.toggleBtn, elements.mainSwitchBtn, elements.modeAutoBtn, elements.modeManualBtn]
    .forEach(btn => { btn.disabled = !connected; });
}

function sendCommand(command) {
  const validCommands = ['toggle', 'override', 'mainSwitch'];
  if (!validCommands.includes(command)) {
    console.error('Invalid command:', command);
    return;
  }

  if (!isConnected) {
    console.error('Cannot send command: not connected to server');
    return;
  }

  if (!isController()) {
    console.error('Cannot send command: this is a Monitor (type M) device');
    return;
  }

  fetch('/command', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ command })
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`Server returned ${response.status}: ${response.statusText}`);
    }
    return response.json();
  })
  .then(data => console.log('Command response:', data))
  .catch(error => {
    console.error('Error sending command:', error);
  });
}

// ---------- Firmware updates (/api/update) ----------
// The unit itself checks GitHub; this page only shows what it found and asks it
// to install. While an update runs the page polls every 2 s, and once the unit
// comes back on a new version it reloads, picking up the new web UI.

let updateInfo = null;
let updatePollTimer = null;
let installFromVersion = null;  // set when Install is pressed here
let checkClickedAt = null;      // set when "Check for updates" is pressed here

function isUpdateRunning(u) {
  return !!u && u.state !== 'idle' && u.state !== 'checking';
}

function scheduleUpdatePoll(ms) {
  clearTimeout(updatePollTimer);
  const fast = isUpdateRunning(updateInfo) || installFromVersion || checkClickedAt;
  updatePollTimer = setTimeout(loadUpdateStatus, ms || (fast ? 2000 : 10 * 60 * 1000));
}

function loadUpdateStatus() {
  fetch('/api/update')
    .then(response => response.ok ? response.json() : Promise.reject(response.status))
    .then(data => {
      // Back after the restart, on the new version: reload for the new web UI
      if (installFromVersion && data.current !== installFromVersion) {
        location.reload();
        return;
      }
      updateInfo = data;
      renderUpdate();
      scheduleUpdatePoll();
    })
    .catch(err => {
      // While the unit restarts it doesn't answer; keep trying
      console.error('Error loading update status:', err);
      scheduleUpdatePoll(installFromVersion ? 3000 : 0);
    });
}

function renderUpdate() {
  const u = updateInfo;
  if (!u) return;
  const running = isUpdateRunning(u);
  let show = false, title = '', detail = '', canInstall = false, failed = false;

  if (running) {
    show = true;
    title = i18n('updateInstalling').replace('{v}', u.latest || '');
    detail = i18n('updateStep_' + u.state);
  } else if (u.available) {
    show = true;
    title = i18n('updateAvailable').replace('{v}', u.latest);
    if (u.usb_flash_required) {
      detail = i18n('updateNeedsUsb');
    } else {
      detail = i18n('updateCurrent').replace('{v}', u.current);
      canInstall = true;
    }
    // A failed install leaves the version on offer: say why, and allow a retry
    if (u.error && installFromVersion) {
      failed = true;
      detail = i18n('updateFailed') + ' ' + u.error;
    }
  }
  if (!running && installFromVersion && u.current === installFromVersion && u.error) {
    installFromVersion = null;  // the attempt is over
  }

  elements.updateBanner.classList.toggle('hidden', !show);
  elements.updateBanner.classList.toggle('error', failed);
  elements.updateTitle.textContent = title;
  elements.updateDetail.textContent = detail;

  const pct = running && typeof u.progress === 'number' ? u.progress : null;
  elements.updateMeter.classList.toggle('hidden', !running);
  elements.updateBar.style.width = (pct === null ? (u.state === 'rebooting' ? 100 : 0) : pct) + '%';

  elements.updateInstallBtn.classList.toggle('hidden', !canInstall);
  elements.updateInstallBtn.disabled = running;
  if (u.release_url) elements.updateNotes.href = u.release_url;
  elements.updateNotes.classList.toggle('hidden', !show || !u.release_url);

  // "Check for updates" result, only after a check started from this page
  elements.updateCheckBtn.disabled = running || u.state === 'checking' || !!checkClickedAt;
  if (checkClickedAt) {
    const sinceClick = (Date.now() - checkClickedAt) / 1000;
    const finished = u.state === 'idle' && typeof u.checked_ago_s === 'number' && u.checked_ago_s <= sinceClick;
    if (finished) {
      checkClickedAt = null;
      elements.updateCheckBtn.disabled = running;
      elements.updateCheckStatus.textContent = u.error
        ? i18n('updateCheckFailed') + ' ' + u.error
        : (u.available ? '' : i18n('upToDate'));
    } else if (sinceClick > 60) {
      checkClickedAt = null;  // the unit never got to it (no WiFi?): give the button back
      elements.updateCheckBtn.disabled = running;
      elements.updateCheckStatus.textContent = '';
    } else {
      elements.updateCheckStatus.textContent = i18n('checkingUpdates');
    }
  }
}

function checkForUpdates() {
  checkClickedAt = Date.now();
  elements.updateCheckBtn.disabled = true;
  elements.updateCheckStatus.textContent = i18n('checkingUpdates');
  fetch('/api/update/check', { method: 'POST' })
    .catch(err => console.error('Error requesting update check:', err))
    .finally(() => scheduleUpdatePoll(2000));
}

function installUpdate() {
  const u = updateInfo;
  if (!u || !u.available) return;
  let question = i18n('confirmAutoUpdate').replace('{v}', u.latest);
  if (isController()) question += i18n('confirmAutoUpdatePump');
  if (!confirm(question)) return;

  elements.updateInstallBtn.disabled = true;
  fetch('/api/update/install', { method: 'POST' })
    .then(response => response.json().then(body => ({ ok: response.ok, body })))
    .then(({ ok, body }) => {
      if (!ok) throw new Error(body.error || 'refused');
      installFromVersion = u.current;
    })
    .catch(err => {
      elements.updateInstallBtn.disabled = false;
      alert(i18n('updateFailed') + ' ' + err.message);
    })
    .finally(() => scheduleUpdatePoll(1000));
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
  // Initialize language before connecting
  if (typeof initializeLanguage === 'function') {
    initializeLanguage();
  }

  render();
  loadDeviceInfo();
  loadConfig();
  loadUpdateStatus();
  connectSSE();

  elements.toggleBtn.addEventListener('click', () => sendCommand('toggle'));
  elements.mainSwitchBtn.addEventListener('click', () => sendCommand('mainSwitch'));

  // 'override' toggles the mode, so only send it when the other mode is picked
  elements.modeAutoBtn.addEventListener('click', () => {
    if (state.manualOverride) sendCommand('override');
  });
  elements.modeManualBtn.addEventListener('click', () => {
    if (!state.manualOverride) sendCommand('override');
  });

  elements.updateInstallBtn.addEventListener('click', installUpdate);
  elements.updateCheckBtn.addEventListener('click', checkForUpdates);

  document.addEventListener('languagechanged', () => {
    render();
    renderUpdate();
  });
});
