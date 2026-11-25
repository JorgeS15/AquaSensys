// Configuration form elements
const formElements = {
  wifiForm: document.getElementById('wifiForm'),
  mqttForm: document.getElementById('mqttForm'),
  deviceForm: document.getElementById('deviceForm'),
  paramsForm: document.getElementById('paramsForm'),
  sensorsForm: document.getElementById('sensorsForm'),
  leakForm: document.getElementById('leakForm'),
  shortCycleForm: document.getElementById('shortCycleForm'),
  protectionForm: document.getElementById('protectionForm'),
  securityForm: document.getElementById('securityForm'),
  
  // Status elements
  wifiStatus: document.getElementById('wifiStatus'),
  ipAddress: document.getElementById('ipAddress'),
  
  // Action buttons
  rebootBtn: document.getElementById('rebootBtn'),
  factoryResetBtn: document.getElementById('factoryResetBtn')
};

let isConnected = false;
let eventSource;

function connectSSE() {
  if (eventSource) {
    eventSource.close();
  }

  eventSource = new EventSource('/events');
  
  eventSource.onopen = () => {
    console.log('SSE connection opened');
    isConnected = true;
    updateConnectionStatus(true);
    // Load configuration after connection is established
    loadConfiguration();
  };

  eventSource.onerror = (e) => {
    console.log('SSE error:', e);
    isConnected = false;
    updateConnectionStatus(false);
    
    // Close the connection before attempting to reconnect
    eventSource.close();
    setTimeout(connectSSE, 3000);
  };
}

function updateConnectionStatus(connected) {
  formElements.wifiStatus.classList.toggle('offline', !connected);
  formElements.wifiStatus.querySelector('i').className =
    connected ? 'fas fa-wifi' : 'fas fa-exclamation-circle';
  formElements.ipAddress.textContent = connected ? window.location.hostname : i18n('connecting');
}

function loadConfiguration() {
  // Fetch configuration from the server
  fetch('/api/config')
    .then(response => {
      if (!response.ok) {
        throw new Error(`Server returned ${response.status}: ${response.statusText}`);
      }
      return response.json();
    })
    .then(data => {
      // Fill in the form fields with the current configuration
      populateFormFields(data);
    })
    .catch(error => {
      console.error('Error loading configuration:', error);
      showNotification('loadConfigError', 'error');
    });
}

function populateFormFields(config) {
  // WiFi settings
  document.getElementById('wifi_ssid').value = config.wifi_ssid || '';
  // Password is not filled for security reasons
  
  // MQTT settings
  document.getElementById('mqtt_server').value = config.mqtt_server || '';
  document.getElementById('mqtt_port').value = config.mqtt_port || 1883;
  document.getElementById('mqtt_user').value = config.mqtt_user || '';
  // Password is not filled for security reasons
  
  // Device profile — defaults match the firmware: 3/4" connection, Controller unit
  document.getElementById('device_type').value = config.device_type === 'M' ? 'M' : 'C';
  document.getElementById('device_name').value = config.device_name || '';
  document.getElementById('device_id').value = config.device_id || '';
  // Show the default a blank field resolves to — it depends on the device type
  const isMonitor = config.device_type === 'M';
  document.getElementById('device_name').placeholder = isMonitor ? 'AquaSensys M' : 'AquaSensys C3';
  document.getElementById('device_id').placeholder = isMonitor ? 'aquasensys-m' : 'aquasensys';
  document.getElementById('connection_size').value = config.connection_size === '1' ? '1' : '3/4';
  document.getElementById('flow_k_factor').value = config.flow_k_factor ?? 0;
  document.getElementById('flow_k_offset').value = config.flow_k_offset ?? 0;
  document.getElementById('flow_debounce_us').value = config.flow_debounce_us ?? 500;
  document.getElementById('max_flow_lpm').value = config.max_flow_lpm ?? 150;

  // System parameters
  document.getElementById('min_pressure').value = config.min_pressure || 2.5;
  document.getElementById('max_pressure').value = config.max_pressure || 3.5;
  document.getElementById('pressure_offset').value = config.pressure_offset || 0;
  document.getElementById('temp_offset').value = config.temp_offset || 0;
  document.getElementById('log_interval_minutes').value = config.log_interval_minutes ?? 5;
  document.getElementById('update_check_hours').value = config.update_check_hours ?? 12;
  setTimezoneField(config.timezone || 'WET0WEST,M3.5.0/1,M10.5.0');
  document.getElementById('ntp_server').value = config.ntp_server || '';

  // Pressure sensors and the clogged-filter alarm
  const dual = config.dual_pressure_sensors === true;
  document.querySelector(`input[name="dual_pressure_sensors"][value="${dual}"]`).checked = true;
  document.getElementById('filter_alarm_enabled').checked = config.filter_alarm_enabled !== false;
  document.getElementById('filter_dp_warning').value = config.filter_dp_warning ?? 0.5;
  document.getElementById('filter_min_flow_lpm').value = config.filter_min_flow_lpm ?? 5;
  document.getElementById('filter_alarm_delay_s').value = config.filter_alarm_delay_s ?? 30;
  updateSensorFields();

  // Leak alarm
  document.getElementById('leak_alarm_enabled').checked = config.leak_alarm_enabled !== false;
  document.getElementById('leak_max_minutes').value = config.leak_max_minutes ?? 60;
  document.getElementById('leak_quiet_enabled').checked = config.leak_quiet_enabled === true;
  document.getElementById('leak_quiet_start').value = config.leak_quiet_start || '01:00';
  document.getElementById('leak_quiet_end').value = config.leak_quiet_end || '06:00';
  document.getElementById('leak_quiet_minutes').value = config.leak_quiet_minutes ?? 5;
  document.getElementById('leak_stop_pump').checked = config.leak_stop_pump === true;
  // Only a Controller has a pump to stop
  document.getElementById('leakStopPumpGroup').classList.toggle('hidden', config.device_type === 'M');
  updateLeakFields();

  // Password
  const hasPassword = config.admin_password_set === true;
  document.getElementById('backupNote').textContent = i18n(hasPassword ? 'backupWithPassword' : 'backupNoPassword');
  document.getElementById('passwordState').textContent = i18n(hasPassword ? 'passwordIsSet' : 'passwordNotSet');
  document.getElementById('passwordClearGroup').classList.toggle('hidden', !hasPassword);
  document.getElementById('admin_password').value = '';
  document.getElementById('admin_password_confirm').value = '';
  document.getElementById('admin_password_clear').checked = false;

  // Fault limits; the motor ones only on a Controller
  lastConfig = config;
  document.getElementById('fault_min_pressure').value = config.fault_min_pressure ?? 1;
  document.getElementById('fault_max_pressure').value = config.fault_max_pressure ?? 5;
  document.getElementById('dry_run_seconds').value = config.dry_run_seconds ?? 10;
  document.getElementById('max_current').value = config.max_current ?? 15;
  document.getElementById('max_phase_imbalance').value = config.max_phase_imbalance ?? 3;
  document.getElementById('motorProtectionFields').classList.toggle('hidden', config.device_type === 'M');

  // Pump short-cycling: a Controller only
  document.getElementById('shortCycleCard').classList.toggle('hidden', config.device_type === 'M');
  document.getElementById('short_cycle_max_starts').value = config.short_cycle_max_starts ?? 20;
  document.getElementById('short_cycle_stop_pump').checked = config.short_cycle_stop_pump === true;
}

function updateLeakFields() {
  const on = document.getElementById('leak_alarm_enabled').checked;
  const quiet = document.getElementById('leak_quiet_enabled').checked;
  document.getElementById('leakFields').classList.toggle('hidden', !on);
  document.getElementById('leakQuietFields').classList.toggle('hidden', !quiet);
}

// A listed zone selects its option; anything else shows as "Other" with the
// POSIX string in the text field
function setTimezoneField(tz) {
  const select = document.getElementById('timezone');
  const listed = [...select.options].some(o => o.value === tz && o.value !== 'custom');
  select.value = listed ? tz : 'custom';
  document.getElementById('timezone_custom').value = listed ? '' : tz;
  updateTimezoneField();
}

function updateTimezoneField() {
  const custom = document.getElementById('timezone').value === 'custom';
  document.getElementById('timezone_custom').classList.toggle('hidden', !custom);
}

// The alarm settings only apply with two sensors, its values only when it's on
function updateSensorFields() {
  const dual = document.querySelector('input[name="dual_pressure_sensors"]:checked').value === 'true';
  const alarmOn = document.getElementById('filter_alarm_enabled').checked;
  document.getElementById('filterAlarmGroup').classList.toggle('hidden', !dual);
  document.getElementById('filterAlarmFields').classList.toggle('hidden', !alarmOn);
}

// The settings as last loaded, to check one form against another
let lastConfig = {};

// A pressure fault inside the pump's start/stop band would trip it on every
// start (or stop): refuse it, whichever of the two forms is being saved
function pressureLimitsProblem(formId) {
  const num = id => Number(document.getElementById(id).value);
  const fromForm = (id, key) => (formId === 'paramsForm' || formId === 'protectionForm') && document.getElementById(id).form.id === formId
    ? num(id) : Number(lastConfig[key]);
  const start = fromForm('min_pressure', 'min_pressure');
  const stop = fromForm('max_pressure', 'max_pressure');
  const lo = fromForm('fault_min_pressure', 'fault_min_pressure');
  const hi = fromForm('fault_max_pressure', 'fault_max_pressure');
  if ([start, stop, lo, hi].some(v => !Number.isFinite(v))) return null;
  if (hi <= lo) return 'faultRangeInverted';
  if (lo >= start) return 'faultMinTooHigh';
  if (hi <= stop) return 'faultMaxTooLow';
  return null;
}

function saveConfiguration(formId, event) {
  event.preventDefault();
  if (formId === 'paramsForm' || formId === 'protectionForm') {
    const problem = pressureLimitsProblem(formId);
    if (problem) { showNotification(problem, 'error'); return; }
  }
  
  const form = document.getElementById(formId);
  const formData = new FormData(form);
  const data = {};
  
  // Fields the firmware parses as numbers; FormData would hand over strings
  const numericKeys = [
    'mqtt_port', 'min_pressure', 'max_pressure', 'pressure_offset', 'temp_offset',
    'flow_k_factor', 'flow_k_offset', 'flow_debounce_us', 'max_flow_lpm',
    'filter_dp_warning', 'filter_min_flow_lpm', 'filter_alarm_delay_s',
    'log_interval_minutes', 'update_check_hours',
    'leak_max_minutes', 'leak_quiet_minutes', 'short_cycle_max_starts',
    'fault_min_pressure', 'fault_max_pressure', 'dry_run_seconds', 'max_current', 'max_phase_imbalance'
  ];

  // Convert FormData to a plain object
  for (const [key, value] of formData.entries()) {
    // Skip empty password fields
    if ((key === 'wifi_password' || key === 'mqtt_password' || key === 'admin_password') && value === '') {
      continue;
    }
    if (key === 'admin_password_confirm') continue;   // checked before saving
    if (numericKeys.includes(key)) {
      const n = Number(value);
      if (value === '' || Number.isNaN(n)) continue;  // leave the stored value alone
      data[key] = n;
      continue;
    }
    if (form.querySelector(`input[type="checkbox"][name="${key}"]`)) continue;   // read below
    if (key === 'timezone_custom') continue;   // folded into timezone below
    if (key === 'timezone' && value === 'custom') {
      const custom = (formData.get('timezone_custom') || '').trim();
      if (custom) data.timezone = custom;
      continue;
    }
    if (key === 'dual_pressure_sensors') {
      data[key] = value === 'true';
      continue;
    }
    data[key] = value;
  }

  // An unticked checkbox is left out of FormData, so read them directly
  form.querySelectorAll('input[type="checkbox"][name]').forEach(box => { data[box.name] = box.checked; });
  
  console.log('Sending configuration:', data);
  
  // Send configuration to the server
  fetch('/api/config', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(data)
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`Server returned ${response.status}: ${response.statusText}`);
    }
    return response.json();
  })
  .then(result => {
    console.log('Configuration saved:', result);
    showNotification('configSaved', 'success');
    
    // Show specific messages based on what changed
    if (result.wifi_changed) {
      showNotification('wifiUpdated', 'info');
    }
    
    if (result.mqtt_changed) {
      showNotification('mqttUpdated', 'info');
    }

    if (result.device_type_changed) {
      showNotification('deviceTypeUpdated', 'info');
    }
    
    // Reload configuration to reflect any changes
    setTimeout(() => {
      loadConfiguration();
    }, 1000);
  })
  .catch(error => {
    console.error('Error saving configuration:', error);
    // 401: a password is set and the browser's sign-in was cancelled or wrong
    showNotification(String(error).includes('401') ? 'passwordRequired' : 'saveConfigError', 'error');
  });
}

// ---- Logs and backup ----

const pad2 = n => String(n).padStart(2, '0');
const isoDay = d => `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`;

function setupDataCard() {
  const from = document.getElementById('exportFrom');
  const to = document.getElementById('exportTo');
  if (!from) return;
  const today = new Date();
  to.value = isoDay(today);
  from.value = isoDay(new Date(today.getFullYear(), today.getMonth(), today.getDate() - 6));
  document.getElementById('exportBtn').addEventListener('click', exportCsv);
  document.getElementById('restoreFile').addEventListener('change', restoreBackup);
  listOtherLogs();
}

// One CSV for the chosen days, from the unit's day files (one header)
async function exportCsv() {
  const status = document.getElementById('exportStatus');
  const from = document.getElementById('exportFrom').value;
  const to = document.getElementById('exportTo').value;
  if (!from || !to || from > to) { status.textContent = i18n('exportBadRange'); return; }
  const start = new Date(from + 'T00:00:00');
  const end = new Date(to + 'T00:00:00');
  if ((end - start) / 86400000 > 366) { status.textContent = i18n('exportTooLong'); return; }

  let header = null;
  const body = [];
  let found = 0;
  for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1)) {
    const key = isoDay(d);
    status.textContent = i18n('exportReading').replace('{day}', key);
    try {
      const response = await fetch(`/logs/${key.slice(0, 4)}/${key}.csv`, { cache: 'no-store' });
      if (!response.ok) continue;
      const lines = (await response.text()).trim().split('\n');
      if (lines.length < 2) continue;
      if (!header) header = lines[0];
      body.push(...lines.slice(1));
      found++;
    } catch (err) { /* a missing day is not an error */ }
  }
  if (!header) { status.textContent = i18n('exportNothing'); return; }

  const blob = new Blob([header + '\n' + body.join('\n') + '\n'], { type: 'text/csv' });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = `aquasensys-${from}_${to}.csv`;
  document.body.appendChild(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(link.href), 10000);
  status.textContent = i18n('exportDone').replace('{n}', body.length).replace('{days}', found);
}

// Events, readings from before the clock was set, and the pre-3.6.10 log
function listOtherLogs() {
  fetch('/api/logs')
    .then(r => r.ok ? r.json() : Promise.reject(r.status))
    .then(data => {
      const list = document.getElementById('otherLogs');
      list.textContent = '';
      const names = { '/logs/events.csv': 'logEvents', '/logs/unsynced.csv': 'logUnsynced', '/log.csv': 'logLegacy' };
      for (const f of data.files || []) {
        const li = document.createElement('li');
        const a = document.createElement('a');
        a.href = f.path;
        a.download = 'aquasensys-' + f.path.split('/').pop();
        a.textContent = names[f.path] ? i18n(names[f.path]) : f.path;
        const size = document.createElement('span');
        size.textContent = ` (${Math.max(1, Math.round(f.size / 1024))} KB)`;
        li.append(a, size);
        list.appendChild(li);
      }
    })
    .catch(() => {});
}

// Keys that belong to one unit, not to a setup: who it is and how its sensors read
const UNIT_KEYS = ['device_id', 'device_name', 'current_offset_l1', 'current_offset_l2', 'current_offset_l3',
  'current_calibrated', 'current_cal_method', 'pressure_in_offset', 'pressure_out_offset',
  'pressure_calibrated', 'pressure_offset', 'temp_offset'];
// Written by the firmware itself
const INTERNAL_KEYS = ['config_version', 'flow_k_factor_effective', 'flow_k_offset_effective', 'admin_password_set'];

function restoreBackup(event) {
  const file = event.target.files[0];
  event.target.value = '';
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    let data;
    try { data = JSON.parse(reader.result); } catch (err) { data = null; }
    if (!data || typeof data !== 'object' || Array.isArray(data)) {
      showNotification('restoreBadFile', 'error');
      return;
    }
    INTERNAL_KEYS.forEach(k => delete data[k]);
    if (document.getElementById('restoreKeepIdentity').checked) UNIT_KEYS.forEach(k => delete data[k]);
    // Without its password a WiFi network can't be joined: keep this unit's
    if (!data.wifi_password) delete data.wifi_ssid;
    if (!confirm(i18n('restoreConfirm'))) return;

    fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    })
      .then(response => {
        if (!response.ok) throw new Error(`Server returned ${response.status}`);
        return response.json();
      })
      .then(result => {
        showNotification('restoreDone', 'success');
        if (result.wifi_changed || result.device_type_changed) showNotification('restoreRestarting', 'info');
        setTimeout(loadConfiguration, 1000);
      })
      .catch(error => {
        showNotification(String(error).includes('401') ? 'passwordRequired' : 'saveConfigError', 'error');
      });
  };
  reader.readAsText(file);
}

function factoryReset() {
  if (confirm(i18n('factoryResetConfirm'))) {
    fetch('/api/factory-reset', {
      method: 'POST'
    })
    .then(response => {
      if (!response.ok) {
        throw new Error(`Server returned ${response.status}: ${response.statusText}`);
      }
      return response.json();
    })
    .then(data => {
      showNotification('factoryResetSuccess', 'success');
      setTimeout(() => {
        window.location.reload();
      }, 5000);
    })
    .catch(error => {
      console.error('Error performing factory reset:', error);
      showNotification('factoryResetError', 'error');
    });
  }
}

function rebootDevice() {
  if (confirm(i18n('rebootConfirm'))) {
    fetch('/reboot', {
      method: 'POST'
    })
    .then(response => {
      if (!response.ok) {
        throw new Error(`Server returned ${response.status}: ${response.statusText}`);
      }
      return response.json();
    })
    .then(data => {
      showNotification('rebootMessage', 'info');
      setTimeout(() => {
        checkConnection();
      }, 5000);
    })
    .catch(error => {
      console.error('Error rebooting device:', error);
      showNotification('rebootError', 'error');
    });
  }
}

function checkConnection() {
  const checkInterval = setInterval(() => {
    fetch('/', { method: 'HEAD' })
      .then(() => {
        clearInterval(checkInterval);
        showNotification('deviceOnline', 'success');
        setTimeout(() => {
          window.location.reload();
        }, 1000);
      })
      .catch(() => {
        console.log('Device still rebooting...');
      });
  }, 2000);
}

function showNotification(messageKey, type = 'info') {
  // Get translated message or use the key as a fallback
  const message = translations[currentLanguage][messageKey] || messageKey;
  
  // Check if notification container exists, if not create it
  let container = document.getElementById('notification-container');
  if (!container) {
    container = document.createElement('div');
    container.id = 'notification-container';
    container.style.position = 'fixed';
    container.style.top = '20px';
    container.style.right = '20px';
    container.style.zIndex = '1000';
    document.body.appendChild(container);
  }
  
  // Create notification element
  const notification = document.createElement('div');
  notification.className = `notification ${type}`;
  notification.style.opacity = '0';
  notification.style.transform = 'translateX(50px)';
  
  notification.innerHTML = message;
  
  // Add close button
  const closeBtn = document.createElement('span');
  closeBtn.innerHTML = '&times;';
  closeBtn.style.position = 'absolute';
  closeBtn.style.right = '14px';
  closeBtn.style.top = '10px';
  closeBtn.style.fontSize = '1.3rem';
  closeBtn.style.cursor = 'pointer';
  closeBtn.onclick = function() {
    notification.style.opacity = '0';
    notification.style.transform = 'translateX(50px)';
    setTimeout(() => {
      container.removeChild(notification);
    }, 300);
  };
  notification.appendChild(closeBtn);
  
  // Add to container
  container.appendChild(notification);
  
  // Show with animation
  setTimeout(() => {
    notification.style.opacity = '1';
    notification.style.transform = 'translateX(0)';
  }, 10);
  
  // Auto-close after 5 seconds
  setTimeout(() => {
    if (notification.parentNode) {
      notification.style.opacity = '0';
      notification.style.transform = 'translateX(50px)';
      setTimeout(() => {
        if (notification.parentNode) {
          container.removeChild(notification);
        }
      }, 300);
    }
  }, 5000);
}

// Initialize when DOM is loaded
// Header shows which unit this is ("AquaSensys C3" / "AquaSensys M")
function loadDeviceName() {
  fetch('/api/device')
    .then(r => r.ok ? r.json() : null)
    .then(d => {
      const name = d && (d.display_name || d.name);
      if (name) document.getElementById('deviceName').textContent = name;
    })
    .catch(() => {});
}

document.addEventListener('DOMContentLoaded', () => {
  loadDeviceName();
  setupDataCard();
  connectSSE();
  
  // Add event listeners for forms
  if (formElements.wifiForm) {
    formElements.wifiForm.addEventListener('submit', (e) => saveConfiguration('wifiForm', e));
  }
  
  if (formElements.mqttForm) {
    formElements.mqttForm.addEventListener('submit', (e) => saveConfiguration('mqttForm', e));
  }
  
  if (formElements.deviceForm) {
    formElements.deviceForm.addEventListener('submit', (e) => saveConfiguration('deviceForm', e));
  }

  if (formElements.paramsForm) {
    formElements.paramsForm.addEventListener('submit', (e) => saveConfiguration('paramsForm', e));
  }

  const tzSelect = document.getElementById('timezone');
  if (tzSelect) tzSelect.addEventListener('change', updateTimezoneField);

  if (formElements.leakForm) {
    formElements.leakForm.addEventListener('submit', (e) => saveConfiguration('leakForm', e));
    formElements.leakForm.querySelectorAll('#leak_alarm_enabled, #leak_quiet_enabled')
      .forEach(input => input.addEventListener('change', updateLeakFields));
  }

  if (formElements.protectionForm) {
    formElements.protectionForm.addEventListener('submit', (e) => saveConfiguration('protectionForm', e));
  }

  if (formElements.shortCycleForm) {
    formElements.shortCycleForm.addEventListener('submit', (e) => saveConfiguration('shortCycleForm', e));
  }

  if (formElements.securityForm) {
    formElements.securityForm.addEventListener('submit', (e) => {
      const pw = document.getElementById('admin_password').value;
      const again = document.getElementById('admin_password_confirm').value;
      const clear = document.getElementById('admin_password_clear').checked;
      if (!clear && pw === '') { e.preventDefault(); showNotification('passwordEmpty', 'error'); return; }
      if (!clear && pw !== again) { e.preventDefault(); showNotification('passwordMismatch', 'error'); return; }
      saveConfiguration('securityForm', e);
    });
  }

  if (formElements.sensorsForm) {
    formElements.sensorsForm.addEventListener('submit', (e) => saveConfiguration('sensorsForm', e));
    formElements.sensorsForm.querySelectorAll('input[name="dual_pressure_sensors"], #filter_alarm_enabled')
      .forEach(input => input.addEventListener('change', updateSensorFields));
  }
  
  // Add event listeners for action buttons
  if (formElements.rebootBtn) {
    formElements.rebootBtn.addEventListener('click', rebootDevice);
  }
  
  if (formElements.factoryResetBtn) {
    formElements.factoryResetBtn.addEventListener('click', factoryReset);
  }
  
  // Language selector event listener
  const languageSelector = document.getElementById('languageSelector');
  if (languageSelector) {
    languageSelector.addEventListener('change', (e) => {
      changeLanguage(e.target.value);
    });
  }
});