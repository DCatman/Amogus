/*
 * Plik: background.js
 * Opis: Glowny proces tla wtyczki Amogus Blocker Companion (Manifest V3).
 *       Cyklicznie pobiera reguly z serwera Amogus (port 13370), zapisuje je w chrome.storage.local
 *       oraz wysyla heartbeat do aplikacji bez ograniczen Mixed Content / CSP.
 */

function detectBrowserName() {
  const ua = navigator.userAgent;
  if (ua.includes('Edg/')) return 'Edge';
  if (ua.includes('OPR/') || ua.includes('Opera')) return 'Opera';
  if (ua.includes('Brave')) return 'Brave';
  if (ua.includes('Chrome/')) return 'Chrome';
  return 'Chrome';
}

function syncRulesWithBackend() {
  const browser = encodeURIComponent(detectBrowserName());
  fetch(`http://127.0.0.1:13370/rules?browser=${browser}`)
    .then(res => {
      if (!res.ok) throw new Error("HTTP " + res.status);
      return res.json();
    })
    .then(data => {
      chrome.storage.local.set({
        blockingActive: data.blockingActive === true,
        exceptions: data.exceptions || []
      });
    })
    .catch(() => {});
}

// Cykliczny heartbeat
setInterval(syncRulesWithBackend, 1500);
syncRulesWithBackend();

// Utrzymanie aktywnosci Service Workera
chrome.alarms.create("amogus_heartbeat", { periodInMinutes: 0.2 });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === "amogus_heartbeat") {
    syncRulesWithBackend();
  }
});

chrome.tabs.onActivated.addListener(syncRulesWithBackend);
chrome.tabs.onCreated.addListener(syncRulesWithBackend);
chrome.tabs.onUpdated.addListener(syncRulesWithBackend);
chrome.windows.onFocusChanged.addListener(syncRulesWithBackend);

// Obsluga komunikatow z kart
chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  if (request.action === "wakeHeartbeat") {
    syncRulesWithBackend();
    sendResponse({ ok: true });
    return false;
  }
  if (request.action === "getRules") {
    chrome.storage.local.get(['blockingActive', 'exceptions'], (data) => {
      sendResponse({
        blockingActive: data.blockingActive === true,
        exceptions: data.exceptions || []
      });
    });
    return true;
  }
});
