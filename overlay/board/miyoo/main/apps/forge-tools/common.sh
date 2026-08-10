#!/bin/sh

# Shared UI and storage helpers for every ForgeOS tool.
# Keep this file POSIX-sh compatible: MiyooCFW uses a small userspace.

FORGE_BRAND=${FORGE_BRAND:-"ForgeOS Q90"}
FORGE_DIALOG_BIN=${FORGE_DIALOG_BIN:-dialog}
FORGE_TMPDIR=${FORGE_TMPDIR:-/tmp}
FORGE_UI_HEIGHT=${FORGE_UI_HEIGHT:-18}
FORGE_UI_WIDTH=${FORGE_UI_WIDTH:-54}
FORGE_TOOL_DIR=${FORGE_TOOL_DIR:-$(CDPATH='' cd "$(dirname "$0")" 2>/dev/null && pwd)}

# Match ForgeShell's Midnight Mint palette as closely as the Linux-console
# 16-colour dialog renderer permits. Respect an explicit user DIALOGRC.
if [ -z "${DIALOGRC:-}" ] && [ -r "$FORGE_TOOL_DIR/dialogrc" ]; then
  DIALOGRC=$FORGE_TOOL_DIR/dialogrc
  export DIALOGRC
fi

forge_has_dialog() {
  command -v "$FORGE_DIALOG_BIN" >/dev/null 2>&1
}

forge_tmpfile() {
  if command -v mktemp >/dev/null 2>&1; then
    (umask 077; mktemp "$FORGE_TMPDIR/forgeos.XXXXXX")
    return $?
  fi

  forge_tmp_base="$FORGE_TMPDIR/forgeos.$$"
  forge_tmp_index=0
  while [ "$forge_tmp_index" -lt 10 ]; do
    forge_tmp="$forge_tmp_base.$forge_tmp_index"
    if (set -C; umask 077; : > "$forge_tmp") 2>/dev/null; then
      printf '%s\n' "$forge_tmp"
      return 0
    fi
    forge_tmp_index=$((forge_tmp_index + 1))
  done
  return 1
}

forge_dialog() {
  forge_title=$1
  forge_message=$2
  # Convert only authored \n markers. Do not interpret other backslash escapes that
  # could arrive from a filename or command error.
  forge_message=$(printf '%s' "$forge_message" | sed 's/\\n/\
/g')

  if forge_has_dialog; then
    "$FORGE_DIALOG_BIN" --clear --no-shadow \
      --backtitle "$FORGE_BRAND" --title "$forge_title" \
      --msgbox "$forge_message" "$FORGE_UI_HEIGHT" "$FORGE_UI_WIDTH"
    return $?
  fi

  printf '\n== %s | %s ==\n\n%s\n' "$FORGE_BRAND" "$forge_title" "$forge_message"
  if [ "${FORGE_NO_SLEEP:-0}" != 1 ]; then
    sleep 4
  fi
}

forge_report() {
  forge_title=$1
  forge_content=$2
  forge_report_file=$(forge_tmpfile) || {
    forge_dialog "$forge_title" "Unable to create a temporary report file."
    return 1
  }

  printf '%s\n' "$forge_content" > "$forge_report_file"
  if forge_has_dialog; then
    "$FORGE_DIALOG_BIN" --clear --no-shadow \
      --backtitle "$FORGE_BRAND" --title "$forge_title" \
      --exit-label "Back" --textbox "$forge_report_file" 20 58
    forge_status=$?
  else
    printf '\n== %s | %s ==\n\n' "$FORGE_BRAND" "$forge_title"
    cat "$forge_report_file"
    forge_status=0
  fi

  rm -f "$forge_report_file"
  return "$forge_status"
}

forge_textbox() {
  forge_title=$1
  forge_file=$2
  [ -r "$forge_file" ] || return 1
  if forge_has_dialog; then
    "$FORGE_DIALOG_BIN" --clear --no-shadow \
      --backtitle "$FORGE_BRAND" --title "$forge_title" \
      --exit-label "Back" --textbox "$forge_file" 20 58
    return $?
  fi
  printf '\n== %s | %s ==\n\n' "$FORGE_BRAND" "$forge_title"
  cat "$forge_file"
}

forge_confirm() {
  forge_title=$1
  forge_message=$2

  if forge_has_dialog; then
    "$FORGE_DIALOG_BIN" --clear --no-shadow \
      --backtitle "$FORGE_BRAND" --title "$forge_title" \
      --yes-label "Continue" --no-label "Cancel" \
      --yesno "$forge_message" 15 "$FORGE_UI_WIDTH"
    return $?
  fi

  [ "${FORGE_ASSUME_YES:-0}" = 1 ]
}

# Prints the selected tag. Remaining arguments are dialog tag/item pairs.
forge_menu() {
  forge_title=$1
  forge_prompt=$2
  shift 2

  if [ -n "${FORGE_MENU_CHOICE:-}" ]; then
    printf '%s\n' "$FORGE_MENU_CHOICE"
    return 0
  fi

  forge_has_dialog || return 1
  forge_menu_file=$(forge_tmpfile) || return 1
  "$FORGE_DIALOG_BIN" --clear --no-shadow \
    --backtitle "$FORGE_BRAND" --title "$forge_title" \
    --cancel-label "Back" --menu "$forge_prompt" 18 58 4 \
    "$@" 2> "$forge_menu_file"
  forge_status=$?
  if [ "$forge_status" -eq 0 ]; then
    cat "$forge_menu_file"
  fi
  rm -f "$forge_menu_file"
  return "$forge_status"
}

forge_is_mountpoint() {
  forge_mount_path=$1
  [ -r /proc/mounts ] || return 1
  awk -v wanted="$forge_mount_path" '$2 == wanted { found=1 } END { exit !found }' /proc/mounts
}

forge_main_mount() {
  forge_candidates=${FORGE_MAIN_CANDIDATES:-"/mnt /media/main /main"}
  for forge_path in $forge_candidates; do
    [ -d "$forge_path" ] || continue
    [ -w "$forge_path" ] || continue
    if [ "${FORGE_ALLOW_UNMOUNTED:-0}" = 1 ] || \
       forge_is_mountpoint "$forge_path" || \
       [ -d "$forge_path/gmenu2x" ] || \
       [ -f "$forge_path/forgeos-version.txt" ]; then
      printf '%s\n' "$forge_path"
      return 0
    fi
  done
  return 1
}

forge_rom_mount() {
  forge_main=$(forge_main_mount 2>/dev/null || true)
  [ -n "$forge_main" ] || return 1

  forge_rom_candidates=${FORGE_ROM_CANDIDATES:-"/mnt/roms /media/roms /roms"}
  for forge_path in $forge_rom_candidates; do
    [ -d "$forge_path" ] || continue
    [ -w "$forge_path" ] || continue

    # Upstream creates /mnt/roms as a symlink to /roms. Do not treat that link
    # as a MAIN-local directory merely because its spelling starts with /mnt;
    # when the ROMS partition is absent it resolves into the read-only rootfs.
    if [ -L "$forge_path" ]; then
      forge_target=$(readlink -f "$forge_path" 2>/dev/null || true)
      if [ -n "$forge_target" ] && { [ "${FORGE_ALLOW_UNMOUNTED:-0}" = 1 ] || forge_is_mountpoint "$forge_target"; }; then
        printf '%s\n' "$forge_path"
        return 0
      fi
      continue
    fi

    case "$forge_path/" in
      "$forge_main"/*) printf '%s\n' "$forge_path"; return 0 ;;
    esac
    if [ "${FORGE_ALLOW_UNMOUNTED:-0}" = 1 ] || forge_is_mountpoint "$forge_path"; then
      printf '%s\n' "$forge_path"
      return 0
    fi
  done

  # Do not turn the platform's /mnt/roms -> /roms link into a fallback when
  # /roms is not mounted. Following it would write into the root filesystem.
  [ -L "$forge_main/roms" ] && return 1
  mkdir -p "$forge_main/roms" || return 1
  [ -w "$forge_main/roms" ] || return 1
  printf '%s\n' "$forge_main/roms"
}

forge_numeric() {
  case ${1:-} in
    ''|*[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}
