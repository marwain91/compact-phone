# Configuration

All Compact Phone settings live under the **Settings** tab in the
sidebar. This page documents what each option does and when you'd want
to change it.

## General

| Setting | What it does |
|---|---|
| **Theme** | Light, Dark, or one of the system-matched options. Changes apply instantly. |
| **Log level** | Verbosity of the in-app log (Logs viewer in **Advanced**). `info` is the sane default. Bump to `debug` or `trace` when reproducing a bug. |
| **Always on top** | When enabled, the main window stays above all other windows. Independent of this setting, an incoming-call ring always pops the window to the front so it can't be hidden behind a fullscreen app. |

## Audio

| Setting | What it does |
|---|---|
| **Microphone** | Pick which capture device to use. Auto-refreshes when you plug or unplug a headset. |
| **Speaker** | Pick which playback device to use. Same auto-refresh behaviour. |
| **Refresh devices** | Force a re-enumeration if you just plugged something in and it didn't appear. |
| **Ringtone** | Toggle ringtone on/off, choose a custom WAV file, or reset to the built-in tone. **Test** previews the selected tone. |

## Calls

| Setting | What it does |
|---|---|
| **Do not disturb** | Reject incoming calls immediately with SIP `486 Busy Here`. The caller hears busy; you see no notification. |
| **Auto-answer** | Pick up incoming calls automatically. Useful for kiosk / hot-desk setups. |
| **Auto-answer delay** | Wait this many milliseconds before picking up (0–60,000 ms). Gives the caller time to abandon a misdial. |

## Forward

Call forwarding has three independent modes. Each takes a target SIP
URI (e.g. `sip:voicemail@example.com` or just `1002` to forward to
an internal extension).

| Mode | When it fires |
|---|---|
| **Always** | All inbound calls go straight to the target. Other modes ignored. |
| **On busy** | Triggers when you're already in a call. |
| **On no-answer** | After the configured timeout (1–120 seconds, default 20). |

## Advanced

| Setting | What it does |
|---|---|
| **Auto-record** | Record every answered call to a local WAV file. Path configurable; defaults to `~/Library/Application Support/Compact Phone/recordings` on macOS, equivalent on Windows/Linux. |
| **Recordings folder** | Custom location for recordings. Pick a directory the user has write access to. |
| **Enterprise features** | Gates the **Messages** (SIP MESSAGE / instant messaging) and **Lines** (BLF / line monitoring) tabs in the sidebar. Off by default; most personal users never need them. |
| **Crash reporting** | Opt-in Sentry crash reports. Disabled by default. Available only in release builds that were built with a `SENTRY_DSN`; unavailable builds show the toggle disabled. SIP credentials, account data, logs, and diagnostics exports are not uploaded automatically. |
| **Diagnostics** | View / export the in-app log. Attach to bug reports. |

## Where settings are stored

Compact Phone keeps your settings in a SQLite database under the
platform standard data directory:

| Platform | Path |
|---|---|
| macOS   | `~/Library/Application Support/Compact Phone/compactphone.db` |
| Windows | `%APPDATA%\Compact Phone\compactphone.db` |
| Linux   | `~/.local/share/Compact Phone/compactphone.db` |

Account passwords are stored in the platform keychain (Keychain on
macOS, Credential Manager on Windows, libsecret on Linux) — not in the
SQLite file. Deleting the db only deletes preferences and history;
your stored account credentials survive until you delete the account.
