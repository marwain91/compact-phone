# First call

A five-minute walkthrough from a fresh install to a working call.

## 1. Add your SIP account

Compact Phone needs at least one SIP account to register with. You'll
get the details from your VoIP provider or PBX admin.

1. Open **Settings → Accounts** (server icon in the sidebar).
2. Click **Add**.
3. Fill in:
   - **Username** — your SIP extension (e.g. `1001`)
   - **Password** — the account password
   - **Server** — host:port of the SIP registrar (e.g.
     `sip.example.com:5060`, or `sip.example.com` if it's the
     default 5060)
   - **Transport** — UDP, TCP, or TLS. Match what your provider
     supports. TLS is recommended if available.
4. Click **Save**.

Within a few seconds the status dot in the bottom-left of the sidebar
should turn green — that means the account is registered and ready.

## 2. Place a test call

1. Open the **Dialer** tab (dialpad icon).
2. Type the number or extension to call.
3. Click the green call button.

Audio routing uses your system's default microphone and speaker. To
change devices, go to **Settings → Audio**.

## 3. Receive a call

When someone calls you, a ring dialog appears with **Accept** and
**Decline** buttons. The window pops to the foreground automatically
even if you're using another full-screen app (this happens regardless
of the Always-on-top setting).

## What's next

- **Configuration** — codecs, ringtones, do-not-disturb, call
  forwarding, auto-answer
- **Provisioning** — set up many machines at once from a single config
  file or a Daktela tenant
- **Troubleshooting** — what to check when registration fails or
  audio is one-way
