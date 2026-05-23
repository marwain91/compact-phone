#!/usr/bin/env bash
set -euo pipefail

cd /workspace

# Install runtime deps needed by linuxdeploy. The dev image already has most
# build deps but not these release packaging tools.
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  file \
  desktop-file-utils \
  zsync

# Stage the AppDir layout that linuxdeploy expects.
rm -rf AppDir
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/share/applications
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps

cp build/linux/src/compactphone AppDir/usr/bin/
cp packaging/linux/compactphone.desktop AppDir/usr/share/applications/
cp branding/generated/icon_256x256.png \
  AppDir/usr/share/icons/hicolor/256x256/apps/compactphone.png

# Download linuxdeploy. Pinning to continuous releases because there is no
# semver-tagged release.
wget -q -O /tmp/linuxdeploy.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x /tmp/linuxdeploy.AppImage

# APPIMAGE_EXTRACT_AND_RUN=1 lets linuxdeploy run without FUSE. Docker
# containers do not have /dev/fuse by default, so the AppImage unpacks itself
# to /tmp and runs from there.
export APPIMAGE_EXTRACT_AND_RUN=1

# Qt is statically linked by tools/dev/triplets/x64-linux.cmake. Plain
# linuxdeploy is enough to gather the remaining dynamic system dependencies.
/tmp/linuxdeploy.AppImage \
  --appdir AppDir \
  --output appimage \
  --desktop-file AppDir/usr/share/applications/compactphone.desktop \
  --icon-file branding/generated/icon_256x256.png

# linuxdeploy names the output Compact_Phone-<arch>.AppImage. Move it to the
# stable filename the landing page hardcodes.
mkdir -p dist
mv Compact_Phone*-x86_64.AppImage dist/Compact-Phone-Linux-x86_64.AppImage
chmod +x dist/Compact-Phone-Linux-x86_64.AppImage
ls -la dist/
