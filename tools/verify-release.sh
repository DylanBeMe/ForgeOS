#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RELEASE_DIR="${1:-$ROOT_DIR/release}"
[[ -d "$RELEASE_DIR" ]] || { echo "missing release directory: $RELEASE_DIR" >&2; exit 1; }
[[ -s "$RELEASE_DIR/SHA256SUMS" ]] || { echo "missing SHA256SUMS" >&2; exit 1; }
(
  cd "$RELEASE_DIR"
  sha256sum -c SHA256SUMS
  for archive in *.zip; do
    [[ -e "$archive" ]] || continue
    unzip -tq "$archive" >/dev/null
  done
  for image in *.img.xz; do
    [[ -e "$image" ]] || continue
    xz -t "$image"
  done
)
