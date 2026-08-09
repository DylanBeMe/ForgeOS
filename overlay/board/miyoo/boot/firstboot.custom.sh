#!/bin/sh
# ForgeOS first-boot repair hook.
# MiyooCFW runs /boot/firstboot.custom.sh before its own partition-resize logic.
# Images are smaller than the target SD card, so the backup GPT initially remains
# at the end of the image. Repair it before upstream tries to grow partition 5.

umask 022
DISK=${FORGEOS_BOOT_DISK:-/dev/mmcblk0}
MAIN=${FORGEOS_MAIN_MOUNT:-/mnt}

log() {
    printf 'ForgeOS firstboot: %s\n' "$*"
}

repair_gpt() {
    [ -b "$DISK" ] || {
        log "disk $DISK is not available; skipping GPT repair"
        return 0
    }
    command -v parted >/dev/null 2>&1 || {
        log "parted is unavailable; skipping GPT repair"
        return 0
    }

    # Newer GNU Parted has --fix specifically for moving a stale backup GPT
    # header to the real end of the disk in script mode. Older MiyooCFW builds
    # do not expose --fix, so answer the two standard GPT repair exceptions on
    # stdin instead. Both paths are harmless when the GPT is already correct.
    if parted --help 2>&1 | grep -q -- '--fix'; then
        if parted --script --fix "$DISK" print >/dev/null 2>&1; then
            log "GPT backup header checked/repaired with parted --fix"
            return 0
        fi
        log "parted --fix failed; trying compatibility repair"
    fi

    # Compatibility path for the Parted shipped by older Buildroot snapshots.
    # It may ask first to move the alternate GPT and then to use all disk space.
    if printf 'Fix\nFix\n' | parted "$DISK" print >/dev/null 2>&1; then
        log "GPT backup header checked/repaired with compatibility mode"
        return 0
    fi

    # Do not abort the whole first boot here. Upstream still gets a chance to
    # operate and its normal log will contain the partitioning error if any.
    log "warning: could not repair GPT metadata"
    return 0
}

normalize_forgeos_permissions() {
    # ForgeShell writes startup state below MAIN. Create it while firstboot runs
    # as root and normalize modes so a restrictive inherited umask or stale
    # directory cannot make the launcher fail at mkdir/write time.
    [ -d "$MAIN" ] || return 0
    [ -w "$MAIN" ] || {
        log "$MAIN is not writable yet; deferring ForgeShell state setup"
        return 0
    }

    if mkdir -p "$MAIN/forgeshell/state" "$MAIN/forgeshell/library" 2>/dev/null; then
        chmod 0755 "$MAIN/forgeshell" "$MAIN/forgeshell/state" "$MAIN/forgeshell/library" 2>/dev/null || true
        log "ForgeShell writable directories prepared"
    else
        log "warning: unable to prepare ForgeShell writable directories"
    fi
}

repair_gpt
normalize_forgeos_permissions
sync
exit 0
