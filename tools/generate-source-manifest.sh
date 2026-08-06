#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
(
  cd "$ROOT_DIR"
  find . \
    -path './.git' -prune -o \
    -path './build' -prune -o \
    -path './dist' -prune -o \
    -path './release' -prune -o \
    -path '*/__pycache__' -prune -o \
    -type f \
    ! -name MANIFEST.sha256 \
    ! -name '*.o' \
    ! -name '*.pyc' \
    ! -name '*.plist' \
    ! -path './src/forgeshell/src/forgeshell' \
    -print0 | LC_ALL=C sort -z | xargs -0 sha256sum
) > "$TMP"
chmod 0644 "$TMP"
mv -f "$TMP" "$ROOT_DIR/MANIFEST.sha256"
trap - EXIT
