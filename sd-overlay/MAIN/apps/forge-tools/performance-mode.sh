#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

CPU_DIR=${FORGE_CPU_DIR:-/sys/devices/system/cpu/cpu0/cpufreq}
AVAIL="$CPU_DIR/scaling_available_frequencies"
AVAILABLE_GOV="$CPU_DIR/scaling_available_governors"
SET="$CPU_DIR/scaling_setspeed"
MIN="$CPU_DIR/scaling_min_freq"
MAX="$CPU_DIR/scaling_max_freq"
GOV="$CPU_DIR/scaling_governor"

if [ ! -d "$CPU_DIR" ]; then
  forge_dialog "CPU Profile" "The running kernel does not expose CPU frequency controls. No changes were made."
  exit 0
fi

sorted=""
if [ -r "$AVAIL" ]; then
  sorted=$(awk '{
    for (field = 1; field <= NF; field++) {
      if ($field ~ /^[0-9]+$/) print $field
    }
  }' "$AVAIL" 2>/dev/null | sort -n -u)
fi
if [ -z "$sorted" ]; then
  cpu_low=$(cat "$CPU_DIR/cpuinfo_min_freq" 2>/dev/null || true)
  cpu_high=$(cat "$CPU_DIR/cpuinfo_max_freq" 2>/dev/null || true)
  if forge_numeric "$cpu_low" && forge_numeric "$cpu_high"; then
    sorted=$(printf '%s\n%s\n' "$cpu_low" "$cpu_high" | sort -n -u)
  fi
fi
if [ -z "$sorted" ]; then
  forge_dialog "CPU Profile" "No valid kernel-provided frequency list was found. No changes were made."
  exit 0
fi

low=$(printf '%s\n' "$sorted" | head -n 1)
high=$(printf '%s\n' "$sorted" | tail -n 1)
count=$(printf '%s\n' "$sorted" | awk 'END {print NR}')
mid_index=$(( (count + 1) / 2 ))
mid=$(printf '%s\n' "$sorted" | sed -n "${mid_index}p")

choice=$(forge_menu "CPU Profile" \
  "Temporary profiles use only frequencies exposed by the kernel. They reset after reboot." \
  eco "Eco - cap at $((low / 1000)) MHz" \
  balanced "Balanced - cap at $((mid / 1000)) MHz" \
  performance "Performance - allow up to $((high / 1000)) MHz" 2>/dev/null || true)

case "$choice" in
  eco) target=$low ;;
  balanced) target=$mid ;;
  performance) target=$high ;;
  *) exit 0 ;;
esac

original_min=$(cat "$MIN" 2>/dev/null || true)
original_max=$(cat "$MAX" 2>/dev/null || true)
original_gov=$(cat "$GOV" 2>/dev/null || true)
available_governors=$(cat "$AVAILABLE_GOV" 2>/dev/null || true)

choose_governor() {
  case "$choice" in
    performance) preferences="performance ondemand userspace" ;;
    *) preferences="ondemand conservative schedutil userspace performance" ;;
  esac

  for candidate in $preferences; do
    case " $available_governors " in
      *" $candidate "*) printf '%s\n' "$candidate"; return 0 ;;
    esac
  done
  return 1
}

desired_gov=$(choose_governor 2>/dev/null || true)

write_value() {
  file=$1
  value=$2
  [ -w "$file" ] || return 1
  printf '%s' "$value" > "$file" 2>/dev/null || return 1
  actual=$(cat "$file" 2>/dev/null || true)
  [ "$actual" = "$value" ]
}

restore_original() {
  if forge_numeric "$original_min" && forge_numeric "$original_max"; then
    current_min=$(cat "$MIN" 2>/dev/null || printf '0')
    current_max=$(cat "$MAX" 2>/dev/null || printf '0')
    forge_numeric "$current_min" || current_min=0
    forge_numeric "$current_max" || current_max=0

    if [ "$original_max" -lt "$current_min" ]; then
      write_value "$MIN" "$original_min" >/dev/null 2>&1 || true
      write_value "$MAX" "$original_max" >/dev/null 2>&1 || true
    elif [ "$original_min" -gt "$current_max" ]; then
      write_value "$MAX" "$original_max" >/dev/null 2>&1 || true
      write_value "$MIN" "$original_min" >/dev/null 2>&1 || true
    else
      write_value "$MIN" "$original_min" >/dev/null 2>&1 || true
      write_value "$MAX" "$original_max" >/dev/null 2>&1 || true
    fi
  fi
  if [ -n "$original_gov" ] && [ -w "$GOV" ]; then
    printf '%s' "$original_gov" > "$GOV" 2>/dev/null || true
  fi
}

applied=0
mode=""

# Prefer a dynamic range. This saves power at idle and avoids pinning the CPU
# at the selected ceiling. Fall back to userspace setspeed on older kernels.
if [ -w "$MIN" ] && [ -w "$MAX" ] && \
   forge_numeric "$original_min" && forge_numeric "$original_max"; then
  desired_min=$low
  desired_max=$target

  current_min=$(cat "$MIN" 2>/dev/null || printf '0')
  current_max=$(cat "$MAX" 2>/dev/null || printf '0')
  forge_numeric "$current_min" || current_min=0
  forge_numeric "$current_max" || current_max=0

  range_ok=0
  if [ "$desired_max" -lt "$current_min" ]; then
    write_value "$MIN" "$desired_min" && write_value "$MAX" "$desired_max" && range_ok=1
  elif [ "$desired_min" -gt "$current_max" ]; then
    write_value "$MAX" "$desired_max" && write_value "$MIN" "$desired_min" && range_ok=1
  else
    write_value "$MIN" "$desired_min" && write_value "$MAX" "$desired_max" && range_ok=1
  fi

  if [ "$range_ok" -eq 1 ]; then
    governor_ok=1
    if [ -n "$desired_gov" ] && [ -w "$GOV" ]; then
      write_value "$GOV" "$desired_gov" || governor_ok=0
    fi
    if [ "$governor_ok" -eq 1 ]; then
      applied=1
      mode="dynamic range $((desired_min / 1000))-$((desired_max / 1000)) MHz"
    fi
  fi
fi

if [ "$applied" -eq 0 ] && [ -w "$SET" ]; then
  restore_original
  setspeed_ready=1
  if [ -w "$GOV" ]; then
    case " $available_governors " in
      *" userspace "*) write_value "$GOV" userspace || setspeed_ready=0 ;;
      *) [ "$original_gov" = userspace ] || setspeed_ready=0 ;;
    esac
  fi
  if [ "$setspeed_ready" -eq 1 ] && write_value "$SET" "$target"; then
    applied=1
    mode="fixed $((target / 1000)) MHz"
  fi
fi

if [ "$applied" -eq 1 ]; then
  forge_dialog "CPU Profile" "Applied the $choice profile using $mode.\n\nThe setting is temporary and resets after reboot."
else
  restore_original
  forge_dialog "CPU Profile" "The kernel exposed frequency information but rejected the requested profile. Original settings were restored."
fi
