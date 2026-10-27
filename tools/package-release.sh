#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(<"$ROOT_DIR/VERSION")"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/release}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

for tool in zip sha256sum find sort xz cp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool" >&2; exit 1; }
done
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

"$ROOT_DIR/tools/sync-sd-overlay.sh"
"$ROOT_DIR/tools/generate-source-manifest.sh"
if [[ "${SKIP_TESTS:-0}" == 1 ]]; then
  printf 'Tests were run separately by the release workflow.\n' > \
    "$OUT_DIR/forgeos-$VERSION-tests.txt"
else
  "$ROOT_DIR/tests/run.sh" | tee "$OUT_DIR/forgeos-$VERSION-tests.txt"
fi

source_stage="$STAGE/forgeos-$VERSION"
mkdir -p "$source_stage"
(
  cd "$ROOT_DIR"
  find . -mindepth 1 -maxdepth 1 \
    ! -name build ! -name dist ! -name release ! -name '.git' -print0 |
    while IFS= read -r -d '' item; do cp -a "$item" "$source_stage/"; done
)
find "$source_stage" -type f -name '*.o' -delete
find "$source_stage" -type d -name __pycache__ -prune -exec rm -rf {} +
find "$source_stage" -type f \( -name '*.pyc' -o -name '*.plist' \) -delete
rm -f "$source_stage/src/forgeshell/src/forgeshell"
if [[ -n "${SOURCE_DATE_EPOCH:-}" ]]; then
  find "$source_stage" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
fi
(
  cd "$STAGE"
  find "forgeos-$VERSION" -type f -print | LC_ALL=C sort | \
    zip -X -q "$OUT_DIR/forgeos-$VERSION-source.zip" -@
)
(
  cd "$ROOT_DIR/sd-overlay"
  find . -type f -print | LC_ALL=C sort | \
    zip -X -q "$OUT_DIR/q90-forgeos-$VERSION-sd-overlay.zip" -@
)

for image in "$DIST_DIR"/forgeos-*"$VERSION"*.img; do
  [[ -e "$image" ]] || continue
  base="$(basename "$image")"
  xz -T0 -6 -c "$image" > "$OUT_DIR/$base.xz"
  [[ "${FORGE_INCLUDE_RAW_IMAGE:-0}" == 1 ]] && cp -a "$image" "$OUT_DIR/$base"
  for suffix in upstream-ref emulators.txt; do
    [[ -f "$image.$suffix" ]] && cp -a "$image.$suffix" "$OUT_DIR/$base.$suffix"
  done
done
for artifact in "$DIST_DIR"/forgeos-*"$VERSION"*.tar.gz; do
  [[ -e "$artifact" ]] || continue
  cp -a "$artifact" "$OUT_DIR/"
done

cp -a "$ROOT_DIR/docs/PORTING.md" "$OUT_DIR/forgeos-$VERSION-porting-guide.md"
cp -a "$ROOT_DIR/docs/PLATFORM-SCHEMA.md" "$OUT_DIR/forgeos-$VERSION-platform-schema.md"
cp -a "$ROOT_DIR/docs/compatibility-matrix.csv" "$OUT_DIR/q90-forgeos-$VERSION-compatibility-matrix.csv"
python3 "$ROOT_DIR/tools/compatibility-report.py" "$ROOT_DIR/docs/compatibility-matrix.csv" > "$OUT_DIR/q90-forgeos-$VERSION-compatibility-report.txt"
cp -a "$ROOT_DIR/docs/library-index-template.csv" "$OUT_DIR/forgeos-$VERSION-library-index-template.csv"
cp -a "$ROOT_DIR/docs/usability-test.csv" "$OUT_DIR/forgeos-$VERSION-usability-test.csv"
cp -a "$ROOT_DIR/docs/performance-results.csv" "$OUT_DIR/q90-forgeos-$VERSION-performance-results.csv"
cp -a "$ROOT_DIR/HARDWARE-TEST.md" "$OUT_DIR/q90-forgeos-$VERSION-hardware-test.md"
cp -a "$ROOT_DIR/RELEASE-CHECKLIST.md" "$OUT_DIR/forgeos-$VERSION-release-checklist.md"
cp -a "$ROOT_DIR/REVIEW.md" "$OUT_DIR/forgeos-$VERSION-review.md"
checksum_file="$STAGE/SHA256SUMS"
(
  cd "$OUT_DIR"
  find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\0' |
    LC_ALL=C sort -z | xargs -0 sha256sum
) > "$checksum_file"
mv "$checksum_file" "$OUT_DIR/SHA256SUMS"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    printf 'version=%s\n' "$VERSION"
    printf 'release_dir=%s\n' "$OUT_DIR"
    printf 'checksums=%s\n' "$OUT_DIR/SHA256SUMS"
  } >> "$GITHUB_OUTPUT"
fi
printf '%s\n' "$OUT_DIR"
