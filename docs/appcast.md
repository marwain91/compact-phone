# Release feed (appcast)

Compact Phone uses the Sparkle 2 appcast schema for cross-platform update
notifications. Linux uses the in-app `UpdateChecker` to surface a "new
version available" banner; macOS will adopt full Sparkle 2 framework
integration in a future task; Windows will use WinSparkle. The feed format
is the same for all three so a single file at
`https://compactphone.eu/appcast.xml` covers everyone.

## Example feed

```xml
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
    <channel>
        <title>Compact Phone</title>
        <link>https://compactphone.eu/</link>
        <language>en</language>
        <item>
            <title>Version 0.4.0</title>
            <pubDate>Mon, 09 Jun 2025 12:00:00 +0000</pubDate>
            <enclosure
                url="https://compactphone.eu/downloads/compactphone-0.4.0.dmg"
                sparkle:shortVersionString="0.4.0"
                sparkle:version="0.4.0"
                sparkle:edSignature="MEQCIH...base64..."
                length="34000000"
                type="application/octet-stream" />
            <sparkle:minimumSystemVersion>12.0</sparkle:minimumSystemVersion>
            <description><![CDATA[
                <ul>
                    <li>Codec picker in account editor</li>
                    <li>Global hotkeys (answer, hangup, mute)</li>
                </ul>
            ]]></description>
        </item>
    </channel>
</rss>
```

## Keys to procure

1. **EdDSA key pair** for Sparkle 2:
   ```sh
   git clone https://github.com/sparkle-project/Sparkle.git
   cd Sparkle/bin && ./generate_keys
   # Stores private key in macOS Keychain; prints the public key.
   ```
   Keep the private key **offline**. Embed the public key in the macOS app
   bundle's Info.plist (`SUPublicEDKey`) when full Sparkle integration lands.

2. **Hosting** — any static host works (GitHub Pages, S3, Cloudflare R2,
   plain nginx). The feed URL must be HTTPS.

## Release procedure (manual)

After publishing a signed/notarized DMG and signed MSI:

```sh
# Sign the artifact with the offline EdDSA key
sign_update compactphone-0.4.0.dmg

# Append an <item> block to appcast.xml and upload.
```

Automation (a `release-feed.yml` workflow) is a follow-up task once the
hosting URL is registered.
