#!/bin/sh
set -u
umask 022

HOME_DIR=${FORGESHELL_HOME:-/mnt/forgeshell}
BIN=${FORGESHELL_BIN:-/usr/bin/forgeshell}
PROFILE=${FORGESHELL_PROFILE:-$HOME_DIR/device.ini}
export FORGESHELL_PROFILE="$PROFILE"
STATE_DIR=$HOME_DIR/state
FAIL_FILE=$STATE_DIR/boot-failures
BOOT_OK=$STATE_DIR/boot-ok
BOOT_WATCH_STOP=$STATE_DIR/boot-watch-stop
BOOT_RESET_DONE=$STATE_DIR/boot-reset-done
LOG_FILE=$STATE_DIR/forgeshell.log
BOOT_MODE=0
SAFE_MODE=0
for arg in "$@"; do
    case $arg in
        --boot) BOOT_MODE=1 ;;
        --safe-mode) SAFE_MODE=1 ;;
    esac
done
if ! mkdir -p "$STATE_DIR" 2>/dev/null || ! chmod 0755 "$STATE_DIR" 2>/dev/null || [ ! -w "$STATE_DIR" ]; then
    printf 'ForgeShell recovery: state directory is not writable: %s\n' "$STATE_DIR" >&2
    exit 42
fi

read_failures() {
    value=0
    if [ -r "$FAIL_FILE" ]; then
        value=$(sed -n '1p' "$FAIL_FILE" 2>/dev/null)
        case "$value" in
            0|1|2|3) ;;
            *) value=3 ;;
        esac
    fi
    printf '%s\n' "$value"
}

write_failures() {
    tmp=$FAIL_FILE.tmp.$$
    if ! printf '%s\n' "$1" > "$tmp"; then
        rm -f "$tmp"
        return 1
    fi
    if ! mv -f "$tmp" "$FAIL_FILE"; then
        rm -f "$tmp"
        return 1
    fi
}

run_forgeshell() {
    export FRONTEND=forgeshell
    if [ "$SAFE_MODE" -eq 1 ]; then
        export FORGESHELL_SAFE_MODE=1
    else
        unset FORGESHELL_SAFE_MODE 2>/dev/null || true
    fi

    # A manual launch from GMenu2X is not a system boot. Keep the recovery
    # handshake and queued "safe mode next boot" semantics reserved for boot.
    if [ "$BOOT_MODE" -eq 0 ]; then
        unset FORGESHELL_BOOT FORGESHELL_BOOT_OK 2>/dev/null || true
        set --
        [ "$SAFE_MODE" -eq 1 ] && set -- --safe-mode
        "$BIN" "$@"
        status=$?
        if [ "$status" -eq 0 ] && ! write_failures 0; then
            printf 'ForgeShell warning: could not reset the startup-failure counter\n' >&2
        fi
        return "$status"
    fi

    rm -f "$BOOT_OK" "$BOOT_WATCH_STOP" "$BOOT_RESET_DONE"
    export FORGESHELL_BOOT=1
    export FORGESHELL_BOOT_OK="$BOOT_OK"
    (
        while [ ! -f "$BOOT_WATCH_STOP" ]; do
            if [ -f "$BOOT_OK" ]; then
                if write_failures 0; then
                    : > "$BOOT_RESET_DONE"
                    rm -f "$BOOT_OK"
                else
                    printf 'ForgeShell warning: could not reset the startup-failure counter\n' >&2
                fi
                exit 0
            fi
            sleep 1
        done
    ) &
    watcher=$!
    set -- --boot
    [ "$SAFE_MODE" -eq 1 ] && set -- "$@" --safe-mode
    "$BIN" "$@"
    status=$?
    : > "$BOOT_WATCH_STOP"
    kill "$watcher" 2>/dev/null || true
    wait "$watcher" 2>/dev/null || true
    if [ ! -f "$BOOT_RESET_DONE" ] && [ -f "$BOOT_OK" ]; then
        if write_failures 0; then
            : > "$BOOT_RESET_DONE"
        else
            printf 'ForgeShell warning: could not reset the startup-failure counter\n' >&2
        fi
    fi
    rm -f "$BOOT_OK" "$BOOT_WATCH_STOP" "$BOOT_RESET_DONE"
    return "$status"
}

if [ ! -x "$BIN" ]; then
    printf 'ForgeShell binary is missing: %s\n' "$BIN" >&2
    exit 42
fi

if [ "$BOOT_MODE" -eq 0 ]; then
    run_forgeshell
    exit $?
fi

failures=$(read_failures)
if [ "$failures" -ge 3 ]; then
    printf 'ForgeShell recovery: %s consecutive startup failures\n' "$failures" >&2
    exit 42
fi

failures=$((failures + 1))
write_failures "$failures" || exit 42
if [ -f "$LOG_FILE" ]; then
    mv -f "$LOG_FILE" "$LOG_FILE.1"
fi
run_forgeshell > "$LOG_FILE" 2>&1
exit $?
