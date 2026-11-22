// theme.js — light / dark theme for every page.
// Loaded in <head>, before the page is drawn, so a dark page never flashes white.
// "Auto" (the default) follows the system setting; "Light" and "Dark" are
// remembered per browser. style.css does the rest: it only swaps colour tokens.
(function () {
  var KEY = 'aquasensys_theme';

  function saved() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }

  function apply(mode) {
    if (mode === 'light' || mode === 'dark') {
      document.documentElement.setAttribute('data-theme', mode);
    } else {
      document.documentElement.removeAttribute('data-theme');
    }
  }

  apply(saved());

  window.setTheme = function (mode) {
    try {
      if (mode === 'light' || mode === 'dark') localStorage.setItem(KEY, mode);
      else localStorage.removeItem(KEY);
    } catch (e) { /* private mode: applies to this page only */ }
    apply(mode);
  };

  // Put the selector next to the language one. This runs before language.js
  // translates the page, so the labels below get translated with the rest.
  document.addEventListener('DOMContentLoaded', function () {
    var language = document.querySelector('.language-selector');
    if (!language || document.getElementById('themeSelector')) return;

    var wrap = document.createElement('div');
    wrap.className = 'language-selector theme-selector';
    wrap.innerHTML =
      '<label for="themeSelector" data-i18n="theme">Theme</label>' +
      '<select id="themeSelector">' +
      '<option value="auto" data-i18n="themeAuto">Auto</option>' +
      '<option value="light" data-i18n="themeLight">Light</option>' +
      '<option value="dark" data-i18n="themeDark">Dark</option>' +
      '</select>';
    language.parentNode.insertBefore(wrap, language.nextSibling);

    var select = wrap.querySelector('select');
    var mode = saved();
    select.value = (mode === 'light' || mode === 'dark') ? mode : 'auto';
    select.addEventListener('change', function () { window.setTheme(select.value); });
  });
})();
