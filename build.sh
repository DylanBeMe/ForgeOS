#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(<"$ROOT_DIR/VERSION")"
FORGE_TARGET="${FORGE_TARGET:-q90}"
PLATFORM_DIR="$ROOT_DIR/platforms/$FORGE_TARGET"
[[ -r "$PLATFORM_DIR/build.env" ]] || { echo "Error: unknown build target: $FORGE_TARGET" >&2; exit 1; }
# shellcheck source=/dev/null
source "$PLATFORM_DIR/build.env"
[[ "${FORGE_BUILD_BACKEND:-}" == miyoocfw-buildroot ]] || {
  echo "Error: build.sh only handles the miyoocfw-buildroot backend" >&2; exit 1;
}
WORKDIR="${WORKDIR:-$ROOT_DIR/build/${FORGE_TARGET}-buildroot}"
DOWNLOAD_DIR="${DOWNLOAD_DIR:-$ROOT_DIR/build/downloads}"
UPSTREAM_URL="${UPSTREAM_URL:-$FORGE_UPSTREAM_URL}"
UPSTREAM_REF="${UPSTREAM_REF:-$FORGE_UPSTREAM_REF}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
OWNED_MARKER="$WORKDIR/.forgeos-managed-worktree"
OVERLAY_MARKER="$WORKDIR/.forgeos-overlay-hash"

fail() {
  echo "Error: $*" >&2
  exit 1
}

[[ "$(uname -s)" == Linux ]] || fail "full-image builds require Linux"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || fail "JOBS must be a positive integer"

for tool in git make rsync sha256sum install find sort xargs awk python3; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done

mkdir -p "$(dirname "$WORKDIR")" "$DOWNLOAD_DIR" "$ROOT_DIR/dist" "$ROOT_DIR/build"

if [[ ! -d "$WORKDIR/.git" ]]; then
  [[ ! -e "$WORKDIR" || -z "$(find "$WORKDIR" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]] \
    || fail "WORKDIR exists and is not an empty Git worktree: $WORKDIR"
  mkdir -p "$WORKDIR"
  git -C "$WORKDIR" init -q
  git -C "$WORKDIR" remote add origin "$UPSTREAM_URL"
  : > "$OWNED_MARKER"
elif [[ ! -f "$OWNED_MARKER" ]]; then
  fail "refusing to reset an unmanaged worktree; use an empty WORKDIR dedicated to ForgeOS"
fi

current_remote=$(git -C "$WORKDIR" remote get-url origin 2>/dev/null || true)
[[ "$current_remote" == "$UPSTREAM_URL" ]] || fail "WORKDIR origin differs from UPSTREAM_URL"

previous_ref=$(cat "$OWNED_MARKER" 2>/dev/null || true)
previous_overlay_hash=$(cat "$OVERLAY_MARKER" 2>/dev/null || true)
overlay_hash=$(
  cd "$ROOT_DIR"
  {
    find overlay -printf '%y %m %p %l\n' | LC_ALL=C sort
    find overlay -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
    find src/forgeshell -type f ! -name '*.o' ! -path 'src/forgeshell/src/forgeshell' \
      -printf '%m %p\n' | LC_ALL=C sort
    find src/forgeshell -type f ! -name '*.o' ! -path 'src/forgeshell/src/forgeshell' \
      -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
    find "platforms/$FORGE_TARGET" -printf '%y %m %p %l\n' | LC_ALL=C sort
    find "platforms/$FORGE_TARGET" -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
    sha256sum VERSION build.sh tools/generate-emulator-manifest.py tools/forge-build tools/validate-platform.py
  } | sha256sum | awk '{print $1}'
)

echo "Fetching pinned upstream revision: $UPSTREAM_REF"
git -C "$WORKDIR" fetch --depth 1 origin "$UPSTREAM_REF"
git -C "$WORKDIR" checkout --detach -f FETCH_HEAD
git -C "$WORKDIR" clean -fdx \
  -e .forgeos-managed-worktree \
  -e .forgeos-overlay-hash \
  -e output/ -e dl/

resolved_ref=$(git -C "$WORKDIR" rev-parse HEAD)
if [[ -n "$previous_ref" && "$previous_ref" != "$resolved_ref" ]]; then
  echo "Upstream revision changed; clearing the Buildroot output tree."
  rm -rf "$WORKDIR/output"
elif [[ -d "$WORKDIR/output" && -z "$previous_overlay_hash" ]]; then
  echo "Enabling ForgeOS input tracking; clearing the previous Buildroot output tree."
  rm -rf "$WORKDIR/output"
elif [[ -n "$previous_overlay_hash" && "$previous_overlay_hash" != "$overlay_hash" ]]; then
  echo "ForgeOS image inputs changed; clearing the Buildroot output tree."
  rm -rf "$WORKDIR/output"
fi
printf '%s\n' "$resolved_ref" > "$OWNED_MARKER"
printf '%s\n' "$overlay_hash" > "$OVERLAY_MARKER"

# Buildroot 2022.02.x generated its -br1 VCS archives with GNU tar <= 1.34.
# GNU tar 1.35 changed the devmajor/devminor header representation, which
# changes archive hashes even though the extracted source tree is identical.
# Backport Buildroot's compatibility check so it builds and uses host-tar 1.34.
tar_check="$WORKDIR/support/dependencies/check-host-tar.sh"
if [[ -f "$tar_check" ]]; then
  python3 - "$tar_check" <<'PY_TAR_CHECK'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
if "major_max=1\nminor_max=34" not in text:
    old = """major_min=1
minor_min=27

if [ $major -lt $major_min ]; then
"""
    new = """major_min=1
minor_min=27

# Maximal version = 1.34 (1.35 changed devmajor/devminor for files)
major_max=1
minor_max=34

if [ $major -lt $major_min -o $major -gt $major_max ]; then
"""
    if old not in text:
        raise SystemExit("could not patch Buildroot tar version check")
    text = text.replace(old, new, 1)

    old = """if [ $major -eq $major_min -a $minor -lt $minor_min ]; then
	# echo nothing: no suitable tar found
	exit 1
fi

# valid
"""
    new = """if [ $major -eq $major_min -a $minor -lt $minor_min ]; then
	# echo nothing: no suitable tar found
	exit 1
fi

if [ $major -eq $major_max -a $minor -gt $minor_max ]; then
	# echo nothing: no suitable tar found
	exit 1
fi

# valid
"""
    if old not in text:
        raise SystemExit("could not finish patching Buildroot tar version check")
    path.write_text(text.replace(old, new, 1))
PY_TAR_CHECK
fi

source_date_epoch=$(git -C "$WORKDIR" show -s --format=%ct HEAD)
export SOURCE_DATE_EPOCH="$source_date_epoch" TZ=UTC LC_ALL=C

manifest_tmp="$ROOT_DIR/build/forgeos-emulators.txt"
python3 "$ROOT_DIR/tools/generate-emulator-manifest.py" \
  --tree "$WORKDIR" \
  --forge-version "$VERSION" \
  --upstream-ref "$resolved_ref" \
  --output "$manifest_tmp"

# Inject ForgeShell as a normal Buildroot package without carrying an upstream fork.
install -d "$WORKDIR/$FORGE_PACKAGE_DIR/forgeshell"
rsync -a --delete --exclude='*.o' --exclude='/src/forgeshell' \
  "$ROOT_DIR/src/forgeshell/" "$WORKDIR/$FORGE_PACKAGE_DIR/forgeshell/"
config_in="$WORKDIR/$FORGE_PACKAGE_DIR/Config.in"
package_source="${FORGE_PACKAGE_DIR}/forgeshell/Config.in"
if ! grep -q "$package_source" "$config_in"; then
  python3 - "$config_in" "$FORGE_PACKAGE_ANCHOR" "$package_source" <<'PY_CONFIG'
from pathlib import Path
import sys
path = Path(sys.argv[1])
anchor = bytes(sys.argv[2], "utf-8").decode("unicode_escape")
source = f'\tsource "{sys.argv[3]}"\n'
text = path.read_text()
if anchor not in text:
    raise SystemExit("could not locate platform package menu anchor")
path.write_text(text.replace(anchor, anchor + source, 1))
PY_CONFIG
fi
config_file="$WORKDIR/$FORGE_CONFIG_FILE"
if ! grep -q '^BR2_PACKAGE_FORGESHELL=y$' "$config_file"; then
  printf '\nBR2_PACKAGE_FORGESHELL=y\n' >> "$config_file"
fi

# Copy only ForgeOS-owned paths so removed files do not linger between builds.
board_dir="$WORKDIR/$FORGE_BOARD_DIR"
overlay_dir="$ROOT_DIR/$FORGE_OVERLAY_DIR"
install -d \
  "$board_dir/boot" \
  "$board_dir/main/apps/forge-tools" \
  "$board_dir/main/gmenu2x/sections/ForgeOS" \
  "$board_dir/main/forgeshell"
rsync -a --delete "$overlay_dir/main/apps/forge-tools/" \
  "$board_dir/main/apps/forge-tools/"
rsync -a --delete "$overlay_dir/main/gmenu2x/sections/ForgeOS/" \
  "$board_dir/main/gmenu2x/sections/ForgeOS/"
rsync -a --delete "$overlay_dir/main/forgeshell/" \
  "$board_dir/main/forgeshell/"
# Keep the runtime input labels/capabilities in lockstep with the selected target.
install -m 0644 "$PLATFORM_DIR/platform.ini" "$board_dir/main/forgeshell/device.ini"
# Zip extraction and host umasks can leave authored directories setgid/group-writable.
# Normalize the modes that enter the firmware instead of relying on host metadata.
for dir in \
  "$board_dir/main/apps/forge-tools" \
  "$board_dir/main/gmenu2x/sections/ForgeOS" \
  "$board_dir/main/forgeshell"; do
  find "$dir" -type d -exec chmod g-s {} + -exec chmod 0755 {} +
done
find "$board_dir/main/apps/forge-tools" -type f -exec chmod 0644 {} +
find "$board_dir/main/apps/forge-tools" -type f -name '*.sh' -exec chmod 0755 {} +
find "$board_dir/main/gmenu2x/sections/ForgeOS" -type f -exec chmod 0644 {} +
find "$board_dir/main/forgeshell" -type f -exec chmod 0644 {} +
find "$board_dir/main/forgeshell" -type f -name '*.sh' -exec chmod 0755 {} +
install -m 0755 "$overlay_dir/main/autoexec.sh" "$board_dir/main/autoexec.sh"
for file in console.cfg options.cfg miyoo-splash.bmp; do
  install -m 0644 "$overlay_dir/boot/$file" "$board_dir/boot/$file"
done
install -m 0755 "$overlay_dir/boot/firstboot.custom.sh" "$board_dir/boot/firstboot.custom.sh"
# The upstream post-image script regenerates miyoo-splash.bmp and otherwise
# overwrites our authored BOOT splash. Feed it the ForgeOS source image instead.
install -m 0644 "$ROOT_DIR/assets/forgeos-splash.png" "$board_dir/miyoo-splash.png"

# Keep user-facing first-boot UI branded as ForgeOS while preserving technical
# hardware identifiers and package paths that are part of the upstream ABI.
firstboot_script="$board_dir/boot/firstboot"
[[ -f "$firstboot_script" ]] || fail "upstream firstboot script is missing"
python3 - "$firstboot_script" <<'PY_FIRSTBOOT'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
changed = text.replace('MiyooCFW 2.0', 'ForgeOS Setup')
changed = changed.replace(r'\ZbMiyooCFW\Zn', r'\ZbForgeOS\Zn')
if changed == text:
    raise SystemExit('could not locate upstream first-boot branding anchors')
path.write_text(changed)
PY_FIRSTBOOT

# Upstream genimage copies board/miyoo/boot and then recreates the splash with
# a MiyooCFW version annotation. Explicitly install our hook into the staging
# BOOT tree and overwrite that generated splash with an unannotated ForgeOS BMP.
genimage_script="$WORKDIR/board/miyoo/scripts/genimage.sh"
[[ -f "$genimage_script" ]] || fail "upstream genimage script is missing"
python3 - "$genimage_script" <<'PY_GENIMAGE'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
copy_anchor = 'cp -r board/miyoo/boot "${BINARIES_DIR}"'
if copy_anchor not in text:
    raise SystemExit('could not locate upstream BOOT staging copy')
if 'ForgeOS: guarantee custom firstboot hook' not in text:
    text = text.replace(
        copy_anchor,
        copy_anchor + '\n# ForgeOS: guarantee custom firstboot hook reaches the FAT image\n'
        'install -m 0755 board/miyoo/boot/firstboot.custom.sh "${BINARIES_DIR}/boot/firstboot.custom.sh"',
        1,
    )
splash_anchor = 'BMP3:"${BINARIES_DIR}"/boot/miyoo-splash.bmp'
pos = text.find(splash_anchor)
if pos < 0:
    raise SystemExit('could not locate upstream generated splash command')
line_end = text.find('\n', pos)
if line_end < 0:
    line_end = len(text)
marker = '# ForgeOS: overwrite version-stamped splash with unannotated artwork'
if marker not in text:
    command = (
        '\n' + marker + '\n'
        'convert board/miyoo/miyoo-splash.png -type Palette -colors 224 -depth 8 '
        '-compress none -verbose BMP3:"${BINARIES_DIR}"/boot/miyoo-splash.bmp'
    )
    text = text[:line_end] + command + text[line_end:]
path.write_text(text)
PY_GENIMAGE

for file in .backlight.conf .volume.conf logo.png logobg.png logo.wav; do
  install -m 0644 "$overlay_dir/main/$file" "$board_dir/main/$file"
done
printf 'ForgeOS %s %s\nTarget: %s\nUpstream Buildroot revision: %s\nEmulator manifest: /mnt/forgeos-emulators.txt\n' \
  "$FORGE_DEVICE_NAME" "$VERSION" "$FORGE_DEVICE_ID" "$resolved_ref" > "$board_dir/main/forgeos-version.txt"
install -m 0644 "$manifest_tmp" "$board_dir/main/forgeos-emulators.txt"

pushd "$WORKDIR" >/dev/null
make BR2_DL_DIR="$DOWNLOAD_DIR" "$FORGE_DEFCONFIG_TARGET"
make BR2_DL_DIR="$DOWNLOAD_DIR" -j "$JOBS"
popd >/dev/null

# Refuse to ship an image if the exact first-boot repair hook that prevents the
# stale-backup-GPT failure was dropped by upstream post-image staging.
generated_boot="$WORKDIR/output/images/boot"
[[ -x "$generated_boot/firstboot.custom.sh" ]] || \
  fail "generated BOOT image is missing firstboot.custom.sh"
[[ -f "$generated_boot/firstboot" ]] || fail "generated BOOT image is missing firstboot"
if grep -q 'MiyooCFW' "$generated_boot/firstboot"; then
  fail "generated firstboot still contains user-facing MiyooCFW branding"
fi

mapfile -t images < <(compgen -G "$WORKDIR/$FORGE_IMAGE_GLOB" || true)
(( ${#images[@]} == 1 )) || fail "expected exactly one target image, found ${#images[@]}"

output_name="forgeos-$FORGE_DEVICE_ID-$VERSION.img"
install -m 0644 "${images[0]}" "$ROOT_DIR/dist/$output_name"
printf '%s\n' "$resolved_ref" > "$ROOT_DIR/dist/$output_name.upstream-ref"
install -m 0644 "$manifest_tmp" "$ROOT_DIR/dist/$output_name.emulators.txt"
(
  cd "$ROOT_DIR/dist"
  sha256sum "$output_name" > "$output_name.sha256"
)
echo "Created: $ROOT_DIR/dist/$output_name"
echo "Emulator manifest: $ROOT_DIR/dist/$output_name.emulators.txt"
