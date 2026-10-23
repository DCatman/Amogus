const API = 'http://127.0.0.1:43127/api/blocking/config';
const $ = id => document.getElementById(id);
const profile = $('profile'), enabled = $('enabled'), status = $('status'), count = $('count');
let profiles = [];
let domainRows = [];
let youtubeRows = [];

function renderExceptions(items) {
  const root = $('exceptionList'); root.replaceChildren();
  const active = Object.entries(items && typeof items === 'object' ? items : {}).filter(([, until]) => Number(until) > Date.now()).sort((a,b) => Number(a[1])-Number(b[1]));
  if (!active.length) { root.textContent = 'Brak aktywnych wyjątków.'; return; }
  for (const [domain, until] of active) { const row = document.createElement('div'); row.className = 'exception-item'; const label = document.createElement('span'); label.textContent = domain; const time = document.createElement('small'); time.textContent = `do ${new Date(Number(until)).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'})}`; const remove = document.createElement('button'); remove.type = 'button'; remove.className = 'secondary'; remove.textContent = 'Usuń'; remove.addEventListener('click', async () => { const state = await chrome.storage.local.get({temporaryAllows:{}}); delete state.temporaryAllows[domain]; await chrome.storage.local.set({temporaryAllows:state.temporaryAllows}); await load(); }); row.append(label,time,remove); root.append(row); }
}

function renderHistory(items) {
  const list = $('historyList'); list.replaceChildren();
  const allRows = Array.isArray(items) ? items : [];
  const query = String($('historySearch')?.value || '').trim().toLowerCase();
  const filtered = query ? allRows.filter(item => String(item.host || '').toLowerCase().includes(query)) : allRows;
  const rows = filtered.slice(0, 20);
  $('historyHint').textContent = rows.length ? `${rows.length} widocznych · ${allRows.length} wpisów w lokalnym dzienniku.` : (query ? 'Brak wyników dla tego filtra.' : 'Brak zarejestrowanych blokad. Dziennik wymaga rozszerzenia załadowanego w trybie dewelopera.');
  if (!rows.length) { const li = document.createElement('li'); li.className = 'history-empty'; li.textContent = 'Historia jest pusta.'; list.append(li); return; }
  for (const item of rows) {
    const li = document.createElement('li');
    const host = document.createElement('span'); host.className = 'history-host'; host.textContent = item.host || 'nieznana domena';
    const time = document.createElement('time'); time.className = 'history-time';
    const date = new Date(Number(item.timestamp)); time.textContent = Number.isFinite(date.getTime()) ? date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) : '';
    li.append(host, time); list.append(li);
  }
}


function todayKey() { const d = new Date(); return `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`; }
function validDomain(raw) {
  const domain = String(raw || '').trim().toLowerCase().replace(/^https?:\/\//, '').split('/')[0].replace(/^www\./, '');
  if (!/^[a-z0-9.-]+\.[a-z]{2,}$/i.test(domain) || domain.includes('..') || domain.startsWith('.') || domain.endsWith('.') || domain === 'localhost' || domain === '127.0.0.1') return '';
  return domain;
}
function renderLimits(limits, usage) {
  const root = $('limitList'); root.replaceChildren();
  const entries = Object.entries(limits && typeof limits === 'object' ? limits : {}).sort((a,b)=>a[0].localeCompare(b[0]));
  if (!entries.length) { const empty = document.createElement('div'); empty.className = 'limit-empty'; empty.textContent = 'Nie ustawiono jeszcze limitów.'; root.append(empty); return; }
  for (const [domain, rawLimit] of entries) {
    const limit = Number(rawLimit), used = Math.floor(Number(usage?.[`${todayKey()}|${domain}`] || 0) / 60);
    const row = document.createElement('div'); row.className = 'limit-item';
    const copy = document.createElement('div'); const title = document.createElement('strong'); title.textContent = domain; const meta = document.createElement('small'); meta.textContent = `${used} / ${limit} min dzisiaj${used >= limit ? ' · limit osiągnięty' : ''}`; copy.append(title,meta);
    const remove = document.createElement('button'); remove.type = 'button'; remove.className = 'secondary'; remove.textContent = 'Usuń'; remove.addEventListener('click', async () => { const state = await chrome.storage.local.get({domainLimits:{}}); const next = {...state.domainLimits}; delete next[domain]; await chrome.storage.local.set({domainLimits:next}); await load(); });
    row.append(copy,remove); root.append(row);
  }
}

function showStatus(text, kind = '') { status.textContent = text; status.className = `status ${kind}`; }
function getDays() { return [...$('days').querySelectorAll('input:checked')].map(el => Number(el.value)); }
function renderStatus(state) {
  if (!state) { showStatus('Nie otrzymano statusu silnika.', 'bad'); $('youtubeStatus').textContent = 'Brak statusu silnika.'; return; }
  if (state.error) { showStatus(state.error, 'bad'); count.textContent = '—'; $('youtubeStatus').textContent = 'Nie udało się zsynchronizować ustawień kanałów.'; return; }
  count.textContent = state.active ? String(state.count ?? 0) : '0';
  $('ruleSummary').textContent = state.active ? `${state.allowedDomains ?? 0} domen z listy wyjątków · ${state.limitedCount ?? 0} limitów osiągniętych · ${state.temporaryAllows ?? 0} wyjątków czasowych.` : 'Reguły są nieaktywne poza harmonogramem lub podczas pauzy.';
  const channelCount = youtubeRows.filter(item => item.profileId === state.profileId).length;
  $('youtubeStatus').textContent = state.youtubeFilteringActive ? `Aktywny dla profilu ${state.profileName || state.profileId}: ${state.youtubeAllowedChannels ?? channelCount} dozwolonych kanałów. Kanały bez dokładnego uchwytu @ są blokowane.` : `Nieaktywny. Dodaj youtube.com do profilu i włącz blokowanie w tym profilu. Lista ma ${channelCount} dozwolonych kanałów.`;
  if (state.active) showStatus(`Blokowanie aktywne: ${state.profileName || profile.selectedOptions[0]?.textContent || 'profil'}. Pozostałe domeny HTTP(S) są blokowane.`, 'good');
  else if (state.enabled) showStatus(state.scheduleReason === 'Pauza czasowa' ? 'Blokowanie wstrzymane czasowo.' : `Nieaktywne: ${state.scheduleReason || 'harmonogram'}.`, '');
  else showStatus('Blokowanie jest wyłączone.', '');
}
async function load() {
  profile.disabled = true; enabled.disabled = true;
  try {
    const response = await fetch(API, { cache: 'no-store' });
    if (!response.ok) throw new Error(`Backend zwrócił HTTP ${response.status}`);
    const config = await response.json(); profiles = config.profiles || []; domainRows = config.domains || []; youtubeRows = config.youtubeChannels || [];
    const saved = await chrome.storage.local.get({ enabled: false, profileId: 'default', schedule: { enabled: false, days: [0,1,2,3,4], start: '08:00', end: '16:00' }, snoozeUntil: 0, blockedHistory: [], temporaryAllows: {}, domainLimits: {}, usageByDay: {} });
    renderHistory(saved.blockedHistory); renderExceptions(saved.temporaryAllows); renderLimits(saved.domainLimits, saved.usageByDay);
    $('ruleSummary').textContent = 'Połączono z lokalnym silnikiem.';
    profile.replaceChildren();
    for (const item of profiles) { const option = document.createElement('option'); option.value = item.id; option.textContent = item.name; profile.append(option); }
    if (!profiles.length) throw new Error('Utwórz najpierw profil w Amogus.');
    profile.value = profiles.some(p => p.id === saved.profileId) ? saved.profileId : profiles[0].id;
    if (profile.value !== saved.profileId) await chrome.storage.local.set({ profileId: profile.value });
    enabled.checked = !!saved.enabled; $('scheduleEnabled').checked = !!saved.schedule.enabled;
    $('startTime').value = saved.schedule.start || '08:00'; $('endTime').value = saved.schedule.end || '16:00';
    $('days').querySelectorAll('input').forEach(el => { el.checked = (saved.schedule.days || []).includes(Number(el.value)); });
    profile.disabled = false; enabled.disabled = false;
    renderStatus(await chrome.runtime.sendMessage({ type: 'AMOGUS_REFRESH' }));
  } catch (error) { showStatus(`${error?.message || 'Nie można połączyć z Amogus Core.'} Uruchom backend i spróbuj ponownie.`, 'bad'); count.textContent = '—'; }
}
profile.addEventListener('change', async () => { await chrome.storage.local.set({ profileId: profile.value }); await load(); });
enabled.addEventListener('change', async () => { await chrome.storage.local.set({ enabled: enabled.checked, snoozeUntil: 0 }); await load(); });
$('saveSchedule').addEventListener('click', async () => {
  const start = $('startTime').value, end = $('endTime').value, days = getDays();
  if (!start || !end || start === end) { showStatus('Wybierz poprawny zakres godzin.', 'bad'); return; }
  if (!days.length && $('scheduleEnabled').checked) { showStatus('Wybierz przynajmniej jeden dzień.', 'bad'); return; }
  await chrome.storage.local.set({ schedule: { enabled: $('scheduleEnabled').checked, days, start, end } });
  await load();
});
document.querySelectorAll('[data-minutes]').forEach(button => button.addEventListener('click', async () => {
  const minutes = Number(button.dataset.minutes); await chrome.storage.local.set({ enabled: true, snoozeUntil: Date.now() + minutes * 60000 }); await load();
}));
$('allowForFive').addEventListener('click', async () => { const domain = $('allowDomain').value.trim(); const result = await chrome.runtime.sendMessage({type:'AMOGUS_TEMP_ALLOW', domain}); if (result?.error) { $('exceptionStatus').textContent = result.error; $('exceptionStatus').className = 'hint bad'; return; } $('allowDomain').value = ''; $('exceptionStatus').textContent = `Wyjątek dla ${result.domain} aktywny przez 5 minut.`; $('exceptionStatus').className = 'hint good'; await load(); });
$('allowDomain').addEventListener('keydown', event => { if (event.key === 'Enter') $('allowForFive').click(); });
$('clearHistory').addEventListener('click', async () => { await chrome.storage.local.set({ blockedHistory: [] }); renderHistory([]); });
chrome.storage.onChanged.addListener((changes, area) => { if (area === 'local' && changes.blockedHistory) renderHistory(changes.blockedHistory.newValue || []); if (area === 'local' && changes.temporaryAllows) renderExceptions(changes.temporaryAllows.newValue || {}); if (area === 'local' && (changes.domainLimits || changes.usageByDay)) { chrome.storage.local.get({domainLimits:{},usageByDay:{}}).then(state=>renderLimits(state.domainLimits,state.usageByDay)); } });

$('limitForm').addEventListener('submit', async event => {
  event.preventDefault();
  const domain = validDomain($('limitDomain').value);
  const minutes = Number($('limitMinutes').value);
  if (!domain) { $('limitStatus').textContent = 'Podaj poprawną domenę, np. youtube.com.'; $('limitStatus').className = 'hint bad'; return; }
  if (!Number.isInteger(minutes) || minutes < 1 || minutes > 1440) { $('limitStatus').textContent = 'Limit musi wynosić od 1 do 1440 minut.'; $('limitStatus').className = 'hint bad'; return; }
  const belongs = domainRows.some(item => item.profileId === profile.value && String(item.domain || '').toLowerCase() === domain);
  if (!belongs) { $('limitStatus').textContent = 'Najpierw dodaj tę domenę do wybranego profilu blokowania w aplikacji Amogus.'; $('limitStatus').className = 'hint bad'; return; }
  const state = await chrome.storage.local.get({domainLimits:{}}); const limits = state.domainLimits && typeof state.domainLimits === 'object' ? {...state.domainLimits} : {};
  limits[domain] = minutes; await chrome.storage.local.set({domainLimits:limits}); $('limitDomain').value = '';
  $('limitStatus').textContent = `Zapisano limit ${minutes} min dziennie dla ${domain}.`; $('limitStatus').className = 'hint good'; await load();
});

$('refresh').addEventListener('click', load);
void load();


$('historySearch').addEventListener('input', async () => {
  const state = await chrome.storage.local.get({ blockedHistory: [] });
  renderHistory(state.blockedHistory);
});
$('exportHistory').addEventListener('click', async () => {
  const state = await chrome.storage.local.get({ blockedHistory: [] });
  const rows = Array.isArray(state.blockedHistory) ? state.blockedHistory : [];
  const quote = value => `"${String(value ?? '').replace(/"/g, '""')}"`;
  const csv = ['timestamp,local_time,host,url,profile_id', ...rows.map(item => [item.timestamp, new Date(Number(item.timestamp)).toISOString(), item.host, item.url, item.profileId].map(quote).join(','))].join('\r\n');
  downloadText(`amogus-block-history-${todayKey()}.csv`, '\ufeff' + csv, 'text/csv;charset=utf-8');
});
function downloadText(filename, content, type) {
  const blob = new Blob([content], { type }); const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a'); anchor.href = url; anchor.download = filename;
  document.body.append(anchor); anchor.click(); anchor.remove(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
$('exportSettings').addEventListener('click', async () => {
  const state = await chrome.storage.local.get({ enabled: false, profileId: 'default', schedule: { enabled: false, days: [0,1,2,3,4], start: '08:00', end: '16:00' }, snoozeUntil: 0, blockedHistory: [], temporaryAllows: {}, domainLimits: {}, usageByDay: {} });
  const backup = { format: 'amogus-blocker-settings', version: 1, exportedAt: new Date().toISOString(), enabled: !!state.enabled, profileId: state.profileId, schedule: state.schedule, snoozeUntil: Number(state.snoozeUntil || 0), blockedHistory: (Array.isArray(state.blockedHistory) ? state.blockedHistory : []).slice(0,100), temporaryAllows: state.temporaryAllows || {}, domainLimits: state.domainLimits || {}, usageByDay: state.usageByDay || {} };
  downloadText(`amogus-blocker-settings-${todayKey()}.json`, JSON.stringify(backup, null, 2), 'application/json');
  $('settingsStatus').textContent = 'Eksport ustawień gotowy.'; $('settingsStatus').className = 'hint good';
});
$('importSettingsButton').addEventListener('click', () => $('settingsInput').click());
$('settingsInput').addEventListener('change', async event => {
  const file = event.target.files?.[0]; if (!file) return;
  try {
    if (file.size > 2 * 1024 * 1024) throw new Error('Plik przekracza limit 2 MB.');
    const backup = JSON.parse(await file.text());
    if (!backup || backup.format !== 'amogus-blocker-settings' || backup.version !== 1 || typeof backup.enabled !== 'boolean' || typeof backup.profileId !== 'string' || !backup.schedule || !Array.isArray(backup.schedule.days) || !/^\d{2}:\d{2}$/.test(backup.schedule.start) || !/^\d{2}:\d{2}$/.test(backup.schedule.end)) throw new Error('To nie jest obsługiwana kopia ustawień Amogus Blocker.');
    if (!profiles.some(item => item.id === backup.profileId)) throw new Error('Profil z kopii nie istnieje w aktualnej bazie Amogus. Najpierw odtwórz profile lub wybierz profil ręcznie.');
    const days = [...new Set(backup.schedule.days.map(Number))].filter(day => Number.isInteger(day) && day >= 0 && day <= 6);
    const domainLimits = {};
    if (backup.domainLimits && typeof backup.domainLimits === 'object') for (const [raw, rawLimit] of Object.entries(backup.domainLimits)) { const domain = validDomain(raw), limit = Number(rawLimit); if (domain && Number.isInteger(limit) && limit >= 1 && limit <= 1440) domainLimits[domain] = limit; }
    const usageByDay = {};
    if (backup.usageByDay && typeof backup.usageByDay === 'object') for (const [key, value] of Object.entries(backup.usageByDay)) if (/^\d{4}-\d{2}-\d{2}\|[a-z0-9.-]+$/i.test(key) && Number.isFinite(Number(value)) && Number(value) >= 0) usageByDay[key] = Math.floor(Number(value));
    const history = Array.isArray(backup.blockedHistory) ? backup.blockedHistory.filter(item => item && typeof item.host === 'string' && Number.isFinite(Number(item.timestamp))).slice(0,100) : [];
    const temporaryAllows = {};
    if (backup.temporaryAllows && typeof backup.temporaryAllows === 'object') for (const [raw, until] of Object.entries(backup.temporaryAllows)) { const domain = validDomain(raw); if (domain && Number(until) > Date.now()) temporaryAllows[domain] = Number(until); }
    await chrome.storage.local.set({ enabled: backup.enabled, profileId: backup.profileId, schedule: { enabled: !!backup.schedule.enabled, days, start: backup.schedule.start, end: backup.schedule.end }, snoozeUntil: Math.max(0, Number(backup.snoozeUntil || 0)), domainLimits, usageByDay, blockedHistory: history, temporaryAllows });
    $('settingsStatus').textContent = 'Ustawienia zaimportowane.'; $('settingsStatus').className = 'hint good'; await load();
  } catch (error) { $('settingsStatus').textContent = error?.message || 'Nie udało się zaimportować ustawień.'; $('settingsStatus').className = 'hint bad'; }
  finally { event.target.value = ''; }
});
