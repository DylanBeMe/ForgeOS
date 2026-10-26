#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"
HOME_DIR=${FORGESHELL_HOME:-/mnt/forgeshell}
LOG=$HOME_DIR/state/forgeshell.log
FAIL=$HOME_DIR/state/boot-failures
CONFIG=$HOME_DIR/config.ini
report=$(forge_tmpfile) || exit 1
trap 'rm -f "$report"' EXIT HUP INT TERM
{
    printf 'ForgeShell Recovery Report\n'
    printf '==========================\n\n'
    printf 'Startup failures: %s\n' "$(sed -n '1p' "$FAIL" 2>/dev/null || printf '0')"
    printf 'Launcher mode: %s\n' "$(sed -n 's/^launcher_mode=//p' "$CONFIG" 2>/dev/null | tail -n 1)"
    printf 'Safe mode next boot: %s\n\n' "$(sed -n 's/^safe_mode_next_boot=//p' "$CONFIG" 2>/dev/null | tail -n 1)"
    if [ -r "$LOG" ]; then
        printf 'Latest boot log\n---------------\n'
        tail -n 120 "$LOG"
    else
        printf 'No ForgeShell boot log exists yet.\n'
    fi
} > "$report"
forge_textbox "Boot Log" "$report"
