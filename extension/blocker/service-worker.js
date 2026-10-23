const API = 'http://127.0.0.1:43127/api/blocking/config';
const RULE_MIN = 1000; // Global web catch-all block
const RULE_MAX = 1499; // Remaining IDs are profile allow-list rules
const ALLOW_MIN = 2000;
const ALLOW_MAX = 2499;
const LIMIT_MIN = 3000;
const LIMIT_MAX = 3499;
const ALLOW_MS = 5 * 60 * 1000;

async function getConfig() {
  const response = await fetch(API, { cache: 'no-store' });
  if (!response.ok) throw new Error(`Amogus API: HTTP ${response.status}`);
  const data = await response.json();
  if (!Array.isArray(data.profiles) || !Array.isArray(data.domains)) throw new Error('Nieprawidłowa odpowiedź API');
  return data;
}
function scheduleState(schedule, date = new Date()) {
  if (!schedule?.enabled) return { active: true, reason: '' };
  const days = Array.isArray(schedule.days) ? schedule.days : [];
  const day = (date.getDay() + 6) % 7; // Monday = 0
  const now = date.getHours() * 60 + date.getMinutes();
  const [sh, sm] = String(schedule.start || '08:00').split(':').map(Number);
  const [eh, em] = String(schedule.end || '16:00').split(':').map(Number);
  const start = sh * 60 + sm, end = eh * 60 + em;
  if (!Number.isFinite(start) || !Number.isFinite(end) || start === end || start < 0 || end > 1439) return { active: false, reason: 'Nieprawidłowy harmonogram' };
  let active = false;
  if (start < end) active = days.includes(day) && now >= start && now < end;
  else active = (days.includes(day) && now >= start) || (days.includes((day + 6) % 7) && now < end);
  return { active, reason: active ? `Harmonogram ${schedule.start}–${schedule.end}` : 'Poza harmonogramem' };
}

function localDayKey(date = new Date()) {
  return `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`;
}
function domainMatches(host, domain) { return host === domain || host.endsWith(`.${domain}`); }
async function trackActiveDomain() {
  const state = await chrome.storage.local.get({ enabled: false, blockerStatus: {}, domainLimits: {}, usageByDay: {}, lastUsageTick: 0, lastUsageDomain: '' });
  const now = Date.now();
  let host = '';
  try {
    const win = await chrome.windows.getLastFocused();
    if (win && win.focused) {
      const tabs = await chrome.tabs.query({ active: true, windowId: win.id });
      const url = tabs?.[0]?.url || '';
      const parsed = new URL(url);
      if (parsed.protocol === 'http:' || parsed.protocol === 'https:') host = parsed.hostname.toLowerCase();
    }
  } catch { /* no focused browser window or restricted URL */ }
  const tracking = !!state.enabled && !!state.blockerStatus?.active;
  const previousTick = Number(state.lastUsageTick || 0);
  const previousDomain = String(state.lastUsageDomain || '');
  const elapsed = previousTick > 0 ? Math.max(0, Math.min(90000, now - previousTick)) : 0;
  const limits = state.domainLimits && typeof state.domainLimits === 'object' ? state.domainLimits : {};
  const usage = state.usageByDay && typeof state.usageByDay === 'object' ? state.usageByDay : {};
  const day = localDayKey(new Date(now));
  if (tracking && previousDomain && elapsed > 0) {
    const matchingLimit = Object.keys(limits).find(domain => domainMatches(previousDomain, domain));
    if (matchingLimit) {
      const key = `${day}|${matchingLimit}`;
      usage[key] = Math.max(0, Number(usage[key] || 0)) + Math.round(elapsed / 1000);
    }
  }
  // Keep only the last eight days to bound local storage growth.
  for (const key of Object.keys(usage)) if (!key.startsWith(day.slice(0, 7)) && key.slice(0, 10) < day.slice(0, 10) && (Date.parse(`${key.slice(0,10)}T00:00:00`) < now - 8*86400000)) delete usage[key];
  await chrome.storage.local.set({ usageByDay: usage, lastUsageTick: now, lastUsageDomain: tracking ? host : '' });
}

async function applyRules() {
  await trackActiveDomain();
  const state = await chrome.storage.local.get({ enabled: false, profileId: 'default', schedule: { enabled: false, days: [0,1,2,3,4], start: '08:00', end: '16:00' }, snoozeUntil: 0, temporaryAllows: {}, domainLimits: {}, usageByDay: {} });
  const schedule = scheduleState(state.schedule);
  const snoozed = Number(state.snoozeUntil || 0) > Date.now();
  const shouldApply = !!state.enabled && schedule.active && !snoozed;
  const existing = await chrome.declarativeNetRequest.getDynamicRules();
  const removeRuleIds = existing.filter(rule => (rule.id >= RULE_MIN && rule.id <= RULE_MAX) || (rule.id >= ALLOW_MIN && rule.id <= ALLOW_MAX) || (rule.id >= LIMIT_MIN && rule.id <= LIMIT_MAX)).map(rule => rule.id);
  let addRules = [];
  let youtubeAllowedHandles = [];
  let youtubeFilteringActive = false;
  const now = Date.now();
  const temporaryAllows = state.temporaryAllows && typeof state.temporaryAllows === 'object' ? state.temporaryAllows : {};
  const activeAllows = Object.entries(temporaryAllows).filter(([domain, until]) => /^[a-z0-9.-]+\.[a-z]{2,}$/i.test(domain) && !domain.includes('..') && !domain.startsWith('.') && !domain.endsWith('.') && Number(until) > now).slice(0, ALLOW_MAX - ALLOW_MIN + 1);
  let status = { enabled: !!state.enabled, active: shouldApply, profileId: state.profileId, count: 0, limitedCount: 0, error: '', scheduleReason: snoozed ? 'Pauza czasowa' : schedule.reason, snoozeUntil: state.snoozeUntil || 0 };
  if (shouldApply) {
    try {
      const config = await getConfig();
      const profile = config.profiles.find(item => item.id === state.profileId);
      if (!profile) throw new Error('Wybrany profil nie istnieje. Wybierz profil ponownie.');
      const domains = [...new Set(config.domains.filter(item => item.profileId === profile.id).map(item => String(item.domain).toLowerCase()).filter(domain => /^[a-z0-9.-]+\.[a-z]{2,}$/i.test(domain) && !domain.includes('..') && !domain.startsWith('.') && !domain.endsWith('.') && domain !== '127.0.0.1' && domain !== 'localhost'))].slice(0, RULE_MAX - RULE_MIN + 1);
      youtubeAllowedHandles = [...new Set((Array.isArray(config.youtubeChannels) ? config.youtubeChannels : []).filter(item => item.profileId === profile.id).map(item => String(item.handle || '').trim().replace(/^@/, '').toLowerCase()).filter(handle => /^[a-z0-9._-]{3,30}$/.test(handle)))];
      youtubeFilteringActive = domains.some(domain => domainMatches('www.youtube.com', domain) || domainMatches('youtube.com', domain));
      // Spec-compatible exception/allow-list mode: deny every HTTP(S) main-frame
      // navigation, then allow only domains listed in the selected profile.
      addRules = [{ id: RULE_MIN, priority: 1, action: { type: 'redirect', redirect: { extensionPath: '/blocked.html' } }, condition: { urlFilter: '|http', resourceTypes: ['main_frame'] } }];
      addRules.push(...domains.slice(0, RULE_MAX - RULE_MIN).map((domain, index) => ({ id: RULE_MIN + 1 + index, priority: 2, action: { type: 'allow' }, condition: { urlFilter: `||${domain}^`, resourceTypes: ['main_frame'] } })));
      const limits = state.domainLimits && typeof state.domainLimits === 'object' ? state.domainLimits : {};
      const usage = state.usageByDay && typeof state.usageByDay === 'object' ? state.usageByDay : {};
      const day = localDayKey();
      const limitedDomains = domains.filter(domain => {
        const limit = Number(limits[domain]);
        return Number.isFinite(limit) && limit >= 1 && limit <= 1440 && Number(usage[`${day}|${domain}`] || 0) >= limit * 60;
      }).slice(0, LIMIT_MAX - LIMIT_MIN + 1);
      if (shouldApply) addRules.push(...limitedDomains.map((domain, index) => ({ id: LIMIT_MIN + index, priority: 3, action: { type: 'redirect', redirect: { extensionPath: '/blocked.html' } }, condition: { urlFilter: `||${domain}^`, resourceTypes: ['main_frame'] } })));
      if (state.enabled && schedule.active && !snoozed) {
        addRules.push(...activeAllows.filter(([domain]) => !domains.some(allowed => domainMatches(domain, allowed))).map(([domain], index) => ({ id: ALLOW_MIN + index, priority: 4, action: { type: 'allow' }, condition: { urlFilter: `||${domain}^`, resourceTypes: ['main_frame'] } })));
      }
      status.count = addRules.length; status.allowedDomains = domains.length; status.limitedCount = limitedDomains.length; status.temporaryAllows = activeAllows.length; status.profileName = profile.name; status.domainLimits = limits; status.usageByDay = usage; status.youtubeFilteringActive = youtubeFilteringActive; status.youtubeAllowedChannels = youtubeAllowedHandles.length;
    } catch (error) {
      status.error = error?.message || 'Nie można pobrać reguł z Amogus Core.';
      await chrome.storage.local.set({ blockerStatus: status });
      return status;
    }
  }
  await chrome.declarativeNetRequest.updateDynamicRules({ removeRuleIds, addRules });
  await chrome.storage.local.set({ blockerStatus: status, youtubeAllowedHandles, youtubeFilteringActive: shouldApply && youtubeFilteringActive, temporaryAllows: Object.fromEntries(Object.entries(temporaryAllows).filter(([, until]) => Number(until) > now)) });
  return status;
}
chrome.runtime.onInstalled.addListener(async () => {
  const saved = await chrome.storage.local.get(['enabled', 'profileId', 'schedule', 'snoozeUntil']);
  const initial = {};
  if (typeof saved.enabled !== 'boolean') initial.enabled = false;
  if (typeof saved.profileId !== 'string') initial.profileId = 'default';
  if (!saved.schedule) initial.schedule = { enabled: false, days: [0,1,2,3,4], start: '08:00', end: '16:00' };
  if (typeof saved.snoozeUntil !== 'number') initial.snoozeUntil = 0;
  await chrome.storage.local.set(initial);
  chrome.alarms.create('amogus-refresh', { periodInMinutes: 1 });
  await applyRules();
});
chrome.runtime.onStartup.addListener(() => { chrome.alarms.create('amogus-refresh', { periodInMinutes: 1 }); void applyRules(); });
chrome.alarms.onAlarm.addListener(alarm => { if (alarm.name === 'amogus-refresh') void applyRules(); });
chrome.storage.onChanged.addListener((changes, area) => { if (area === 'local' && (changes.enabled || changes.profileId || changes.schedule || changes.snoozeUntil || changes.domainLimits)) void applyRules(); });
chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type === 'AMOGUS_REFRESH') { applyRules().then(sendResponse).catch(error => sendResponse({ error: error?.message || 'Błąd aktualizacji' })); return true; }
  if (message?.type === 'AMOGUS_TEMP_ALLOW') {
    const domain = String(message.domain || '').trim().toLowerCase().replace(/^https?:\/\//, '').split('/')[0].replace(/^www\./, '');
    if (!/^[a-z0-9.-]+\.[a-z]{2,}$/i.test(domain) || domain.includes('..') || domain.startsWith('.') || domain.endsWith('.') || domain === 'localhost' || domain === '127.0.0.1') { sendResponse({ error: 'Podaj poprawną domenę, np. example.com.' }); return false; }
    (async () => { const current = await chrome.storage.local.get({ temporaryAllows: {} }); const next = current.temporaryAllows && typeof current.temporaryAllows === 'object' ? current.temporaryAllows : {}; next[domain] = Date.now() + ALLOW_MS; await chrome.storage.local.set({ temporaryAllows: next }); const state = await applyRules(); return { ok: true, domain, expiresAt: next[domain], state }; })().then(sendResponse).catch(error => sendResponse({ error: error?.message || 'Nie udało się dodać wyjątku.' })); return true;
  }
});

// Debug match events are available for unpacked/developer-installed extensions when
// declarativeNetRequestFeedback permission is granted. This log is local to the browser.
chrome.declarativeNetRequest.onRuleMatchedDebug?.addListener(async (info) => {
  try {
    const ruleId = Number(info?.rule?.ruleId);
    if (!Number.isInteger(ruleId) || !(ruleId === RULE_MIN || (ruleId >= LIMIT_MIN && ruleId <= LIMIT_MAX))) return;
    const url = String(info?.request?.url || '');
    let host = '';
    try { host = new URL(url).hostname.toLowerCase(); } catch { return; }
    const now = Date.now();
    const state = await chrome.storage.local.get({ blockedHistory: [] });
    const history = Array.isArray(state.blockedHistory) ? state.blockedHistory : [];
    // Avoid repeated duplicate entries from identical requests in a very short interval.
    const previous = history[0];
    if (previous && previous.host === host && now - Number(previous.timestamp || 0) < 1500) return;
    history.unshift({ host, timestamp: now, profileId: (await chrome.storage.local.get({ profileId: 'default' })).profileId, url: url.slice(0, 2048) });
    await chrome.storage.local.set({ blockedHistory: history.slice(0, 100) });
  } catch { /* Logging must never interfere with blocking. */ }
});
