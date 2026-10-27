#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

BOOT_ROOT=${FORGE_BOOT_ROOT:-/boot}
PROC_ROOT=${FORGE_PROC_ROOT:-/proc}
POWER_ROOT=${FORGE_POWER_ROOT:-/sys/class/power_supply}
CPU_DIR=${FORGE_CPU_DIR:-/sys/devices/system/cpu/cpu0/cpufreq}

main=$(forge_main_mount 2>/dev/null || true)
variant="unknown"
if [ -r "$BOOT_ROOT/console.cfg" ]; then
  detected=$(sed -n 's/^CONSOLE_VARIANT=//p' "$BOOT_ROOT/console.cfg" | head -n 1)
  [ -n "$detected" ] && variant=$detected
fi

kernel=$(uname -r 2>/dev/null || printf 'unknown')

uptime="unknown"
if [ -r "$PROC_ROOT/uptime" ]; then
  uptime_seconds=$(awk '{print int($1)}' "$PROC_ROOT/uptime" 2>/dev/null)
  if forge_numeric "${uptime_seconds:-}"; then
    uptime_days=$((uptime_seconds / 86400))
    uptime_hours=$((uptime_seconds % 86400 / 3600))
    uptime_minutes=$((uptime_seconds % 3600 / 60))
    uptime="${uptime_days}d ${uptime_hours}h ${uptime_minutes}m"
  fi
fi

memory="unknown"
if [ -r "$PROC_ROOT/meminfo" ]; then
  memory=$(awk '
    /MemTotal:/ { total=$2 }
    /MemAvailable:/ { available=$2 }
    /MemFree:/ { free=$2 }
    /Buffers:/ { buffers=$2 }
    /^Cached:/ { cached=$2 }
    END {
      if (total > 0) {
        if (available <= 0) available=free + buffers + cached
        printf "%d MiB available / %d MiB total", available/1024, total/1024
      }
    }
  ' "$PROC_ROOT/meminfo")
  [ -n "$memory" ] || memory="unknown"
fi

filesystem_line() {
  info_path=$1
  df -hP "$info_path" 2>/dev/null | awk 'NR==2 {print $4 " free / " $2 " total (" $5 " used)"}'
}
root_space=$(filesystem_line /)
[ -n "$root_space" ] || root_space="unknown"
main_space="not mounted"
if [ -n "$main" ]; then
  main_space=$(filesystem_line "$main")
  [ -n "$main_space" ] || main_space="unknown"
fi

frequency="unavailable"
for freq_file in "$CPU_DIR/scaling_cur_freq" "$CPU_DIR/cpuinfo_cur_freq"; do
  if [ -r "$freq_file" ]; then
    khz=$(cat "$freq_file" 2>/dev/null || true)
    if forge_numeric "$khz"; then
      frequency="$((khz / 1000)) MHz"
      break
    fi
  fi
done

battery="unavailable"
for capacity_file in "$POWER_ROOT"/*/capacity; do
  if [ -r "$capacity_file" ]; then
    capacity=$(cat "$capacity_file" 2>/dev/null || true)
    if forge_numeric "$capacity"; then
      battery="${capacity}%"
      break
    fi
  fi
done

version="ForgeOS Q90"
if [ -n "$main" ] && [ -r "$main/forgeos-version.txt" ]; then
  version=$(head -n 1 "$main/forgeos-version.txt")
fi

report=$(printf 'FIRMWARE\n%s\n\nDEVICE\nVariant: %s\nKernel: %s\nCPU now: %s\nBattery: %s\nUptime: %s\n\nMEMORY\n%s\n\nSTORAGE\nRoot: %s\nMain: %s' \
  "$version" "$variant" "$kernel" "$frequency" "$battery" "$uptime" "$memory" "$root_space" "$main_space")
forge_report "System Overview" "$report"
