// Configuration form elements
const formElements = {
  wifiForm: document.getElementById('wifiForm'),
  mqttForm: document.getElementById('mqttForm'),
  deviceForm: document.getElementById('deviceForm'),
  paramsForm: document.getElementById('paramsForm'),
  sensorsForm: document.getElementById('sensorsForm'),
  
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

  // Pressure sensors and the clogged-filter alarm
  const dual = config.dual_pressure_sensors === true;
  document.querySelector(`input[name="dual_pressure_sensors"][value="${dual}"]`).checked = true;
  document.getElementById('filter_alarm_enabled').checked = config.filter_alarm_enabled !== false;
  document.getElementById('filter_dp_warning').value = config.filter_dp_warning ?? 0.5;
  document.getElementById('filter_min_flow_lpm').value = config.filter_min_flow_lpm ?? 5;
  document.getElementById('filter_alarm_delay_s').value = config.filter_alarm_delay_s ?? 30;
  updateSensorFields();
}

// The alarm settings only apply with two sensors, its values only when it's on
function updateSensorFields() {
  const dual = document.querySelector('input[name="dual_pressure_sensors"]:checked').value === 'true';
  const alarmOn = document.getElementById('filter_alarm_enabled').checked;
  document.getElementById('filterAlarmGroup').classList.toggle('hidden', !dual);
  document.getElementById('filterAlarmFields').classList.toggle('hidden', !alarmOn);
}

function saveConfiguration(formId, event) {
  event.preventDefault();
  
  const form = document.getElementById(formId);
  const formData = new FormData(form);
  const data = {};
  
  // Fields the firmware parses as numbers; FormData would hand over strings
  const numericKeys = [
    'mqtt_port', 'min_pressure', 'max_pressure', 'pressure_offset', 'temp_offset',
    'flow_k_factor', 'flow_k_offset', 'flow_debounce_us', 'max_flow_lpm',
    'filter_dp_warning', 'filter_min_flow_lpm', 'filter_alarm_delay_s'
  ];

  // Convert FormData to a plain object
  for (const [key, value] of formData.entries()) {
    // Skip empty password fields
    if ((key === 'wifi_password' || key === 'mqtt_password') && value === '') {
      continue;
    }
    if (numericKeys.includes(key)) {
      const n = Number(value);
      if (value === '' || Number.isNaN(n)) continue;  // leave the stored value alone
      data[key] = n;
      continue;
    }
    if (key === 'dual_pressure_sensors') {
      data[key] = value === 'true';
      continue;
    }
    data[key] = value;
  }

  // An unticked checkbox is left out of FormData, so read it directly
  const alarmBox = form.querySelector('#filter_alarm_enabled');
  if (alarmBox) data.filter_alarm_enabled = alarmBox.checked;
  
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
    showNotification('saveConfigError', 'error');
  });
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