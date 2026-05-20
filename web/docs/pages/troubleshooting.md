# Troubleshooting

Common problems and where to look. If your issue isn't here, grab a
diagnostics bundle (**Settings → Advanced → Export diagnostics**) and
[file an issue](https://github.com/marwain91/compact-phone/issues).

## Status dot stays red (account not registered)

The dot in the bottom-left of the sidebar is your registration health
indicator. Red means the SIP REGISTER request is reaching the server
but failing, or the server isn't reachable.

**Check:**
- **Server hostname + port** are correct. Typos here are the #1 cause.
- **Transport** matches what the server supports. TLS to a non-TLS
  port silently fails the handshake.
- **Credentials** — try logging into your provider's web UI with the
  same username/password.
- **Network** — can you reach the server at all?
  `nc -vz sip.example.com 5060` from a terminal will tell you fast.
- **Logs** — open **Settings → Advanced → Diagnostics**. Filter for
  `register` to see the SIP exchange.

## No incoming calls

Outbound calls work but inbound doesn't reach you.

**Most likely:** NAT / firewall blocking inbound SIP traffic. The
SIP REGISTER tells the server where to send calls, but the server
needs a return path through your NAT.

**Check:**
- **STUN** is configured (Settings → Advanced). Most public SIP
  providers require it.
- **Do not disturb** isn't accidentally on.
- **Auto-answer** with a high delay (or a bug) — try toggling
  auto-answer off entirely.
- **Call forwarding Always** with an unreachable target — Compact
  Phone forwards before ringing your device.

## One-way audio

You can hear them but they can't hear you (or vice versa).

**Likely causes:**
- **Wrong microphone selected** — go to **Settings → Audio**, pick
  the right device, test with the ringtone preview.
- **Microphone permission denied** — macOS Settings → Privacy &
  Security → Microphone → enable Compact Phone. Windows Settings →
  Privacy → Microphone → same.
- **NAT / SRTP misconfig** — the SIP signalling reaches both sides
  but the RTP media stream is firewalled in one direction. Often
  fixes itself with STUN; sometimes needs a TURN relay.

## Crackling, dropouts, robotic audio

Network jitter or CPU starvation.

**Check:**
- **Latency** to the SIP server: `ping sip.example.com`. Anything
  over 150 ms one-way will be noticeable.
- **CPU pressure** — if another app is hammering all cores, the
  audio thread gets pre-empted. Quit the offender and try again.
- **Wi-Fi vs ethernet** — wired is dramatically more reliable for
  voice. If you're on Wi-Fi and seeing problems, try wired even
  temporarily to confirm.
- **Codec mismatch** — Compact Phone offers Opus (high quality, low
  bitrate) and the legacy G.711 codecs (high bitrate, near-zero CPU).
  If your provider only accepts G.711, Opus offers won't help.

## App won't launch

**macOS** — first launch needs right-click → **Open** to accept the
developer signature. After that it launches normally.

**Windows** — SmartScreen may quarantine the binary. Right-click →
**Properties → Unblock**, or re-download from the official Releases
page.

**Linux** — missing system libraries. Run from a terminal to see the
real error: `./compactphone`. Common culprits: `libpulse`, `libdbus`,
`libxcb-cursor` not installed.

## Bug reports

If something else is wrong, attach:

1. **Diagnostics bundle** — Settings → Advanced → Export diagnostics
2. **Steps to reproduce** — what you clicked, what you expected,
   what actually happened
3. **Versions** — your OS and CompactPhone version (visible in
   Settings → General → About)

File at: <https://github.com/marwain91/compact-phone/issues>
