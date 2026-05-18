# CompactPhone — Brand assets

Two canonical variants of the application mark.

| File | Use on… | Tile colour | Phone glyph | Signal dot |
|---|---|---|---|---|
| `compactphone-mark-dark.svg`  | **light** backgrounds | `#003366` (CompactPhone Midnight Blue) | `#FFFFFF` | `#FF5349` ring on white |
| `compactphone-mark-light.svg` | **dark** backgrounds  | `#FFFFFF` | `#003366` | `#FF5349` ring on `#0A0F1A` |

The mark is a rounded square ("tile") containing a filled phone silhouette (the Lucide handset), with a red-orange "signal pulse" dot sitting on the upper-right rounded-corner arc center. The two variants are pure colour inversions of each other so the same shape works on any background. The corner dot uses CompactPhone's red-orange (`#FF5349`) in both variants — that's the brand pop.

## Sizing & consistency

All three SVGs (`mark-dark`, `mark-light`, `appicon`) share the same phone path
(filled, so the glyph reads as a solid silhouette at every size), the same
dot proportion (6.25% of canvas radius), and the same dot position relative to
the rounded-corner arc — so the dock icon and the in-app sidebar mark read as
the same logo at any size. The tray icon (`TrayController::buildPhoneIcon`)
also fills the same Lucide path; macOS treats it as a template image and
tints the silhouette to match the menu bar.

- 64×64 marks: full-bleed tile (no padding), used in the sidebar at 28–30 px.
- 1024×1024 app icon: tile inset 72 px (~7% padding, matching Apple template
  proportions), used as the `.icns` / `.ico` source via
  `tools/generate-icons.sh`.

## In-app use

QML loads these from the Qt resource system via `qrc:/branding/...`. `src/ui/qml/components/BrandMark.qml` picks the light variant in dark themes and the dark variant in light themes.

## Derived platform assets

The canonical source is `compactphone-appicon.svg`. Every native icon container
is generated from it by `tools/generate-icons.sh` and committed:

| File | Format | Consumed by |
|---|---|---|
| `compactphone.icns` | Apple icon | macOS bundle (`CFBundleIconFile` in `src/Info.plist.in`, embedded by `src/CMakeLists.txt`). |
| `compactphone.ico`  | Windows icon | Linked into `compactphone.exe` via `src/compactphone.rc`. |
| `generated/icon_*x*.png` | hicolor PNGs (16, 32, 48, 64, 128, 256, 512, 1024) | Linux `/usr/share/icons/hicolor/<sz>/apps/compactphone.png` install (see packaging). |
| `compactphone.desktop` | XDG Desktop Entry | Linux `/usr/share/applications/`. |

To regenerate after editing the source SVG:

```sh
# Linux / Debian-family
sudo apt-get install -y librsvg2-bin icnsutils icoutils
./tools/generate-icons.sh

# macOS
brew install librsvg libicns icoutils
./tools/generate-icons.sh
```

Always re-commit the regenerated `.icns`, `.ico`, and `generated/` PNG set so
release builds don't require these tools on every CI runner.

## Theme alignment

The dock icon carries two brand colors: **navy `#003366`** (the phone ink) and **red-orange `#FF5349`** (the signal dot). Every theme in `Theme.qml` should keep its accent in that palette so the running app feels like it belongs to the dock tile.

| Theme    | Background        | Accent            | Notes |
|---|---|---|---|
| Light    | off-white         | navy `#003366`    | Dock icon, large. |
| Dark     | near-black        | red-orange `#FF5349` | The signal dot, dominant. |
| Midnight | deep navy         | red-orange `#FF5349` | The dock icon literally — navy body, red-orange pop. |
| Ivory    | warm off-white    | red-orange `#FF5349` | Same accent as dark, warmer canvas. |
| Velvet   | deep violet       | violet `#8B5CF6`  | Intentional outlier — opt-in alternate identity. |

If you add a theme, pull its `accent` from navy or red-orange (or define a clear reason to deviate).
