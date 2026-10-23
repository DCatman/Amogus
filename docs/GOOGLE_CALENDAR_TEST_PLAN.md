# Google Calendar sync — verification checklist

Use a disposable Google calendar/account first. The current connector targets the account's **primary** calendar.

1. Configure Google Calendar API and OAuth consent. Put a Desktop OAuth client JSON at `Library/credentials.json`; do not commit it.
2. Start `amogus_core`, open **Integracje**, click **Połącz Google Calendar**, and confirm that OAuth returns to `http://127.0.0.1:43127` and the status changes to connected.
3. Create a local Amogus event, click **Synchronizuj teraz**, and verify one matching event appears in Google Calendar.
4. Create a Google Calendar event and sync; verify it appears in Amogus with the same title and time.
5. Edit the local title/time and sync; verify Google is updated. Then edit the Google event and sync; verify Amogus receives the remote edit when the local event was not changed since the last sync.
6. Delete a local event and sync; verify it is deleted remotely. Delete a remote event and sync; verify it is removed locally.
7. Repeat sync without making changes; event counts should not increase.
8. Verify all-day events, daylight-saving transitions, recurring events, cancelled instances, large calendars, network loss, revoked OAuth access, and token refresh. These require live regression testing and are not certified by static checks alone.

Conflict policy: if local event content differs from its last synchronized signature, the local version wins that sync round. Remote changes are pulled when the local content has not changed since the previous sync. Simultaneous edits are therefore local-wins, not a three-way merge.
