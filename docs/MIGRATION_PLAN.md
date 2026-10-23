# Migration plan

## Stage 0 — scaffold (this delivery)
- Establish Qt-free CMake/C++ service and React frontend.
- Prove API ↔ SQLite read/write with profiles.
- Provide a calendar interaction prototype with SQLite persistence for event create/update; recurrence and Google sync remain future work.

## Stage 1 — reliable service foundation
- Add local API authentication/authorization and strict request validation.
- Add structured logs, health diagnostics, schema migrations, backup/restore tests.
- Add automated tests for API and persistence.

## Stage 2 — domain model
- Port profile/rule data model and schedule calculations without UI dependencies.
- Preserve existing SQLite data shape where practical; define explicit migration scripts otherwise.
- Add tests before replacing any old behavior.

## Stage 3 — calendar and Google sync
- Persist events and implement recurrence/time-zone handling.
- Port Google Calendar synchronization carefully, including idempotency and stable external event IDs.
- Test drag/drop, resize, overlap, overnight events, DST, retry behavior and duplicate prevention.

## Stage 4 — enforcement
- Port AppBlocker and browser enforcement into standalone C++/WinAPI modules.
- Treat frontend as untrusted: it cannot authoritatively unlock profiles or disable enforcement.
- Review watchdog design, stop authorization, extension detection and failure modes separately.

## Stage 5 — ICS / Overdue Tracker / release
- Port ICS cycle rules, AFK compensation, postponement and overdue lock lifecycle with unit tests.
- Add extension integration tests, clean install/update/uninstall flow and signed release builds.

## Acceptance rule
A module is not considered migrated because it compiles or looks correct. Its behavioral tests and integration tests must pass, including restart/recovery cases.


## Implemented in scaffold 0.2.0
- Added persistent SQLite-backed Overdue Tracker tasks (create, list, complete/reopen, delete).
- Added calendar event deletion to the browser UI and enabled DELETE in CORS preflight.
- Removed obsolete FullCalendar v5-style CSS imports; FullCalendar v6 ships its styles with the packages.
- Still not implemented: original ICS ingestion, recurring rules, Google Calendar sync, enforcement engine, watchdog, extension communication, and authenticated API/IPC.


## 0.3.0 update
- Added SQLite-backed per-profile domain rules and UI to add/list/remove domains.
- Domain input normalization accepts a domain or full HTTP(S) URL and stores the hostname only.
- Explicitly not enforced yet; next stage is the enforcement design plus local API authentication and tests.


## 0.7.0 update — first subject-domain slice
- Added a persistent SQLite `subjects` table and list/create/delete API routes.
- Connected the Subjects UI to real backend data, including color selection and delete confirmation.
- Added duplicate-name and color-format validation.
- Not yet migrated: subject classes, note editing, schedule generation, and automatic subject-to-calendar/overdue synchronization.
- Next: add subject class records and edit flows, then connect class events to calendar with explicit tests for time zones and restarts.


## Implemented in 0.11.0

- Added persistent `subject_sessions` records linked to subjects with weekday, start/end time, session type, and optional room.
- Added `GET /api/sessions`, `POST /api/sessions`, and `DELETE /api/sessions/:id`.
- Added weekly timetable editor/list in the Subjects page.
- Included sessions in JSON backup and transactional restore; legacy backups without sessions remain supported.
- Still not connected to calendar event generation, the Overdue spaced-learning engine, or enforcement.

### Wersja 0.16.0
- Dodano edycję istniejących zajęć i walidację wejścia w endpointzie aktualizacji.
- Dodano wyszukiwarkę oraz filtry listy zadań po statusie i zaległości.


### Wersja 0.16.0 — powtórki rozłożone w czasie
- Dodano `study_cards` powiązane opcjonalnie z przedmiotami.
- Dodano API listowania, tworzenia, oceniania i usuwania fiszek.
- Interwały bazują na ocenie użytkownika; jest to prosty harmonogram, a nie pełna implementacja FSRS/SM-2.
- Kopie JSON eksportują i przywracają fiszki, a starsze kopie bez tej sekcji są akceptowane.
- Nadal do wykonania: testy integracyjne API i pełny build w środowisku docelowym.


## 0.21.0 — rozszerzenie blokowania
- Dodano lokalne limity dzienne czasu dla domen w rozszerzeniu.
- Rozszerzenie szacuje czas aktywnej karty w trakcie aktywnego blokowania, a po osiągnięciu limitu dodaje regułę przekierowania.
- Pomiar jest przybliżony (alarm co minutę, maksymalny pojedynczy przyrost 90 s); nie stanowi zabezpieczenia przed świadomym obejściem.

## Update 0.22.0 — study roadmaps and subject analytics

- `study_plans` stores named goals optionally associated with a subject, a target date and notes.
- `study_stages` stores ordered stages, optional due dates, completion state and completion timestamps. Deleting a plan cascades to its stages.
- API routes expose plan listing/creation, stage creation/completion/deletion and plan deletion.
- JSON backup includes both tables. Restore validates subject/plan references before opening the destructive transaction, then restores the new records inside the same transaction.
- The browser UI adds a dedicated Study Paths page, progress percentage, overdue stage indicator, CSV export and action to add a stage deadline to the calendar. The Subjects page includes per-subject focus time, flashcard counts/due cards and weekly timetable counts.
- Validation performed: TypeScript JSX transpile, extension JavaScript syntax, extension manifest JSON and ZIP integrity. Full C++ build and browser integration still need local verification.


## Update 0.24.0 — browser allow-list policy and extension backup

- Switched active blocker profiles to the source-spec allow-list policy: global HTTP(S) main-frame deny, profile domains allow, temporary allows, and highest-priority daily-limit enforcement.
- Added local extension settings export/import, filtered block history, CSV history export, and clearer status counts.
- Remaining major parity work: mutual watchdog, advanced profile break modes, Overdue ICS scheduling/AFK compensation, and automatic Google Calendar background/push sync and advanced recurrence/time-zone support. Windows process blocker and current-user autostart are implemented in 0.28.0, but require live Windows regression tests. Manual OAuth and two-way sync are implemented in 0.26.0 but require live Google API regression tests. YouTube channel filtering is implemented in 0.25.0 but still requires live browser regression tests.


## Update 0.24.0 — profile locking

- Added `PUT /api/profiles/:id/lock`; unlock requires the exact Japanese ritual phrase.
- Locked profiles reject domain create/delete requests and cannot be deleted.
- UI exposes lock/unlock controls and makes locked domain lists read-only.
- This is deliberate friction, not authentication or tamper-proof enforcement.


## Update 0.25.0 — YouTube channel filtering

- Added profile-scoped exact `@handle` allow-list storage, API CRUD, extension config snapshot and backup/restore support.
- Added YouTube content script and CSS for blackout cards, Shorts shelves and playback shield.
- Remaining validation: real Chrome/Edge tests on homepage, search, watch page, Shorts, SPA navigation, responsive layouts and signed-in/out sessions. Do not treat the feature as fully verified until those tests pass.


## Update 0.26.0 — Google Calendar
- Added local OAuth connect/callback, protected refresh-token storage, connection status, and manual two-way primary-calendar sync.
- Added Google event mapping/sync signatures and local-delete tombstones.
- Requires a user-supplied `Library/credentials.json`; live OAuth and API tests remain outstanding.


## Update 0.28.0 — Windows integration

Added the Windows-only process/window/path blocker and current-user Run-key autostart. Full Windows build and end-to-end process termination tests remain required. Mutual watchdog and browser-extension enforcement are not included in this release.
