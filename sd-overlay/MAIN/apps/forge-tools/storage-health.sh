#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

main=$(forge_main_mount 2>/dev/null || true)

filesystems=$(df -hP 2>/dev/null | awk -v main="$main" '
  NR == 1 { print; next }
  $6 == "/" || $6 == "/boot" || (main != "" && $6 == main) { print }
')
[ -n "$filesystems" ] || filesystems="Filesystem information is unavailable."

usage_warning=""
if [ -n "$main" ]; then
  used=$(df -P "$main" 2>/dev/null | awk 'NR==2 {gsub(/%/, "", $5); print $5}')
  if forge_numeric "${used:-}" && [ "$used" -ge 90 ]; then
    usage_warning="Main partition usage is ${used}%. Free space soon."
  fi
fi

kernel_log_available=0
messages=""
if kernel_log=$(dmesg 2>/dev/null); then
  kernel_log_available=1
  messages=$(printf '%s\n' "$kernel_log" | \
    grep -Ei 'I/O error|buffer I/O|blk_update_request|end_request|mmc[^:]*:.*(error|fail|timeout|timed out|reset|crc)|EXT[234]-fs.*(error|warning)|FAT-fs.*(error|warning)|BTRFS.*(error|warning|corrupt)|corrupt' | \
    tail -n 12)
fi

status="No matching storage issue detected."
[ -n "$usage_warning" ] && status="$usage_warning"
if [ "$kernel_log_available" -eq 0 ]; then
  messages="Kernel messages are unavailable; only filesystem capacity was checked."
  [ -n "$usage_warning" ] || status="Capacity looks normal; kernel messages could not be checked."
elif [ -z "$messages" ]; then
  messages="No matching storage warnings were detected since boot."
else
  status="Review the kernel messages below before trusting this SD card."
fi

report=$(printf 'STATUS\n%s\n\nFILESYSTEMS\n%s\n\nKERNEL STORAGE MESSAGES\n%s\n\nRepeated I/O, corruption, or filesystem errors usually indicate a failing or unsafe SD card.' "$status" "$filesystems" "$messages")
forge_report "Storage Check" "$report"
