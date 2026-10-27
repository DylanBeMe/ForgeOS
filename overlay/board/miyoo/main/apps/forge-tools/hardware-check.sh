#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"
main=$(forge_main_mount 2>/dev/null || true)
[ -n "$main" ] || main=/mnt
outdir=$main/forgeos-test-reports
mkdir -p "$outdir" || { forge_dialog "Hardware Check" "Unable to create $outdir"; exit 1; }
stamp=$(date +%Y%m%d-%H%M%S 2>/dev/null || printf current)
report=$outdir/q90-hardware-$stamp.txt
start=$(date +%s 2>/dev/null || printf 0)
{
  printf 'ForgeOS Q90 Hardware Check\n'
  printf 'Generated: %s\n\n' "$(date 2>/dev/null || printf unknown)"
  printf '[Firmware]\n'
  cat /mnt/forgeos-version.txt 2>/dev/null || true
  /usr/bin/forgeshell --version 2>/dev/null || true
  printf '\n[Kernel]\n'; uname -a 2>/dev/null || true
  printf '\n[Memory]\n'; sed -n '1,12p' /proc/meminfo 2>/dev/null || true
  printf '\n[CPU]\n'; cat /proc/cpuinfo 2>/dev/null || true
  printf '\n[Frequency]\n'
  for f in /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq \
           /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq \
           /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq \
           /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; do
    [ -r "$f" ] && printf '%s=%s\n' "$(basename "$f")" "$(cat "$f")"
  done
  printf '\n[Storage]\n'; df -h 2>/dev/null || true
  printf '\n[Mounts]\n'; mount 2>/dev/null || true
  printf '\n[Input devices]\n'; cat /proc/bus/input/devices 2>/dev/null || true
  printf '\n[Framebuffer]\n'; cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || true
  cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null || true
  printf '\n[Audio]\n'; cat /proc/asound/cards 2>/dev/null || true
  printf '\n[Battery]\n'
  for f in /sys/class/power_supply/*/capacity /sys/class/power_supply/*/status; do
    [ -r "$f" ] && printf '%s=%s\n' "$f" "$(cat "$f")"
  done
  printf '\n[ForgeShell files]\n'
  ls -l /mnt/forgeshell/state 2>/dev/null || true
  printf '\n[Recent kernel warnings]\n'
  dmesg 2>/dev/null | tail -n 80 || true
  end=$(date +%s 2>/dev/null || printf 0)
  if forge_numeric "$start" && forge_numeric "$end"; then printf '\nCollection seconds: %s\n' "$((end-start))"; fi
} > "$report"
forge_dialog "Hardware Check" "Report saved to:\n\n$report\n\nPlease include it with your test notes."
