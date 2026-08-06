#!/bin/sh
set -u
. "$(dirname "$0")/common.sh"
HOME_DIR=${FORGESHELL_HOME:-/mnt/forgeshell}
CONFIG=$HOME_DIR/config.ini
STATE=$HOME_DIR/state
mkdir -p "$STATE" || exit 1
choice=$(forge_menu "Reset ForgeShell" \
  "Recovery actions preserve ROMs, saves and GMenu2X." \
  last-good "Restore last-known-good settings" \
  defaults "Reset ForgeShell settings" \
  safe "Start ForgeShell in safe mode next boot" \
  gmenu "Force GMenu2X on next boot" 2>/dev/null || true)
case $choice in
  last-good)
    if [ -r "$CONFIG.last-good" ]; then
      cp "$CONFIG.last-good" "$CONFIG.tmp.$$" && mv -f "$CONFIG.tmp.$$" "$CONFIG" &&
        forge_dialog "Reset ForgeShell" "Last-known-good settings were restored."
    else
      forge_dialog "Reset ForgeShell" "No last-known-good settings file is available."
    fi
    ;;
  defaults)
    stamp=$(date +%Y%m%d-%H%M%S 2>/dev/null || printf current)
    [ -r "$CONFIG" ] && cp "$CONFIG" "$STATE/config-before-reset-$stamp.ini" 2>/dev/null || true
    cat > "$CONFIG.tmp.$$" <<'CFG'
# ForgeShell settings reset by the recovery tool.
launcher_mode=gmenu2x
scan_on_start=0
large_text=0
high_contrast=0
scan_budget=12
onboarding_complete=0
safe_mode_next_boot=0
metadata_enabled=1
show_recovery_hint=1
CFG
    if mv -f "$CONFIG.tmp.$$" "$CONFIG"; then
      rm -f "$STATE/library-cache.tsv" "$STATE/boot-failures"
      forge_dialog "Reset ForgeShell" "Settings were reset. GMenu2X remains the boot launcher and setup will run again."
    else
      rm -f "$CONFIG.tmp.$$"
      forge_dialog "Reset ForgeShell" "The settings file could not be reset."
      exit 1
    fi
    ;;
  safe)
    : > "$STATE/force-safe-mode" && forge_dialog "Reset ForgeShell" "Safe mode is armed for the next ForgeShell boot."
    ;;
  gmenu)
    : > "$STATE/force-gmenu2x" && forge_dialog "Reset ForgeShell" "GMenu2X will be forced on the next boot."
    ;;
  *) exit 0 ;;
esac
