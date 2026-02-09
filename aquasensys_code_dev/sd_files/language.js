// language.js - Language management for the Water Pressure Controller
let currentLanguage = 'en';

// Initialize language on page load
document.addEventListener('DOMContentLoaded', () => {
  initializeLanguage();
});

function initializeLanguage() {
  // Try to load language from localStorage
  const savedLanguage = localStorage.getItem('wpc_language');
  if (savedLanguage && ['en', 'pt'].includes(savedLanguage)) {
    currentLanguage = savedLanguage;
  } else {
    // Default to browser language if possible, otherwise fallback to English
    const browserLang = navigator.language || navigator.userLanguage;
    if (browserLang.startsWith('pt')) {
      currentLanguage = 'pt';
    } else {
      currentLanguage = 'en';
    }
    
    // Save the default language
    localStorage.setItem('wpc_language', currentLanguage);
  }
  
  // Initialize the language selector
  const languageSelector = document.getElementById('languageSelector');
  if (languageSelector) {
    languageSelector.value = currentLanguage;
  }
  
  // Apply translations to the page
  applyTranslations();
}

function changeLanguage(lang) {
  if (['en', 'pt'].includes(lang)) {
    currentLanguage = lang;
    localStorage.setItem('wpc_language', lang);
    applyTranslations();
  }
}

function applyTranslations() {
  const elements = document.querySelectorAll('[data-i18n]');
  
  // Update the document title based on the current page
  if (window.location.pathname.includes('settings.html')) {
    document.title = translations[currentLanguage].settingsTitle;
  } else {
    document.title = translations[currentLanguage].mainTitle;
  }
  
  // Update all elements with data-i18n attribute
  elements.forEach(element => {
    const key = element.getAttribute('data-i18n');
    if (translations[currentLanguage][key]) {
      if (element.tagName === 'INPUT' && element.type === 'placeholder') {
        element.placeholder = translations[currentLanguage][key];
      } else {
        element.textContent = translations[currentLanguage][key];
      }
    }
  });
  
  // Special cases for certain elements
  const ipAddressElement = document.getElementById('ipAddress');
  if (ipAddressElement && ipAddressElement.textContent === 'Connecting...') {
    ipAddressElement.textContent = translations[currentLanguage].connecting;
  }
  
  // Update placeholder for inputs
  const placeholderElements = document.querySelectorAll('[data-i18n-placeholder]');
  placeholderElements.forEach(element => {
    const key = element.getAttribute('data-i18n-placeholder');
    if (translations[currentLanguage][key]) {
      element.placeholder = translations[currentLanguage][key];
    }
  });
  
  // Update main switch button text if present
  const mainSwitchBtnText = document.getElementById('mainSwitchBtnText');
  if (mainSwitchBtnText) {
    const switchState = document.getElementById('mainSwitch');
    if (switchState && switchState.dataset.status === 'ON') {
      mainSwitchBtnText.textContent = translations[currentLanguage].turnOff;
    } else {
      mainSwitchBtnText.textContent = translations[currentLanguage].turnOn;
    }
  }
  
  // Update labels inside small tags
  const smallElements = document.querySelectorAll('small[data-i18n]');
  smallElements.forEach(element => {
    const key = element.getAttribute('data-i18n');
    if (translations[currentLanguage][key]) {
      element.textContent = translations[currentLanguage][key];
    }
  });
}

// Function to get translated text by key
function i18n(key) {
  return translations[currentLanguage][key] || key;
}
