// Configuration
const config = {
  pressure: {
    min: 2,
    max: 4,
    warningThreshold: 3.5,
    dangerThreshold: 4
  },
  temperature: {
    min: 0,
    max: 50,
    warningThreshold: 40,
    dangerThreshold: 45
  },
  flow: {
    min: 0,
    max: 40
  }
};

// DOM Elements
const elements = {
  pressure: document.getElementById('pressure'),
  temperature: document.getElementById('temperature'),
  flow: document.getElementById('flow'),
  motor: document.getElementById('motor'),
  mode: document.getElementById('mode'),
  mainSwitch: document.getElementById('mainSwitch'),
  systemStatus: document.getElementById('systemStatus'),
  pressureBar: document.getElementById('pressureBar'),
  wifiStatus: document.getElementById('wifiStatus'),
  ipAddress: document.getElementById('ipAddress'),
  toggleBtn: document.getElementById('toggleBtn'),
  mainSwitchBtn: document.getElementById('mainSwitchBtn'),
  overrideBtn: document.getElementById('overrideBtn'),
  deviceType: document.getElementById('deviceType'),
  connectionSize: document.getElementById('connectionSize'),
  systemCardTitle: document.getElementById('systemCardTitle'),
  deviceName: document.getElementById('deviceName'),
  deviceModel: document.getElementById('deviceModel'),
  deviceVersion: document.getElementById('deviceVersion')
};

// Identity of the unit this page is talking to, from /api/device
let deviceInfo = null;

let eventSource;
let isConnected = false;
// "C" = Controller (motor control available), "M" = Monitor (readings only)
let deviceType = 'C';

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
}
function loadDeviceInfo() {
  fetch('/api/device')
    .then(response => {
      if (!response.ok) throw new Error(`Server returned ${response.status}`);
      return response.json();
    })
    .then(data => {
      deviceInfo = data;
      applyDeviceIdentity();
    })
    .catch(error => console.error('Error loading device info:', error));
}

// Show which unit this is. The name lives in the firmware, so until it arrives
// the page keeps the generic "AquaSensys" placeholder.
function applyDeviceIdentity() {
  if (!deviceInfo) return;

  // display_name is "AquaSensys - C3" / "AquaSensys - M" (or the user's own name);
  // fall back to the plain name for firmware older than 3.4.1
  const title = deviceInfo.display_name || deviceInfo.name;
  if (elements.deviceName && title) {
    elements.deviceName.textContent = title;
    // Stop applyTranslations() putting the generic title back on a language change
    elements.deviceName.removeAttribute('data-i18n');
    document.title = title;
  }
  if (elements.deviceModel) {
    elements.deviceModel.textContent = deviceInfo.model || '--';
  }
  if (elements.deviceVersion) {
    elements.deviceVersion.textContent = deviceInfo.version ? 'v' + deviceInfo.version : '--';
  }
}

// Monitor units have no motor: hide every control and motor-state element.
function applyDeviceType(type) {
  deviceType = type === 'M' ? 'M' : 'C';
  const isController = deviceType === 'C';

  document.querySelectorAll('.motor-only').forEach(el => {
    el.style.display = isController ? '' : 'none';
  });

  if (elements.deviceType) {
    elements.deviceType.textContent = i18n(isController ? 'controllerType' : 'monitorType');
  }

  if (elements.systemCardTitle) {
    elements.systemCardTitle.textContent =
      i18n(isController ? 'systemControls' : 'systemStatusTitle');
  }
}

// A sensor that can't be read — a disconnected temperature probe, say — arrives
// as null. Calling toFixed() on it threw, which aborted the whole update, so one
// dead probe froze flow, motor and status too. Show a dash for it instead.
function isReading(v) {
  return typeof v === 'number' && Number.isFinite(v);
}

function formatReading(v, decimals) {
  return isReading(v) ? v.toFixed(decimals) : '--';
}

function updateDashboard(data) {
  if (data.deviceType !== undefined) {
    applyDeviceType(data.deviceType);
  }

  if (data.connectionSize !== undefined && elements.connectionSize) {
    elements.connectionSize.textContent = data.connectionSize + '"';
  }

  if (data.pressure !== undefined) {
    elements.pressure.textContent = formatReading(data.pressure, 2);
    if (isReading(data.pressure)) updatePressureBar(data.pressure);
  }
  
  if (data.temperature !== undefined) {
    elements.temperature.textContent = formatReading(data.temperature, 2);
    if (isReading(data.temperature)) updateTemperatureColor(data.temperature);
  }
  
  if (data.flow !== undefined) {
    elements.flow.textContent = formatReading(data.flow, 2);
  }
  
  if (data.motor !== undefined) {
    elements.motor.textContent = data.motor ? "ON" : "OFF";
    elements.motor.dataset.status = data.motor ? "ON" : "OFF";
  }
  
  if (data.manualOverride !== undefined) {
    elements.mode.textContent = data.manualOverride ? "MANUAL" : "AUTO";
    elements.mode.dataset.status = data.manualOverride ? "MANUAL" : "AUTO";
    elements.overrideBtn.style.backgroundColor =
      data.manualOverride ? 'var(--warning)' : 'var(--secondary)';
  }

  if (data.mainSwitch !== undefined) {
    elements.mainSwitch.textContent = data.mainSwitch ? "ON" : "OFF";
    elements.mainSwitch.dataset.status = data.mainSwitch ? "ON" : "OFF";
    elements.mainSwitchBtn.style.backgroundColor =
      data.mainSwitch ? 'var(--success)' : 'var(--danger)';
  }

  if (data.error !== undefined) {
    elements.systemStatus.textContent = data.error ? "ERROR" : "OK";
    elements.systemStatus.dataset.status = data.error ? "ERROR" : "OK";
  }
}

function updatePressureBar(pressure) {
  const percentage = Math.min(100, (pressure / config.pressure.max) * 100);
  elements.pressureBar.style.setProperty('--width', `${percentage}%`);
  
  if (pressure >= config.pressure.dangerThreshold) {
    elements.pressureBar.style.setProperty('--color', 'var(--danger)');
  } else if (pressure >= config.pressure.warningThreshold) {
    elements.pressureBar.style.setProperty('--color', 'var(--warning)');
  } else {
    elements.pressureBar.style.setProperty('--color', 'var(--secondary)');
  }
}

function updateTemperatureColor(temp) {
  if (temp >= config.temperature.dangerThreshold) {
    elements.temperature.style.color = 'var(--danger)';
  } else if (temp >= config.temperature.warningThreshold) {
    elements.temperature.style.color = 'var(--warning)';
  } else {
    elements.temperature.style.color = 'var(--secondary)';
  }
}

function updateConnectionStatus(connected) {
  const wifiIcon = elements.wifiStatus.querySelector('i');
  const statusText = connected ? window.location.hostname : 'Disconnected';
  
  wifiIcon.className = connected ? 'fas fa-wifi' : 'fas fa-wifi-slash';
  wifiIcon.style.color = connected ? '#4bb543' : '#ec0b43';
  elements.ipAddress.textContent = statusText;
  
  const controlsUsable = connected && deviceType === 'C';
  elements.toggleBtn.disabled = !controlsUsable;
  elements.mainSwitchBtn.disabled = !controlsUsable;
  elements.overrideBtn.disabled = !controlsUsable;
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

  if (deviceType === 'M') {
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
    // You could add UI feedback here
  });
}

function rebootDevice() {
    fetch('/reboot', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        console.log('Reboot initiated');
        alert('Device will reboot shortly');
    });
}

// Initialize when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
  // Initialize language before connecting
  if (typeof initializeLanguage === 'function') {
    initializeLanguage();
  }
  
  loadDeviceInfo();
  connectSSE();
  
  // Set initial styles
  elements.pressureBar.style.setProperty('--width', '0%');
  elements.pressureBar.style.setProperty('--color', 'var(--secondary)');
  
  // Event listeners
  elements.toggleBtn.addEventListener('click', () => {
    sendCommand('toggle');
  });

  elements.mainSwitchBtn.addEventListener('click', () => {
    sendCommand('mainSwitch');
  });

  elements.overrideBtn.addEventListener('click', () => {
    sendCommand('override');
  });
});