#!/bin/sh
set -u

HOME_DIR=${FORGESHELL_HOME:-/mnt/forgeshell}
CONFIG=$HOME_DIR/config.ini
STATE_DIR=$HOME_DIR/state
MODE=gmenu2x
SAFE_MODE=0
REBOOT_BIN=${FORGESHELL_REBOOT_BIN:-/sbin/reboot}
POWEROFF_BIN=${FORGESHELL_POWEROFF_BIN:-/sbin/poweroff}

config_value() {
    key=$1
    [ -r "$CONFIG" ] || return 0
    sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*\\([^#;[:space:]]*\\).*/\\1/p" "$CONFIG" | tail -n 1
}

configured=$(config_value launcher_mode)
case $configured in forgeshell|gmenu2x) MODE=$configured ;; esac
case $(config_value safe_mode_next_boot) in
    1|yes|true|on)
        SAFE_MODE=1
        if [ -w "$CONFIG" ]; then
            if ! awk 'BEGIN{done=0} /^[[:space:]]*safe_mode_next_boot[[:space:]]*=/ {print "safe_mode_next_boot=0"; done=1; next} {print} END{if(!done) print "safe_mode_next_boot=0"}' \
                "$CONFIG" > "$CONFIG.tmp.$$" 2>/dev/null || \
               ! mv -f "$CONFIG.tmp.$$" "$CONFIG"; then
                rm -f "$CONFIG.tmp.$$"
            fi
        fi
        ;;
esac
if [ -e "$STATE_DIR/force-safe-mode" ]; then
    SAFE_MODE=1
    rm -f "$STATE_DIR/force-safe-mode"
fi
if [ -e "$STATE_DIR/force-gmenu2x" ]; then
    rm -f "$STATE_DIR/force-gmenu2x"
    MODE=gmenu2x
fi

if [ "$MODE" = forgeshell ] && [ -x "$HOME_DIR/forgeshell-start.sh" ]; then
    set -- --boot
    [ "$SAFE_MODE" -eq 1 ] && set -- "$@" --safe-mode
    "$HOME_DIR/forgeshell-start.sh" "$@"
    status=$?
    case $status in
        43) sync; exec "$REBOOT_BIN" ;;
        44) sync; exec "$POWEROFF_BIN" ;;
        0) ;;
        42) printf 'ForgeShell requested GMenu2X recovery\n' >&2 ;;
        *) printf 'ForgeShell returned %s; starting GMenu2X recovery\n' "$status" >&2 ;;
    esac
fi

exec "$HOME_DIR/run-gmenu2x.sh"
