#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(<"$ROOT_DIR/VERSION")"
DRY_RUN=0
FORCE=0

usage() {
  cat >&2 <<USAGE
Usage: $0 [--dry-run] [--force] /path/to/BOOT /path/to/main

  --dry-run  Show the files that would be copied.
  --force    Skip MiyooCFW partition marker checks.
USAGE
  exit 2
}

fail() {
  echo "Error: $*" >&2
  exit 1
}

while (($#)); do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    --force) FORCE=1; shift ;;
    --help|-h) usage ;;
    --) shift; break ;;
    -*) usage ;;
    *) break ;;
  esac
done

[[ $# -eq 2 ]] || usage
BOOT_MOUNT=$1
MAIN_MOUNT=$2

for tool in rsync date cp grep chmod sync install; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done

[[ -d "$BOOT_MOUNT" && -d "$MAIN_MOUNT" ]] || fail "both mount points must exist"
[[ -w "$BOOT_MOUNT" && -w "$MAIN_MOUNT" ]] || fail "both mount points must be writable"

canonical_dir() {
  (cd "$1" && pwd -P)
}
BOOT_MOUNT=$(canonical_dir "$BOOT_MOUNT")
MAIN_MOUNT=$(canonical_dir "$MAIN_MOUNT")
[[ "$BOOT_MOUNT" != "$MAIN_MOUNT" ]] || fail "BOOT and main must be different directories"
[[ "$BOOT_MOUNT" != / && "$MAIN_MOUNT" != / ]] || fail "refusing to install into the host root directory"
case "$BOOT_MOUNT/" in
  "$MAIN_MOUNT"/*) fail "BOOT cannot be inside the main directory" ;;
esac
case "$MAIN_MOUNT/" in
  "$BOOT_MOUNT"/*) fail "main cannot be inside the BOOT directory" ;;
esac

if (( ! FORCE )); then
  [[ -e "$BOOT_MOUNT/options.cfg" || -e "$BOOT_MOUNT/console.cfg" || -e "$BOOT_MOUNT/zImage" ]] \
    || fail "BOOT does not look like a MiyooCFW boot partition (use --force only after checking the path)"
  [[ -d "$MAIN_MOUNT/gmenu2x" || -d "$MAIN_MOUNT/apps" ]] \
    || fail "main does not look like a MiyooCFW data partition (use --force only after checking the path)"
fi

if (( DRY_RUN )); then
  echo "Dry run: BOOT changes"
  rsync -ani "$ROOT_DIR/sd-overlay/BOOT/" "$BOOT_MOUNT/"
  echo
  echo "Dry run: ForgeOS tools"
  rsync -ani --delete \
    "$ROOT_DIR/sd-overlay/MAIN/apps/forge-tools/" \
    "$MAIN_MOUNT/apps/forge-tools/"
  echo
  echo "Dry run: ForgeOS launcher section"
  rsync -ani --delete --filter='protect forgeshell' --filter='protect forgeshell-safe' \
    --filter='protect forgeshell-log' --filter='protect performance-snapshot' \
    --filter='protect reset-forgeshell' \
    "$ROOT_DIR/sd-overlay/MAIN/gmenu2x/sections/ForgeOS/" \
    "$MAIN_MOUNT/gmenu2x/sections/ForgeOS/"
  echo
  echo "Dry run: version marker"
  rsync -ani \
    "$ROOT_DIR/sd-overlay/MAIN/forgeos-version.txt" \
    "$MAIN_MOUNT/forgeos-version.txt"
  if [[ -f "$MAIN_MOUNT/forgeos-version.txt" ]] && \
     grep -q '^ForgeOS Q90 0\.1\.0$' "$MAIN_MOUNT/forgeos-version.txt" && \
     [[ -d "$MAIN_MOUNT/gmenu2x/sections/forge" ]]; then
    echo "would remove legacy launcher section: $MAIN_MOUNT/gmenu2x/sections/forge"
  fi
  exit 0
fi

STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP_ROOT=${BACKUP_ROOT:-"$ROOT_DIR/host-backups"}
BACKUP_DIR="$BACKUP_ROOT/$STAMP"
index=1
while [[ -e "$BACKUP_DIR" ]]; do
  BACKUP_DIR="$BACKUP_ROOT/$STAMP-$index"
  ((index++))
done
mkdir -p "$BACKUP_DIR/BOOT" "$BACKUP_DIR/MAIN" || fail "cannot create backup directory: $BACKUP_DIR"

backup_item() {
  local source=$1 destination=$2
  [[ -e "$source" ]] || return 0
  mkdir -p "$destination"
  cp -a "$source" "$destination/"
}

backup_item "$BOOT_MOUNT/miyoo-splash.bmp" "$BACKUP_DIR/BOOT"
backup_item "$MAIN_MOUNT/apps/forge-tools" "$BACKUP_DIR/MAIN/apps"
backup_item "$MAIN_MOUNT/gmenu2x/sections/ForgeOS" "$BACKUP_DIR/MAIN/gmenu2x/sections"
backup_item "$MAIN_MOUNT/gmenu2x/sections/forge" "$BACKUP_DIR/MAIN/gmenu2x/sections"
backup_item "$MAIN_MOUNT/forgeos-version.txt" "$BACKUP_DIR/MAIN"

rsync -a "$ROOT_DIR/sd-overlay/BOOT/" "$BOOT_MOUNT/"
mkdir -p "$MAIN_MOUNT/apps/forge-tools" "$MAIN_MOUNT/gmenu2x/sections/ForgeOS"
rsync -a --delete "$ROOT_DIR/sd-overlay/MAIN/apps/forge-tools/" "$MAIN_MOUNT/apps/forge-tools/"
rsync -a --delete --filter='protect forgeshell' --filter='protect forgeshell-safe' \
    --filter='protect forgeshell-log' --filter='protect performance-snapshot' \
    --filter='protect reset-forgeshell' \
  "$ROOT_DIR/sd-overlay/MAIN/gmenu2x/sections/ForgeOS/" \
  "$MAIN_MOUNT/gmenu2x/sections/ForgeOS/"
install -m 0644 "$ROOT_DIR/sd-overlay/MAIN/forgeos-version.txt" "$MAIN_MOUNT/forgeos-version.txt"
chmod 755 "$MAIN_MOUNT"/apps/forge-tools/*.sh 2>/dev/null || true

# Remove only the legacy 0.1.0 section after its contents have been backed up.
if [[ -f "$BACKUP_DIR/MAIN/forgeos-version.txt" ]] && \
   grep -q '^ForgeOS Q90 0\.1\.0$' "$BACKUP_DIR/MAIN/forgeos-version.txt" && \
   [[ -d "$MAIN_MOUNT/gmenu2x/sections/forge" ]]; then
  rm -rf "$MAIN_MOUNT/gmenu2x/sections/forge"
fi

sync

echo "ForgeOS Q90 $VERSION tools and artwork installed."
echo "Emulator binaries were not replaced; build and flash the full image for emulator upgrades."
echo "Previous ForgeOS files and the boot splash were saved to:"
echo "$BACKUP_DIR"
