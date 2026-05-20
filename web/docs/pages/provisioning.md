# Provisioning

Provisioning is how you set up SIP accounts without typing them in
by hand. Useful for fleet deployments or for letting users sign in
to a PBX with their existing credentials.

Compact Phone supports two paths today:

1. **Static file** — drop a JSON file with account details where the
   app can find it; it gets imported on first launch.
2. **Provider sign-in** — interactive wizard that probes a PBX for
   its auth methods and provisions an account from credentials.

## Static file

On startup, Compact Phone looks for a JSON file at the platform's
standard config path:

| Platform | Path |
|---|---|
| macOS   | `~/Library/Application Support/Compact Phone/provisioning.json` |
| Windows | `%APPDATA%\Compact Phone\provisioning.json` |
| Linux   | `~/.config/Compact Phone/provisioning.json` |

Or pointed at explicitly via the `--provisioning` CLI flag.

Example file:

```json
{
  "accounts": [
    {
      "username": "1001",
      "password": "secret",
      "server": "sip.example.com:5060",
      "transport": "tls",
      "display_name": "Alice"
    }
  ]
}
```

Each entry is added on first launch (or merged if it doesn't already
exist). Passwords are migrated into the keychain immediately; the
JSON file can then be deleted if you want.

## Provider sign-in (Daktela)

For Daktela tenants, the **Accounts** tab has a *Sign in with Daktela*
tile in the footer. Clicking it opens a wizard:

1. **Instance** — enter your Daktela host (e.g. `mycompany.daktela.com`)
2. **Discover** — Compact Phone fetches the instance's
   `/internal/globalsettings.json` to learn which auth methods are
   enabled (username/password, optional SSO methods)
3. **Credentials** — for username+password, enter and click Sign in.
   The wizard authenticates against `/api/v6/login.json`, fetches the
   user's SIP device via `/api/v6/extensions/sipdevices/<ext>.json`,
   and adds the account.

If the discovery endpoint isn't reachable, the wizard falls back to a
plain username+password flow.

## Other providers

The provisioning framework is provider-agnostic — adding a new backend
means implementing the `Provider` interface in
`src/core/provisioning/`. See `DaktelaProvider.{h,cpp}` for the
reference implementation.

## CLI flags

| Flag | What it does |
|---|---|
| `--provisioning <path>` | Use a specific JSON file instead of the default location |
| `--reset-provisioning` | Re-apply the provisioning file on this launch even if it was already imported |
| `--diagnostics <path>` | Dump diagnostics bundle to the given file and exit (useful for support requests) |

## Mass deployment

For deploying to many machines (managed-device fleets):

- **macOS** — drop `provisioning.json` into
  `~/Library/Application Support/Compact Phone/` via your MDM. JAMF /
  Kandji have file-deploy primitives that work here.
- **Windows** — same, into `%APPDATA%\Compact Phone\`. Group Policy
  Preferences can place files per-user.
- **Linux** — same, into `~/.config/Compact Phone/`. Configuration
  management (Ansible, Salt, Puppet) handles this trivially.

Per-user credentials should come from your identity provider, not be
baked into the JSON. The static-file path is best for *shared* settings
like server hostname + transport; account credentials should flow
through provider sign-in or the keychain directly.
