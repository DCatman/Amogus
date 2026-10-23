import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import FullCalendar from '@fullcalendar/react';
import timeGridPlugin from '@fullcalendar/timegrid';
import dayGridPlugin from '@fullcalendar/daygrid';
import interactionPlugin from '@fullcalendar/interaction';
import type { EventInput } from '@fullcalendar/core';

type Profile = { id: string; name: string; locked: boolean; createdAt?: string; updatedAt?: string };
type ProfileDomain = { id: string; domain: string; createdAt?: string };
type YouTubeChannel = { id: string; handle: string; createdAt?: string };
type Subject = { id: string; name: string; color: string; isLecture: boolean; notes: string; createdAt?: string; updatedAt?: string };
type OverdueTask = { id: string; title: string; dueAt: string; notes: string; completed: boolean; priority: 'low'|'normal'|'high'|'urgent'; category: string; createdAt?: string };
type SubjectSession = { id: string; subjectId: string; subjectName: string; color: string; weekday: number; startTime: string; endTime: string; room: string; sessionType: string };
type FocusSession = { id: string; subjectId: string; subjectName: string; durationSeconds: number; startedAt: string; endedAt: string; note: string };
type StudyCard = { id: string; subjectId: string; subjectName: string; front: string; back: string; dueAt: string; intervalDays: number; repetitions: number; lastReviewed: string; createdAt: string };
type StudyStage = { id: string; title: string; stageOrder: number; dueAt: string; completed: boolean; notes: string; completedAt: string };
type StudyPlan = { id: string; subjectId: string; subjectName: string; title: string; targetDate: string; notes: string; stages: StudyStage[]; createdAt: string; updatedAt: string };
type GoogleStatus = { configured: boolean; authenticated: boolean; lastSync: string; lastError: string; credentialsPath: string };
type WindowsRule = { id: string; type: 'process'|'title'|'path'; pattern: string; enabled: boolean; createdAt: string };
type WindowsStatus = { supported: boolean; blockerEnabled: boolean; autostart: boolean; blockedCount: number; lastAction: string; message?: string };
type AppPage = 'focus' | 'calendar' | 'blocks' | 'subjects' | 'overdue' | 'integrations' | 'settings';
type DialogState =
  | { kind: 'event'; mode: 'create' | 'edit'; id?: string; title: string; start: string; end: string; eventType: string; repeatWeekly: boolean; repeatUntil: string }
  | { kind: 'confirm'; title: string; message: string; confirmLabel?: string; danger?: boolean; onConfirm: () => void }
  | { kind: 'info'; title: string; description: string; sections?: string[] }
  | null;
const API = 'http://127.0.0.1:43127/api';
const weekdays = ['Poniedziałek','Wtorek','Środa','Czwartek','Piątek','Sobota','Niedziela'];
function currentMondayInput(): string {
  const date = new Date();
  date.setHours(12, 0, 0, 0);
  const day = (date.getDay() + 6) % 7;
  date.setDate(date.getDate() - day);
  return `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`;
}
const pageInfo: Record<AppPage, { title: string; description: string; icon: string }> = {
  focus: { title: 'Skupienie', description: 'Sesje pracy, przerwy i historia Twojego skupienia.', icon: '◷' },
  calendar: { title: 'Kalendarz', description: 'Zaplanuj dzień i miej kontrolę nad swoim czasem.', icon: '▦' },
  blocks: { title: 'Blokady', description: 'Profile blokowania i listy dozwolonych domen.', icon: '◈' },
  subjects: { title: 'Przedmioty', description: 'Przejrzysty widok obszarów nauki i pracy.', icon: '▤' },
  overdue: { title: 'Zadania i terminy', description: 'Śledź terminy, zaległości i ukończone zadania.', icon: '☷' },
  integrations: { title: 'Integracje', description: 'Połączone usługi i narzędzia pomocnicze.', icon: '⌘' },
  settings: { title: 'Ustawienia', description: 'Preferencje wyglądu i informacje o aplikacji.', icon: '⚙' },
};
function formatDueDate(value: string) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat('pl-PL', { dateStyle: 'medium', timeStyle: 'short' }).format(date);
}
function toLocalInput(value: Date | string) {
  const date = value instanceof Date ? value : new Date(value);
  if (Number.isNaN(date.getTime())) return '';
  const offset = date.getTimezoneOffset();
  return new Date(date.getTime() - offset * 60000).toISOString().slice(0, 16);
}

export default function App() {
  const [health, setHealth] = useState<'checking'|'online'|'offline'>('checking');
  const [profiles, setProfiles] = useState<Profile[]>([]);
  const [name, setName] = useState('');
  const [selectedProfileId, setSelectedProfileId] = useState('');
  const [blockEditorTab, setBlockEditorTab] = useState<'websites'|'exceptions'|'apps'|'breaks'|'schedule'>('websites');
  const [profileDomains, setProfileDomains] = useState<ProfileDomain[]>([]);
  const [youtubeChannels, setYoutubeChannels] = useState<YouTubeChannel[]>([]);
  const [youtubeHandleInput, setYoutubeHandleInput] = useState('');
  const [savingYoutubeChannel, setSavingYoutubeChannel] = useState(false);
  const [domainInput, setDomainInput] = useState('');
  const [savingDomain, setSavingDomain] = useState(false);
  const [error, setError] = useState('');
  const [active, setActive] = useState<AppPage>('calendar');
  const [events, setEvents] = useState<EventInput[]>([]);
  const [tasks, setTasks] = useState<OverdueTask[]>([]);
  const [subjects, setSubjects] = useState<Subject[]>([]);
  const [sessions, setSessions] = useState<SubjectSession[]>([]);
  const [focusHistory, setFocusHistory] = useState<FocusSession[]>([]);
  const [studyCards, setStudyCards] = useState<StudyCard[]>([]);
  const [studyPlans, setStudyPlans] = useState<StudyPlan[]>([]);
  const [studyPlanTitle, setStudyPlanTitle] = useState('');
  const [studyPlanSubject, setStudyPlanSubject] = useState('');
  const [studyPlanDate, setStudyPlanDate] = useState('');
  const [studyPlanNotes, setStudyPlanNotes] = useState('');
  const [studyPlanSaving, setStudyPlanSaving] = useState(false);
  const [stageDrafts, setStageDrafts] = useState<Record<string,{title:string;dueAt:string}>>({});
  const [cardFront, setCardFront] = useState('');
  const [cardBack, setCardBack] = useState('');
  const [cardSubjectId, setCardSubjectId] = useState('');
  const [revealedCards, setRevealedCards] = useState<string[]>([]);
  const [savingCard, setSavingCard] = useState(false);
  const [focusMinutes, setFocusMinutes] = useState(25);
  const [focusRemaining, setFocusRemaining] = useState(25 * 60);
  const [focusRunning, setFocusRunning] = useState(false);
  const [focusEndAt, setFocusEndAt] = useState<number | null>(null);
  const focusStartedAtRef = useRef<string | null>(null);
  const [focusSubjectId, setFocusSubjectId] = useState('');
  const [focusNote, setFocusNote] = useState('');
  const [focusSaving, setFocusSaving] = useState(false);
  const [sessionSubjectId, setSessionSubjectId] = useState('');
  const [sessionWeekday, setSessionWeekday] = useState('0');
  const [sessionStart, setSessionStart] = useState('09:00');
  const [sessionEnd, setSessionEnd] = useState('10:30');
  const [sessionRoom, setSessionRoom] = useState('');
  const [sessionType, setSessionType] = useState('Wykład');
  const [savingSession, setSavingSession] = useState(false);
  const [editingSessionId, setEditingSessionId] = useState<string | null>(null);
  const [taskSearch, setTaskSearch] = useState('');
  const [taskFilter, setTaskFilter] = useState<'all'|'open'|'late'|'done'>('all');
  const [taskPriorityFilter, setTaskPriorityFilter] = useState<'all'|'low'|'normal'|'high'|'urgent'>('all');
  const [scheduleWeekStart, setScheduleWeekStart] = useState(currentMondayInput);
  const [generatingSchedule, setGeneratingSchedule] = useState(false);
  const [subjectName, setSubjectName] = useState('');
  const [savingSubject, setSavingSubject] = useState(false);
  const [taskTitle, setTaskTitle] = useState('');
  const [taskDueAt, setTaskDueAt] = useState('');
  const [taskNotes, setTaskNotes] = useState('');
  const [taskPriority, setTaskPriority] = useState<OverdueTask['priority']>('normal');
  const [taskCategory, setTaskCategory] = useState('Nauka');
  const [savingTask, setSavingTask] = useState(false);
  const [dialog, setDialog] = useState<DialogState>(null);
  const [mobileNavOpen, setMobileNavOpen] = useState(false);
  const icsInputRef = useRef<HTMLInputElement>(null);
  const backupInputRef = useRef<HTMLInputElement>(null);
  const [importingIcs, setImportingIcs] = useState(false);
  const [restoringBackup, setRestoringBackup] = useState(false);
  const [googleStatus, setGoogleStatus] = useState<GoogleStatus | null>(null);
  const [googleBusy, setGoogleBusy] = useState(false);
  const [googleMessage, setGoogleMessage] = useState('');
  const [windowsStatus, setWindowsStatus] = useState<WindowsStatus | null>(null);
  const [windowsRules, setWindowsRules] = useState<WindowsRule[]>([]);
  const [windowsRuleType, setWindowsRuleType] = useState<WindowsRule['type']>('process');
  const [windowsRulePattern, setWindowsRulePattern] = useState('');
  const [windowsBusy, setWindowsBusy] = useState(false);

  const load = useCallback(async () => {
    try {
      const [h, p, ev, ov, su, se, fs, rc, sp] = await Promise.all([
        fetch(`${API}/health`), fetch(`${API}/profiles`), fetch(`${API}/events`), fetch(`${API}/overdue`), fetch(`${API}/subjects`), fetch(`${API}/sessions`), fetch(`${API}/focus-sessions`), fetch(`${API}/review-cards`), fetch(`${API}/study-plans`)
      ]);
      if (!h.ok || !p.ok || !ev.ok || !ov.ok || !su.ok || !se.ok || !fs.ok || !rc.ok || !sp.ok) throw new Error('Backend zwrócił błąd.');
      const healthJson = await h.json(); const profileJson = await p.json();
      const eventJson = await ev.json(); const overdueJson = await ov.json(); const subjectJson = await su.json(); const sessionJson = await se.json(); const focusJson = await fs.json(); const reviewJson = await rc.json(); const studyPlanJson = await sp.json();
      setHealth(healthJson.ok ? 'online' : 'offline');
      setProfiles(profileJson.profiles ?? []); setEvents((eventJson.events ?? []).map((event: any) => ({...event, backgroundColor: ({lab:'#43a66b',lecture:'#3988e8',exercise:'#e9a23b',project:'#e5c72e',examLecture:'#e04444',examExercise:'#e04444',gym:'#858b94',ownProject:'#a56be0',ownEvent:'#76c9f2'} as Record<string,string>)[event.eventType || 'lecture'] || '#3988e8', textColor: ['project','exercise','ownEvent'].includes(event.eventType) ? '#20251f' : '#ffffff', borderColor: 'transparent', extendedProps:{eventType:event.eventType || 'lecture'}})));  setTasks(overdueJson.tasks ?? []); setSubjects(subjectJson.subjects ?? []); setSessions(sessionJson.sessions ?? []); setFocusHistory(focusJson.focusSessions ?? []); setStudyCards(reviewJson.cards ?? []); setStudyPlans(studyPlanJson.plans ?? []); setError('');
    } catch {
      setHealth('offline'); setError('Nie można połączyć się z silnikiem C++. Uruchom amogus_core.');
    }
  }, []);
  useEffect(() => { void load(); void refreshGoogleStatus(); void refreshWindows(); }, [load]);

  async function refreshGoogleStatus() {
    try { const response = await fetch(`${API}/google/status`); const data = await response.json(); if (response.ok) setGoogleStatus(data as GoogleStatus); } catch { /* Backend health indicator covers offline state. */ }
  }
  async function refreshWindows() {
    try { const [sr, rr] = await Promise.all([fetch(`${API}/windows/status`), fetch(`${API}/windows/rules`)]); const st = await sr.json(); const rl = await rr.json(); if (sr.ok) setWindowsStatus(st as WindowsStatus); if (rr.ok) setWindowsRules(rl.rules ?? []); } catch { /* The panel reports backend availability separately. */ }
  }
  async function addWindowsRule(e: React.FormEvent) {
    e.preventDefault(); const pattern = windowsRulePattern.trim(); if (!pattern || windowsBusy) return; setWindowsBusy(true);
    try { const response = await fetch(`${API}/windows/rules`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({type:windowsRuleType, pattern})}); const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się dodać reguły.'); setWindowsRulePattern(''); await refreshWindows(); setError('Dodano regułę Windows.'); } catch(err) { setError(err instanceof Error ? err.message : 'Błąd reguły Windows.'); } finally { setWindowsBusy(false); }
  }
  async function deleteWindowsRule(rule: WindowsRule) {
    if (!window.confirm(`Usunąć regułę „${rule.pattern}”?`)) return;
    try { const response = await fetch(`${API}/windows/rules/${encodeURIComponent(rule.id)}`, {method:'DELETE'}); const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się usunąć reguły.'); await refreshWindows(); } catch(err) { setError(err instanceof Error ? err.message : 'Błąd usuwania reguły.'); }
  }
  async function setWindowsFeature(feature: 'blocker'|'autostart', enabled: boolean) {
    setWindowsBusy(true); try { const response = await fetch(`${API}/windows/${feature}`, {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled})}); const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zmienić ustawienia Windows.'); await refreshWindows(); } catch(err) { setError(err instanceof Error ? err.message : 'Błąd ustawienia Windows.'); } finally { setWindowsBusy(false); }
  }
  async function connectGoogleCalendar() {
    if (googleBusy) return;
    const popup = window.open('about:blank', 'amogus-google-oauth', 'popup=yes,width=540,height=720');
    setGoogleBusy(true); setGoogleMessage('Przygotowywanie autoryzacji Google…');
    try { const response = await fetch(`${API}/google/connect`); const data = await response.json(); if (!response.ok) throw new Error(data.error ?? 'Nie udało się rozpocząć OAuth.');
      if (popup) popup.location.href = data.authUrl; else { setGoogleMessage('Przeglądarka zablokowała popup. Zezwól na popupy dla lokalnej aplikacji i spróbuj ponownie.'); throw new Error('Popup OAuth został zablokowany przez przeglądarkę.'); }
      setGoogleMessage('Dokończ zgodę w otwartym oknie Google, a następnie wróć tutaj.');
      const started = Date.now(); const poll = window.setInterval(async () => { await refreshGoogleStatus(); if (Date.now() - started > 180000 || popup.closed) window.clearInterval(poll); }, 2500);
    } catch (err) { if (popup && !popup.closed) popup.close(); setGoogleMessage(err instanceof Error ? err.message : 'Błąd połączenia z Google.'); }
    finally { setGoogleBusy(false); }
  }
  async function syncGoogleCalendar() {
    if (googleBusy) return; setGoogleBusy(true); setGoogleMessage('Synchronizacja dwukierunkowa w toku…');
    try { const response = await fetch(`${API}/google/sync`, {method:'POST'}); const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Synchronizacja nie powiodła się.');
      setGoogleMessage(`Synchronizacja zakończona: wysłano ${result.uploaded}, pobrano ${result.downloaded}, zaktualizowano ${result.updated}, usunięto ${result.deleted} wydarzeń.`); await load(); await refreshGoogleStatus();
    } catch (err) { setGoogleMessage(err instanceof Error ? err.message : 'Błąd synchronizacji.'); await refreshGoogleStatus(); } finally { setGoogleBusy(false); }
  }
  useEffect(() => {
    if (!dialog) return;
    const onKey = (event: KeyboardEvent) => { if (event.key === 'Escape') setDialog(null); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [dialog]);

  useEffect(() => {
    if (!focusRunning || focusEndAt === null) return;
    const tick = () => setFocusRemaining(Math.max(0, Math.ceil((focusEndAt - Date.now()) / 1000)));
    tick(); const timer = window.setInterval(tick, 250);
    return () => window.clearInterval(timer);
  }, [focusRunning, focusEndAt]);

  useEffect(() => {
    if (focusRemaining !== 0 || !focusRunning) return;
    setFocusRunning(false); setFocusEndAt(null); setFocusSaving(true);
    const endedAt = new Date().toISOString();
    const startedAt = focusStartedAtRef.current ?? new Date(Date.now() - focusMinutes * 60000).toISOString();
    const durationSeconds = Math.max(60, Math.min(14400, focusMinutes * 60));
    void fetch(`${API}/focus-sessions`, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({subjectId: focusSubjectId, durationSeconds, startedAt, endedAt, note: focusNote.trim()}) })
      .then(async response => { const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zapisać sesji.'); await load(); setFocusNote(''); setError('Sesja skupienia zapisana.'); })
      .catch(err => setError(err instanceof Error ? err.message : 'Nie udało się zapisać sesji skupienia.'))
      .finally(() => setFocusSaving(false));
  }, [focusRemaining, focusRunning, focusMinutes, focusSubjectId, focusNote, load]);

  function startFocusTimer() {
    if (focusRunning) { setFocusRemaining(Math.max(0, Math.ceil(((focusEndAt ?? Date.now()) - Date.now()) / 1000))); setFocusRunning(false); setFocusEndAt(null); return; }
    if (focusRemaining <= 0) setFocusRemaining(focusMinutes * 60);
    if (!focusStartedAtRef.current || focusRemaining <= 0) focusStartedAtRef.current = new Date().toISOString();
    setFocusEndAt(Date.now() + Math.max(1, focusRemaining || focusMinutes * 60) * 1000); setFocusRunning(true);
  }
  function resetFocusTimer() {
    setFocusRunning(false); setFocusEndAt(null); setFocusRemaining(focusMinutes * 60); focusStartedAtRef.current = null;
  }

  async function addProfile(e: React.FormEvent) {
    e.preventDefault(); const trimmed = name.trim(); if (!trimmed) return;
    try {
      const response = await fetch(`${API}/profiles`, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({name: trimmed}) });
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zapisać profilu.');
      setName(''); setProfiles(old => [...old, result.profile]); setError(''); void selectProfile(result.profile);
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd zapisu profilu.'); }
  }
  async function editProfile(profile: Profile) {
    if (profile.locked) { setError('Najpierw odblokuj profil, aby zmienić jego nazwę.'); return; }
    const nextName = window.prompt('Nazwa profilu:', profile.name);
    if (nextName === null) return;
    if (!nextName.trim()) { setError('Nazwa profilu nie może być pusta.'); return; }
    try {
      const response = await fetch(`${API}/profiles/${encodeURIComponent(profile.id)}`, { method: 'PUT', headers: {'Content-Type':'application/json'}, body: JSON.stringify({name: nextName.trim()}) });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error ?? 'Nie udało się zmienić nazwy profilu.');
      setProfiles(old => old.map(item => item.id === profile.id ? {...item, ...result.profile} : item));
      setError('');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd zmiany nazwy profilu.'); }
  }
  async function toggleProfileLock(profile: Profile) {
    let ritual = '';
    if (profile.locked) {
      const entered = window.prompt('Rytuał odblokowania profilu\n\nWpisz dokładnie: 私は自分の目標を守ります');
      if (entered === null) return;
      ritual = entered;
    }
    try {
      const response = await fetch(`${API}/profiles/${encodeURIComponent(profile.id)}/lock`, { method: 'PUT', headers: {'Content-Type':'application/json'}, body: JSON.stringify(profile.locked ? {locked:false, ritual} : {locked:true}) });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error ?? 'Nie udało się zmienić blokady profilu.');
      setProfiles(old => old.map(item => item.id === profile.id ? {...item, locked: !profile.locked} : item));
      setError(profile.locked ? 'Profil odblokowany. Możesz ponownie edytować jego domeny.' : 'Profil zablokowany. Edycja i usuwanie domen są wyłączone.');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd zmiany blokady profilu.'); }
  }

  async function selectProfile(profile: Profile) {
    setSelectedProfileId(profile.id); setProfileDomains([]); setYoutubeChannels([]); setDomainInput(''); setYoutubeHandleInput('');
    try {
      const [response, channelResponse] = await Promise.all([fetch(`${API}/profiles/${encodeURIComponent(profile.id)}/domains`), fetch(`${API}/profiles/${encodeURIComponent(profile.id)}/youtube-channels`)]);
      const result = await response.json(); const channelResult = await channelResponse.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się pobrać domen.'); if (!channelResponse.ok) throw new Error(channelResult.error ?? 'Nie udało się pobrać kanałów YouTube.');
      setProfileDomains(result.domains ?? []); setYoutubeChannels(channelResult.channels ?? []); setError('');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd pobierania domen profilu.'); }
  }
  async function addProfileDomain(e: React.FormEvent) {
    e.preventDefault(); const domain = domainInput.trim();
    if (!selectedProfileId || !domain || savingDomain) return;
    setSavingDomain(true);
    try {
      const response = await fetch(`${API}/profiles/${encodeURIComponent(selectedProfileId)}/domains`, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({domain}) });
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się dodać domeny.');
      setProfileDomains(old => [...old, result.domain].sort((a,b) => a.domain.localeCompare(b.domain))); setDomainInput(''); setError('');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd dodawania domeny.'); }
    finally { setSavingDomain(false); }
  }
  async function addYoutubeChannel(e: React.FormEvent) {
    e.preventDefault(); const handle = youtubeHandleInput.trim();
    if (!selectedProfileId || !handle || savingYoutubeChannel) return;
    setSavingYoutubeChannel(true);
    try {
      const response = await fetch(`${API}/profiles/${encodeURIComponent(selectedProfileId)}/youtube-channels`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({handle})});
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się dodać kanału YouTube.');
      setYoutubeChannels(old => [...old, result.channel].sort((a,b) => a.handle.localeCompare(b.handle))); setYoutubeHandleInput(''); setError('Kanał YouTube dodany do białej listy profilu.');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd dodawania kanału YouTube.'); }
    finally { setSavingYoutubeChannel(false); }
  }
  async function deleteYoutubeChannel(channel: YouTubeChannel) {
    if (!selectedProfileId) return;
    setDialog({kind:'confirm', title:'Usunąć dozwolony kanał?', message:`Kanał @${channel.handle} zostanie zablokowany w tym profilu.`, confirmLabel:'Usuń kanał', danger:true, onConfirm:async()=>{
      setDialog(null);
      try { const response=await fetch(`${API}/profiles/${encodeURIComponent(selectedProfileId)}/youtube-channels/${encodeURIComponent(channel.id)}`,{method:'DELETE'}); const result=await response.json(); if(!response.ok) throw new Error(result.error ?? 'Nie udało się usunąć kanału.'); setYoutubeChannels(old=>old.filter(item=>item.id!==channel.id)); setError('Kanał usunięty. Rozszerzenie odświeży filtr przy kolejnym pobraniu konfiguracji.'); }
      catch(err){setError(err instanceof Error?err.message:'Błąd usuwania kanału.');}
    }});
  }

  async function deleteProfileDomain(rule: ProfileDomain) {
    if (!selectedProfileId) return;
    setDialog({ kind: 'confirm', title: 'Usunąć domenę?', message: `Domena ${rule.domain} zostanie usunięta z konfiguracji tego profilu.`, confirmLabel: 'Usuń domenę', danger: true, onConfirm: async () => {
      setDialog(null);
      try {
        const response = await fetch(`${API}/profiles/${encodeURIComponent(selectedProfileId)}/domains/${encodeURIComponent(rule.id)}`, {method:'DELETE'});
        const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się usunąć domeny.');
        setProfileDomains(old => old.filter(item => item.id !== rule.id)); setError('');
      } catch (err) { setError(err instanceof Error ? err.message : 'Błąd usuwania domeny.'); }
    }});
  }
  async function addSubject(e: React.FormEvent) {
    e.preventDefault(); const trimmed = subjectName.trim(); if (!trimmed || savingSubject) return;
    setSavingSubject(true);
    try {
      const response = await fetch(`${API}/subjects`, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({name: trimmed, color: '#718096'}) });
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zapisać przedmiotu.');
      setSubjects(old => [...old, result.subject].sort((a,b) => a.name.localeCompare(b.name, 'pl'))); setSubjectName(''); setError('');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd zapisu przedmiotu.'); }
    finally { setSavingSubject(false); }
  }
  async function addSession(e: React.FormEvent) {
    e.preventDefault(); if (!(sessionSubjectId || subjects[0]?.id) || savingSession) return;
    setSavingSession(true);
    try {
      const response = await fetch(editingSessionId ? `${API}/sessions/${encodeURIComponent(editingSessionId)}` : `${API}/sessions`, {method:editingSessionId?'PUT':'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({subjectId:sessionSubjectId || subjects[0]?.id || '',weekday:Number(sessionWeekday),startTime:sessionStart,endTime:sessionEnd,room:sessionRoom.trim(),sessionType})});
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zapisać zajęć.');
      const refreshed = await fetch(`${API}/sessions`); const data = await refreshed.json(); if (!refreshed.ok) throw new Error('Zajęcia zapisano, ale nie udało się odświeżyć listy.');
      setSessions(data.sessions ?? []); setSessionRoom(''); setEditingSessionId(null); setError('');
    } catch(err) { setError(err instanceof Error ? err.message : 'Błąd zapisu zajęć.'); }
    finally { setSavingSession(false); }
  }
  async function generateCalendarWeek() {
    if (!scheduleWeekStart || generatingSchedule || sessions.length === 0) return;
    const date = new Date(`${scheduleWeekStart}T12:00:00`);
    if (Number.isNaN(date.getTime()) || (date.getDay() + 6) % 7 !== 0) {
      setError('Wybierz poniedziałek jako początek tygodnia.');
      return;
    }
    setGeneratingSchedule(true);
    try {
      const response = await fetch(`${API}/sessions/generate-events`, {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({weekStart: scheduleWeekStart})
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error ?? 'Nie udało się wygenerować wydarzeń.');
      await load();
      setActive('calendar');
      setError(`Plan kalendarza wygenerowany: ${result.generated ?? 0} nowych wydarzeń, ${result.updated ?? 0} zaktualizowanych. Ponowne uruchomienie dla tego samego tygodnia nie tworzy duplikatów.`);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Błąd generowania wydarzeń kalendarza.');
    } finally {
      setGeneratingSchedule(false);
    }
  }

  function editSession(session: SubjectSession) {
    setEditingSessionId(session.id); setSessionSubjectId(session.subjectId); setSessionWeekday(String(session.weekday));
    setSessionStart(session.startTime); setSessionEnd(session.endTime); setSessionRoom(session.room); setSessionType(session.sessionType);
    document.querySelector('.session-form')?.scrollIntoView({behavior:'smooth',block:'center'});
  }
  function cancelSessionEdit() { setEditingSessionId(null); setSessionRoom(''); setSessionSubjectId(''); setSessionWeekday('0'); setSessionStart('09:00'); setSessionEnd('10:30'); setSessionType('Wykład'); }
  function deleteSession(session: SubjectSession) {
    setDialog({kind:'confirm', title:'Usunąć zajęcia z planu?', message:`${session.subjectName}, ${weekdays[session.weekday]} ${session.startTime}–${session.endTime} zostaną usunięte z tygodniowego planu.`, confirmLabel:'Usuń zajęcia', danger:true, onConfirm:async()=>{
      setDialog(null); try { const response=await fetch(`${API}/sessions/${encodeURIComponent(session.id)}`,{method:'DELETE'}); const result=await response.json(); if(!response.ok) throw new Error(result.error??'Nie udało się usunąć zajęć.'); setSessions(old=>old.filter(item=>item.id!==session.id)); setError(''); }
      catch(err){setError(err instanceof Error?err.message:'Błąd usuwania zajęć.');}
    }});
  }
  function deleteSubject(subject: Subject) {
    setDialog({kind:'confirm', title:'Usunąć przedmiot?', message:`„${subject.name}” zostanie usunięty z listy przedmiotów.`, confirmLabel:'Usuń przedmiot', danger:true, onConfirm: async () => {
      setDialog(null);
      try {
        const response = await fetch(`${API}/subjects/${encodeURIComponent(subject.id)}`, {method:'DELETE'}); const result = await response.json();
        if (!response.ok) throw new Error(result.error ?? 'Nie udało się usunąć przedmiotu.');
        setSubjects(old => old.filter(item => item.id !== subject.id)); setError('');
      } catch (err) { setError(err instanceof Error ? err.message : 'Błąd usuwania przedmiotu.'); }
    }});
  }
  async function editSubject(subject: Subject) {
    const nextName = window.prompt('Nazwa przedmiotu:', subject.name);
    if (nextName === null) return;
    try {
      const response = await fetch(`${API}/subjects/${encodeURIComponent(subject.id)}`, {method:'PUT', headers:{'Content-Type':'application/json'}, body:JSON.stringify({name:nextName.trim(), color:subject.color})});
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się edytować przedmiotu.');
      setSubjects(old => old.map(item => item.id === subject.id ? {...item, ...result.subject} : item).sort((a,b) => a.name.localeCompare(b.name,'pl'))); setError('');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd edycji przedmiotu.'); }
  }
  function deleteProfile(profile: Profile) {
    if (profile.id === 'default') { setError('Profil domyślny jest chroniony i nie może zostać usunięty.'); return; }
    setDialog({kind:'confirm',title:'Usunąć profil?',message:`Profil „${profile.name}” i jego reguły domen zostaną usunięte.`,confirmLabel:'Usuń profil',danger:true,onConfirm:async()=>{
      setDialog(null);
      try { const response=await fetch(`${API}/profiles/${encodeURIComponent(profile.id)}`,{method:'DELETE'}); const result=await response.json(); if(!response.ok) throw new Error(result.error??'Nie udało się usunąć profilu.');
        setProfiles(old=>old.filter(item=>item.id!==profile.id)); if(selectedProfileId===profile.id){setSelectedProfileId('');setProfileDomains([]);setYoutubeChannels([]);} setError('');
      } catch(err){setError(err instanceof Error?err.message:'Błąd usuwania profilu.');}
    }});
  }
  async function exportBackup() {
    try {
      const response = await fetch(`${API}/backup`); const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się utworzyć kopii.');
      const blob = new Blob([JSON.stringify(result,null,2)],{type:'application/json'}); const url=URL.createObjectURL(blob); const anchor=document.createElement('a'); anchor.href=url; anchor.download=`amogus-backup-${new Date().toISOString().slice(0,10)}.json`; document.body.appendChild(anchor); anchor.click(); anchor.remove(); URL.revokeObjectURL(url); setError('');
    } catch(err) { setError(err instanceof Error?err.message:'Błąd eksportu kopii zapasowej.'); }
  }
  async function chooseBackupFile(file?: File) {
    if (!file) return;
    try {
      if (file.size > 10 * 1024 * 1024) throw new Error('Plik kopii jest zbyt duży (limit 10 MB).');
      const backup = JSON.parse(await file.text()) as {format?: string; profiles?: unknown[]; domains?: unknown[]; subjects?: unknown[]; events?: unknown[]; tasks?: unknown[]; sessions?: unknown[]};
      if (backup.format !== 'amogus-backup' || !Array.isArray(backup.profiles) || !Array.isArray(backup.domains) || !Array.isArray(backup.subjects) || !Array.isArray(backup.events) || !Array.isArray(backup.tasks) || (backup.sessions !== undefined && !Array.isArray(backup.sessions))) {
        throw new Error('Plik nie wygląda na kompletną kopię zapasową Amogus.');
      }
      setDialog({kind:'confirm', title:'Przywrócić kopię zapasową?', message:`Plik „${file.name}” zastąpi aktualne dane aplikacji: profile, domeny, przedmioty, tygodniowy plan zajęć, wydarzenia i zadania. Przed kontynuacją wyeksportuj obecną bazę, jeśli chcesz zachować możliwość powrotu.`, confirmLabel:'Zastąp dane i przywróć', danger:true, onConfirm:async()=>{
        setDialog(null); setRestoringBackup(true);
        try {
          const response = await fetch(`${API}/backup/restore`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(backup)});
          const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się przywrócić kopii.');
          await load(); setError(`Przywrócono kopię: ${result.restored?.profiles ?? 0} profili, ${result.restored?.subjects ?? 0} przedmiotów, ${result.restored?.sessions ?? 0} pozycji planu, ${result.restored?.events ?? 0} wydarzeń i ${result.restored?.tasks ?? 0} zadań.`);
        } catch(err) { setError(err instanceof Error ? err.message : 'Błąd przywracania kopii.'); }
        finally { setRestoringBackup(false); if (backupInputRef.current) backupInputRef.current.value=''; }
      }});
    } catch(err) { setError(err instanceof Error ? err.message : 'Nie udało się odczytać pliku kopii.'); if (backupInputRef.current) backupInputRef.current.value=''; }
  }
  function parseIcsDate(raw: string): string | null {
    const value=raw.trim();
    const match=value.match(/^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})?(Z)?)?$/);
    if(!match)return null;
    const [,y,mo,d,hh='00',mm='00',ss='00',z]=match;
    const date=z?new Date(Date.UTC(+y,+mo-1,+d,+hh,+mm,+(ss||0))):new Date(+y,+mo-1,+d,+hh,+mm,+(ss||0));
    return Number.isNaN(date.getTime())?null:date.toISOString();
  }
  async function importIcsFile(file?: File) {
    if(!file)return;
    setImportingIcs(true);
    try {
      let text=await file.text(); text=text.replace(/\r\n[ \t]/g,'').replace(/\n[ \t]/g,'');
      const blocks=text.split(/BEGIN:VEVENT/i).slice(1).map(part=>part.split(/END:VEVENT/i)[0]);
      const parsed=blocks.map((block,index)=>{
        const lines=block.split(/\r?\n/); const get=(key:string)=>lines.find(line=>new RegExp('^'+key+'(?:;[^:]*)?:','i').test(line))?.split(':').slice(1).join(':').trim()??'';
        const unescape=(v:string)=>v.replace(/\\n/gi,'\n').replace(/\\,/g,',').replace(/\\;/g,';').replace(/\\\\/g,'\\');
        const title=unescape(get('SUMMARY'))||'Wydarzenie z ICS'; const startLine=lines.find(line=>/^DTSTART(?:;[^:]*)?:/i.test(line)); const endLine=lines.find(line=>/^DTEND(?:;[^:]*)?:/i.test(line));
        const startRaw=startLine?.split(':').slice(1).join(':').trim()??''; const endRaw=endLine?.split(':').slice(1).join(':').trim()??'';
        const start=parseIcsDate(startRaw); let end=parseIcsDate(endRaw); if(!end&&start){end=new Date(new Date(start).getTime()+60*60*1000).toISOString();}
        return start&&end?{id:`ics-${Date.now()}-${index}`,title,start,end}:null;
      }).filter((event):event is {id:string;title:string;start:string;end:string}=>!!event&&new Date(event.end)>new Date(event.start));
      if(!parsed.length)throw new Error('Plik nie zawiera poprawnych wydarzeń VEVENT z datami DTSTART/DTEND.');
      let added=0;
      for(const event of parsed){const response=await fetch(`${API}/events`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(event)});const result=await response.json();if(!response.ok)throw new Error(result.error??`Błąd zapisu wydarzenia ${event.title}`);added++;}
      await load(); setActive('calendar'); setError(`Zaimportowano ${added} wydarzeń z pliku ${file.name}.`);
    } catch(err){setError(err instanceof Error?err.message:'Nie udało się zaimportować pliku ICS.');}
    finally{setImportingIcs(false);if(icsInputRef.current)icsInputRef.current.value='';}
  }
  async function saveCalendarEvent(payload: {id: string; title: string; start: string; end: string; eventType?: string}) {
    try {
      const response = await fetch(`${API}/events`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(payload)});
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zapisać wydarzenia.');
      const colors: Record<string,string> = {lab:'#43a66b',lecture:'#3988e8',exercise:'#e9a23b',project:'#e5c72e',examLecture:'#e04444',examExercise:'#e04444',gym:'#858b94',ownProject:'#a56be0',ownEvent:'#76c9f2'}; const styledPayload = {...payload, backgroundColor:colors[payload.eventType || 'lecture'] || '#3988e8', textColor:['project','exercise','ownEvent'].includes(payload.eventType || 'lecture') ? '#20251f' : '#ffffff', borderColor:'transparent', extendedProps:{eventType:payload.eventType || 'lecture'}}; setEvents(old => { const found = old.some(event => String(event.id) === payload.id); return found ? old.map(event => String(event.id) === payload.id ? styledPayload : event) : [...old, styledPayload]; });
      setError(''); setDialog(null);
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd zapisu wydarzenia.'); void load(); }
  }
  function openCreateEvent(start: Date | string, end: Date | string) {
    setDialog({kind:'event', mode:'create', title:'', start:toLocalInput(start), end:toLocalInput(end), eventType:'lecture', repeatWeekly:false, repeatUntil:''});
  }
  function openEditEvent(event: {id:string; title:string; start:Date|null; end:Date|null; eventType?:string}) {
    setDialog({kind:'event', mode:'edit', id:event.id, title:event.title, start:event.start ? toLocalInput(event.start) : '', end:event.end ? toLocalInput(event.end) : '', eventType:event.eventType || 'lecture', repeatWeekly:false, repeatUntil:''});
  }
  async function submitEvent(e: React.FormEvent) {
    e.preventDefault(); if (!dialog || dialog.kind !== 'event') return;
    if (!dialog.title.trim() || !dialog.start || !dialog.end || new Date(dialog.end) <= new Date(dialog.start)) {
      setError('Sprawdź nazwę oraz kolejność dat wydarzenia.'); return;
    }
    const colors: Record<string,string> = {lab:'#43a66b',lecture:'#3988e8',exercise:'#e9a23b',project:'#e5c72e',examLecture:'#e04444',examExercise:'#e04444',gym:'#858b94',ownProject:'#a56be0',ownEvent:'#76c9f2'};
    const baseStart = new Date(dialog.start); const baseEnd = new Date(dialog.end); const dates: Date[] = [baseStart];
    if (dialog.repeatWeekly && dialog.mode === 'create' && dialog.repeatUntil) { const until = new Date(`${dialog.repeatUntil}T23:59:59`); let next = new Date(baseStart.getTime()+7*86400000); while(next <= until && dates.length < 104) { dates.push(next); next = new Date(next.getTime()+7*86400000); } }
    for (let index=0; index<dates.length; index++) { const start = dates[index]; const end = new Date(start.getTime()+(baseEnd.getTime()-baseStart.getTime())); const id = dialog.id && index===0 ? dialog.id : `event-${Date.now()}-${index}-${Math.random().toString(36).slice(2,7)}`; const response = await fetch(`${API}/events`, {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,title:dialog.title.trim(),start:start.toISOString(),end:end.toISOString(),eventType:dialog.eventType})}); const result = await response.json(); if(!response.ok) { setError(result.error || 'Nie udało się zapisać wydarzenia.'); return; } }
    await load(); setDialog(null); setError(dialog.repeatWeekly && dates.length>1 ? `Zapisano wydarzenie i ${dates.length-1} kolejnych wystąpień co tydzień.` : 'Wydarzenie zapisane.');
  }
  function deleteCalendarEvent(id: string, title: string) {
    setDialog({kind:'confirm', title:'Usunąć wydarzenie?', message:`„${title}” zostanie trwale usunięte z lokalnego kalendarza.`, confirmLabel:'Usuń wydarzenie', danger:true, onConfirm: async () => {
      setDialog(null);
      try {
        const response = await fetch(`${API}/events/${encodeURIComponent(id)}`, {method:'DELETE'}); const result = await response.json();
        if (!response.ok) throw new Error(result.error ?? 'Nie udało się usunąć wydarzenia.');
        setEvents(old => old.filter(event => String(event.id) !== id)); setError('');
      } catch (err) { setError(err instanceof Error ? err.message : 'Błąd usuwania wydarzenia.'); void load(); }
    }});
  }
  async function addTask(e: React.FormEvent) {
    e.preventDefault(); const title = taskTitle.trim(); if (!title || !taskDueAt || savingTask) return;
    setSavingTask(true);
    try {
      const response = await fetch(`${API}/overdue`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({title, dueAt:new Date(taskDueAt).toISOString(), notes:taskNotes.trim(), priority:taskPriority, category:taskCategory})});
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się dodać zadania.');
      setTasks(old => [...old, result.task].sort((a,b) => Number(a.completed)-Number(b.completed) || new Date(a.dueAt).getTime()-new Date(b.dueAt).getTime()));
      setTaskTitle(''); setTaskDueAt(''); setTaskNotes(''); setTaskPriority('normal'); setTaskCategory('Nauka'); setError('');
    } catch (err) { setError(err instanceof Error ? err.message : 'Błąd zapisu zadania.'); }
    finally { setSavingTask(false); }
  }
  async function editTask(task: OverdueTask) {
    const title=window.prompt('Nazwa zadania:',task.title); if(title===null)return;
    const due=window.prompt('Termin (YYYY-MM-DDTHH:mm):',toLocalInput(task.dueAt)); if(due===null)return;
    const notes=window.prompt('Notatka:',task.notes); if(notes===null)return;
    const priority=window.prompt('Priorytet (low / normal / high / urgent):',task.priority||'normal'); if(priority===null)return;
    if(!(['low','normal','high','urgent'] as string[]).includes(priority)){setError('Priorytet musi być low, normal, high albo urgent.');return;}
    const category=window.prompt('Kategoria:',task.category||'Ogólne'); if(category===null)return;
    const dueDate=new Date(due); if(!title.trim()||Number.isNaN(dueDate.getTime())){setError('Sprawdź nazwę zadania i format terminu.');return;}
    try{const response=await fetch(`${API}/overdue/${encodeURIComponent(task.id)}`,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({title:title.trim(),dueAt:dueDate.toISOString(),notes,priority,category})});const result=await response.json();if(!response.ok)throw new Error(result.error??'Nie udało się edytować zadania.');
      setTasks(old=>old.map(item=>item.id===task.id?{...item,...result.task}:item).sort((a,b)=>Number(a.completed)-Number(b.completed)||new Date(a.dueAt).getTime()-new Date(b.dueAt).getTime()));setError('');
    }catch(err){setError(err instanceof Error?err.message:'Błąd edycji zadania.');}
  }
  async function setTaskCompleted(task: OverdueTask, completed: boolean) {
    const previous = tasks;
    setTasks(old => old.map(item => item.id === task.id ? {...item, completed} : item).sort((a,b) => Number(a.completed)-Number(b.completed) || new Date(a.dueAt).getTime()-new Date(b.dueAt).getTime()));
    try {
      const response = await fetch(`${API}/overdue/status`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({id:task.id,completed})});
      const result = await response.json(); if (!response.ok) throw new Error(result.error ?? 'Nie udało się zmienić statusu.'); setError('');
    } catch (err) { setTasks(previous); setError(err instanceof Error ? err.message : 'Błąd zmiany statusu.'); }
  }
  async function deleteTask(task: OverdueTask) {
    setDialog({kind:'confirm', title:'Usunąć zadanie?', message:`Zadanie „${task.title}” zostanie trwale usunięte.`, confirmLabel:'Usuń zadanie', danger:true, onConfirm: async () => {
      setDialog(null); const previous = tasks; setTasks(old => old.filter(item => item.id !== task.id));
      try {
        const response = await fetch(`${API}/overdue/${encodeURIComponent(task.id)}`, {method:'DELETE'}); const result = await response.json();
        if (!response.ok) throw new Error(result.error ?? 'Nie udało się usunąć zadania.'); setError('');
      } catch (err) { setTasks(previous); setError(err instanceof Error ? err.message : 'Błąd usuwania zadania.'); }
    }});
  }
  const visibleTasks = tasks.filter(task => {
    const late = !task.completed && new Date(task.dueAt).getTime() < Date.now();
    const matchesFilter = taskFilter === 'all' || (taskFilter === 'open' && !task.completed) || (taskFilter === 'late' && late) || (taskFilter === 'done' && task.completed);
    const query = taskSearch.trim().toLocaleLowerCase('pl');
    const matchesPriority = taskPriorityFilter === 'all' || (task.priority || 'normal') === taskPriorityFilter;
    return matchesFilter && matchesPriority && (!query || task.title.toLocaleLowerCase('pl').includes(query) || task.notes.toLocaleLowerCase('pl').includes(query) || (task.category||'').toLocaleLowerCase('pl').includes(query) || (task.priority||'normal').toLocaleLowerCase('pl').includes(query));
  });
  async function completeVisibleTasks() {
    const pending = visibleTasks.filter(task => !task.completed);
    if (!pending.length) return;
    setError('');
    try {
      const results = await Promise.all(pending.map(async task => {
        const response = await fetch(`${API}/overdue/status`, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({id:task.id,completed:true})});
        const result = await response.json();
        if (!response.ok) throw new Error(result.error ?? `Nie udało się ukończyć zadania: ${task.title}`);
        return task.id;
      }));
      const completedIds = new Set(results);
      setTasks(old => old.map(task => completedIds.has(task.id) ? {...task, completed:true} : task));
    } catch (err) {
      setError(err instanceof Error ? `${err.message} Odświeżam stan z backendu.` : 'Nie udało się ukończyć wszystkich zadań.');
      await load();
    }
  }
  async function addStudyCard(e: React.FormEvent) {
    e.preventDefault(); if (!cardFront.trim() || !cardBack.trim() || savingCard) return; setSavingCard(true);
    try { const response=await fetch(`${API}/review-cards`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({front:cardFront.trim(),back:cardBack.trim(),subjectId:cardSubjectId})}); const result=await response.json(); if(!response.ok) throw new Error(result.error||'Nie udało się dodać fiszki.'); setCardFront('');setCardBack('');await load();setError('Fiszka została zapisana.'); } catch(err) {setError(err instanceof Error?err.message:'Błąd zapisu fiszki.');} finally {setSavingCard(false);} 
  }
  async function gradeStudyCard(card: StudyCard, grade: 'again'|'hard'|'good'|'easy') {
    try { const response=await fetch(`${API}/review-cards/${encodeURIComponent(card.id)}/review`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({grade})}); const result=await response.json(); if(!response.ok) throw new Error(result.error||'Nie udało się zapisać oceny.'); setRevealedCards(old=>old.filter(id=>id!==card.id)); await load(); setError(`Powtórka zapisana. Następny termin za ${result.intervalDays} dni.`); } catch(err) {setError(err instanceof Error?err.message:'Błąd zapisu powtórki.');}
  }
  async function deleteStudyCard(card: StudyCard) {
    setDialog({kind:'confirm',title:'Usunąć fiszkę?',message:`Fiszka „${card.front}” zostanie trwale usunięta.`,danger:true,confirmLabel:'Usuń fiszkę',onConfirm:()=>{setDialog(null);void (async()=>{try{const response=await fetch(`${API}/review-cards/${encodeURIComponent(card.id)}`,{method:'DELETE'});const result=await response.json();if(!response.ok)throw new Error(result.error||'Nie udało się usunąć fiszki.');await load();}catch(err){setError(err instanceof Error?err.message:'Błąd usuwania fiszki.');}})();}});
  }
  async function addStudyPlan(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault(); setStudyPlanSaving(true);
    try { const response=await fetch(`${API}/study-plans`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({title:studyPlanTitle.trim(),subjectId:studyPlanSubject,targetDate:studyPlanDate,notes:studyPlanNotes.trim()})}); const result=await response.json(); if(!response.ok)throw new Error(result.error||'Nie udało się utworzyć ścieżki nauki.'); setStudyPlanTitle('');setStudyPlanDate('');setStudyPlanNotes('');await load();setError('Utworzono ścieżkę nauki.'); }
    catch(err){setError(err instanceof Error?err.message:'Błąd zapisu ścieżki nauki.');} finally{setStudyPlanSaving(false);}
  }
  async function addStudyStage(plan: StudyPlan) {
    const draft=stageDrafts[plan.id]||{title:'',dueAt:''}; if(!draft.title.trim())return;
    try{const response=await fetch(`${API}/study-plans/${encodeURIComponent(plan.id)}/stages`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({title:draft.title.trim(),dueAt:draft.dueAt,notes:''})});const result=await response.json();if(!response.ok)throw new Error(result.error||'Nie udało się dodać etapu.');setStageDrafts(old=>({...old,[plan.id]:{title:'',dueAt:''}}));await load();}
    catch(err){setError(err instanceof Error?err.message:'Błąd dodawania etapu.');}
  }
  async function setStudyStageCompleted(stage: StudyStage, completed: boolean) {
    try{const response=await fetch(`${API}/study-stages/${encodeURIComponent(stage.id)}/complete`,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({completed})});const result=await response.json();if(!response.ok)throw new Error(result.error||'Nie udało się zaktualizować etapu.');await load();}
    catch(err){setError(err instanceof Error?err.message:'Błąd aktualizacji etapu.');}
  }
  async function deleteStudyPlan(plan: StudyPlan) {
    setDialog({kind:'confirm',title:'Usunąć ścieżkę nauki?',message:`Plan „${plan.title}” i wszystkie jego etapy zostaną usunięte.`,danger:true,confirmLabel:'Usuń plan',onConfirm:()=>{setDialog(null);void(async()=>{try{const response=await fetch(`${API}/study-plans/${encodeURIComponent(plan.id)}`,{method:'DELETE'});const result=await response.json();if(!response.ok)throw new Error(result.error||'Nie udało się usunąć planu.');await load();}catch(err){setError(err instanceof Error?err.message:'Błąd usuwania planu.');}})();}});
  }
  async function deleteStudyStage(stage: StudyStage) {
    try{const response=await fetch(`${API}/study-stages/${encodeURIComponent(stage.id)}`,{method:'DELETE'});const result=await response.json();if(!response.ok)throw new Error(result.error||'Nie udało się usunąć etapu.');await load();}
    catch(err){setError(err instanceof Error?err.message:'Błąd usuwania etapu.');}
  }

  async function addStageToCalendar(plan: StudyPlan, stage: StudyStage) {
    if (!stage.dueAt) { setError('Najpierw ustaw termin etapu.'); return; }
    const start = `${stage.dueAt}T09:00:00`; const end = `${stage.dueAt}T09:30:00`;
    const id = `study_stage_${stage.id}`;
    try { const response=await fetch(`${API}/events`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id,title:`${plan.title} — ${stage.title}`.slice(0,200),start,end})}); const result=await response.json(); if(!response.ok)throw new Error(result.error||'Nie udało się dodać etapu do kalendarza.'); await load(); setError('Termin etapu zapisany w kalendarzu na 09:00.'); }
    catch(err){setError(err instanceof Error?err.message:'Błąd zapisu do kalendarza.');}
  }
  function exportStudyPlansCsv() {
    const quote=(value:string)=>`"${value.replace(/"/g,'""')}"`;
    const rows=[['Cel','Przedmiot','Termin docelowy','Etap','Termin etapu','Status','Notatki'],...studyPlans.flatMap(plan=>plan.stages.length?plan.stages.map(stage=>[plan.title,plan.subjectName,plan.targetDate,stage.title,stage.dueAt,stage.completed?'Ukończony':'Otwarty',plan.notes]):[[plan.title,plan.subjectName,plan.targetDate,'','','Brak etapów',plan.notes]])];
    const url=URL.createObjectURL(new Blob(['\ufeff'+rows.map(row=>row.map(quote).join(';')).join('\r\n')],{type:'text/csv;charset=utf-8;'}));const anchor=document.createElement('a');anchor.href=url;anchor.download='amogus-sciezki-nauki.csv';anchor.click();URL.revokeObjectURL(url);
  }

  function exportTasksCsv() {
    const quote = (value: string) => `"${value.replace(/"/g, '""')}"`;
    const rows = [['Zadanie','Termin','Status','Priorytet','Kategoria','Notatka'], ...visibleTasks.map(task => [task.title, task.dueAt, task.completed ? 'Ukończone' : (new Date(task.dueAt).getTime() < Date.now() ? 'Po terminie' : 'Otwarte'), task.priority||'normal', task.category||'Ogólne', task.notes])];
    const csv = '\ufeff' + rows.map(row => row.map(quote).join(';')).join('\r\n');
    const url = URL.createObjectURL(new Blob([csv], {type:'text/csv;charset=utf-8;'}));
    const anchor = document.createElement('a'); anchor.href = url; anchor.download = 'amogus-zadania.csv'; anchor.click(); URL.revokeObjectURL(url);
  }
  const pendingCount = tasks.filter(task => !task.completed).length;
  const overdueCount = tasks.filter(task => !task.completed && new Date(task.dueAt).getTime() < Date.now()).length;
  const todayLabel = new Intl.DateTimeFormat('pl-PL', { weekday: 'long', day: 'numeric', month: 'long' }).format(new Date());
  const eventCountToday = useMemo(() => events.filter((event) => { const start = new Date(String(event.start ?? '')); const today = new Date(); return !Number.isNaN(start.getTime()) && start.toDateString() === today.toDateString(); }).length, [events]);
  const navItems: AppPage[] = ['calendar','overdue','subjects','blocks','focus','integrations','settings'];
  const go = (page: AppPage) => { setActive(page); setMobileNavOpen(false); setError(''); };
  const showInfo = (title: string, description: string, sections?: string[]) => setDialog({kind:'info',title,description,sections});
  const updateEventDialog = (patch: Partial<Extract<DialogState, {kind:'event'}>>) => setDialog(current => current?.kind === 'event' ? {...current, ...patch} : current);

  return <div className="app-shell" onContextMenu={event => event.preventDefault()}>
    <aside className={`sidebar ${mobileNavOpen?'sidebar-open':''}`}>
      <a className="brand" href="#home" onClick={e=>{e.preventDefault();go('calendar');}} aria-label="Amogus — kalendarz"><span className="brand-mark">a</span><span className="brand-copy">amogus<small>PERSONAL FOCUS</small></span><span className="brand-version">0.30</span></a>
      <div className="nav-label">PRZESTRZEŃ ROBOCZA</div>
      <nav className="primary-nav" aria-label="Główna nawigacja">{navItems.map(page=><button key={page} aria-current={active===page?'page':undefined} className={`nav-item ${active===page?'active':''}`} onClick={()=>go(page)}><span className="nav-icon" aria-hidden="true">{pageInfo[page].icon}</span><span className="nav-text">{pageInfo[page].title}</span>{page==='overdue'&&pendingCount>0&&<em className="nav-count">{pendingCount}</em>}</button>)}</nav>
      <div className="sidebar-note"><span className="sidebar-note-icon">✳</span><div><strong>Pracuj spokojniej</strong><p>Małe kroki. Więcej skupienia.</p></div></div>
      <div className="sidebar-bottom"><span className={`status-dot ${health}`}/><div className="engine-copy"><strong>Silnik lokalny</strong><small>{health==='online'?'Połączono':health==='checking'?'Łączenie…':'Brak połączenia'}</small></div><button className="icon-button" title="Odśwież status połączenia" aria-label="Odśwież status połączenia" onClick={()=>void load()}>↻</button></div>
    </aside>
    {mobileNavOpen&&<button className="mobile-scrim" aria-label="Zamknij menu" onClick={()=>setMobileNavOpen(false)}/>}
    <main className="main">
      <header className="topbar"><button className="mobile-menu-button" onClick={()=>setMobileNavOpen(v=>!v)} aria-label="Otwórz nawigację">☰</button><div className="page-heading"><div className="eyebrow">AMOGUS <span>/</span> PRZESTRZEŃ ROBOCZA</div><h1>{pageInfo[active].title}</h1><p>{pageInfo[active].description}</p></div><div className="topbar-meta"><span className="today-label">{todayLabel}</span><div className={`connection ${health}`}><i/>{health==='online'?'Połączono':health==='checking'?'Łączenie…':'Backend offline'}</div></div></header>
      {error&&<div className="notice" role="alert"><span>!</span><div>{error}</div><button onClick={()=>void load()}>Spróbuj ponownie</button><button className="notice-close" aria-label="Zamknij komunikat" onClick={()=>setError('')}>×</button></div>}

      {active==='focus'&&<div className="page-stack focus-page">
        <section className="panel focus-main-panel"><div className="panel-head"><div><h3>Timer skupienia</h3><p>Wybierz czas, opcjonalnie przypisz przedmiot i rozpocznij sesję. Po zakończeniu zapisze się ona w historii.</p></div><span className="tag">TRYB SKUPIENIA</span></div>
          <div className="focus-timer-layout"><div className={`focus-clock ${focusRunning?'is-running':''}`} aria-live="polite"><svg viewBox="0 0 220 220" role="img" aria-label="Postęp timera"><circle className="focus-clock-track" cx="110" cy="110" r="96"/><circle className="focus-clock-progress" cx="110" cy="110" r="96" strokeDasharray={2*Math.PI*96} strokeDashoffset={2*Math.PI*96*(1-focusRemaining/Math.max(1,focusMinutes*60))}/></svg><div className="focus-clock-text"><strong>{String(Math.floor(focusRemaining/60)).padStart(2,'0')}:{String(focusRemaining%60).padStart(2,'0')}</strong><small>{focusRunning?'W TRAKCIE':focusRemaining===focusMinutes*60?'GOTOWY DO STARTU':'WSTRZYMANO'}</small></div></div>
            <div className="focus-controls"><label>Czas sesji<select value={focusMinutes} disabled={focusRunning} onChange={e=>{const v=Number(e.target.value);setFocusMinutes(v);setFocusRemaining(v*60);focusStartedAtRef.current=null;}}><option value={15}>15 minut · krótka sesja</option><option value={25}>25 minut · Pomodoro</option><option value={45}>45 minut · głęboka praca</option><option value={60}>60 minut · długa sesja</option><option value={90}>90 minut · blok pracy</option></select></label><label>Przedmiot (opcjonalnie)<select value={focusSubjectId} onChange={e=>setFocusSubjectId(e.target.value)}><option value="">Bez przypisania</option>{subjects.map(subject=><option key={subject.id} value={subject.id}>{subject.name}</option>)}</select></label><label>Cel sesji (opcjonalnie)<input value={focusNote} onChange={e=>setFocusNote(e.target.value)} maxLength={500} placeholder="Np. powtórzyć rozdział 3"/></label><div className="focus-button-row"><button className="primary-button" onClick={startFocusTimer} disabled={focusSaving}>{focusRunning?'Wstrzymaj':focusRemaining<focusMinutes*60?'Wznów':'Rozpocznij sesję'}</button><button className="secondary-button" onClick={resetFocusTimer}>Resetuj</button></div><p className="focus-hint">Timer działa w otwartej karcie aplikacji. Jeśli karta lub przeglądarka zostanie zamknięta, niezakończona sesja nie zostanie zapisana.</p></div></div>
        </section>
        <div className="focus-stats-grid"><article className="focus-stat"><span>Dzisiaj</span><strong>{Math.round(focusHistory.filter(item=>new Date(item.startedAt).toDateString()===new Date().toDateString()).reduce((sum,item)=>sum+item.durationSeconds,0)/60)} <small>min</small></strong><em>{focusHistory.filter(item=>new Date(item.startedAt).toDateString()===new Date().toDateString()).length} sesji</em></article><article className="focus-stat"><span>Ostatnie 7 dni</span><strong>{Math.round(focusHistory.filter(item=>Date.now()-Date.parse(item.startedAt)<7*86400000&&Date.now()>=Date.parse(item.startedAt)).reduce((sum,item)=>sum+item.durationSeconds,0)/60)} <small>min</small></strong><em>Łączny czas skupienia</em></article><article className="focus-stat"><span>Historia</span><strong>{focusHistory.length}</strong><em>ostatnich zapisanych sesji</em></article></div>
        <section className="panel"><div className="panel-head"><div><h3>Ostatnie sesje</h3><p>Zapisane sesje skupienia i przypisane do nich przedmioty.</p></div><span className="tag">{focusHistory.length} SESJI</span></div>{focusHistory.length===0?<div className="empty-state"><span className="empty-state-icon">◷</span><strong>Nie ma jeszcze sesji skupienia</strong><p>Ukończ pierwszą sesję, aby zacząć budować historię.</p></div>:<div className="focus-history-list">{focusHistory.slice(0,30).map(item=><article className="focus-history-row" key={item.id}><span className="focus-history-mark">✓</span><div className="focus-history-copy"><strong>{item.subjectName||'Sesja bez przedmiotu'}</strong><small>{new Intl.DateTimeFormat('pl-PL',{dateStyle:'medium',timeStyle:'short'}).format(new Date(item.startedAt))}{item.note?` · ${item.note}`:''}</small></div><b>{Math.round(item.durationSeconds/60)} min</b></article>)}</div>}</section>
      </div>}

      {active==='calendar'&&<div className="page-stack"><section className="panel calendar-panel"><div className="panel-head"><div><h3>Harmonogram</h3><p>Wybierz przedział czasu, aby utworzyć wydarzenie. Kliknij istniejące wydarzenie, aby je edytować.</p></div><button className="primary-button" onClick={()=>{const start=new Date();start.setMinutes(0,0,0);start.setHours(start.getHours()+1);const end=new Date(start.getTime()+60*60*1000);openCreateEvent(start,end);}}>＋ Nowe wydarzenie</button></div><div className="event-type-legend"><span><i className="legend-lab"/>Laboratoria</span><span><i className="legend-lecture"/>Wykład</span><span><i className="legend-exercise"/>Ćwiczenia</span><span><i className="legend-project"/>Projekt</span><span><i className="legend-exam"/>Kolokwium</span><span><i className="legend-gym"/>Siłownia</span><span><i className="legend-own-project"/>Projekt własny</span><span><i className="legend-own-event"/>Event własny</span></div><FullCalendar plugins={[timeGridPlugin,dayGridPlugin,interactionPlugin]} initialView="timeGridWeek" headerToolbar={{left:'prev,next today',center:'title',right:'timeGridDay,timeGridWeek,dayGridMonth'}} height="auto" nowIndicator editable selectable selectMirror dayMaxEvents events={events} eventDrop={info=>{if(info.event.start&&info.event.end)void saveCalendarEvent({id:info.event.id,title:info.event.title,start:info.event.start.toISOString(),end:info.event.end.toISOString(),eventType:String(info.event.extendedProps.eventType || 'lecture')});}} eventResize={info=>{if(info.event.start&&info.event.end)void saveCalendarEvent({id:info.event.id,title:info.event.title,start:info.event.start.toISOString(),end:info.event.end.toISOString(),eventType:String(info.event.extendedProps.eventType || 'lecture')});}} select={info=>openCreateEvent(info.start,info.end)} eventClick={info=>openEditEvent({id:info.event.id,title:info.event.title,start:info.event.start,end:info.event.end,eventType:String(info.event.extendedProps.eventType || 'lecture')})}/></section></div>}

      {active==='blocks'&&<div className="page-stack blocks-page"><section className="panel profiles-panel blocks-panel"><div className="blocks-title-row"><div><div className="blocks-eyebrow">WEBSITE &amp; APP BLOCKS</div><p>Wybierz blok, aby edytować jego reguły.</p></div><button type="button" className="blocks-add-icon" aria-label="Dodaj nowy blok" title="Dodaj nowy blok" onClick={()=>document.getElementById('new-block-name')?.focus()}>＋</button></div><form className="add-profile blocks-add-form" onSubmit={addProfile}><input id="new-block-name" value={name} onChange={e=>setName(e.target.value)} placeholder="Nazwa nowego bloku…" maxLength={100} aria-label="Nazwa nowego bloku"/><button type="submit" disabled={!name.trim()||health!=='online'}>＋ Dodaj blok</button></form>{profiles.length===0?<div className="blocks-empty">Nie ma jeszcze żadnych bloków. Dodaj pierwszy blok powyżej.</div>:<div className="blocks-profile-list">{profiles.map(p=><article className="blocks-profile-row" key={p.id}><span className="blocks-profile-stripe"/><div className="blocks-profile-copy"><strong>{p.name}</strong><small>{p.id==='default'?'Profil domyślny':'Blok stron i aplikacji'}</small></div><div className="blocks-profile-actions"><button type="button" className="blocks-status-button" onClick={()=>void selectProfile(p)}>Edytuj reguły <span>›</span></button><button type="button" className={`blocks-lock-button ${p.locked?'is-locked':''}`} onClick={()=>void toggleProfileLock(p)}>{p.locked?'Locked':'Unlocked'}</button></div></article>)}</div>}</section>
      {selectedProfileId&&(()=>{const selectedProfile=profiles.find(p=>p.id===selectedProfileId); if(!selectedProfile)return null; const locked=selectedProfile.locked; return <div className="blocks-modal-backdrop" role="presentation" onMouseDown={e=>{if(e.target===e.currentTarget)setSelectedProfileId('');}}><section className="blocks-editor-modal" role="dialog" aria-modal="true" aria-label={`Edycja bloku ${selectedProfile.name}`}><header className="blocks-editor-heading"><span>▣</span><strong>Edycja bloku - {selectedProfile.name}</strong><button type="button" aria-label="Zamknij edycję" onClick={()=>setSelectedProfileId('')}>×</button></header><h2>Ustawienia bloku: {selectedProfile.name}</h2><nav className="blocks-editor-tabs" aria-label="Kategorie reguł">{([{id:'websites',label:'Strony'},{id:'exceptions',label:'Wyjątki'},{id:'apps',label:'YouTube'},{id:'breaks',label:'Przerwy'},{id:'schedule',label:'Harmonogram'}] as const).map(tab=><button type="button" key={tab.id} className={blockEditorTab===tab.id?'active':''} onClick={()=>setBlockEditorTab(tab.id)}>{tab.label}</button>)}</nav>
      {blockEditorTab==='websites'&&<div className="blocks-tab-content"><form className="blocks-rule-form" onSubmit={addProfileDomain}><input value={domainInput} onChange={e=>setDomainInput(e.target.value)} placeholder="Wpisz * dla całego internetu lub domenę" maxLength={300} aria-label="Domena do dodania"/><button type="submit" disabled={!domainInput.trim()||savingDomain||health!=='online'||locked}>＋ Dodaj</button></form><div className="blocks-rule-list">{profileDomains.length===0?<div className="blocks-rule-empty">Brak reguł stron dla tego bloku.</div>:profileDomains.map(rule=><div className="blocks-rule-row" key={rule.id}><span>{rule.domain}</span><button type="button" disabled={locked} onClick={()=>void deleteProfileDomain(rule)} aria-label={`Usuń ${rule.domain}`}>×</button></div>)}</div><div className="blocks-import-row"><button type="button" className="blocks-green-button" onClick={()=>showInfo('Wklej pełną listę reguł','Import list zbiorczy nie jest jeszcze dostępny w tym widoku. Na razie dodawaj domeny pojedynczo, używając pola powyżej.')}>▤ Wklej pełną listę reguł (Tekst)</button><button type="button" className="blocks-muted-button" onClick={()=>showInfo('Wczytaj reguły z pliku','Import plików .txt nie jest jeszcze podłączony do nowego backendu.')}>📁 Wczytaj reguły z pliku .txt</button></div><p className="blocks-hint">Lista działa obecnie jako biała lista domen: domena oraz jej subdomeny są dozwolone. Wzorce tekstowe ze starej aplikacji nie są jeszcze w pełni obsługiwane.</p></div>}
      {blockEditorTab==='exceptions'&&<div className="blocks-placeholder-tab"><strong>Wyjątki od blokowania</strong><p>Obsługa wyjątków opartych na wzorcach, np. <code>*steam*</code> lub <code>*.pdf</code>, nie została jeszcze przeniesiona do nowego silnika. Nie zapisuję ich jako domen, żeby nie zmienić znaczenia reguł.</p></div>}
      {blockEditorTab==='apps'&&<div className="blocks-tab-content"><div className="blocks-youtube-heading"><strong>Filtr kanałów YouTube</strong><small>Dozwolone kanały w tym profilu</small></div><form className="blocks-rule-form" onSubmit={addYoutubeChannel}><input value={youtubeHandleInput} onChange={e=>setYoutubeHandleInput(e.target.value)} placeholder="np. @konopskyy" maxLength={31} aria-label="Uchwyt kanału YouTube"/><button type="submit" disabled={!youtubeHandleInput.trim()||savingYoutubeChannel||health!=='online'||locked}>＋ Dodaj</button></form><div className="blocks-rule-list">{youtubeChannels.length===0?<div className="blocks-rule-empty">Brak dozwolonych kanałów YouTube.</div>:youtubeChannels.map(channel=><div className="blocks-rule-row" key={channel.id}><span>@{channel.handle}</span><button type="button" disabled={locked} onClick={()=>void deleteYoutubeChannel(channel)} aria-label={`Usuń @${channel.handle}`}>×</button></div>)}</div><p className="blocks-hint">Blokowanie aplikacji Windows jest obecnie globalne, nie przypisane do konkretnego profilu. Reguły aplikacji z poprzedniej wersji wymagają jeszcze migracji.</p></div>}
      {(blockEditorTab==='breaks'||blockEditorTab==='schedule')&&<div className="blocks-placeholder-tab"><strong>{blockEditorTab==='breaks'?'Przerwy':'Harmonogram blokady'}</strong><p>{blockEditorTab==='breaks'?'Konfiguracja przerw i zasad odblokowywania z poprzedniej aplikacji nie została jeszcze przeniesiona.':'Harmonogram profilu nie jest jeszcze edytowalny w tym oknie. Harmonogram rozszerzenia konfiguruje się osobno.'}</p></div>}
      {locked&&<div className="blocks-locked-note">Ten blok jest zablokowany. Odblokuj go przyciskiem w wierszu profilu, aby zmienić reguły.</div>}<footer className="blocks-editor-footer">{selectedProfile.id!=='default'&&<button type="button" className="blocks-delete-profile" disabled={locked} onClick={()=>deleteProfile(selectedProfile)}>Usuń ten profil</button>}<span/><button type="button" className="blocks-close-button" onClick={()=>setSelectedProfileId('')}>Anuluj</button><button type="button" className="blocks-save-button" onClick={()=>setSelectedProfileId('')}>Gotowe</button></footer></section></div>})()}</div>}

      {active==='subjects'&&<div className="page-stack"><section className="panel"><div className="panel-head"><div><h3>Przedmioty i obszary nauki</h3><p>Przedmioty zapisują się lokalnie i pozostają po ponownym uruchomieniu.</p></div><span className="tag">{subjects.length} PRZEDMIOTÓW</span></div><form className="add-profile subject-form" onSubmit={addSubject}><input value={subjectName} onChange={e=>setSubjectName(e.target.value)} placeholder="Nazwa przedmiotu, np. Matematyka" maxLength={120} aria-label="Nazwa przedmiotu" required/><button type="submit" disabled={!subjectName.trim()||savingSubject||health!=='online'}>{savingSubject?'Zapisywanie…':'＋ Dodaj przedmiot'}</button></form>{subjects.length===0?<div className="empty-state"><span className="empty-state-icon">▤</span><strong>Lista przedmiotów jest pusta</strong><p>Dodaj przedmiot, a następnie przypisz do niego zajęcia w tygodniowym planie.</p></div>:<div className="subject-preview-grid">{subjects.map(subject=><article className="subject-preview" key={subject.id}><div><strong>{subject.name}</strong><small>{sessions.filter(session=>session.subjectId===subject.id).length} pozycji w planie</small></div><button className="secondary-button" onClick={()=>void editSubject(subject)}>Edytuj</button><button className="task-delete" onClick={()=>deleteSubject(subject)} title={`Usuń ${subject.name}`} aria-label={`Usuń ${subject.name}`}>×</button></article>)}</div>}</section><section className="panel"><div className="panel-head"><div><h3>Tygodniowy plan zajęć</h3><p>Zapisz stałe godziny zajęć. W Kalendarzu możesz też dodać wydarzenie i ustawić powtarzanie co tydzień.</p></div><span className="tag">{sessions.length} ZAJĘĆ</span></div>{subjects.length===0?<div className="domain-empty">Najpierw utwórz co najmniej jeden przedmiot.</div>:<form className="session-form" onSubmit={addSession}><label>Przedmiot<select value={sessionSubjectId || subjects[0]?.id || ''} onChange={e=>setSessionSubjectId(e.target.value)} required>{subjects.map(subject=><option value={subject.id} key={subject.id}>{subject.name}</option>)}</select></label><label>Dzień<select value={sessionWeekday} onChange={e=>setSessionWeekday(e.target.value)}>{weekdays.map((day,index)=><option value={index} key={day}>{day}</option>)}</select></label><label>Od<input type="time" value={sessionStart} onChange={e=>setSessionStart(e.target.value)} required/></label><label>Do<input type="time" value={sessionEnd} onChange={e=>setSessionEnd(e.target.value)} required min={sessionStart}/></label><label>Typ<select value={sessionType} onChange={e=>setSessionType(e.target.value)}><option>Wykład</option><option>Ćwiczenia</option><option>Laboratorium</option><option>Seminarium</option><option>Inne</option></select></label><label>Sala (opcjonalnie)<input value={sessionRoom} onChange={e=>setSessionRoom(e.target.value)} maxLength={120} placeholder="np. 204 / B1"/></label><button type="submit" disabled={savingSession||health!=='online'||!subjects.length||sessionEnd<=sessionStart}>{savingSession?'Zapisywanie…':editingSessionId?'Zapisz zmiany':'＋ Dodaj do planu'}</button>{editingSessionId&&<button type="button" className="secondary-button" onClick={cancelSessionEdit}>Anuluj edycję</button>}</form>}{sessions.length===0?<div className="empty-tasks"><div>▦</div><strong>Plan jest pusty</strong><p>Dodaj pierwszy stały termin zajęć.</p></div>:<div className="session-list">{weekdays.map((day,weekday)=>{const rows=sessions.filter(session=>session.weekday===weekday);return rows.length?<div className="session-day" key={day}><h4>{day}</h4>{rows.map(session=><article className="session-row" key={session.id}><div className="session-time">{session.startTime}<span>–</span>{session.endTime}</div><div className="session-info"><strong>{session.subjectName}</strong><small>{session.sessionType}{session.room?` · ${session.room}`:''}</small></div><button className="secondary-button session-edit-button" onClick={()=>editSession(session)} aria-label={`Edytuj zajęcia ${session.subjectName}`} title="Edytuj zajęcia">Edytuj</button><button className="task-delete" onClick={()=>deleteSession(session)} aria-label={`Usuń zajęcia ${session.subjectName}`} title="Usuń zajęcia">×</button></article>)}</div>:null})}</div>}</section></div>}
      {active==='overdue'&&<div className="page-stack"><section className="panel overdue-panel"><div className="panel-head"><div><h3>Twoje zadania i terminy</h3><p>Zadania są przechowywane w lokalnej bazie danych i pozostają po ponownym uruchomieniu.</p></div><span className="tag">{pendingCount} OTWARTE</span></div><div className="task-stats"><div><small>Otwarte zadania</small><strong>{pendingCount}</strong></div><div className={overdueCount?'stat-late':''}><small>Po terminie</small><strong>{overdueCount}</strong></div><div><small>Ukończone</small><strong>{tasks.length-pendingCount}</strong></div></div><form className="task-form" onSubmit={addTask}><label>Nazwa zadania<input value={taskTitle} onChange={e=>setTaskTitle(e.target.value)} maxLength={200} placeholder="Np. Dokończyć projekt" required/></label><label>Termin<input type="datetime-local" value={taskDueAt} onChange={e=>setTaskDueAt(e.target.value)} required/></label><label>Priorytet<select value={taskPriority} onChange={e=>setTaskPriority(e.target.value as OverdueTask['priority'])}><option value="low">Niski</option><option value="normal">Normalny</option><option value="high">Wysoki</option><option value="urgent">Pilny</option></select></label><label>Kategoria<select value={taskCategory} onChange={e=>setTaskCategory(e.target.value)}><option>Nauka</option><option>Projekt</option><option>Administracyjne</option><option>Osobiste</option><option>Ogólne</option></select></label><label className="task-notes-field">Notatka (opcjonalnie)<input value={taskNotes} onChange={e=>setTaskNotes(e.target.value)} maxLength={2000} placeholder="Dodatkowe informacje"/></label><button type="submit" disabled={!taskTitle.trim()||!taskDueAt||savingTask||health!=='online'}>{savingTask?'Zapisywanie…':'＋ Dodaj zadanie'}</button></form>{tasks.length===0?<div className="empty-tasks"><div>✓</div><strong>Na razie pusto</strong><p>Dodaj zadanie z terminem, żeby zacząć korzystać z Overdue Trackera.</p></div>:<><div className="task-toolbar"><input value={taskSearch} onChange={e=>setTaskSearch(e.target.value)} placeholder="Szukaj zadania lub notatki…" aria-label="Szukaj zadań"/><div className="task-filter-tabs">{([{id:'all',label:'Wszystkie'},{id:'open',label:'Otwarte'},{id:'late',label:'Po terminie'},{id:'done',label:'Ukończone'}] as const).map(item=><button key={item.id} type="button" className={taskFilter===item.id?'active':''} onClick={()=>setTaskFilter(item.id)}>{item.label}</button>)}</div><select className="task-priority-filter" value={taskPriorityFilter} onChange={e=>setTaskPriorityFilter(e.target.value as typeof taskPriorityFilter)} aria-label="Filtruj po priorytecie"><option value="all">Każdy priorytet</option><option value="urgent">Pilny</option><option value="high">Wysoki</option><option value="normal">Normalny</option><option value="low">Niski</option></select><button type="button" className="secondary-button task-export-button" onClick={()=>void completeVisibleTasks()} disabled={!visibleTasks.some(task=>!task.completed)}>Zakończ widoczne</button><button type="button" className="secondary-button task-export-button" onClick={exportTasksCsv} disabled={visibleTasks.length===0}>Eksportuj CSV</button><span className="task-result-count">{visibleTasks.length} / {tasks.length}</span></div>{visibleTasks.length===0?<div className="empty-tasks"><strong>Brak pasujących zadań</strong><p>Zmień filtr lub wyszukiwaną frazę.</p></div>:<div className="task-list">{visibleTasks.map(task=>{const late=!task.completed&&new Date(task.dueAt).getTime()<Date.now();return <article className={`task-row ${task.completed?'task-completed':''} ${late?'task-late':''}`} key={task.id}><input className="task-check" type="checkbox" checked={task.completed} onChange={e=>void setTaskCompleted(task,e.target.checked)} aria-label={`Oznacz ${task.title} jako ${task.completed?'niewykonane':'wykonane'}`}/><div className="task-copy"><strong>{task.title}</strong><div className="task-badges"><span className={`priority-badge priority-${task.priority||'normal'}`}>{({low:'Niski',normal:'Normalny',high:'Wysoki',urgent:'Pilny'} as Record<string,string>)[task.priority||'normal']}</span><span className="category-badge">{task.category||'Ogólne'}</span></div><small className={late?'late-label':''}>{late?'Po terminie · ':task.completed?'Ukończone · ':'Termin · '}{formatDueDate(task.dueAt)}</small>{task.notes&&<p>{task.notes}</p>}</div><button type="button" className="secondary-button task-edit-button" onClick={()=>void editTask(task)}>Edytuj</button><button className="task-delete" onClick={()=>void deleteTask(task)} title="Usuń zadanie" aria-label="Usuń zadanie">×</button></article>})}</div>}</>}</section></div>}

      {active==='integrations'&&<div className="page-stack"><section className="panel"><div className="panel-head"><div><h3>Integracja z Windowsem</h3><p>Blokuj wybrane procesy, ścieżki plików i tytuły okien. Reguły są lokalne dla tego komputera.</p></div><span className="tag">{windowsStatus?.supported?'WINDOWS':'TYLKO WINDOWS'}</span></div>{windowsStatus?.supported===false&&<div className="settings-note"><strong>Funkcje systemowe niedostępne</strong><p>{windowsStatus.message ?? 'Uruchom backend Amogus na Windows, aby zarządzać procesami i autostartem.'}</p></div>}<div className="settings-list"><div className="settings-row"><span className="settings-symbol">◉</span><div><strong>AppBlocker</strong><small>{windowsStatus?.blockerEnabled?'Aktywny — sprawdza reguły co ok. sekundę':'Wyłączony — reguły nie są egzekwowane'}</small></div><button className={windowsStatus?.blockerEnabled?'danger-button':'primary-button'} disabled={windowsBusy||!windowsStatus?.supported} onClick={()=>void setWindowsFeature('blocker',!windowsStatus?.blockerEnabled)}>{windowsStatus?.blockerEnabled?'Wyłącz blokowanie':'Włącz blokowanie'}</button></div><div className="settings-row"><span className="settings-symbol">↻</span><div><strong>Autostart użytkownika</strong><small>Uruchamiaj silnik Amogus przy logowaniu do Windows (wpis HKCU Run).</small></div><button className="secondary-button" disabled={windowsBusy||!windowsStatus?.supported} onClick={()=>void setWindowsFeature('autostart',!windowsStatus?.autostart)}>{windowsStatus?.autostart?'Wyłącz autostart':'Włącz autostart'}</button></div><div className="settings-row"><span className="settings-symbol">✓</span><div><strong>Zablokowane uruchomienia</strong><small>{windowsStatus?.lastAction?`Ostatnia akcja: ${windowsStatus.lastAction}`:'Brak zarejestrowanych akcji w tej sesji.'}</small></div><span className="settings-value">{windowsStatus?.blockedCount ?? 0}</span></div></div><form className="add-profile" onSubmit={addWindowsRule}><select value={windowsRuleType} onChange={e=>setWindowsRuleType(e.target.value as WindowsRule['type'])} aria-label="Typ reguły Windows"><option value="process">Proces .exe</option><option value="title">Tytuł okna</option><option value="path">Ścieżka pliku</option></select><input value={windowsRulePattern} onChange={e=>setWindowsRulePattern(e.target.value)} placeholder={windowsRuleType==='process'?'np. discord.exe':windowsRuleType==='title'?'np. Minecraft*':'np. C:\\Games\\Game.exe'} maxLength={260} aria-label="Wzorzec reguły Windows"/><button type="submit" disabled={windowsBusy||!windowsRulePattern.trim()||!windowsStatus?.supported}>＋ Dodaj regułę</button></form>{windowsRules.length===0?<div className="domain-empty">Brak reguł blokowania aplikacji.</div>:<div className="domain-list">{windowsRules.map(rule=><div className="domain-row" key={rule.id}><span className="domain-globe">{rule.type==='process'?'EXE':rule.type==='title'?'OKNO':'PATH'}</span><strong>{rule.pattern}</strong><span className="domain-mode">{rule.type==='process'?'Proces':rule.type==='title'?'Tytuł okna':'Ścieżka'}</span><button className="task-delete" title={`Usuń ${rule.pattern}`} aria-label={`Usuń regułę ${rule.pattern}`} onClick={()=>void deleteWindowsRule(rule)}>×</button></div>)}</div>}<div className="settings-note"><strong>Ważne przed włączeniem</strong><p>Reguła procesu lub tytułu okna kończy cały pasujący proces, nie tylko pojedyncze okno. Używaj konkretnych nazw, przetestuj najpierw na nieszkodliwej aplikacji i nie dodawaj procesów systemowych. Blokowanie działa tylko podczas działania backendu; nie ma jeszcze wzajemnego watchdogu ani gwarancji odporności na wyłączenie procesu.</p></div></section><section className="panel"><div className="panel-head"><div><h3>Integracje i narzędzia</h3><p>Importuj wydarzenia z pliku ICS i eksportuj lokalne dane do kopii JSON.</p></div><span className="tag">NARZĘDZIA LOKALNE</span></div><div className="tool-grid"><button className="tool-card" onClick={()=>void connectGoogleCalendar()} disabled={googleBusy}><span className="tool-icon">G</span><span><strong>{googleStatus?.authenticated?'Konto Google połączone':'Połącz Google Calendar'}</strong><small>{googleStatus?.configured?(googleStatus.authenticated?'OAuth aktywne · kalendarz główny':'Gotowe do autoryzacji'): 'Dodaj własny plik credentials.json'}</small></span><b>{googleBusy?'…':'↗'}</b></button><button className="tool-card" onClick={()=>void syncGoogleCalendar()} disabled={googleBusy||!googleStatus?.authenticated}><span className="tool-icon">⇄</span><span><strong>{googleBusy?'Synchronizowanie…':'Synchronizuj teraz'}</strong><small>Synchronizacja dwukierunkowa z kalendarzem głównym</small></span><b>↻</b></button><button className="tool-card" onClick={()=>icsInputRef.current?.click()} disabled={importingIcs}><span className="tool-icon">↥</span><span><strong>{importingIcs?'Importowanie…':'Import kalendarza ICS'}</strong><small>Wczytaj wydarzenia z pliku .ics</small></span><b>→</b></button><input ref={icsInputRef} type="file" accept=".ics,text/calendar" hidden onChange={e=>void importIcsFile(e.target.files?.[0])}/><button className="tool-card" onClick={()=>showInfo('Amogus Blocker — instalacja','Rozpakuj projekt i otwórz chrome://extensions lub edge://extensions. Włącz tryb dewelopera, wybierz „Załaduj rozpakowane” i wskaż folder extension/blocker. Uruchom Amogus Core, dodaj domeny do profilu i włącz blokowanie w popupie rozszerzenia.', ['Działa w Chrome i Edge z Manifest V3','Tryb białej listy: blokuje wszystkie strony HTTP(S) poza wyjątkami profilu','Wymaga działającego lokalnego backendu','Reguły odświeżają się okresowo'])}><span className="tool-icon">⌘</span><span><strong>Rozszerzenie przeglądarki</strong><small>Diagnostyka i konfiguracja</small></span><b>→</b></button><button className="tool-card" onClick={()=>void exportBackup()}><span className="tool-icon">▣</span><span><strong>Eksport kopii zapasowej</strong><small>Pobierz profile, domeny, przedmioty, wydarzenia, zadania i historię skupienia jako JSON</small></span><b>↓</b></button><button className="tool-card" onClick={()=>backupInputRef.current?.click()} disabled={restoringBackup}><span className="tool-icon">↧</span><span><strong>{restoringBackup?'Przywracanie…':'Przywróć kopię zapasową'}</strong><small>Odtwórz dane z wcześniej wyeksportowanego pliku JSON</small></span><b>↑</b></button><input ref={backupInputRef} type="file" accept=".json,application/json" hidden onChange={e=>void chooseBackupFile(e.target.files?.[0])}/></div>{(googleMessage||googleStatus)&&<div className="settings-note"><strong>Google Calendar — status</strong><p>{googleMessage || (googleStatus?.authenticated ? 'Konto jest autoryzowane.' : googleStatus?.configured ? 'Wymagana jest autoryzacja OAuth.' : `Brak pliku OAuth. Oczekiwana ścieżka: ${googleStatus?.credentialsPath ?? 'Library/credentials.json'}`)}</p>{googleStatus?.lastSync&&<p>Ostatnia synchronizacja: {new Date(Number(googleStatus.lastSync)*1000).toLocaleString('pl-PL')}</p>}{googleStatus?.lastError&&<p>Ostatni błąd: {googleStatus.lastError}</p>}</div>}<div className="settings-note"><strong>Zakres kopii</strong><p>Przywracanie zastępuje aktualne profile, domeny, przedmioty, wydarzenia, zadania, plan zajęć i historię skupienia. Operacja jest transakcyjna: przy błędzie baza nie powinna pozostać częściowo odtworzona. Przed przywróceniem wyeksportuj bieżące dane.</p></div></section></div>}

      {active==='settings'&&<div className="page-stack"><section className="panel"><div className="panel-head"><div><h3>Ustawienia interfejsu</h3><p>Elementy wizualne i informacje o aktualnej wersji.</p></div><span className="tag">PREFERENCJE</span></div><div className="settings-list"><div className="settings-row"><span className="settings-symbol">Aa</span><div><strong>Wygląd</strong><small>Domyślny ciemny motyw, stonowane powierzchnie i zielony akcent.</small></div><span className="settings-value">Dark · domyślny</span></div><div className="settings-row"><span className="settings-symbol">⌘</span><div><strong>Skróty i dostępność</strong><small>Obsługa klawiatury, widoczny fokus i zamykanie okien klawiszem Escape.</small></div><button className="secondary-button" onClick={()=>showInfo('Skróty i dostępność','Nawigacja może być obsługiwana klawiaturą. Escape zamyka otwarte okna dialogowe.', ['Tab — przechodzenie po kontrolkach','Enter / Spacja — aktywacja przycisku','Escape — zamknięcie okna'])}>Szczegóły</button></div><div className="settings-row"><span className="settings-symbol">ⓘ</span><div><strong>O aplikacji</strong><small>Amogus Rebuild · interfejs webowy</small></div><span className="settings-value">0.29.0</span></div><div className="settings-row"><span className="settings-symbol">◉</span><div><strong>Stan silnika</strong><small>{health==='online'?'Lokalny backend odpowiada.':health==='checking'?'Trwa sprawdzanie połączenia.':'Backend nie odpowiada.'}</small></div><button className="secondary-button" onClick={()=>void load()}>Sprawdź ponownie</button></div></div><div className="settings-note"><strong>Informacja o zakresie</strong><p>Timer skupienia zapisuje ukończone sesje w lokalnej bazie i kopii JSON. Import ICS jest dostępny. Blokowanie stron działa przez osobne rozszerzenie Chrome/Edge, gdy jest zainstalowane i włączone. Harmonogram konfiguruje się w panelu rozszerzenia. Google Calendar obsługuje OAuth i ręczną synchronizację dwukierunkową po skonfigurowaniu credentials.json. Funkcje Windows obejmują reguły procesów/okien i autostart użytkownika.</p></div></section><section className="panel danger-zone"><div><h3>Sesja aplikacji</h3><p>Przygotowane miejsce na bezpieczne zamykanie i wybór zachowania aplikacji.</p></div><button className="secondary-button" onClick={()=>showInfo('Zamykanie Amogusa','Aplikacja działa obecnie w przeglądarce i lokalnym procesie C++. Ten przycisk nie zatrzymuje procesów — pokazuje jedynie projektowany panel potwierdzenia.', ['Zminimalizuj do zasobnika','Pozostaw silnik lokalny','Zamknij interfejs'])}>Opcje zamykania</button></section></div>}
      <footer>AMOGUS CORE <span>•</span> INTERFEJS 0.30.0 <span>•</span> LOKALNY PROTOTYP</footer>
    </main>

    {dialog&&<div className="modal-backdrop" role="presentation" onMouseDown={e=>{if(e.target===e.currentTarget)setDialog(null);}}><section className="modal" role="dialog" aria-modal="true" aria-labelledby="modal-title">
      {dialog.kind==='event'&&<form onSubmit={submitEvent}><div className="modal-header"><div><span className="modal-kicker">KALENDARZ</span><h2 id="modal-title">{dialog.mode==='create'?'Nowe wydarzenie':'Edytuj wydarzenie'}</h2><p>Uzupełnij nazwę i czas trwania.</p></div><button type="button" className="modal-close" aria-label="Zamknij" onClick={()=>setDialog(null)}>×</button></div><div className="modal-body"><label className="field-label">Nazwa wydarzenia<input autoFocus value={dialog.title} onChange={e=>updateEventDialog({title:e.target.value})} placeholder="Np. Laboratorium z fizyki" maxLength={200} required/></label><label className="field-label">Rodzaj wydarzenia<select value={dialog.eventType} onChange={e=>updateEventDialog({eventType:e.target.value})}><optgroup label="Uczelnia"><option value="lab">Laboratoria — zielony</option><option value="lecture">Wykład — niebieski</option><option value="exercise">Ćwiczenia — żółtopomarańczowy</option><option value="project">Projekt — żółty</option><option value="examLecture">Kolokwium na wykładzie — czerwony</option><option value="examExercise">Kolokwium na ćwiczeniach — czerwony</option></optgroup><optgroup label="Własne"><option value="gym">Siłownia — szary</option><option value="ownProject">Projekt własny — fioletowy</option><option value="ownEvent">Event własny — jasnoniebieski</option></optgroup></select></label><div className="field-grid"><label className="field-label">Początek<input type="datetime-local" value={dialog.start} onChange={e=>updateEventDialog({start:e.target.value})} required/></label><label className="field-label">Koniec<input type="datetime-local" value={dialog.end} onChange={e=>updateEventDialog({end:e.target.value})} required/></label></div>{dialog.mode==='create'&&<div className="recurrence-box"><label className="recurrence-toggle"><input type="checkbox" checked={dialog.repeatWeekly} onChange={e=>updateEventDialog({repeatWeekly:e.target.checked})}/> Powtarzaj co tydzień</label>{dialog.repeatWeekly&&<label className="field-label">Powtarzaj do dnia<input type="date" min={dialog.start.slice(0,10)} value={dialog.repeatUntil} onChange={e=>updateEventDialog({repeatUntil:e.target.value})} required/></label>}</div>}{dialog.start&&dialog.end&&new Date(dialog.end)<=new Date(dialog.start)&&<p className="field-error">Koniec wydarzenia musi przypadać po jego początku.</p>}</div><div className="modal-footer">{dialog.mode==='edit'&&<button type="button" className="danger-text-button" onClick={()=>deleteCalendarEvent(dialog.id!,dialog.title)}>Usuń wydarzenie</button>}<span className="modal-spacer"/><button type="button" className="secondary-button" onClick={()=>setDialog(null)}>Anuluj</button><button type="submit" className="primary-button" disabled={!dialog.title.trim()||!dialog.start||!dialog.end||new Date(dialog.end)<=new Date(dialog.start)}>{dialog.mode==='create'?'Dodaj wydarzenie':'Zapisz zmiany'}</button></div></form>}
      {dialog.kind==='confirm'&&<><div className="modal-header"><div><span className="modal-kicker">POTWIERDZENIE</span><h2 id="modal-title">{dialog.title}</h2><p>{dialog.message}</p></div><button type="button" className="modal-close" aria-label="Zamknij" onClick={()=>setDialog(null)}>×</button></div><div className="modal-footer"><span className="modal-spacer"/><button className="secondary-button" onClick={()=>setDialog(null)}>Anuluj</button><button className={dialog.danger?'danger-button':'primary-button'} onClick={dialog.onConfirm}>{dialog.confirmLabel??'Potwierdź'}</button></div></>}
      {dialog.kind==='info'&&<><div className="modal-header"><div><span className="modal-kicker">PANEL INTERFEJSU</span><h2 id="modal-title">{dialog.title}</h2><p>{dialog.description}</p></div><button type="button" className="modal-close" aria-label="Zamknij" onClick={()=>setDialog(null)}>×</button></div>{dialog.sections&&<div className="modal-body"><div className="modal-preview-list">{dialog.sections.map((section,i)=><div key={section}><span>{String(i+1).padStart(2,'0')}</span><strong>{section}</strong><em>Podgląd</em></div>)}</div><div className="preview-disclaimer">To podgląd interfejsu. Nie uruchamia operacji ani nie zmienia danych.</div></div>}<div className="modal-footer"><span className="modal-spacer"/><button className="primary-button" onClick={()=>setDialog(null)}>Gotowe</button></div></>}
    </section></div>}
  </div>;
}
