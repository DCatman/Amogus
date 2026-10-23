# Amogus Blocker (Chrome / Edge)

This Manifest V3 extension applies domain rules from the local Amogus Core API using `declarativeNetRequest`.

## Install

1. Build and start Amogus Core. It must listen on `127.0.0.1:43127`.
2. Open `chrome://extensions` in Chrome or `edge://extensions` in Edge.
3. Enable Developer mode.
4. Choose **Load unpacked** and select this `extension/blocker` folder.
5. Open the extension popup, choose a profile and enable blocking.
6. Add domains to that profile in the Amogus UI. Extension rules refresh on enable/profile change and periodically (about once a minute).

## Behaviour and limitations

- Uses browser-level `declarativeNetRequest` block rules for main-frame navigation. Matching domains include subdomains.
- The extension is off by default; the user explicitly enables it in the popup.
- If the backend cannot be reached while refresh runs, Amogus-owned dynamic rules are cleared instead of leaving a stale rule set silently active.
- Up to 500 domain rules are applied. Current version uses IDs 1000–1499 for its own rules.
- This is not tamper-proof and can be disabled by the browser user or bypassed in another browser/profile. It is not a system-wide firewall.
- The backend's read-only `/api/blocking/config` endpoint permits GET requests from Chrome extension origins only; other extension-origin API access remains rejected. The API is still a local prototype, not a security boundary for hostile local software.

## Test checklist

1. Start Amogus Core and the UI.
2. Add `example.com` to a profile.
3. Load the extension, select that profile and enable blocking.
4. Visit `https://example.com` and verify navigation is blocked.
5. Disable the extension toggle and verify the page loads again.
6. Remove the domain in Amogus and wait for the next refresh; verify the stale rule is removed.


## Local blocked-navigation history (0.19.0)

The popup shows recent matching navigation events, stored only in `chrome.storage.local` (maximum 100). The event source is `declarativeNetRequest.onRuleMatchedDebug`, which is intended for unpacked/developer-installed extensions and requires the `declarativeNetRequestFeedback` permission. Do not treat this as a complete production audit log: browser policies/builds may limit debug events, and it does not record all subresources or attempts when the extension is not running. Clear the list from the popup at any time.


### Dzienne limity domen (0.21.0)

W popupie można ustawić dzienny limit minut dla domeny, która już występuje w wybranym profilu. Rozszerzenie zlicza przybliżony czas aktywnej karty, gdy blokowanie i harmonogram są aktywne. Po przekroczeniu limitu nawigacja do domeny jest przekierowywana na `blocked.html`. Zużycie i limity są przechowywane lokalnie w `chrome.storage.local`. Wymagana jest zgoda `tabs` do odczytu URL aktywnej karty.


## Filtr YouTube (0.25.0)

1. Dodaj `youtube.com` do listy domen wybranego profilu.
2. W aplikacji Amogus otwórz **Blokady → Profil → Filtr YouTube** i dodaj uchwyty kanałów, np. `@konopskyy`. Możesz wpisać uchwyt z `@` lub bez niego.
3. Włącz blokowanie w popupie rozszerzenia i wybierz ten sam profil.
4. Po zmianie listy otwórz popup i wybierz „Odśwież reguły”, a następnie odśwież kartę YouTube. Bez wymuszenia zmiany propagują się przez cykliczny odczyt konfiguracji (zwykle do minuty).

Zezwolenie opiera się na dokładnym uchwycie z linku `/@handle`, nie na nazwie kanału. Kafelki bez możliwego do odczytania uchwytu są blokowane (fail-closed). Wideo na stronie `/watch` zostaje zatrzymane i przykryte kurtyną, jeśli kanał nie znajduje się na liście. Shorts/reels są ukrywane podczas aktywnego filtra. YouTube może zmieniać strukturę DOM; przed codziennym użyciem sprawdź działanie na aktualnej wersji Chrome/Edge.
