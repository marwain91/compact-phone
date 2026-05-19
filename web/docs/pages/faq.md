# FAQ

## Is Compact Phone free?

Yes. It's licensed under GPL-3.0-or-later. You can download, use,
modify, and redistribute it freely; modifications must be published
under the same license. Commercial users — same rules.

## Why GPL and not MIT / Apache?

Compact Phone embeds [PJSIP](https://www.pjsip.org), the SIP/RTP
audio stack. PJSIP is dual-licensed: GPL or a commercial license from
Teluu. Open-source projects that link PJSIP must inherit GPL. We could
swap GPL for a paid commercial PJSIP license later, but for now we're
taking the open path.

## Which platforms are supported?

- **macOS** — Apple Silicon and Intel, macOS 12 (Monterey) and up.
  Signed `.app` bundle in Releases.
- **Windows** — x64, Windows 10 (1903) and up. MSI installer in
  Releases.
- **Linux** — builds from source today; binary packaging is on the
  roadmap.

## Will you support video calls?

Not soon. Video adds significant complexity (codec licensing, GPU
acceleration, screen-share UX) and the project's audience today is
voice-first. If you need video, look at
[Linphone](https://www.linphone.org) or [Jami](https://jami.net).

## Will you support push notifications on macOS / Windows / mobile?

There's no mobile build today, so push there is moot. On desktop, the
app keeps a SIP registration alive while it's running (including
backgrounded / hidden in the system tray) — incoming calls ring
normally. If the app is fully quit, you can't be called. That's an
intentional trade-off: a always-on background daemon would have
significant security and privacy implications we're not ready to ship.

## Does it work behind corporate NAT / VPN?

Usually yes, with STUN configured. Some corporate networks require
TURN relay; we don't ship a TURN configuration UI yet (use the
provisioning file's `transport` and STUN settings as a workaround).

## Where are my passwords stored?

In the platform secure-credential store:

- macOS: **Keychain Access**
- Windows: **Credential Manager**
- Linux: **libsecret** (kwallet / gnome-keyring / etc.)

Never in plain text on disk. Removing the app removes the keychain
entries; reinstalling does not restore them (you'll need to re-enter
account passwords).

## Can I use Compact Phone with my Asterisk / FreePBX / 3CX / etc.?

Yes. Compact Phone is a standards-compliant SIP UA. Any PBX that
speaks SIP over UDP/TCP/TLS will work. We test regularly against
Asterisk 20; the dev container even ships an Asterisk fixture you can
register against.

## Is there a CLI version?

Not today. The desktop UI is the only entry point. There's an
internal CLI for diagnostics (`--diagnostics <path>`, `--version`,
`--provisioning <file>`) but no headless mode for unattended calling.

## How do I report bugs or contribute?

- **Bugs / feature requests:** <https://github.com/marwain91/compact-phone/issues>
- **PRs:** welcome — read `CONTRIBUTING.md` in the repo root for the
  branch and review workflow.
- **Security issues:** please don't open public issues for security
  reports. See `SECURITY.md` for the private disclosure path.

## Is there a roadmap?

Loose, public:

1. **Stabilize** the v0.x line — packaging, signing, auto-update on
   all three desktop platforms.
2. **Provisioning** improvements — real OAuth for Daktela (currently
   token-paste), more provider backends.
3. **Mobile** — eventually. Not on the near horizon.
4. **Video** — see above. Not planned.

The truthful version of the roadmap lives in the GitHub project
boards.
