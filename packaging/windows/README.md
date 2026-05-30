# Windows packaging & signing

This directory holds the WiX 4 installer source and the build/sign pipeline
for the Compact Phone MSI.

## Signing — Azure Artifact Signing

The MSI is signed with **Azure Artifact Signing** (formerly "Trusted
Signing"), a managed signing service — there is **no EV certificate,
hardware token, or thumbprint** to procure or guard. Microsoft's CA chains
the signature to a root Windows already trusts, so SmartScreen treats it
like any other commercially-signed installer (reputation still accrues over
the first installs — see below).

The Azure resources are already provisioned (signing account `CompactPhone`,
certificate profile `CompactPhone`, westeurope). CI authenticates via an
OIDC federated credential — no secret is stored. See CLAUDE.md
("Windows MSI is signed with Azure Artifact Signing") for the account/role/
secret details and how to re-provision if the identity ever changes.

## Prerequisites

1. **WiX Toolset v4** — `dotnet tool install --global wix --version 4.0.6` on
   the Windows build runner.
2. **Azure CLI** (only for *local* signing, not for CI) —
   `az login` against the subscription that owns the signing account.

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

# 4. Sign with Azure Artifact Signing.
#    In CI this is done by azure/trusted-signing-action; locally you sign
#    via the Trusted Signing dlib + signtool after `az login`. Install the
#    dlib once: `dotnet tool install --global Azure.CodeSigning.Dlib` (or
#    download Microsoft.Trusted.Signing.Client), then:
& signtool sign /v /fd SHA256 `
    /tr http://timestamp.acs.microsoft.com /td SHA256 `
    /dlib "<path>\Azure.CodeSigning.Dlib.dll" `
    /dmdf metadata.json `
    dist\compactphone-0.3.0.msi
# metadata.json: { "Endpoint": "https://weu.codesigning.azure.net/",
#   "CodeSigningAccountName": "CompactPhone",
#   "CertificateProfileName": "CompactPhone" }
```

Most of the time you do **not** sign locally — push a tag and let CI sign
(a `-test` tag signs too, when the profile is configured, so you can prove
the pipeline without a public release).

## CI

`.github/workflows/release-windows.yml` fires on tags matching `v*`, builds
the MSI, signs it via Azure Artifact Signing (OIDC login in the `release`
environment), and uploads it to the matching GitHub release. The
`signing-config` step decides two things: whether to `publish` at all and
whether to `should_sign`. A production tag without `TRUSTED_SIGNING_PROFILE`
configured is **skipped, not failed** (a job summary explains why);
`-test` tags always publish and sign when the profile is configured.

## SmartScreen reputation

Artifact Signing chains to a trusted Microsoft CA, but a *new* signing
identity still earns SmartScreen reputation over the first installs, so
early downloads may show a warning that fades — this is expected, not a
misconfiguration. To accelerate:
- Submit the signed binary to Microsoft Defender for analysis:
  https://www.microsoft.com/en-us/wdsi/filesubmission
- Keep using the same Artifact Signing certificate profile across releases
  so reputation accrues to one identity.

## What's still missing

- Auto-update hookup (WinSparkle) — see task #8.
