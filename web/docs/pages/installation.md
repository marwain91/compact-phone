# Installation

Compact Phone is distributed as a signed application bundle for macOS and
an installer for Windows. Linux builds run inside the headless dev
container today; a native package is on the roadmap.

## macOS

1. Download the latest `Compact-Phone-x.y.z.dmg` from the
   [Releases page](https://github.com/marwain91/compact-phone/releases).
2. Open the `.dmg` and drag **Compact Phone** to `/Applications`.
3. First launch: right-click → **Open** to accept the developer
   signature. macOS will remember the choice for future launches.
4. When prompted, allow microphone access. Audio calls won't work
   without it.

**System requirements:** macOS 12 (Monterey) or later. Apple Silicon
and Intel both supported.

## Windows

1. Download the latest `Compact-Phone-Setup-x.y.z.exe` from
   [Releases](https://github.com/marwain91/compact-phone/releases).
2. Run the installer. SmartScreen may warn about the publisher — click
   **More info → Run anyway**.
3. The app appears in the Start menu as **Compact Phone**.
4. Allow microphone access in Windows Settings → Privacy → Microphone
   if you're not prompted on first call.

**System requirements:** Windows 10 (1903) or later, x64.

## Linux

No prebuilt package yet. Build from source:

```bash
git clone https://github.com/marwain91/compact-phone.git
cd compact-phone
make up build
```

The result lives under `build/linux/src/compactphone`. See the project
README for the full build prerequisites.

## Verifying the install

After first launch, look for a status dot in the bottom-left of the
sidebar. Grey = no account, red = registered but failing, green =
registered. If you see green, the SIP stack is healthy and you're ready
to place a call.
