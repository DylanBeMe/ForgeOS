#!/bin/sh
set -u
umask 022

HOME_DIR=${FORGESHELL_HOME:-/mnt/forgeshell}
CONFIG=$HOME_DIR/config.ini
STATE_DIR=$HOME_DIR/state
MODE=forgeshell
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
# First-run setup always belongs to ForgeShell. GMenu2X remains an explicit
# recovery/default choice, but a missing or fresh config now falls back to ForgeShell.
case $(config_value onboarding_complete) in
    1|yes|true|on) ;;
    *) MODE=forgeshell ;;
esac
case $(config_value safe_mode_next_boot) in
    1|yes|true|on)
        SAFE_MODE=1
        if [ -w "$CONFIG" ]; then
            config_tmp=$CONFIG.tmp.$$
            if ! awk 'BEGIN{done=0} /^[[:space:]]*safe_mode_next_boot[[:space:]]*=/ {if(!done){print "safe_mode_next_boot=0"; done=1} next} {print} END{if(!done) print "safe_mode_next_boot=0"}' \
                "$CONFIG" > "$config_tmp" 2>/dev/null || \
               ! chmod 0644 "$config_tmp" 2>/dev/null || \
               ! mv -f "$config_tmp" "$CONFIG"; then
                rm -f "$config_tmp"
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
