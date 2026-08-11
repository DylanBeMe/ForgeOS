#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

main=$(forge_main_mount 2>/dev/null || true)
if [ -z "$main" ]; then
  forge_dialog "Date & Time" "The MAIN partition is not writable, so the clock cannot be saved."
  exit 1
fi

if ! forge_has_dialog && { [ -z "${FORGE_DATE_VALUE:-}" ] || [ -z "${FORGE_TIME_VALUE:-}" ]; }; then
  forge_dialog "Date & Time" "The interactive dialog program is not available."
  exit 1
fi

day=$(date +%d 2>/dev/null || printf '01')
month=$(date +%m 2>/dev/null || printf '01')
year=$(date +%Y 2>/dev/null || printf '2026')
hour=$(date +%H 2>/dev/null || printf '12')
minute=$(date +%M 2>/dev/null || printf '00')
second=$(date +%S 2>/dev/null || printf '00')

selected_date=${FORGE_DATE_VALUE:-}
if [ -z "$selected_date" ]; then
  selected_date=$(forge_dialog_capture --clear --no-shadow \
    --backtitle "$FORGE_BRAND" --title "Date & Time" \
    --date-format "%Y-%m-%d" \
    --calendar "Set the date. D-pad changes values; START confirms; + cancels." \
    0 0 "$day" "$month" "$year") || exit 0
fi

selected_time=${FORGE_TIME_VALUE:-}
if [ -z "$selected_time" ]; then
  selected_time=$(forge_dialog_capture --clear --no-shadow \
    --backtitle "$FORGE_BRAND" --title "Date & Time" \
    --timebox "Set the time. D-pad changes values; START confirms; + cancels." \
    0 0 "$hour" "$minute" "$second") || exit 0
fi

case "$selected_date" in
  ????-??-??) ;;
  *) forge_dialog "Date & Time" "The selected date was not valid."; exit 1 ;;
esac
case "$selected_time" in
  ??:??|??:??:??) ;;
  *) forge_dialog "Date & Time" "The selected time was not valid."; exit 1 ;;
esac

selected_time=$(printf '%s\n' "$selected_time" | cut -c1-5)
stamp="$selected_date $selected_time"
if ! date "+%Y-%m-%d %H:%M" -s "$stamp" >/dev/null 2>&1; then
  forge_dialog "Date & Time" "The kernel rejected the requested date or time."
  exit 1
fi

tmp="$main/.date.conf.tmp.$$"
if printf '%s\n' "$stamp" > "$tmp" && chmod 0644 "$tmp" 2>/dev/null && mv -f "$tmp" "$main/.date.conf"; then
  forge_dialog "Date & Time" "Clock updated to $stamp.\n\nIt will be restored on the next boot."
else
  rm -f "$tmp"
  forge_dialog "Date & Time" "The clock changed for this session, but it could not be saved for the next boot."
  exit 1
fi
