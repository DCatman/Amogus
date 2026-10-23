# Amogus Rebuild 0.30.0

Browser UI (React + TypeScript + Vite) with a local C++17/SQLite backend. The interface defaults to dark mode and suppresses the browser's native context menu inside the app.

## What's implemented in this version

- Persistent weekly subject timetable in SQLite (`subject_sessions`).
- Materialize recurring timetable entries into calendar events for a selected Monday-to-Sunday week through `POST /api/sessions/generate-events`.
- Generated calendar event IDs are deterministic per timetable entry and date, so generating the same week again updates the generated events instead of duplicating them.
- The generator validates that `weekStart` is a real Monday (`YYYY-MM-DD`) and writes all events in a transaction.
- Task priorities (low/normal/high/urgent) and categories are stored in SQLite, editable, searchable and included in CSV/JSON backups. Existing databases are migrated with additive columns.

## Run

1. Build the C++ backend using CMake/Qt Creator. Backend binds only to `127.0.0.1:43127`.
2. In `frontend`, run `npm install`, `npm run build`, then `npm run dev`.
3. Open `http://127.0.0.1:5173/`.

## Generate calendar events from the timetable

In **Kalendarz**, select a Monday in “Tydzień od poniedziałku” and choose “Generuj z planu”. All recurring timetable entries matching that week are materialized as calendar events. Repeating the operation for the same week does not create duplicates. If a generated event is manually deleted, generating that week again will recreate it. This does not yet implement two-way synchronization or automatic event generation in the background.

## Important limitations

- Full C++ and browser integration builds have not been verified in this environment; rebuild and test locally before relying on it.
- Browser-level blocking is now implemented through the optional Chrome/Edge Manifest V3 extension in `extension/blocker`. It must be installed and explicitly enabled; it is not system-wide or tamper-proof. Follow the extension README and test the block/unblock flow in your own browser.
- Google Calendar uses local OAuth authorization and manual two-way synchronization for the primary calendar. Requires the user to configure Google OAuth credentials; automatic background/push sync and advanced recurrence/time-zone handling remain future work. Study-cycle/Overdue Tracker automation and watchdog enforcement are not yet implemented.
- The local API binds to loopback and has basic Origin/Host checks, but it is not production-grade authentication. Do not expose it to the network.

## Data

On Windows, SQLite is stored under `%LOCALAPPDATA%\Amogus\data.db`. Existing database tables are preserved and the timetable-to-calendar generator adds entries to `calendar_events`.

## Funkcje zachowane z poprzedniej wersji
- Edycja istniejących pozycji tygodniowego planu zajęć (API `PUT /api/sessions/:id` z walidacją godzin, dnia, typu i sali).
- Wyszukiwanie zadań po nazwie i notatce oraz filtry: wszystkie, otwarte, po terminie i ukończone.
- Licznik wyników filtrowania i stan pustej listy dla wyszukiwania bez trafień.
- Poprawka formularza planu: pierwszy przedmiot może zostać użyty nawet wtedy, gdy użytkownik nie zmienił domyślnej opcji selecta.

Pełna kompilacja C++ i build Vite muszą zostać wykonane w środowisku projektu; same zmiany źródłowe nie stanowią potwierdzenia testu integracyjnego.


## Nowości w 0.21.0

- Added an optional Chrome/Edge Manifest V3 extension in `extension/blocker`. It uses `declarativeNetRequest` to block main-frame navigation to domains in the selected profile, including subdomains.
- Added a read-only `/api/blocking/config` endpoint for extension sync. Extension-origin requests are restricted to that exact GET route; the regular web UI API policy remains unchanged.
- Extension is off by default, applies up to 500 domain rules, and refreshes periodically. It clears Amogus-owned rules if it cannot refresh from the backend. This is browser-level enforcement, not a system-wide firewall.

## Nowości w 0.16.0
- Zadania mają priorytet (niski, normalny, wysoki, pilny) i kategorię.
- Filtr priorytetu oraz akcja „Zakończ widoczne” pozwalają grupowo domknąć wyniki aktualnego wyszukiwania/filtrów; w razie częściowego błędu UI odświeża stan z backendu.
- Backend sortuje otwarte zadania z pilniejszym priorytetem wcześniej; wyszukiwanie obejmuje również kategorię i priorytet.
- Eksport CSV i kopia JSON uwzględniają nowe pola; przywracanie starych kopii ustawia wartości domyślne.
- Migracja SQLite dodaje brakujące kolumny bez usuwania dotychczasowych danych.


## Nowości w 0.16.0

- Dodano stronę **Skupienie** z timerem 15/25/45/60/90 minut, pauzą, wznowieniem i resetem.
- Ukończone sesje są zapisywane w tabeli `focus_sessions` SQLite, opcjonalnie przypisane do przedmiotu i opisane celem sesji.
- Historia pokazuje ostatnie sesje, przedmiot, czas i notatkę; statystyki podsumowują dzisiejszy i siedmiodniowy czas skupienia.
- Eksport i przywracanie kopii JSON obejmują historię skupienia; starsze kopie bez `focusSessions` pozostają obsługiwane.
- Ważne ograniczenie: timer działa w otwartej karcie przeglądarki; nie jest usługą działającą w tle i przerwana sesja nie zapisuje się automatycznie.


### Powtórki rozłożone w czasie

Wersja 0.16.0 dodaje osobny moduł fiszek. Karty można przypisać do przedmiotu, odsłonić odpowiedź i ocenić jako „Nie pamiętam”, „Trudne”, „Dobrze” lub „Łatwe”. Backend zapisuje interwał, liczbę powtórek i następny termin w SQLite. Fiszki są uwzględniane w eksporcie i przywracaniu kopii JSON. To podstawowy algorytm powtórek, nie implementacja pełnego SM-2/FSRS.

## Amogus Blocker (0.21.0)

A Chrome/Edge Manifest V3 extension is included at `extension/blocker`. It reads the read-only `/api/blocking/config` snapshot and applies selected-profile domains using `declarativeNetRequest`. Install instructions and limitations are in `extension/blocker/README.md`. The extension starts disabled and requires the local backend to be running. This is browser-level blocking, not a system-wide firewall or tamper-proof enforcement.


## Amogus Blocker 0.21.0

- Added optional weekly schedule (local computer time, Monday-first weekdays) to the Chrome/Edge extension.
- Added temporary pauses of 15, 30, and 60 minutes.
- Rules are recalculated every minute; schedule is disabled by default so existing manual enablement remains predictable.
- When schedule is outside its active interval, Amogus-owned rules are removed. If the backend fails during an active interval, last known-good rules remain active until a successful refresh.
- This is browser extension blocking, not a system-wide firewall. Full native build and live Chrome/Edge integration tests still need to be run on the target Windows machine.


## 0.21.0 — historia blokad

Rozszerzenie pokazuje ostatnie dopasowania reguł nawigacji w lokalnym popupie. Historia jest przechowywana w `chrome.storage.local` (do 100 wpisów) i może zostać wyczyszczona. Diagnostyczne zdarzenie `onRuleMatchedDebug` wymaga `declarativeNetRequestFeedback` i jest przeznaczone dla rozszerzeń załadowanych jako rozpakowane; nie jest to gwarantowany, kompletny dziennik produkcyjny.


## Amogus Blocker 0.21.0 — strona blokady i wyjątki czasowe

- Zablokowana nawigacja główna przekierowuje na lokalną stronę rozszerzenia `blocked.html`.
- Popup rozszerzenia pozwala dodać wyjątek dla pojedynczej domeny na 5 minut. Reguła `allow` ma wyższy priorytet niż reguła blokująca; wyjątki są usuwane automatycznie po wygaśnięciu.
- Aktywne wyjątki można podejrzeć i ręcznie usunąć w popupie.
- Wyjątki są lokalne dla profilu rozszerzenia/przeglądarki i nie są synchronizowane z backendem Amogus.
- Rozszerzenie nadal wymaga lokalnego backendu pod `127.0.0.1:43127`; używaj go wyłącznie lokalnie.

Weryfikacja tej wersji obejmuje kontrolę składni JS, poprawności manifestu i integralności ZIP; nie potwierdzono pełnej kompilacji C++ ani testu end-to-end w Chrome/Edge.


## Amogus Blocker 0.21.0 — strona blokady i wyjątki czasowe

- Zablokowane nawigacje główne kierują na lokalną stronę rozszerzenia `blocked.html`.
- Popup rozszerzenia pozwala dodać wyjątek dla pojedynczej domeny na 5 minut; reguła `allow` ma wyższy priorytet niż blokada.
- Aktywne wyjątki można obejrzeć i ręcznie usunąć. Wygasłe wpisy są czyszczone podczas synchronizacji reguł.
- Wyjątki są lokalne dla profilu rozszerzenia/przeglądarki i nie są synchronizowane z backendem.

Weryfikacja: składnia JS rozszerzenia i poprawność JSON manifestu sprawdzone; ZIP przechodzi test integralności. Nie potwierdzono pełnej kompilacji C++ ani testu end-to-end w Chrome/Edge.


## 0.21.0 — dzienne limity czasu domen

Rozszerzenie Amogus Blocker może mierzyć przybliżony czas aktywnej karty dla domen zdefiniowanych w limitach. W popupie dodaj domenę i limit w minutach (1–1440); domena musi już należeć do wybranego profilu Amogus. Gdy blokowanie profilu i jego harmonogram są aktywne, rozszerzenie nalicza czas aktywnej karty z ograniczeniem błędu pojedynczego pomiaru do 90 sekund. Po osiągnięciu dziennego limitu dodawana jest reguła przekierowująca do strony blokady. Dane użycia są lokalne w `chrome.storage.local`; przechowywane jest ok. osiem dni danych.

Pomiar jest przybliżony, zależny od alarmów przeglądarki i czasu aktywności okna. Nie jest narzędziem do rozliczeń ani gwarantowanym licznikiem czasu. Wyjątek czasowy ma wyższy priorytet niż reguła limitu, zgodnie z funkcją wyjątków. Limity są ustawiane osobno w każdym profilu przeglądarkowym/instalacji rozszerzenia.


## Nowości w 0.22.0 — ścieżki nauki

- Dodano niezależny moduł **Ścieżki nauki** do rozbijania większych celów na uporządkowane etapy.
- Plany można przypisywać do przedmiotów, ustawiać termin docelowy i opisywać notatkami.
- Etapy mają osobne terminy, można je oznaczać jako ukończone i usuwać; postęp procentowy wylicza się na podstawie ukończonych etapów.
- Nowe tabele `study_plans` i `study_stages` tworzą się automatycznie; usunięcie planu kaskadowo usuwa jego etapy.
- Kopia JSON obejmuje plany i etapy. Przywracanie waliduje referencje i wykonuje operację w transakcji; stare kopie bez tych sekcji nadal są akceptowane.
- Dodano API `GET/POST /api/study-plans`, `POST /api/study-plans/:id/stages`, `PUT /api/study-stages/:id/complete` i endpointy DELETE.

Weryfikacja w tym środowisku: sprawdzono składnię TSX przez TypeScript transpile i integralność archiwum ZIP. Pełny build C++/Vite i test API w uruchomionej aplikacji należy wykonać lokalnie.


## Nowości w 0.22.0 — ścieżki nauki i analityka

- Dodano osobny moduł **Ścieżki nauki**, który pozwala tworzyć cele przypisane do przedmiotów i rozbijać je na uporządkowane etapy z terminami, notatkami oraz postępem procentowym.
- Etapy można oznaczać jako ukończone i usuwać; usunięcie planu kaskadowo usuwa etapy.
- Dodano statystyki dla przedmiotów: czas zapisanych sesji skupienia, liczba fiszek, liczba fiszek oczekujących na powtórkę oraz liczba pozycji tygodniowego planu.
- Kopie JSON obejmują plany i etapy nauki, a walidacja sprawdza referencje do istniejących przedmiotów i planów. Starsze kopie bez nowych sekcji pozostają akceptowane.
- Dodano endpointy `/api/study-plans`, `/api/study-plans/:id/stages`, `/api/study-stages/:id/complete` i endpointy usuwania.

Weryfikacja: transpile TSX przez TypeScript nie zgłosił diagnostyki składni; manifest rozszerzenia jest poprawnym JSON, a ZIP przechodzi kontrolę integralności. Pełny build C++/Vite oraz integracja z uruchomioną przeglądarką wymagają lokalnego testu.


## Nowości w 0.24.0 — silnik białej listy i narzędzia rozszerzenia

- Zmieniono egzekwowanie domen na tryb zgodny ze specyfikacją: podczas aktywnego profilu nawigacje HTTP(S) są domyślnie blokowane, a domeny wpisane do profilu działają jako wyjątki.
- Reguły wyjątków mają wyższy priorytet niż blokada globalna; osiągnięty limit dzienny ma najwyższy priorytet i ponownie blokuje domenę.
- Panel rozszerzenia pokazuje podsumowanie liczby wyjątków, limitów i wyjątków czasowych.
- Historia blokad ma filtr domen i eksport CSV.
- Dodano osobny eksport/import ustawień rozszerzenia (profil, harmonogram, limity, wykorzystanie limitów, wyjątki czasowe i historia) z walidacją pliku i zgodności profilu.
- Uściślono opisy w interfejsie głównym: domeny profilu są listą dozwolonych wyjątków, a nie listą stron do zablokowania.

**Ważne:** historia oparta o `onRuleMatchedDebug` jest dostępna w rozszerzeniu załadowanym w trybie deweloperskim. Rozszerzenie egzekwuje politykę w Chrome/Edge; nie jest to blokada systemowa aplikacji Windows ani mechanizm odporny na odinstalowanie rozszerzenia. Pełny build C++/frontend i testy integracyjne w Windows wymagają środowiska docelowego.


## Nowości w 0.24.0 — blokowanie konfiguracji profili

- Profile można zablokować bez usuwania ich reguł. Zablokowany profil pozostaje dostępny do odczytu, ale edycja i usuwanie domen są odrzucane również przez backend.
- Odblokowanie wymaga wpisania japońskiej frazy `私は自分の目標を守ります` ("Będę chronić swoje cele"). Fraza jest sprawdzana po stronie lokalnego backendu, a nie tylko w interfejsie.
- Nie można usunąć zablokowanego profilu.
- Blokada jest celowym utrudnieniem przypadkowych zmian, nie zabezpieczeniem przed osobą mającą dostęp do lokalnego backendu lub bazy danych.
- Kontrole: do wykonania pozostają pełny build C++/frontendu oraz testy integracyjne w Chrome/Edge i Windows.


## Nowości w 0.25.0 — filtrowanie YouTube

- Każdy profil ma osobną listę dozwolonych kanałów YouTube opartą o dokładne uchwyty `@handle`; sama nazwa wyświetlana kanału nie daje dostępu.
- Dodano trwałą tabelę SQLite `profile_youtube_channels`, API CRUD profilu oraz uwzględnienie kanałów w snapshotcie rozszerzenia i kopii zapasowej/odtwarzaniu. Starsze kopie bez `youtubeChannels` nadal są obsługiwane.
- Rozszerzenie MV3 wstrzykuje `youtube-filter.js` i `youtube-filter.css` na YouTube. Niedozwolone kafelki są zaciemniane z zachowaniem miejsca w siatce, Shorts są ukrywane, a strona odtwarzania otrzymuje kurtynę i zatrzymuje wideo, jeśli nie znaleziono dokładnego dozwolonego uchwytu.
- Aby wejść na YouTube, profil musi również zawierać domenę `youtube.com`. Rozszerzenie odświeża snapshot backendu co minutę; po zmianie kanałów można wymusić odświeżenie w popupie rozszerzenia.
- Filtr działa w warstwie DOM strony YouTube i zależy od zmieniającego się markupu YouTube. Nie jest ochroną nie do obejścia; kanały, których uchwyt nie jest dostępny w DOM, są blokowane zgodnie z zasadą fail-closed.
- Weryfikacja źródła obejmuje składnię JavaScript, JSON manifestu, kontrolę zmian API/UI oraz integralność ZIP. Nie wykonano testów na żywo z zalogowanym YouTube ani pełnego builda C++/Vite w tej sesji, więc nie deklarujemy jeszcze potwierdzonej zgodności 100% na wszystkich wariantach layoutu YouTube.


## Nowości w 0.26.0 — Google Calendar

- Added OAuth connect flow using `GET /api/google/connect` and the local callback `http://127.0.0.1:43127`. Add a Google OAuth client JSON file at `Library/credentials.json` (accepted formats: `installed` or `web`) and register the callback URL as an authorized redirect URI where the selected OAuth client type permits it.
- Refresh tokens are stored in the local Amogus data directory (`google_token.json` beside `data.db`), not in frontend storage. Treat this file as a secret and do not commit it.
- Added manual two-way sync from **Integracje**. Local events are uploaded, Google events are imported, updates are reconciled using the last synchronized event signature, and local deletions are queued as tombstones for remote deletion.
- Synchronization targets the primary Google calendar. It is manual, not a background push channel. Google OAuth consent/testing status, API enablement, redirect URI, network access, and a working `curl` executable are prerequisites.
- Existing events created before 0.26.0 may need a first-run review: matching is strongest for events carrying Amogus private metadata; unrelated events with identical titles/times are not guessed to be the same event.
- OAuth and Google API calls could not be live-tested without a user's Google OAuth credentials. Build and test locally before relying on calendar data.


## Nowości w 0.27.0 — integracja z Windowsem

- Dodano lokalne reguły AppBlocker: nazwa procesu `.exe`, fragment tytułu okna oraz ścieżka pliku. Reguły są przechowywane w SQLite i sprawdzane przez backend Windows co ok. 900 ms, gdy blokada jest włączona.
- Dopasowanie tytułu okna lub procesu kończy cały pasujący proces. Chronione są podstawowe procesy systemowe, Eksplorator i proces Amogus. Wzorce mogą zawierać `*` jako wildcard. Nie dodawaj szerokich wzorców do aplikacji systemowych.
- Dodano opcjonalny autostart bieżącego pliku wykonywalnego backendu przez klucz `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; nie wymaga uprawnień administratora.
- Stan AppBlockera jest przechowywany w SQLite i przywracany przy uruchomieniu backendu. Domyślnie jest wyłączony.
- Panel **Integracje → Integracja z Windowsem** pozwala dodawać/usuwać reguły, włączać blokowanie i autostart oraz sprawdzać liczbę akcji.
- To nie jest jeszcze wzajemny watchdog, usługa Windows ani mechanizm odporny na zakończenie procesu; reguły działają wyłącznie, gdy backend działa. Operacje kończą pasujący proces, dlatego należy je testować ostrożnie.
- Weryfikacja w tym środowisku: kontrola składni JS, JSON i ZIP. Pełna kompilacja Windows oraz testy rzeczywistego procesu wymagają komputera Windows i nie zostały wykonane tutaj.


## 0.28.0 — profile renaming
- Added profile renaming through the UI and API.
- Renaming is rejected for locked profiles; the backend enforces this rule.
- Website blocking continues to use the selected profile allow-list: unlisted HTTP(S) main-frame navigation is redirected while the extension is active.

## 0.30.0 — widok blokad bliższy starej aplikacji
- Zmieniono stronę Blokady na kompaktowy, ciemny układ wzorowany na poprzednim widoku Qt.
- Edycja profilu otwiera osobne okno z kartami Websites, Website Exceptions, Apps, Breaks i Schedule.
- Działająca lista domen i filtr kanałów YouTube są dostępne w oknie profilu; karty dla nieprzeniesionych jeszcze funkcji informują wprost o brakach zamiast udawać, że stare reguły są obsługiwane.
- Ujednolicono styl wierszy profili, statusu blokady, kontrolek i stopki okna edycji.


## 0.30.0 — prostszy układ i czytelniejsze terminy

- Usunięto Pulpit, Powtórki i Ścieżki nauki z głównej nawigacji. Aplikacja otwiera się bezpośrednio w kalendarzu.
- Usunięto wybór koloru przedmiotu i kolorowe oznaczenia przedmiotów w listach.
- Poprawiono czytelność formularza zadań, statystyk, listy zadań i terminów: większa typografia, kontrast i przestrzeń.
- Dodano typy wydarzeń z ustalonymi kolorami: laboratoria (zielony), wykłady (niebieski), ćwiczenia (żółtopomarańczowy), projekt (żółty), kolokwium (czerwony), siłownia (szary), projekt własny (fioletowy), event własny (jasnoniebieski).
- Dodano powtarzanie wydarzenia co tydzień do wskazanej daty, bez potrzeby przygotowywania JSON-a. Tworzone wystąpienia są zapisane jako osobne wydarzenia.
- Dodano kolumnę `event_type` do lokalnej bazy z automatyczną migracją istniejących baz.
- Ustalono, że kolokwium może być osobnym typem zarówno dla wykładu, jak i ćwiczeń; oba warianty mają czerwony kolor.

Uwaga: pełna kompilacja React/C++ i testy przeglądarkowe wymagają zależności projektu i nie zostały wykonane w tym środowisku.
"# Amogus" 
