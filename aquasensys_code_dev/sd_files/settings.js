// Configuration form elements
const formElements = {
  wifiForm: document.getElementById('wifiForm'),
  mqttForm: document.getElementById('mqttForm'),
  paramsForm: document.getElementById('paramsForm'),
  
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
  const wifiIcon = formElements.wifiStatus.querySelector('i');
  const statusText = connected ? window.location.hostname : i18n('connecting');
  
  wifiIcon.className = connected ? 'fas fa-wifi' : 'fas fa-wifi-slash';
  wifiIcon.style.color = connected ? '#4bb543' : '#ec0b43';
  formElements.ipAddress.textContent = statusText;
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
  
  // System parameters
  document.getElementById('min_pressure').value = config.min_pressure || 2.5;
  document.getElementById('max_pressure').value = config.max_pressure || 3.5;
  document.getElementById('pressure_offset').value = config.pressure_offset || 0;
  document.getElementById('temp_offset').value = config.temp_offset || 0;
}

function saveConfiguration(formId, event) {
  event.preventDefault();
  
  const form = document.getElementById(formId);
  const formData = new FormData(form);
  const data = {};
  
  // Convert FormData to a plain object
  for (const [key, value] of formData.entries()) {
    // Skip empty password fields
    if ((key === 'wifi_password' || key === 'mqtt_password') && value === '') {
      continue;
    }
    data[key] = value;
  }
  
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
  notification.style.backgroundColor = 
    type === 'success' ? '#4bb543' : 
    type === 'error' ? '#ec0b43' : 
    type === 'warning' ? '#ff9505' : '#0f8b8d';
  notification.style.color = 'white';
  notification.style.padding = '15px 20px';
  notification.style.marginBottom = '10px';
  notification.style.borderRadius = '5px';
  notification.style.boxShadow = '0 2px 5px rgba(0,0,0,0.2)';
  notification.style.transition = 'transform 0.3s ease, opacity 0.3s ease';
  notification.style.opacity = '0';
  notification.style.transform = 'translateX(50px)';
  
  notification.innerHTML = message;
  
  // Add close button
  const closeBtn = document.createElement('span');
  closeBtn.innerHTML = '&times;';
  closeBtn.style.marginLeft = '10px';
  closeBtn.style.cursor = 'pointer';
  closeBtn.style.float = 'right';
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
document.addEventListener('DOMContentLoaded', () => {
  connectSSE();
  
  // Add event listeners for forms
  if (formElements.wifiForm) {
    formElements.wifiForm.addEventListener('submit', (e) => saveConfiguration('wifiForm', e));
  }
  
  if (formElements.mqttForm) {
    formElements.mqttForm.addEventListener('submit', (e) => saveConfiguration('mqttForm', e));
  }
  
  if (formElements.paramsForm) {
    formElements.paramsForm.addEventListener('submit', (e) => saveConfiguration('paramsForm', e));
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