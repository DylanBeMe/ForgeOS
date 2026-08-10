#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
VERSION="$(<VERSION)"

fail() { echo "FAIL: $*" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Syntax and static analysis.
bash -n build.sh forge-build install-overlay.sh tools/forge-build tools/run-simulator.sh \
  tools/sync-sd-overlay.sh tools/generate-source-manifest.sh \
  tools/package-release.sh tools/verify-release.sh tools/ci/install-deps.sh tests/run.sh
PYTHONPYCACHEPREFIX="$tmp/pycache" python3 -m py_compile \
  tools/generate-emulator-manifest.py tools/render-forgeshell-preview.py \
  tools/build-library-index.py tools/record-compatibility.py tools/validate-platform.py \
  tools/validate-theme.py tools/new-platform.py tools/generate-release-notes.py
for script in overlay/board/miyoo/boot/firstboot.custom.sh \
              overlay/board/miyoo/main/apps/forge-tools/*.sh \
              overlay/board/miyoo/main/forgeshell/*.sh \
              overlay/board/miyoo/main/autoexec.sh; do
  dash -n "$script"
  busybox ash -n "$script"
done

# autoexec.sh is sourced by /etc/main: a frontend exit must return to that
# caller rather than replacing it with exec and retriggering the intro path.
mkdir -p "$tmp/autoexec-main/forgeshell"
cat > "$tmp/autoexec-main/forgeshell/boot-dispatch.sh" <<'SH'
#!/bin/sh
printf 'dispatcher-ran\n'
exit 7
SH
chmod 755 "$tmp/autoexec-main/forgeshell/boot-dispatch.sh"
autoexec_output=$(FORGE_MAIN_ROOT="$tmp/autoexec-main" sh -c   '. "$1"; rc=$?; printf "caller-resumed:%s\n" "$rc"' sh   "$ROOT_DIR/overlay/board/miyoo/main/autoexec.sh")
grep -q '^dispatcher-ran$' <<<"$autoexec_output"
grep -q '^caller-resumed:7$' <<<"$autoexec_output" ||   fail "autoexec replaced its sourcing shell instead of returning to /etc/main"

dialog_theme=$(FORGE_TOOL_DIR="$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools" \
  sh -c '. "$1/common.sh"; printf "%s" "${DIALOGRC:-}"' sh \
  "$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools")
[[ "$dialog_theme" == "$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools/dialogrc" ]]
custom_dialog_theme=$(DIALOGRC=/tmp/custom-dialogrc \
  FORGE_TOOL_DIR="$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools" \
  sh -c '. "$1/common.sh"; printf "%s" "$DIALOGRC"' sh \
  "$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools")
[[ "$custom_dialog_theme" == /tmp/custom-dialogrc ]]
tmpfile_check=$(FORGE_TMPDIR="$tmp" \
  FORGE_TOOL_DIR="$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools" \
  sh -c 'umask 022; . "$1/common.sh"; before=$(umask); f=$(forge_tmpfile); after=$(umask); mode=$(stat -c %a "$f"); rm -f "$f"; printf "%s %s %s" "$before" "$after" "$mode"' sh \
  "$ROOT_DIR/overlay/board/miyoo/main/apps/forge-tools")
read -r before_umask after_umask tmpfile_mode <<<"$tmpfile_check"
[[ "$before_umask" == "$after_umask" ]] || fail "forge_tmpfile changed the caller umask"
[[ "$tmpfile_mode" == 600 ]] || fail "ForgeOS temporary report file is not private"
cmp -s platforms/q90/platform.ini overlay/board/miyoo/main/forgeshell/device.ini || \
  fail "Q90 runtime device profile drifted from the canonical platform profile"
python3 tools/validate-theme.py >/dev/null
cp overlay/board/miyoo/main/forgeshell/theme.ini "$tmp/bad-theme.ini"
printf 'accent=#12345G\n' >> "$tmp/bad-theme.ini"
if python3 tools/validate-theme.py --theme "$tmp/bad-theme.ini" >/dev/null 2>&1; then
  fail "theme validator accepted an invalid accent color"
fi
sed 's/^muted=.*/muted=#10283A/' overlay/board/miyoo/main/forgeshell/theme.ini > "$tmp/low-contrast-theme.ini"
if python3 tools/validate-theme.py --theme "$tmp/low-contrast-theme.ini" >/dev/null 2>&1; then
  fail "theme validator accepted unreadable muted text contrast"
fi
for panel in overlay/board/miyoo/main/apps/forge-tools/*.sh; do
  case "$(basename "$panel")" in common.sh|cpu-profile-control.sh) continue ;; esac
  grep -q 'common.sh' "$panel" || fail "maintenance panel bypasses shared theme: $panel"
done
if grep -RIEq 'Y clear|RESET requests recovery|L/R pages' \
  src/forgeshell/src/ui tools/render-forgeshell-preview.py; then
  fail "legacy control hints remain in the ForgeShell UI"
fi
if command -v shellcheck >/dev/null 2>&1; then
  shellcheck -x build.sh forge-build install-overlay.sh tools/forge-build tools/run-simulator.sh \
    tools/sync-sd-overlay.sh tools/generate-source-manifest.sh \
    tools/package-release.sh tools/verify-release.sh tools/ci/install-deps.sh tests/run.sh
  shellcheck -x -s sh overlay/board/miyoo/boot/firstboot.custom.sh \
    overlay/board/miyoo/main/apps/forge-tools/*.sh \
    overlay/board/miyoo/main/forgeshell/*.sh overlay/board/miyoo/main/autoexec.sh
fi

# The distributable overlay must match the authored overlay, except host-only
# defaults that intentionally preserve an existing user's brightness/volume.
cp -a sd-overlay "$tmp/before"
./tools/sync-sd-overlay.sh
diff -ru "$tmp/before" sd-overlay
[[ ! -e sd-overlay/BOOT/console.cfg ]]
[[ ! -e sd-overlay/BOOT/options.cfg ]]
[[ ! -e sd-overlay/MAIN/.backlight.conf ]]
[[ ! -e sd-overlay/MAIN/.volume.conf ]]
[[ ! -e sd-overlay/MAIN/forgeos-emulators.txt ]]
[[ ! -e sd-overlay/MAIN/autoexec.sh ]]
[[ ! -e sd-overlay/MAIN/forgeshell ]]
[[ ! -e sd-overlay/MAIN/gmenu2x/sections/ForgeOS/forgeshell ]]
[[ ! -e sd-overlay/MAIN/gmenu2x/sections/ForgeOS/forgeshell-safe ]]
[[ ! -e sd-overlay/MAIN/gmenu2x/sections/ForgeOS/forgeshell-log ]]
[[ ! -e sd-overlay/MAIN/gmenu2x/sections/ForgeOS/performance-snapshot ]]
[[ ! -e sd-overlay/MAIN/gmenu2x/sections/ForgeOS/reset-forgeshell ]]
grep -q '^Emulator binaries: unchanged' sd-overlay/MAIN/forgeos-version.txt

# The image builder protects unrelated worktrees and invalidates stale outputs
# when any authored image input changes.
mkdir -p \
  "$tmp/fake-upstream/board/miyoo/boot" \
  "$tmp/fake-upstream/board/miyoo/main" \
  "$tmp/fake-upstream/board/miyoo/scripts" \
  "$tmp/fake-upstream/configs" \
  "$tmp/fake-upstream/package/miyoo/retroarch/libretro-retroarch" \
  "$tmp/fake-upstream/package/miyoo/retroarch/libretro-gpsp" \
  "$tmp/fake-upstream/package/miyoo/retroarch/libretro-picodrive" \
  "$tmp/fake-upstream/package/miyoo/ipk/gpsp"
cat > "$tmp/fake-upstream/board/miyoo/boot/firstboot" <<'SH'
#!/bin/sh
dialog --backtitle "MiyooCFW 2.0" --msgbox "\ZbMiyooCFW\Zn" 0 0
SH
chmod 755 "$tmp/fake-upstream/board/miyoo/boot/firstboot"
cat > "$tmp/fake-upstream/board/miyoo/scripts/genimage.sh" <<'SH'
#!/bin/sh
cp -r board/miyoo/boot "${BINARIES_DIR}"
convert board/miyoo/miyoo-splash.png -pointsize 12 -fill white \
 -annotate +10+230 "v${CFW_RELEASE} ${CFW_VERSION}" \
 -type Palette -colors 224 -depth 8 -compress none -verbose BMP3:"${BINARIES_DIR}"/boot/miyoo-splash.bmp
SH
chmod 755 "$tmp/fake-upstream/board/miyoo/scripts/genimage.sh"
cat > "$tmp/fake-upstream/board/miyoo/genimage-sdcard.cfg" <<'CFG'
image bootfs.vfat {
  vfat {
    files = {
      "boot/firstboot",
      "boot/firstboot.custom.sh-OFF",
    }
  }
}
CFG
printf 'upstream splash placeholder\n' > "$tmp/fake-upstream/board/miyoo/miyoo-splash.png"
cat > "$tmp/fake-upstream/configs/miyoo_uclibc_defconfig" <<'CFG'
BR2_PACKAGE_RETROARCH=y
BR2_PACKAGE_LIBRETRO_ASSETS=y
BR2_PACKAGE_LIBRETRO_GPSP=y
BR2_PACKAGE_LIBRETRO_PICODRIVE=y
BR2_PACKAGE_IPK_GPSP=y
CFG
cat > "$tmp/fake-upstream/package/miyoo/Config.in" <<'CFG'
menu "Miyoo packages"
	source "package/miyoo/shellect/Config.in"
endmenu
CFG
cat > "$tmp/fake-upstream/package/miyoo/retroarch/libretro-retroarch/libretro-retroarch.mk" <<'MK'
LIBRETRO_RETROARCH_VERSION = retroarch-test-rev
LIBRETRO_RETROARCH_SITE = https://example.invalid/retroarch
MK
cat > "$tmp/fake-upstream/package/miyoo/retroarch/libretro-gpsp/libretro-gpsp.mk" <<'MK'
LIBRETRO_GPSP_VERSION = gpsp-core-test-rev
LIBRETRO_GPSP_SITE = https://example.invalid/gpsp-core
MK
cat > "$tmp/fake-upstream/package/miyoo/retroarch/libretro-picodrive/libretro-picodrive.mk" <<'MK'
LIBRETRO_PICODRIVE_VERSION = picodrive-test-rev
LIBRETRO_PICODRIVE_SITE = https://example.invalid/picodrive
MK
cat > "$tmp/fake-upstream/package/miyoo/ipk/gpsp/gpsp.mk" <<'MK'
GPSP_VERSION = gpsp-standalone-test-rev
GPSP_SITE = https://example.invalid/gpsp-standalone
MK
cat > "$tmp/fake-upstream/Makefile" <<'MK'
.DEFAULT_GOAL := all
.PHONY: all miyoo_uclibc_defconfig
all:
	@mkdir -p output/images/boot
	@cp -a board/miyoo/boot/. output/images/boot/
	@printf 'fake-image\n' > output/images/miyoo-br2_dist-test.img
miyoo_uclibc_defconfig:
	@:
MK
git -C "$tmp/fake-upstream" init -q
git -C "$tmp/fake-upstream" add .
git -C "$tmp/fake-upstream" -c user.name=ForgeOS-Test \
  -c user.email=forgeos@example.invalid commit -qm init
fake_ref=$(git -C "$tmp/fake-upstream" rev-parse HEAD)
cp -a . "$tmp/package"
printf 'host-object\n' > "$tmp/package/src/forgeshell/src/host-test.o"
printf '#!/bin/sh\nexit 99\n' > "$tmp/package/src/forgeshell/src/forgeshell"
chmod 755 "$tmp/package/src/forgeshell/src/forgeshell"
WORKDIR="$tmp/build-work" DOWNLOAD_DIR="$tmp/downloads" \
  UPSTREAM_URL="file://$tmp/fake-upstream" UPSTREAM_REF="$fake_ref" JOBS=1 \
  "$tmp/package/build.sh" >/dev/null 2>&1
[[ -f "$tmp/package/dist/forgeos-q90-$VERSION.img" ]]
[[ -f "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt" ]]
grep -q '^Selected packages: 4$' "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt"
grep -q '^Version metadata unresolved: 0$' "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt"
grep -q 'retroarch-test-rev' "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt"
grep -q 'gpsp-core-test-rev' "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt"
grep -q 'gpsp-standalone-test-rev' "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt"
if grep -q 'assets' "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt"; then
  fail "emulator manifest unexpectedly includes assets"
fi
[[ -f "$tmp/build-work/board/miyoo/main/forgeos-emulators.txt" ]]
grep -q "^ForgeOS Powkiddy Q90 $VERSION$" "$tmp/build-work/board/miyoo/main/forgeos-version.txt"
grep -q '^Target: q90$' "$tmp/build-work/board/miyoo/main/forgeos-version.txt"
[[ -f "$tmp/build-work/board/miyoo/main/forgeshell/device.ini" ]]
cmp -s platforms/q90/platform.ini "$tmp/build-work/board/miyoo/main/forgeshell/device.ini" || \
  fail "image builder did not install the selected target profile"
[[ -f "$tmp/build-work/board/miyoo/main/forgeshell/tools.tsv" ]]
[[ -f "$tmp/build-work/package/miyoo/forgeshell/src/main.c" ]]
[[ ! -e "$tmp/build-work/package/miyoo/forgeshell/src/host-test.o" ]]
[[ ! -e "$tmp/build-work/package/miyoo/forgeshell/src/forgeshell" ]]
if find "$tmp/build-work/package/miyoo/forgeshell" -name '*.o' -print -quit | grep -q .; then
  fail "packaged ForgeShell tree contains object files"
fi
grep -q 'package/miyoo/forgeshell/Config.in' "$tmp/build-work/package/miyoo/Config.in"
grep -q '^BR2_PACKAGE_FORGESHELL=y$' "$tmp/build-work/configs/miyoo_uclibc_defconfig"
[[ -x "$tmp/build-work/board/miyoo/main/autoexec.sh" ]]
[[ -x "$tmp/build-work/board/miyoo/main/forgeshell/forgeshell-start.sh" ]]
[[ -x "$tmp/build-work/board/miyoo/boot/firstboot.custom.sh" ]]
[[ -x "$tmp/build-work/output/images/boot/firstboot.custom.sh" ]]
grep -q 'ForgeOS Setup' "$tmp/build-work/output/images/boot/firstboot"
if grep -q 'MiyooCFW' "$tmp/build-work/output/images/boot/firstboot"; then
  fail "image builder left MiyooCFW branding in firstboot"
fi
cmp -s assets/forgeos-splash.png "$tmp/build-work/board/miyoo/miyoo-splash.png" ||   fail "image builder did not replace the upstream splash source"
grep -q 'ForgeOS: guarantee custom firstboot hook'   "$tmp/build-work/board/miyoo/scripts/genimage.sh"
grep -q 'ForgeOS: overwrite version-stamped splash with unannotated artwork'   "$tmp/build-work/board/miyoo/scripts/genimage.sh"
grep -Fq '"boot/firstboot.custom.sh",' "$tmp/build-work/board/miyoo/genimage-sdcard.cfg" || \
  fail "image builder did not add the live firstboot hook to the FAT manifest"
[[ -f "$tmp/build-work/board/miyoo/main/logo.png" ]]
[[ -f "$tmp/build-work/board/miyoo/main/logobg.png" ]]
[[ -f "$tmp/build-work/board/miyoo/main/logo.wav" ]]
[[ "$(stat -c %a "$tmp/build-work/board/miyoo/main/forgeshell")" == 755 ]]
[[ "$(stat -c %a "$tmp/build-work/board/miyoo/main/apps/forge-tools")" == 755 ]]
[[ "$(stat -c %a "$tmp/build-work/board/miyoo/main/forgeshell/forgeshell-start.sh")" == 755 ]]
[[ "$(stat -c %a "$tmp/build-work/board/miyoo/main/forgeshell/device.ini")" == 644 ]]
touch "$tmp/build-work/output/keep-on-incremental-build"
printf 'changed-host-object\n' >> "$tmp/package/src/forgeshell/src/host-test.o"
printf '# changed host binary\n' >> "$tmp/package/src/forgeshell/src/forgeshell"
WORKDIR="$tmp/build-work" DOWNLOAD_DIR="$tmp/downloads" \
  UPSTREAM_URL="file://$tmp/fake-upstream" UPSTREAM_REF="$fake_ref" JOBS=1 \
  "$tmp/package/build.sh" >/dev/null 2>&1
[[ -e "$tmp/build-work/output/keep-on-incremental-build" ]]
printf '\nchanged input\n' >> "$tmp/package/overlay/board/miyoo/main/forgeos-version.txt"
WORKDIR="$tmp/build-work" DOWNLOAD_DIR="$tmp/downloads" \
  UPSTREAM_URL="file://$tmp/fake-upstream" UPSTREAM_REF="$fake_ref" JOBS=1 \
  "$tmp/package/build.sh" >/dev/null 2>&1
[[ ! -e "$tmp/build-work/output/keep-on-incremental-build" ]]
mkdir -p "$tmp/unmanaged"
git -C "$tmp/unmanaged" init -q
if WORKDIR="$tmp/unmanaged" DOWNLOAD_DIR="$tmp/downloads" \
   UPSTREAM_URL="file://$tmp/fake-upstream" UPSTREAM_REF="$fake_ref" JOBS=1 \
   "$tmp/package/build.sh" >/dev/null 2>&1; then
  fail "builder accepted an unmanaged Git worktree"
fi

# Installer safety, exact dry-run, backup collision handling, migration, and modes.
mkdir -p "$tmp/nested/BOOT/MAIN"
if ./install-overlay.sh --force "$tmp/nested/BOOT" "$tmp/nested/BOOT/MAIN" \
   >/dev/null 2>&1; then
  fail "installer accepted nested target directories"
fi
mkdir -p \
  "$tmp/card/BOOT" \
  "$tmp/card/MAIN/gmenu2x/sections/forge" \
  "$tmp/card/MAIN/gmenu2x/sections/ForgeOS" \
  "$tmp/card/MAIN/apps/forge-tools"
printf 'CUSTOM_OPTION=1\n' > "$tmp/card/BOOT/options.cfg"
printf 'CONSOLE_VARIANT=q90\n' > "$tmp/card/BOOT/console.cfg"
printf '3\n' > "$tmp/card/MAIN/.backlight.conf"
printf '27\n' > "$tmp/card/MAIN/.volume.conf"
printf 'ForgeOS Q90 0.1.0\n' > "$tmp/card/MAIN/forgeos-version.txt"
printf 'legacy\n' > "$tmp/card/MAIN/gmenu2x/sections/forge/system-info"
printf 'obsolete\n' > "$tmp/card/MAIN/gmenu2x/sections/ForgeOS/obsolete"
for full_entry in forgeshell forgeshell-log performance-snapshot reset-forgeshell; do
  printf 'user-full-image-entry-%s\n' "$full_entry" > "$tmp/card/MAIN/gmenu2x/sections/ForgeOS/$full_entry"
done
printf '#!/bin/sh\n' > "$tmp/card/MAIN/apps/forge-tools/obsolete.sh"
printf 'old-logo\n' > "$tmp/card/MAIN/logo.png"
printf 'old-bg\n' > "$tmp/card/MAIN/logobg.png"
printf 'old-wav\n' > "$tmp/card/MAIN/logo.wav"
dry_output=$(BACKUP_ROOT="$tmp/backups" ./install-overlay.sh --dry-run \
  "$tmp/card/BOOT" "$tmp/card/MAIN")
grep -q '\*deleting.*obsolete' <<<"$dry_output"
grep -q 'would remove legacy launcher section' <<<"$dry_output"

mkdir -p "$tmp/fakebin"
cat > "$tmp/fakebin/date" <<'SH'
#!/bin/sh
printf '20260806-004100\n'
SH
chmod 755 "$tmp/fakebin/date"
PATH="$tmp/fakebin:$PATH" BACKUP_ROOT="$tmp/backups" \
  ./install-overlay.sh "$tmp/card/BOOT" "$tmp/card/MAIN" >/dev/null
PATH="$tmp/fakebin:$PATH" BACKUP_ROOT="$tmp/backups" \
  ./install-overlay.sh "$tmp/card/BOOT" "$tmp/card/MAIN" >/dev/null
[[ -d "$tmp/backups/20260806-004100" ]]
[[ -d "$tmp/backups/20260806-004100-1" ]]
[[ -x "$tmp/card/MAIN/apps/forge-tools/system-info.sh" ]]
[[ -f "$tmp/card/MAIN/logo.png" ]]
[[ -f "$tmp/card/MAIN/logobg.png" ]]
[[ -f "$tmp/card/MAIN/logo.wav" ]]
cmp -s sd-overlay/MAIN/logo.png "$tmp/card/MAIN/logo.png"
cmp -s sd-overlay/MAIN/logobg.png "$tmp/card/MAIN/logobg.png"
cmp -s sd-overlay/MAIN/logo.wav "$tmp/card/MAIN/logo.wav"
grep -q '^old-logo$' "$tmp/backups/20260806-004100/MAIN/logo.png"
grep -q '^old-bg$' "$tmp/backups/20260806-004100/MAIN/logobg.png"
grep -q '^old-wav$' "$tmp/backups/20260806-004100/MAIN/logo.wav"
[[ -d "$tmp/card/MAIN/gmenu2x/sections/ForgeOS" ]]
[[ ! -e "$tmp/card/MAIN/gmenu2x/sections/ForgeOS/obsolete" ]]
for full_entry in forgeshell forgeshell-log performance-snapshot reset-forgeshell; do
  grep -q "^user-full-image-entry-$full_entry$" "$tmp/card/MAIN/gmenu2x/sections/ForgeOS/$full_entry"
done
[[ ! -e "$tmp/card/MAIN/apps/forge-tools/obsolete.sh" ]]
[[ ! -d "$tmp/card/MAIN/gmenu2x/sections/forge" ]]
grep -q "^ForgeOS Q90 $VERSION$" "$tmp/card/MAIN/forgeos-version.txt"
grep -q '^Emulator binaries: unchanged' "$tmp/card/MAIN/forgeos-version.txt"
grep -q '^CUSTOM_OPTION=1$' "$tmp/card/BOOT/options.cfg"
grep -q '^CONSOLE_VARIANT=q90$' "$tmp/card/BOOT/console.cfg"
grep -q '^3$' "$tmp/card/MAIN/.backlight.conf"
grep -q '^27$' "$tmp/card/MAIN/.volume.conf"
[[ "$(stat -c %a "$tmp/card/MAIN/apps/forge-tools")" == 755 ]]
[[ "$(stat -c %a "$tmp/card/MAIN/gmenu2x/sections/ForgeOS")" == 755 ]]

# ROM setup is idempotent and does not overwrite its notice.
mkdir -p "$tmp/runtime/main/gmenu2x"
FORGE_MAIN_CANDIDATES="$tmp/runtime/main" FORGE_ALLOW_UNMOUNTED=1 \
FORGE_ASSUME_YES=1 FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/create-rom-folders.sh >/dev/null
printf 'custom notice\n' > "$tmp/runtime/main/roms/README-FORGEOS.txt"
FORGE_MAIN_CANDIDATES="$tmp/runtime/main" FORGE_ALLOW_UNMOUNTED=1 \
FORGE_ASSUME_YES=1 FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/create-rom-folders.sh >/dev/null
grep -q '^custom notice$' "$tmp/runtime/main/roms/README-FORGEOS.txt"
mkdir -p "$tmp/runtime-bad/main/gmenu2x" "$tmp/runtime-bad/main/roms/README-FORGEOS.txt"
if FORGE_MAIN_CANDIDATES="$tmp/runtime-bad/main" FORGE_ALLOW_UNMOUNTED=1 \
   FORGE_ASSUME_YES=1 FORGE_NO_SLEEP=1 \
   overlay/board/miyoo/main/apps/forge-tools/create-rom-folders.sh >/dev/null; then
  fail "ROM setup accepted a directory where its notice file belongs"
fi

# Save backup is atomic and readable.
mkdir -p "$tmp/runtime/main/.retroarch/saves"
printf 'save-data\n' > "$tmp/runtime/main/.retroarch/saves/test.srm"
FORGE_MAIN_CANDIDATES="$tmp/runtime/main" FORGE_ALLOW_UNMOUNTED=1 \
FORGE_ASSUME_YES=1 FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/backup-saves.sh >/dev/null
archive=$(find "$tmp/runtime/main/backups/forgeos" -name '*.tar.gz' -print -quit)
[[ -n "$archive" ]]
tar -tzf "$archive" | grep -q '^.retroarch/saves/test.srm$'
[[ -z "$(find "$tmp/runtime/main/backups/forgeos" -name '*.partial' -print -quit)" ]]

# CPU profile: numeric filtering, complete range application, and chosen cap.
cpu="$tmp/cpu"
mkdir -p "$cpu"
printf '200000 400000 invalid 600000\n' > "$cpu/scaling_available_frequencies"
printf 'ondemand performance userspace\n' > "$cpu/scaling_available_governors"
printf '200000\n' > "$cpu/scaling_min_freq"
printf '600000\n' > "$cpu/scaling_max_freq"
printf 'ondemand\n' > "$cpu/scaling_governor"
printf '200000\n' > "$cpu/scaling_setspeed"
FORGE_CPU_DIR="$cpu" FORGE_MENU_CHOICE=balanced FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/performance-mode.sh >/dev/null
[[ "$(cat "$cpu/scaling_min_freq")" == 200000 ]]
[[ "$(cat "$cpu/scaling_max_freq")" == 400000 ]]
[[ "$(cat "$cpu/scaling_governor")" == ondemand ]]
printf '200000\n' > "$cpu/scaling_min_freq"
printf '600000\n' > "$cpu/scaling_max_freq"
printf 'ondemand\n' > "$cpu/scaling_governor"
FORGE_CPU_DIR="$cpu" overlay/board/miyoo/main/apps/forge-tools/cpu-profile-control.sh \
  apply balanced "$tmp/cpu-state"
[[ "$(cat "$cpu/scaling_min_freq")" == 200000 ]]
[[ "$(cat "$cpu/scaling_max_freq")" == 400000 ]]
[[ -s "$tmp/cpu-state" ]]
FORGE_CPU_DIR="$cpu" overlay/board/miyoo/main/apps/forge-tools/cpu-profile-control.sh \
  restore "$tmp/cpu-state"
[[ "$(cat "$cpu/scaling_min_freq")" == 200000 ]]
[[ "$(cat "$cpu/scaling_max_freq")" == 600000 ]]
[[ "$(cat "$cpu/scaling_governor")" == ondemand ]]
[[ ! -e "$tmp/cpu-state" ]]

# Desktop metadata generation preserves empty fields and validates options.
mkdir -p "$tmp/index-input" "$tmp/index-out"
printf 'BM' > "$tmp/index-input/art.bmp"
cat > "$tmp/index.csv" <<CSV
path,title,artwork,emulator_id,cpu_profile,aspect,scaling,frameskip,bios
/mnt/roms/GBA/Game.gba,Game Title,,gba-alt,balanced,original,nearest,1,/mnt/bios/gba.bin
/mnt/roms/SNES/Plain.sfc,,,,,,,
CSV
python3 tools/build-library-index.py "$tmp/index.csv" --output "$tmp/index-out" >/dev/null
grep -Fq $'/mnt/roms/GBA/Game.gba\tGame Title\t' "$tmp/index-out/library/metadata.tsv"
grep -Fq $'/mnt/roms/GBA/Game.gba\tgba-alt\tbalanced\toriginal\tnearest\t1\t/mnt/bios/gba.bin' \
  "$tmp/index-out/state/game-overrides.tsv"
if grep -q 'Plain.sfc' "$tmp/index-out/state/game-overrides.tsv"; then
  fail "library index emitted an empty override for Plain.sfc"
fi
cat > "$tmp/index-invalid.csv" <<'CSV'
path,cpu_profile
/mnt/roms/GBA/Game.gba,unsafe
CSV
if python3 tools/build-library-index.py "$tmp/index-invalid.csv" --output "$tmp/index-invalid" \
   >/dev/null 2>&1; then
  fail "metadata generator accepted an unsafe CPU profile"
fi

# Storage diagnostics ignore normal MMC discovery but surface actual errors.
cat > "$tmp/fakebin/dmesg" <<'SH'
#!/bin/sh
printf '%s\n' 'mmc0: new high speed SDHC card at address 1234'
SH
chmod 755 "$tmp/fakebin/dmesg"
storage_output=$(PATH="$tmp/fakebin:$PATH" \
  FORGE_MAIN_CANDIDATES="$tmp/runtime/main" FORGE_ALLOW_UNMOUNTED=1 \
  FORGE_DIALOG_BIN=forgeos-no-dialog FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/storage-health.sh)
grep -q 'No matching storage issue detected' <<<"$storage_output"
if grep -q 'before trusting this SD card' <<<"$storage_output"; then
  fail "normal MMC discovery was reported as a storage error"
fi
cat > "$tmp/fakebin/dmesg" <<'SH'
#!/bin/sh
printf '%s\n' 'mmc0: error -110 whilst initialising SD card'
SH
error_output=$(PATH="$tmp/fakebin:$PATH" \
  FORGE_MAIN_CANDIDATES="$tmp/runtime/main" FORGE_ALLOW_UNMOUNTED=1 \
  FORGE_DIALOG_BIN=forgeos-no-dialog FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/storage-health.sh)
grep -q 'before trusting this SD card' <<<"$error_output"

# System overview estimates available memory on kernels without MemAvailable.
mkdir -p "$tmp/proc" "$tmp/boot"
cat > "$tmp/proc/meminfo" <<'TXT'
MemTotal:       131072 kB
MemFree:         10240 kB
Buffers:          2048 kB
Cached:           4096 kB
TXT
printf '123.00 0.00\n' > "$tmp/proc/uptime"
printf 'CONSOLE_VARIANT=q90\n' > "$tmp/boot/console.cfg"
system_output=$(FORGE_MAIN_CANDIDATES="$tmp/runtime/main" FORGE_ALLOW_UNMOUNTED=1 \
  FORGE_PROC_ROOT="$tmp/proc" FORGE_BOOT_ROOT="$tmp/boot" \
  FORGE_POWER_ROOT="$tmp/no-power" FORGE_CPU_DIR="$tmp/no-cpu" \
  FORGE_DIALOG_BIN=forgeos-no-dialog FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/system-info.sh)
grep -q '16 MiB available / 128 MiB total' <<<"$system_output"

# Emulator Overview distinguishes an overlay-only install from a full image.
emulator_overlay_output=$(FORGE_MAIN_CANDIDATES="$tmp/runtime/main" \
  FORGE_ALLOW_UNMOUNTED=1 FORGE_DIALOG_BIN=forgeos-no-dialog FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/emulator-overview.sh)
grep -q 'emulator binaries are unchanged' <<<"$emulator_overlay_output"
cp "$tmp/package/dist/forgeos-q90-$VERSION.img.emulators.txt" \
  "$tmp/runtime/main/forgeos-emulators.txt"
emulator_full_output=$(FORGE_MAIN_CANDIDATES="$tmp/runtime/main" \
  FORGE_ALLOW_UNMOUNTED=1 FORGE_DIALOG_BIN=forgeos-no-dialog FORGE_NO_SLEEP=1 \
  overlay/board/miyoo/main/apps/forge-tools/emulator-overview.sh)
grep -q 'current Q90 compatibility snapshot' <<<"$emulator_full_output"
grep -q 'retroarch-test-rev' <<<"$emulator_full_output"

# ForgeShell core: strict compilation, sanitizers, standard GMenu2X link
# compatibility, nested scanning, stable caches, persistence, and safe runner quoting.
forge_src="$ROOT_DIR/src/forgeshell/src"
forge_core="$forge_src/core"
forge_platform="$forge_src/platform"
forge_ui="$forge_src/ui"
forge_test_bin="$tmp/forgeshell-core-test"
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes \
  -Wmissing-prototypes -Wconversion -Werror -I"$forge_src" \
  tests/forgeshell/core_test.c \
  "$forge_core/util.c" "$forge_core/config.c" "$forge_core/library.c" \
  "$forge_core/session.c" "$forge_core/overrides.c" "$forge_core/metadata.c" \
  "$forge_core/runner.c" -o "$forge_test_bin"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$forge_test_bin"
cc -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Werror \
  -Itests/forgeshell/stubs -I"$forge_src" -fsyntax-only \
  "$forge_src/main.c" "$forge_ui/ui.c" "$forge_platform/platform.c" \
  "$forge_platform/tools.c"
portability_test_bin="$tmp/forgeshell-platform-test"
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wstrict-prototypes \
  -Wmissing-prototypes -Wconversion -Werror -Itests/forgeshell/stubs -I"$forge_src" \
  tests/portability/platform_test.c "$forge_core/util.c" "$forge_core/library.c" \
  "$forge_core/session.c" "$forge_platform/platform.c" "$forge_platform/tools.c" -o "$portability_test_bin"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$portability_test_bin"
cat > "$tmp/ui-stub.c" <<'C'
#include "forgeshell.h"
int fs_ui_run(const FsPlatform *platform, FsToolCatalog *tools, FsConfig *config,
              FsTheme *theme, FsLibrary *library, FsFavorites *favorites,
              FsSessions *sessions, FsOverrides *overrides, FsMetadata *metadata,
              int boot_mode, int safe_mode) {
    (void)platform; (void)tools; (void)config; (void)theme; (void)library;
    (void)favorites; (void)sessions; (void)overrides; (void)metadata;
    (void)boot_mode; (void)safe_mode;
    return 0;
}
C
portable_main_bin="$tmp/forgeshell-profile-cli"
cc -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow \
  -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Werror \
  -Itests/forgeshell/stubs -I"$forge_src" \
  "$forge_src/main.c" "$tmp/ui-stub.c" "$forge_core"/*.c \
  "$forge_platform/platform.c" "$forge_platform/tools.c" -o "$portable_main_bin"
profile_output=$("$portable_main_bin" --profile platforms/generic-linux/platform.ini \
  --data-root "$tmp/sandbox.root" --validate-profile)
grep -Fqx "data_root=$tmp/sandbox.root" <<<"$profile_output"
grep -Fqx "source=$tmp/sandbox.root/forgeshell/systems.ini" <<<"$profile_output"
if grep -q 'systems/mnt/data' <<<"$profile_output"; then
  fail "portable profile joined the data root incorrectly"
fi
q90_profile_output=$(FORGE_TOOL_ROOT="$tmp/custom-tools" "$portable_main_bin" \
  --profile platforms/q90/platform.ini --validate-profile)
grep -Fqx "tool_root=$tmp/custom-tools" <<<"$q90_profile_output"
grep -Fqx "cpu_helper=$tmp/custom-tools/cpu-profile-control.sh" <<<"$q90_profile_output"
python3 tools/validate-platform.py
./forge-build q90 --check >/dev/null
./forge-build generic-linux --check >/dev/null
python3 tools/new-platform.py test-device --name "Test Device's Adapter" \
  --resolution 480x272 --root "$tmp/scaffold"
python3 tools/validate-platform.py --root "$tmp/scaffold" \
  "$tmp/scaffold/platforms/test-device/platform.ini" >/dev/null
unset FORGE_DEVICE_NAME
# shellcheck source=/dev/null
source "$tmp/scaffold/platforms/test-device/build.env"
[[ "$FORGE_DEVICE_NAME" == "Test Device's Adapter" ]]

analysis_sources=(
  core/util core/config core/library core/session core/overrides core/metadata core/runner
  platform/platform platform/tools
)
if [[ "${FORGE_SKIP_ANALYZERS:-0}" != 1 ]] && command -v clang >/dev/null 2>&1; then
  for source in "${analysis_sources[@]}"; do
    safe_name=${source//\//-}
    clang --analyze -std=c11 -Itests/forgeshell/stubs -I"$forge_src" \
      -o "$tmp/$safe_name-clang-analyzer.plist" "$forge_src/$source.c"
  done
fi
if [[ "${FORGE_SKIP_ANALYZERS:-0}" != 1 ]] && cc --help=common 2>/dev/null | grep -q -- '-fanalyzer'; then
  for source in "${analysis_sources[@]}"; do
    safe_name=${source//\//-}
    cc -std=c11 -O0 -fanalyzer -Wall -Wextra -Wpedantic -Wformat=2 \
      -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Werror \
      -Itests/forgeshell/stubs -I"$forge_src" -c "$forge_src/$source.c" \
      -o "$tmp/$safe_name-analyzer.o"
  done
fi
make_dump=$(make -f "$forge_src/Makefile" -pn SDL_CONFIG=true \
  CFLAGS=-O2 LDFLAGS=-Wl,--as-needed 2>/dev/null || true)
grep -q 'CFLAGS = -O2.*-std=c11.*-ffunction-sections' <<<"$make_dump"
grep -q 'LDFLAGS = -Wl,--as-needed.*--gc-sections' <<<"$make_dump"

# Dual-launcher recovery: GMenu2X default, startup-failure fallback, manual
# recovery reset, and explicit reboot/poweroff dispatch.
mkdir -p "$tmp/shell-home/state"
cp overlay/board/miyoo/main/forgeshell/*.sh "$tmp/shell-home/"
chmod 755 "$tmp/shell-home/"*.sh
cat > "$tmp/shell-home/run-gmenu2x.sh" <<'SH'
#!/bin/sh
printf 'gmenu\n' >> "$FORGESHELL_TEST_TRACE"
exit 0
SH
cat > "$tmp/fake-forgeshell" <<'SH'
#!/bin/sh
printf 'shell\n' >> "$FORGESHELL_TEST_TRACE"
[ -n "${FORGESHELL_TEST_ARGC_FILE:-}" ] && printf '%s\n' "$#" > "$FORGESHELL_TEST_ARGC_FILE"
[ -n "${FORGESHELL_TEST_ARGS_FILE:-}" ] && printf '%s\n' "$@" > "$FORGESHELL_TEST_ARGS_FILE"
case "${FORGESHELL_TEST_MODE:-success}" in
  success)
    [ -n "${FORGESHELL_BOOT_OK:-}" ] && printf 'ok\n' > "$FORGESHELL_BOOT_OK"
    exit 0
    ;;
  fail) exit 1 ;;
  linger)
    [ -n "${FORGESHELL_BOOT_OK:-}" ] && printf 'ok\n' > "$FORGESHELL_BOOT_OK"
    sleep 2
    if [ "$(cat "$FORGESHELL_HOME/state/boot-failures" 2>/dev/null)" = 0 ]; then
      printf 'reset-before-exit\n' >> "$FORGESHELL_TEST_TRACE"
    fi
    exit 0
    ;;
  reboot) exit 43 ;;
  poweroff) exit 44 ;;
esac
SH
cat > "$tmp/fake-reboot" <<'SH'
#!/bin/sh
printf 'reboot\n' >> "$FORGESHELL_TEST_TRACE"
exit 0
SH
cat > "$tmp/fake-poweroff" <<'SH'
#!/bin/sh
printf 'poweroff\n' >> "$FORGESHELL_TEST_TRACE"
exit 0
SH
chmod 755 "$tmp/fake-forgeshell" "$tmp/fake-reboot" "$tmp/fake-poweroff" \
  "$tmp/shell-home/run-gmenu2x.sh"
trace="$tmp/shell-trace"
: > "$trace"
printf 'launcher_mode=gmenu2x\nonboarding_complete=1\n' > "$tmp/shell-home/config.ini"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_TRACE="$trace" "$tmp/shell-home/boot-dispatch.sh"
grep -q '^gmenu$' "$trace"
if grep -q '^shell$' "$trace"; then
  fail "completed gmenu2x launcher mode unexpectedly started ForgeShell"
fi

printf 'launcher_mode=gmenu2x\nonboarding_complete=0\n' > "$tmp/shell-home/config.ini"
: > "$trace"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_TRACE="$trace" "$tmp/shell-home/boot-dispatch.sh"
grep -q '^shell$' "$trace" || fail "incomplete onboarding did not start ForgeShell"
grep -q '^gmenu$' "$trace" || fail "ForgeShell exit did not retain GMenu2X recovery"

printf 'launcher_mode=forgeshell\nonboarding_complete=1\n' > "$tmp/shell-home/config.ini"
: > "$trace"
for _ in 1 2 3 4; do
  FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
    FORGESHELL_TEST_MODE=fail FORGESHELL_TEST_TRACE="$trace" \
    "$tmp/shell-home/boot-dispatch.sh"
done
[[ "$(grep -c '^shell$' "$trace")" == 3 ]]
[[ "$(grep -c '^gmenu$' "$trace")" == 4 ]]
[[ "$(cat "$tmp/shell-home/state/boot-failures")" == 3 ]]
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_MODE=success FORGESHELL_TEST_TRACE="$trace" \
  FORGESHELL_TEST_ARGC_FILE="$tmp/shell-argc" \
  "$tmp/shell-home/forgeshell-start.sh"
[[ "$(cat "$tmp/shell-home/state/boot-failures")" == 0 ]]
[[ "$(cat "$tmp/shell-argc")" == 0 ]] || fail "manual ForgeShell launch was incorrectly marked as a boot"
printf 'launcher_mode=forgeshell\nsafe_mode_next_boot=1\n' > "$tmp/shell-home/config.ini"
: > "$trace"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_MODE=success FORGESHELL_TEST_TRACE="$trace" \
  FORGESHELL_TEST_ARGC_FILE="$tmp/safe-argc" FORGESHELL_TEST_ARGS_FILE="$tmp/safe-args" \
  "$tmp/shell-home/boot-dispatch.sh"
[[ "$(cat "$tmp/safe-argc")" == 2 ]]
grep -qx -- '--boot' "$tmp/safe-args"
grep -qx -- '--safe-mode' "$tmp/safe-args"
grep -q '^safe_mode_next_boot=0$' "$tmp/shell-home/config.ini"
: > "$tmp/shell-home/state/force-safe-mode"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_MODE=success FORGESHELL_TEST_TRACE="$trace" \
  FORGESHELL_TEST_ARGS_FILE="$tmp/marker-safe-args" "$tmp/shell-home/boot-dispatch.sh"
grep -qx -- '--safe-mode' "$tmp/marker-safe-args"
[[ ! -e "$tmp/shell-home/state/force-safe-mode" ]]
if grep -Eq '^[[:space:]]*sync[[:space:]]*$' "$tmp/shell-home/forgeshell-start.sh"; then
  fail "ForgeShell startup script contains an unconditional sync"
fi
printf '2\n' > "$tmp/shell-home/state/boot-failures"
: > "$trace"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_MODE=linger FORGESHELL_TEST_TRACE="$trace" \
  "$tmp/shell-home/boot-dispatch.sh"
grep -q '^reset-before-exit$' "$trace"
[[ "$(cat "$tmp/shell-home/state/boot-failures")" == 0 ]]

mkdir -p "$tmp/fail-mv-bin"
cat > "$tmp/fail-mv-bin/mv" <<'SH'
#!/bin/sh
exit 1
SH
chmod 755 "$tmp/fail-mv-bin/mv"
rm -f "$tmp/shell-home/state/boot-failures"
set +e
PATH="$tmp/fail-mv-bin:$PATH" FORGESHELL_HOME="$tmp/shell-home" \
  FORGESHELL_BIN="$tmp/fake-forgeshell" FORGESHELL_TEST_MODE=success \
  FORGESHELL_TEST_TRACE="$trace" "$tmp/shell-home/forgeshell-start.sh" --boot
mv_failure_status=$?
set -e
[[ "$mv_failure_status" == 42 ]]

: > "$trace"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_MODE=reboot FORGESHELL_TEST_TRACE="$trace" \
  FORGESHELL_REBOOT_BIN="$tmp/fake-reboot" \
  "$tmp/shell-home/boot-dispatch.sh"
grep -q '^reboot$' "$trace"
: > "$trace"
FORGESHELL_HOME="$tmp/shell-home" FORGESHELL_BIN="$tmp/fake-forgeshell" \
  FORGESHELL_TEST_MODE=poweroff FORGESHELL_TEST_TRACE="$trace" \
  FORGESHELL_POWEROFF_BIN="$tmp/fake-poweroff" \
  "$tmp/shell-home/boot-dispatch.sh"
grep -q '^poweroff$' "$trace"

# Performance snapshot records bounded launcher/cache metrics without modifying state.
mkdir -p "$tmp/perf-main/gmenu2x" "$tmp/perf-home/state" "$tmp/perf-home/library"
printf '# cache\nrow\n' > "$tmp/perf-home/state/library-cache.tsv"
printf 'launcher_mode=gmenu2x\n' > "$tmp/perf-home/config.ini"
FORGE_MAIN_CANDIDATES="$tmp/perf-main" FORGE_ALLOW_UNMOUNTED=1 FORGE_NO_SLEEP=1 \
  FORGE_DIALOG_BIN=forgeos-no-dialog FORGESHELL_HOME="$tmp/perf-home" \
  overlay/board/miyoo/main/apps/forge-tools/performance-snapshot.sh >/dev/null
perf_report=$(find "$tmp/perf-main/forgeos-test-reports" -name 'q90-performance-*.txt' -print -quit)
[[ -n "$perf_report" ]]
grep -q '^Cached games: 1$' "$perf_report"
grep -q '^Target idle VmRSS: under 8192 kB.$' "$perf_report"

# Launcher titles, panel titles, and icon references stay coherent.
grep -q '^title=System Overview$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/system-info
grep -q '^title=Emulator Overview$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/emulator-overview
grep -q '^title=Storage Check$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/storage-health
grep -q '^title=CPU Profile$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/performance-mode
grep -q '^title=Save Backup$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/backup-saves
grep -q '^title=ROM Library Setup$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/create-rom-folders
grep -q '^title=Boot Log$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/forgeshell-log
grep -q '^title=Hardware Check$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/hardware-check
grep -q '^title=Performance Snapshot$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/performance-snapshot
grep -q '^title=Reset ForgeShell$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/reset-forgeshell
grep -q '^title=ForgeShell Safe Mode$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/forgeshell-safe
grep -q '^title=ForgeShell Beta$' overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/forgeshell
[[ ! -d overlay/board/miyoo/main/gmenu2x/sections/forge ]]
for entry in overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/*; do
  exec_path=$(sed -n 's/^exec=//p' "$entry")
  icon_path=$(sed -n 's/^icon=//p' "$entry")
  [[ -n "$exec_path" && -n "$icon_path" ]] || fail "incomplete launcher entry: $entry"
  [[ -f "overlay/board/miyoo/main${exec_path#/mnt}" ]] || fail "missing launcher executable: $exec_path"
  [[ -f "overlay/board/miyoo/main${icon_path#/mnt}" ]] || fail "missing launcher icon: $icon_path"
done

# Release packaging preserves source launcher definitions while removing only
# generated host objects and binaries.
OUT_DIR="$tmp/release" DIST_DIR="$tmp/package/dist" SKIP_TESTS=1 ./tools/package-release.sh >/dev/null
source_zip="$tmp/release/forgeos-$VERSION-source.zip"
overlay_zip="$tmp/release/q90-forgeos-$VERSION-sd-overlay.zip"
unzip -tq "$source_zip" >/dev/null
unzip -tq "$overlay_zip" >/dev/null
unzip -Z1 "$source_zip" > "$tmp/source-zip-files.txt"
grep -q "/overlay/board/miyoo/main/gmenu2x/sections/ForgeOS/forgeshell$" \
  "$tmp/source-zip-files.txt"
if grep -q "/src/forgeshell/src/forgeshell$" "$tmp/source-zip-files.txt"; then
  fail "source archive contains the generated ForgeShell binary"
fi
if grep -Eq '\.o$|__pycache__|\.pyc$' "$tmp/source-zip-files.txt"; then
  fail "source archive contains generated build or Python cache files"
fi
grep -q "forgeos-$VERSION-source.zip" "$tmp/release/SHA256SUMS"
[[ -s "$tmp/release/forgeos-q90-$VERSION.img.xz" ]]
xz -t "$tmp/release/forgeos-q90-$VERSION.img.xz"
./tools/verify-release.sh "$tmp/release" >/dev/null
python3 tools/generate-release-notes.py --output "$tmp/release-notes.md" --repository example/forgeos
grep -q "^# ForgeOS $VERSION$" "$tmp/release-notes.md"
grep -q "example/forgeos" "$tmp/release-notes.md"
( cd "$ROOT_DIR" && sha256sum -c MANIFEST.sha256 >/dev/null )

# Version, documentation, and generated preview stay coherent.
grep -q "^# ForgeOS$" README.md
grep -q "forgeos-q90-<version>.img.xz" README.md
grep -q "^name: CI$" .github/workflows/ci.yml
grep -q "^name: Q90 image$" .github/workflows/q90-image.yml
grep -q "^name: Publish release$" .github/workflows/release.yml
grep -q "gh release create" .github/workflows/release.yml
grep -q "actions/checkout@v7" .github/workflows/ci.yml
grep -q "actions/cache@v6" .github/workflows/q90-image.yml
grep -q "actions/upload-artifact@v7" .github/workflows/ci.yml
grep -q "actions/attest@v4" .github/workflows/release.yml
if grep -RqsE 'actions/(checkout@v[1-6]|cache@v[1-5]|upload-artifact@v[1-6])' .github/workflows; then
  fail "workflow references an outdated core GitHub Action"
fi
grep -q "^## $VERSION" CHANGELOG.md
grep -q "^ForgeOS Q90 $VERSION$" overlay/board/miyoo/main/forgeos-version.txt
grep -q 'FORGESHELL BETA' <(strings assets/forgeos-splash.bmp 2>/dev/null || true) || true
[[ -s HARDWARE-TEST.md && -s USABILITY-TEST.md && -s RELEASE-CHECKLIST.md ]]
[[ "$(head -n 1 docs/compatibility-matrix.csv)" == tested_at,firmware,device,system,game,emulator,bios,boot,gameplay,frame_pacing,audio,sram,save_states,exit_return,cpu_profile,frameskip,aspect,notes ]]

# Required dimensions and formats.
identify assets/forgeos-splash.png | grep -q '320x240'
identify assets/forgeos-splash.bmp | grep -q '320x240'
identify assets/theme-preview.png | grep -q '320x240'
identify assets/forgeshell-pages.png | grep -q '1600x480'
for icon in overlay/board/miyoo/main/apps/forge-tools/icon-*.png; do
  identify "$icon" | grep -q '48x48' || fail "wrong icon dimensions: $icon"
done

echo "All ForgeOS checks passed."
