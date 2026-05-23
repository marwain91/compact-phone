# Windows packaging & signing

This directory holds the WiX 4 installer source and the build/sign pipeline
for the Compact Phone MSI.

## Prerequisites you must procure

1. **EV code-signing certificate** — required so Windows SmartScreen trusts
   the installer immediately on first download. Standard (non-EV) certs work
   but require building reputation, which means the first thousands of users
   see a scary warning.
   - Vendors: DigiCert, Sectigo, GlobalSign (~$300–$500/year).
   - Lead time: 1–2 weeks for company verification.
   - The cert is bound to a hardware token (USB key); signing must run on
     a machine that physically holds the token, OR you proxy through a
     KMS like SignPath / SSL.com Cloud Signing.
   - Capture the SHA-1 thumbprint of the cert after install: `certutil -store My`.
2. **Timestamp authority URL** — bundled with most cert vendors. Defaults
   used by the signing step: `http://timestamp.digicert.com`.
3. **WiX Toolset v4** — `dotnet tool install --global wix --version 4.0.6` on the Windows
   build runner.

## Build steps (manual)

```powershell
# 1. Build with PJSIP from source
$env:VCPKG_ROOT = "C:\v"
$env:PJSIP_ROOT = "C:\p\pjproject-2.17"
cmake --preset windows -B C:\b -DCMAKE_TOOLCHAIN_FILE=C:/v/scripts/buildsystems/vcpkg.cmake
cmake --build C:\b

# 2. Stage the runtime
$stage = "C:\b\stage"
New-Item -ItemType Directory -Path $stage | Out-Null
$app = Get-ChildItem -Path C:\b -Recurse -Filter "compactphone.exe" -File `
    | Sort-Object { $_.FullName.Length } `
    | Select-Object -First 1
Copy-Item $app.FullName "$stage\compactphone.exe"
$deploy = Get-ChildItem -Recurse -Filter "windeployqt.exe" `
    -Path C:\b\vcpkg_installed,C:\v `
    | Select-Object -First 1
& $deploy.FullName --qmldir src\ui\qml `
    --release --no-translations --no-system-d3d-compiler `
    "$stage\compactphone.exe"

# 2a. Stage licence notices alongside the binary so the MSI ships them
#     under Program Files\Compact Phone. This is what makes the install
#     compliant with GPLv3 §6 and LGPLv3 §4 — the notices must travel with
#     the binary, not just live in the repo. Do NOT skip this step.
Copy-Item LICENSE $stage
Copy-Item THIRD_PARTY_LICENSES.md $stage

# 3. Build MSI
New-Item -ItemType Directory -Path dist -Force | Out-Null
.\packaging\windows\generate-stage-wxs.ps1 `
    -StageDir $stage `
    -OutputPath dist\stage-files.wxs
wix build packaging\windows\installer.wxs dist\stage-files.wxs -arch x64 `
    -d Version="0.3.0" `
    -out "dist\compactphone-0.3.0.msi"

# 4. Sign (with EV cert in cert store)
& signtool sign /v `
    /sha1 $env:CODE_SIGN_THUMBPRINT `
    /tr http://timestamp.digicert.com /td sha256 /fd sha256 `
    dist\compactphone-0.3.0.msi
```

## CI

`.github/workflows/platform-matrix.yml` builds an unsigned MSI on manual
validation runs. `.github/workflows/release-windows.yml` fires on tags
matching `v*`, signs when certificate secrets are present, and uploads the
MSI to the matching GitHub release.

## SmartScreen reputation

Even with EV signing the first few installs may show SmartScreen warnings
until Microsoft accumulates positive telemetry. To accelerate:
- Submit the signed binary to Microsoft Defender for analysis:
  https://www.microsoft.com/en-us/wdsi/filesubmission
- Bundle the same EV cert thumbprint across every release.

## What's still missing

- Self-hosted signing runner provisioning notes if the EV certificate cannot
  be exposed to GitHub-hosted runners.
- Auto-update hookup (WinSparkle) — see task #8.
