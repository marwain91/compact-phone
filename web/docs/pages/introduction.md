# Compact Phone

Compact Phone is a free, open-source SIP softphone for macOS, Windows, and Linux. It pairs a modern, clean UI with the industry-standard PJSIP / PJSUA2 audio stack — the same engine that powers many commercial softphones.

This documentation is a work in progress and will be filled in as features stabilize. For now, the source of truth is the [README on GitHub](https://github.com/marwain91/compact-phone).

## What's here

- **Installation** — how to get Compact Phone onto your machine
- **First call** — five-minute walkthrough from install to a working call
- **Configuration** — SIP account fields, transport, codecs, ringtones
- **Provisioning** — CLI flags and the JSON file format for mass deployment
- **Troubleshooting** — what to check when something isn't working
- **FAQ** — short answers to recurring questions

## Keyboard shortcuts

| Action | macOS | Windows / Linux |
| --- | --- | --- |
| Toggle sidebar | ⌘ B | Ctrl + B |
| Accept incoming call | ⌘ ⇧ A | Ctrl + Shift + A |
| Hang up / decline | ⌘ ⇧ H | Ctrl + Shift + H |
| Toggle Do-not-disturb | ⌘ ⇧ D | Ctrl + Shift + D |
| Redial last number | ⌘ ⇧ R | Ctrl + Shift + R |

On macOS these are also exposed under the **View** and **Phone** menus in the menu bar.

## Project status

Compact Phone is in early alpha. Expect rough edges, especially around packaging and platform integration. File issues — preferably with logs (Settings → Advanced → Diagnostics) — at the [GitHub issue tracker](https://github.com/marwain91/compact-phone/issues).
