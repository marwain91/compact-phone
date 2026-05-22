# Provisioning

Compact Phone supports zero-touch boot-time configuration for mass deployment
via MDM (macOS), GPO (Windows), or Ansible/Puppet (Linux). At launch the app
reads a JSON provisioning file plus an optional set of command-line flags
and applies them to the live profile.

## Discovery order

Each layer below is read at startup. When multiple layers set the same scalar
(e.g. `theme`), **the layer further down wins** — so per-user provisioning
overrides system-wide, and CLI flags override everything. Accounts are
appended, not replaced.

1. `/etc/compactphone/provisioning.json` (Linux/macOS system-wide)
2. The user's `AppDataLocation` provisioning file:
   - macOS: `~/Library/Application Support/Compact Phone/provisioning.json`
   - Linux: `~/.local/share/Compact Phone/provisioning.json`
   - Windows: `%APPDATA%\Havliczech\Compact Phone\provisioning.json`
3. `$COMPACTPHONE_CONFIG` environment variable (path)
4. `--config <path>` command-line flag
5. Other `--sip-*` and global CLI flags

A path can appear at multiple layers; missing files are silently skipped.
Parse errors emit a single warning to stderr and the layer is treated as
empty.

## File format

JSON, `schema_version: 1`. Comments are not supported (standard JSON).

```json
{
    "schema_version": 1,
    "replace_accounts": true,
    "accounts": [
        {
            "label": "ACME PBX",
            "server": "pbx.acme.com",
            "user": "1001",
            "auth_user": "1001",
            "realm": "acme",
            "password": "@env:ACME_PWD",
            "display_name": "Alice Example",
            "transport": "tls",
            "proxy": "sbc.acme.com:5061",
            "srtp": "mandatory",
            "stun": "stun.acme.com:3478",
            "public_address": "203.0.113.10",
            "codecs": ["opus", "PCMA", "PCMU"],
            "voicemail": "*97",
            "register_on_start": true,
            "register_interval_sec": 300,
            "keepalive_interval_sec": 30,
            "session_timers_enabled": true,
            "publish_presence_enabled": false,
            "ice_enabled": true,
            "hide_caller_id": false,
            "zrtp_enabled": false,
            "allow_untrusted_cert": false,
            "dtmf_method": "rfc2833",
            "enabled": true,
            "default": true,
            "sort_order": 0
        }
    ],
    "defaults": {
        "theme": "light",
        "minimize_to_tray": true,
        "autoanswer": false,
        "dnd": false,
        "log_level": "info",
        "log_file": "/var/log/compactphone.log"
    },
    "headless": {
        "call": "sip:600@pbx.acme.com",
        "auto_answer": false,
        "play_file": "/opt/compactphone/prompt.wav",
        "loop_play_file": true,
        "duration_sec": 300,
        "exit_after_call": true
    }
}
```

### Field reference

| Field | Type | Notes |
|---|---|---|
| `schema_version` | int | Must be `1` for this build. |
| `replace_accounts` | bool | When true, an account matching `(user, server)` is updated rather than duplicated on repeat runs. |
| `accounts[].label` | string | UI label; defaults to `<user>@<server>`. |
| `accounts[].server` | string | **Required.** SIP server, optionally `host:port`. |
| `accounts[].user` | string | **Required.** SIP username. |
| `accounts[].auth_user` | string | Defaults to `user`. |
| `accounts[].realm` | string | Digest auth realm. Defaults to `*`. |
| `accounts[].password` | string | See **Password sources** below. |
| `accounts[].display_name` | string | From-header display name. |
| `accounts[].transport` | string | `udp` \| `tcp` \| `tls`. |
| `accounts[].proxy` | string | Outbound proxy, optionally `host:port`. |
| `accounts[].srtp` | string | `disabled` \| `optional` \| `mandatory`. |
| `accounts[].stun` | string | STUN server (host or host:port). |
| `accounts[].public_address` | string | Forced Contact address. |
| `accounts[].codecs` | array or string | Priority list, top first. Comma-separated string also accepted. |
| `accounts[].voicemail` | string | Mailbox extension dialed by the voicemail button. |
| `accounts[].register_on_start` | bool | Default `true`. |
| `accounts[].register_interval_sec` | int | REGISTER refresh interval; `0` uses PJSIP default. |
| `accounts[].keepalive_interval_sec` | int | UDP keepalive interval; `0` uses PJSIP default. |
| `accounts[].session_timers_enabled` | bool | Default `true`. |
| `accounts[].publish_presence_enabled` | bool | Default `false`. |
| `accounts[].ice_enabled` | bool | Default `false`. |
| `accounts[].hide_caller_id` | bool | Default `false`. |
| `accounts[].zrtp_enabled` | bool | Stored in the account profile for ZRTP-capable builds. |
| `accounts[].allow_untrusted_cert` | bool | Allows self-signed or otherwise untrusted TLS server certs. |
| `accounts[].dtmf_method` | string | `inband` \| `rfc2833` \| `info`. |
| `accounts[].enabled` | bool | Default `true`. |
| `accounts[].default` | bool | Marks this as the default account. |
| `accounts[].sort_order` | int | UI sort order. |
| `defaults.theme` | string | `light` \| `dark` \| `midnight` \| `ivory` \| `velvet`. |
| `defaults.minimize_to_tray` | bool | |
| `defaults.autoanswer` | bool | |
| `defaults.dnd` | bool | |
| `defaults.log_level` | string | `trace` \| `debug` \| `info` \| `warn` \| `error`. |
| `defaults.log_file` | string | Append-target log path. |
| `headless.call` | string | SIP URI to call from `compactphone-headless`. |
| `headless.auto_answer` | bool | Auto-answer inbound calls in `compactphone-headless`. |
| `headless.play_file` | string | WAV file to transmit after media is confirmed. |
| `headless.loop_play_file` | bool | Loop `play_file` until the call ends. |
| `headless.duration_sec` | int | Exit after this many seconds. |
| `headless.exit_after_call` | bool | Exit when the active call disconnects. |

## Password sources

Storing a literal password in a config file or on the command line is
discouraged. Compact Phone resolves any of the following spec forms:

| Spec | Resolution |
|---|---|
| `"@env:VAR_NAME"` | Reads from the environment variable. |
| `"@file:/path/to/secret"` | Reads the first line of the file, trimmed. |
| `"@stdin"` (CLI only) | Reads one line from stdin. |
| Anything else | Treated as a literal password. |

A literal `--sip-password` on the command line emits a warning to stderr
because it leaks via `ps`. Prefer `--sip-password-file` or `--sip-password-stdin`.

## CLI surface

The flags below override the corresponding file values. They're useful for
one-shot installs that don't ship a config file.

```
--sip-server, --sip-port, --sip-user, --sip-auth-user
--sip-password, --sip-password-file, --sip-password-stdin
--sip-realm, --sip-proxy, --sip-transport, --sip-srtp
--sip-display-name, --sip-stun, --sip-public-address
--sip-codecs, --sip-voicemail, --sip-register-interval
--sip-keepalive-interval, --sip-session-timers yes|no
--sip-publish-presence, --no-sip-publish-presence
--sip-ice, --no-sip-ice
--sip-hide-caller-id, --no-sip-hide-caller-id
--sip-zrtp, --no-sip-zrtp
--sip-allow-untrusted-cert, --no-sip-allow-untrusted-cert
--sip-dtmf-method, --sip-enabled yes|no
--sip-default, --sip-sort-order, --sip-label
--register-on-start yes|no
--replace-account
--autoanswer, --auto-answer, --dnd, --minimize-to-tray
--theme <id>, --log-level <level>, --log-file <path>
--config <path>
--call <sip-uri>, --play-file <path>, --loop-play-file
--duration-sec <seconds>, --exit-after-call
```

Run `compactphone --help` for the live, version-pinned list.

## Headless SIP test runner

`compactphone-headless` uses the same provisioning parser and SIP core as the
GUI executable, but it runs with `QCoreApplication`, an in-memory database,
and an in-memory keychain. This is intended for SIP client testing in the
same style as `pjsua`.

Example: register one account, place a call, transmit a WAV file in a loop,
and exit when the call ends:

```sh
compactphone-headless \
    --sip-server pbx.acme.com \
    --sip-port 5060 \
    --sip-user 1001 \
    --sip-password-file /run/secrets/1001.pass \
    --sip-transport udp \
    --call sip:600@pbx.acme.com \
    --play-file /opt/compactphone/prompt.wav \
    --loop-play-file \
    --exit-after-call
```

For inbound tests, omit `--call` and add `--auto-answer`. `--duration-sec`
is useful in CI so a test instance cannot run forever.

## Deployment recipes

### macOS via MDM

Drop a system-wide config and pre-create a keychain entry for the password:

```sh
sudo mkdir -p /etc/compactphone
sudo cp ./provisioning.json /etc/compactphone/provisioning.json
```

The MDM payload should also install the signed `Compact Phone.app` and the
launcher LaunchAgent. The provisioning file is read on every launch, so
admins can rotate accounts without re-installing the app.

### Windows via GPO

Push `provisioning.json` to `%PROGRAMDATA%\CompactPhone\provisioning.json`
via a Group Policy file preference, and set the `COMPACTPHONE_CONFIG`
environment variable to that path in the same GPO. (Per-machine AppData
discovery for Windows lands in a future release.)

### Linux via Ansible / Debian package

```yaml
- name: install compact phone config
  copy:
    src: provisioning.json
    dest: /etc/compactphone/provisioning.json
    owner: root
    group: root
    mode: "0644"
```

## Locking fields (future)

Locking specific fields so users can't change them in the UI is planned but
not yet implemented. Today every field provisioned at boot can still be
edited by the user; on next launch the file values are re-applied.
