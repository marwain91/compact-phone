# Stable download URLs

The CompactPhone release workflows publish artifacts to GitHub Releases
with **stable, version-less filenames** so the landing page (in MyWebs)
can hardcode the download links without needing per-release updates.

GitHub's `/releases/latest/download/<filename>` URL redirects to the
asset of that name on the most recent published release.

## Public URLs

| Platform | URL |
|---|---|
| macOS (DMG) | `https://github.com/marwain91/compact-phone/releases/latest/download/Compact-Phone-macOS.dmg` |
| Linux (AppImage) | `https://github.com/marwain91/compact-phone/releases/latest/download/Compact-Phone-Linux-x86_64.AppImage` |
| Windows (MSI) | `https://github.com/marwain91/compact-phone/releases/latest/download/Compact-Phone-Windows.msi` |
| macOS appcast (auto-update feed) | `https://github.com/marwain91/compact-phone/releases/latest/download/appcast-macos.xml` |
| Windows appcast | `https://github.com/marwain91/compact-phone/releases/latest/download/appcast-windows.xml` *(once PR #7 lands)* |

The macOS DMG + appcast and Windows MSI are produced by the tag release
workflows. Linux AppImage is produced from `release-linux.yml`.

## Landing-page integration (MyWebs side)

In `sites/compactphone.app/public/index.html` (or whatever the current
landing page template is), the "Download for macOS" / "Download for
Windows" buttons should point at the URLs above. Examples:

```html
<a href="https://github.com/marwain91/compact-phone/releases/latest/download/Compact-Phone-macOS.dmg"
   class="download-button">
   Download for macOS
</a>
<a href="https://github.com/marwain91/compact-phone/releases/latest/download/Compact-Phone-Linux-x86_64.AppImage"
   class="download-button">
   Download for Linux
</a>
<a href="https://github.com/marwain91/compact-phone/releases/latest/download/Compact-Phone-Windows.msi"
   class="download-button">
   Download for Windows
</a>
```

Optionally, the landing page can fetch the latest release metadata
from GitHub's API to display the current version next to the button:

```
GET https://api.github.com/repos/marwain91/compact-phone/releases/latest
{ "tag_name": "v0.1.1", "published_at": "..." }
```

## Version-in-filename concerns

Two trade-offs the stable-filename pattern accepts:

1. **No filename collision protection**: a user downloading multiple
   versions over time gets `Compact-Phone-macOS.dmg`,
   `Compact-Phone-macOS (1).dmg`, etc. — the browser handles dedup
   automatically. The version is visible inside the app's About card
   (Settings → General → ABOUT).
2. **Can't link to a specific historical version directly**: users who
   want, say, v0.1.0 specifically would need to go via the GitHub
   Releases page (`/releases/tag/v0.1.0`) and grab from there. Most
   users want "the latest" and that's what the buttons deliver.

The auto-update path (Sparkle appcast) does carry the version inside
its XML (`sparkle:shortVersionString`), so UpdateChecker still compares
correctly across versions. The stable URL is purely for the landing-page
download button experience.
