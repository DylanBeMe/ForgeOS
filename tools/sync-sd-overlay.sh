#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(<"$ROOT_DIR/VERSION")"
rm -rf "$ROOT_DIR/sd-overlay/BOOT" "$ROOT_DIR/sd-overlay/MAIN"
mkdir -p "$ROOT_DIR/sd-overlay/BOOT" "$ROOT_DIR/sd-overlay/MAIN"
rsync -a \
  --exclude='/console.cfg' \
  --exclude='/options.cfg' \
  "$ROOT_DIR/overlay/board/miyoo/boot/" "$ROOT_DIR/sd-overlay/BOOT/"
rsync -a \
  --exclude='/.backlight.conf' \
  --exclude='/.volume.conf' \
  --exclude='/forgeos-emulators.txt' \
  --exclude='/autoexec.sh' \
  --exclude='/forgeshell/***' \
  --exclude='/gmenu2x/sections/ForgeOS/forgeshell' \
  --exclude='/gmenu2x/sections/ForgeOS/forgeshell-safe' \
  --exclude='/gmenu2x/sections/ForgeOS/forgeshell-log' \
  --exclude='/gmenu2x/sections/ForgeOS/performance-snapshot' \
  --exclude='/gmenu2x/sections/ForgeOS/reset-forgeshell' \
  "$ROOT_DIR/overlay/board/miyoo/main/" "$ROOT_DIR/sd-overlay/MAIN/"
# Keep archive/install permissions deterministic even when this checkout lives
# under a setgid or group-writable directory.
find "$ROOT_DIR/sd-overlay/BOOT" "$ROOT_DIR/sd-overlay/MAIN" -type d \
  -exec chmod g-s {} + -exec chmod 0755 {} +
find "$ROOT_DIR/sd-overlay/BOOT" "$ROOT_DIR/sd-overlay/MAIN" -type f -exec chmod 0644 {} +
find "$ROOT_DIR/sd-overlay/MAIN/apps/forge-tools" -type f -name '*.sh' -exec chmod 0755 {} +
chmod 0755 "$ROOT_DIR/sd-overlay/BOOT/firstboot.custom.sh"
cat > "$ROOT_DIR/sd-overlay/MAIN/forgeos-version.txt" <<EOF
ForgeOS Q90 $VERSION
Install mode: update-safe SD overlay
Emulator binaries: unchanged; a full-image build is required to upgrade them
EOF
cat > "$ROOT_DIR/sd-overlay/INSTALL.txt" <<EOF
ForgeOS Q90 SD overlay $VERSION

This is an update-safe maintenance overlay for an existing MiyooCFW/ForgeOS
card. It updates ForgeOS-owned artwork, maintenance tools, and GMenu2X entries.

It deliberately does not install ForgeShell, ForgeShell-only launcher panels,
/mnt/autoexec.sh, emulator binaries, boot options, device selection, brightness,
or volume settings. Existing full-image ForgeShell launcher panels are preserved.
ForgeShell and the refreshed emulator stack require a full ForgeOS image build.

Recommended host installation:
  ./install-overlay.sh /path/to/BOOT /path/to/main

Preview changes first:
  ./install-overlay.sh --dry-run /path/to/BOOT /path/to/main
EOF
