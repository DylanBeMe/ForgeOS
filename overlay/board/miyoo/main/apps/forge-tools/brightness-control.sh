#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

direction=${1:-}
case "$direction" in up|down) ;; *) exit 2 ;; esac

brightness=${FORGE_BACKLIGHT_PATH:-/sys/class/backlight/backlight/brightness}
max_path=${FORGE_BACKLIGHT_MAX_PATH:-/sys/class/backlight/backlight/max_brightness}
[ -r "$brightness" ] && [ -w "$brightness" ] || exit 1

current=$(cat "$brightness" 2>/dev/null || true)
maximum=$(cat "$max_path" 2>/dev/null || true)
forge_numeric "$current" || exit 1
if ! forge_numeric "$maximum" || [ "$maximum" -lt 1 ]; then
  maximum=10
  [ "$current" -gt "$maximum" ] && maximum=$current
fi

step=$(( (maximum + 9) / 10 ))
[ "$step" -lt 1 ] && step=1
case "$direction" in
  up) target=$((current + step)) ;;
  down) target=$((current - step)) ;;
esac
[ "$target" -gt "$maximum" ] && target=$maximum
# Never let a shortcut turn the panel completely black.
[ "$target" -lt 1 ] && target=1

printf '%s\n' "$target" > "$brightness" 2>/dev/null || exit 1
actual=$(cat "$brightness" 2>/dev/null || true)
forge_numeric "$actual" || actual=$target

config=${FORGE_BACKLIGHT_CONFIG:-}
if [ -z "$config" ]; then
  main=$(forge_main_mount 2>/dev/null || true)
  [ -n "$main" ] && config="$main/.backlight.conf"
fi
if [ -n "$config" ]; then
  tmp="$config.tmp.$$"
  if printf '%s\n' "$actual" > "$tmp" 2>/dev/null; then
    chmod 0644 "$tmp" 2>/dev/null || true
    mv -f "$tmp" "$config" 2>/dev/null || rm -f "$tmp"
  fi
fi

percent=$((actual * 100 / maximum))
printf '%s\n' "$percent"
