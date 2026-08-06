#!/bin/sh
set -u
# shellcheck source=overlay/board/miyoo/main/apps/forge-tools/common.sh
. "$(dirname "$0")/common.sh"

main=$(forge_main_mount 2>/dev/null || true)
if [ -z "$main" ]; then
  forge_dialog "Emulator Overview" "The main data partition is not mounted or is not readable."
  exit 1
fi

count_matching_files() {
  count_dir=$1
  count_pattern=$2
  count=0
  [ -d "$count_dir" ] || {
    printf '0\n'
    return 0
  }
  for count_file in "$count_dir"/$count_pattern; do
    [ -f "$count_file" ] || continue
    count=$((count + 1))
  done
  printf '%s\n' "$count"
}

count_subdirs() {
  count_dir=$1
  count=0
  [ -d "$count_dir" ] || {
    printf '0\n'
    return 0
  }
  for count_item in "$count_dir"/*; do
    [ -d "$count_item" ] || continue
    count=$((count + 1))
  done
  printf '%s\n' "$count"
}

core_dir=""
for candidate in \
  "$main/retroarch/cores" \
  "$main/.retroarch/cores" \
  /usr/lib/libretro; do
  if [ -d "$candidate" ]; then
    core_dir=$candidate
    break
  fi
done

installed_cores=0
[ -n "$core_dir" ] && installed_cores=$(count_matching_files "$core_dir" '*_libretro.so')
emus_dir="$main/emus"
standalone_dirs=$(count_subdirs "$emus_dir")
manifest="$main/forgeos-emulators.txt"

if [ -r "$manifest" ]; then
  selected=$(sed -n 's/^Selected packages: //p' "$manifest" | head -n 1)
  unresolved=$(sed -n 's/^Version metadata unresolved: //p' "$manifest" | head -n 1)
  [ -n "$selected" ] || selected="unknown"
  [ -n "$unresolved" ] || unresolved="unknown"
  source_versions=$(cat "$manifest")
  status="This full-image build includes the current Q90 compatibility snapshot."
else
  selected="not recorded"
  unresolved="not recorded"
  source_versions="No build-time emulator manifest is present.\n\nThe update-safe SD overlay changes ForgeOS tools and artwork only; it deliberately does not replace emulator binaries. Build and flash the full ForgeOS image to receive the updated emulator set."
  status="Overlay installation detected; emulator binaries are unchanged."
fi

[ -n "$core_dir" ] || core_dir="not found"
[ -d "$emus_dir" ] || emus_dir="not found"

report=$(printf 'STATUS\n%s\n\nINSTALLED FILES\nLibretro cores: %s\nCore folder: %s\nStandalone folders: %s\nStandalone root: %s\n\nBUILD METADATA\nSelected packages: %s\nUnresolved versions: %s\n\nSOURCE SNAPSHOT\n%s' \
  "$status" "$installed_cores" "$core_dir" "$standalone_dirs" "$emus_dir" \
  "$selected" "$unresolved" "$source_versions")
forge_report "Emulator Overview" "$report"
