# Google Calendar OAuth setup

1. In Google Cloud Console, create/select a project and enable **Google Calendar API**.
2. Configure the OAuth consent screen and add your Google account as a test user if the app is in Testing mode.
3. Create an OAuth client ID. A desktop client JSON is the simplest supported input format; save/copy it as `Library/credentials.json` (this repository intentionally does not include real credentials).
4. Start `amogus_core`, open Integracje in the UI and click **Połącz Google Calendar**.
5. If the OAuth client requires a redirect URI, configure `http://127.0.0.1:43127` exactly. Some Google OAuth client types only allow their prescribed redirect modes; choose a client type that supports the local loopback flow.
6. Click **Synchronizuj teraz** to perform a manual two-way sync with the primary calendar.

`google_token.json` is written beside `data.db` in the Amogus local data directory. It contains refresh credentials. Do not commit or share it. Delete this file to disconnect the account; also revoke access from your Google Account if needed.
