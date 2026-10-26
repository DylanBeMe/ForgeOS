#!/bin/sh
set -u

CPU_DIR=${FORGE_CPU_DIR:-/sys/devices/system/cpu/cpu0/cpufreq}
AVAIL=$CPU_DIR/scaling_available_frequencies
MIN=$CPU_DIR/scaling_min_freq
MAX=$CPU_DIR/scaling_max_freq
GOV=$CPU_DIR/scaling_governor
AVAILABLE_GOV=$CPU_DIR/scaling_available_governors

numeric() { case ${1:-} in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac; }
read_value() {
    [ -r "$1" ] || return 0
    sed -n '1p' "$1" 2>/dev/null || true
}
write_value() {
    file=$1 value=$2
    [ -w "$file" ] || return 1
    printf '%s' "$value" > "$file" 2>/dev/null || return 1
    [ "$(read_value "$file")" = "$value" ]
}

restore_file() {
    state=$1
    [ -r "$state" ] || return 0
    original_min=$(sed -n 's/^min=//p' "$state" | tail -n 1)
    original_max=$(sed -n 's/^max=//p' "$state" | tail -n 1)
    original_gov=$(sed -n 's/^governor=//p' "$state" | tail -n 1)
    numeric "$original_min" && numeric "$original_max" || return 1
    current_min=$(read_value "$MIN"); numeric "$current_min" || current_min=0
    restore_ok=1
    if [ "$original_max" -lt "$current_min" ]; then
        write_value "$MIN" "$original_min" || restore_ok=0
        write_value "$MAX" "$original_max" || restore_ok=0
    else
        write_value "$MAX" "$original_max" || restore_ok=0
        write_value "$MIN" "$original_min" || restore_ok=0
    fi
    if [ -n "$original_gov" ] && [ -w "$GOV" ]; then
        write_value "$GOV" "$original_gov" || restore_ok=0
    fi
    if [ "$restore_ok" -eq 1 ]; then
        rm -f "$state"
        return 0
    fi
    return 1
}

case ${1:-} in
    restore)
        [ "$#" -eq 2 ] || exit 2
        restore_file "$2"
        exit $?
        ;;
    apply)
        [ "$#" -eq 3 ] || exit 2
        profile=$2 state=$3
        case $profile in eco|balanced|performance) ;; *) exit 2 ;; esac
        ;;
    *) exit 2 ;;
esac

[ -d "$CPU_DIR" ] && [ -w "$MIN" ] && [ -w "$MAX" ] || exit 1
sorted=""
if [ -r "$AVAIL" ]; then
    sorted=$(awk '{for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+$/) print $i}' "$AVAIL" 2>/dev/null | sort -n -u)
fi
if [ -z "$sorted" ]; then
    low=$(read_value "$CPU_DIR/cpuinfo_min_freq")
    high=$(read_value "$CPU_DIR/cpuinfo_max_freq")
    if numeric "$low" && numeric "$high"; then sorted=$(printf '%s\n%s\n' "$low" "$high" | sort -n -u); fi
fi
[ -n "$sorted" ] || exit 1
low=$(printf '%s\n' "$sorted" | head -n 1)
high=$(printf '%s\n' "$sorted" | tail -n 1)
count=$(printf '%s\n' "$sorted" | awk 'END {print NR}')
mid=$(printf '%s\n' "$sorted" | sed -n "$(((count + 1) / 2))p")
case $profile in eco) target=$low ;; balanced) target=$mid ;; performance) target=$high ;; esac
numeric "$target" || exit 1

original_min=$(read_value "$MIN")
original_max=$(read_value "$MAX")
original_gov=$(read_value "$GOV")
numeric "$original_min" && numeric "$original_max" || exit 1
mkdir -p "$(dirname "$state")" 2>/dev/null || exit 1
tmp=$state.tmp.$$
if ! printf 'min=%s\nmax=%s\ngovernor=%s\n' "$original_min" "$original_max" "$original_gov" > "$tmp" ||
   ! mv -f "$tmp" "$state"; then
    rm -f "$tmp"
    exit 1
fi

current_min=$(read_value "$MIN"); numeric "$current_min" || current_min=0
if [ "$target" -lt "$current_min" ]; then
    if ! write_value "$MIN" "$low" || ! write_value "$MAX" "$target"; then
        restore_file "$state"
        exit 1
    fi
else
    if ! write_value "$MAX" "$target" || ! write_value "$MIN" "$low"; then
        restore_file "$state"
        exit 1
    fi
fi

available=$(read_value "$AVAILABLE_GOV")
case $profile in performance) prefs='performance ondemand userspace' ;; *) prefs='ondemand conservative schedutil userspace performance' ;; esac
for candidate in $prefs; do
    case " $available " in
        *" $candidate "*) write_value "$GOV" "$candidate" >/dev/null 2>&1 || true; break ;;
    esac
done
exit 0
