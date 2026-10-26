#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

main=$(forge_main_mount 2>/dev/null || true)
if [ -z "$main" ]; then
  forge_dialog "Save Backup" "The main data partition is not mounted or is not writable."
  exit 1
fi

backup_dir="$main/backups/forgeos"
if ! mkdir -p "$backup_dir" || [ ! -w "$backup_dir" ]; then
  forge_dialog "Save Backup" "Unable to create a writable backup folder at:\n\n$backup_dir"
  exit 1
fi

set --
for rel in \
  .retroarch/saves \
  .retroarch/states \
  retroarch/saves \
  retroarch/states \
  gmenu2x/gmenu2x.conf \
  gmenu2x/sections \
  .buttons.conf \
  .backlight.conf \
  .volume.conf \
  .usbmode \
  .tvmode; do
  [ -e "$main/$rel" ] && set -- "$@" "$rel"
done

if [ "$#" -eq 0 ]; then
  forge_dialog "Save Backup" "No known save, state, or menu configuration files were found."
  exit 0
fi

source_kib=0
for rel in "$@"; do
  path_kib=$(du -sk "$main/$rel" 2>/dev/null | awk 'NR==1 {print $1}')
  forge_numeric "${path_kib:-}" || path_kib=0
  source_kib=$((source_kib + path_kib))
done

free_kib=$(df -Pk "$backup_dir" 2>/dev/null | awk 'NR==2 {print $4}')
forge_numeric "${free_kib:-}" || free_kib=0
required_kib=$((source_kib + source_kib / 10 + 1024))
if [ "$free_kib" -gt 0 ] && [ "$required_kib" -gt "$free_kib" ]; then
  forge_dialog "Save Backup" "There is not enough free space for a safe backup.\n\nNeeded: about $((required_kib / 1024)) MiB\nFree: about $((free_kib / 1024)) MiB"
  exit 1
fi

stamp=$(date +%Y%m%d-%H%M%S 2>/dev/null || printf 'current')
archive="$backup_dir/q90-saves-$stamp.tar.gz"
index=1
while [ -e "$archive" ] || [ -e "$archive.partial" ]; do
  archive="$backup_dir/q90-saves-$stamp-$index.tar.gz"
  index=$((index + 1))
done
partial="$archive.partial"
error_file=$(forge_tmpfile) || {
  forge_dialog "Save Backup" "Unable to create a temporary error log."
  exit 1
}
trap 'rm -f "$partial" "$error_file"' EXIT HUP INT TERM

confirm_message=$(printf 'Back up saves, states, and menu settings?\n\nDestination:\n%s\n\nEstimated source size: %s MiB' "$archive" "$((source_kib / 1024))")
forge_confirm "Save Backup" "$confirm_message" || exit 0

if tar -C "$main" -czf "$partial" "$@" 2> "$error_file"; then
  if mv "$partial" "$archive"; then
    sync
    archive_size=$(du -h "$archive" 2>/dev/null | awk 'NR==1 {print $1}')
    success_message=$(printf 'Backup completed.\n\n%s\n\nArchive size: %s' "$archive" "${archive_size:-unknown}")
    forge_dialog "Save Backup" "$success_message"
    exit 0
  fi
fi

error_text=$(tail -n 6 "$error_file" 2>/dev/null)
[ -n "$error_text" ] || error_text="The archive could not be written. Check free space and SD-card health."
forge_dialog "Save Backup" "Backup failed. No incomplete archive was kept.\n\n$error_text"
exit 1
