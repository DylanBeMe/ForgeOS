#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

roms=$(forge_rom_mount 2>/dev/null || true)
if [ -z "$roms" ]; then
  forge_dialog "ROM Library Setup" "A writable ROM library location could not be found or created."
  exit 1
fi

message=$(printf 'Create optional library folders in:\n\n%s\n\nThis does not add games, BIOS files, or change emulator launcher paths.' "$roms")
forge_confirm "ROM Library Setup" "$message" || exit 0

folders="Arcade Atari2600 GB GBC GBA GameGear Genesis MasterSystem NES PCE PS1 SNES WonderSwan ports"
created=0
existing=0
failed=""
for folder in $folders; do
  if [ -d "$roms/$folder" ]; then
    existing=$((existing + 1))
  elif mkdir -p "$roms/$folder"; then
    created=$((created + 1))
  else
    failed="$failed $folder"
  fi
done

readme="$roms/README-FORGEOS.txt"
if [ ! -e "$readme" ]; then
  if ! cat > "$readme" <<'TXT'
ForgeOS ROM Library

Add only game backups that you are legally entitled to use.
ForgeOS does not include ROMs or BIOS files.
These folders are optional and do not automatically change emulator paths.
TXT
  then
    rm -f "$readme"
    failed="$failed README-FORGEOS.txt"
  fi
elif [ ! -f "$readme" ]; then
  failed="$failed README-FORGEOS.txt"
fi

if [ -n "$failed" ]; then
  forge_dialog "ROM Library Setup" "Some folders could not be created:$failed\n\nCreated: $created\nAlready present: $existing"
  exit 1
fi

forge_dialog "ROM Library Setup" "Library folders are ready.\n\nCreated: $created\nAlready present: $existing\n\nLocation: $roms"
