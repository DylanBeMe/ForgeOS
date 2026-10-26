#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME="${FORGESHELL_SIM_ROOT:-$ROOT_DIR/runtime/generic-linux}"
PROFILE="$ROOT_DIR/platforms/generic-linux/platform.ini"
BIN="${FORGESHELL_BIN:-$ROOT_DIR/src/forgeshell/src/forgeshell}"

"$ROOT_DIR/tools/validate-platform.py" "$PROFILE"
if [[ ! -x "$BIN" ]]; then
  command -v sdl-config >/dev/null 2>&1 || {
    echo "Simulator compilation requires SDL 1.2 development files and sdl-config." >&2
    exit 1
  }
  make -C "$ROOT_DIR/src/forgeshell/src" clean all
fi
install -d "$RUNTIME/forgeshell/library" "$RUNTIME/forgeshell/state" "$RUNTIME/tools" "$RUNTIME/roms/demo"
install -m 0644 "$ROOT_DIR/platforms/generic-linux/systems.ini" "$RUNTIME/forgeshell/systems.ini"
install -m 0644 "$ROOT_DIR/platforms/generic-linux/tools.tsv" "$RUNTIME/forgeshell/tools.tsv"
install -m 0644 "$ROOT_DIR/overlay/board/miyoo/main/forgeshell/theme.ini" "$RUNTIME/forgeshell/theme.ini"
if [[ ! -e "$RUNTIME/forgeshell/config.ini" ]]; then
  install -m 0644 "$ROOT_DIR/overlay/board/miyoo/main/forgeshell/config.default.ini" "$RUNTIME/forgeshell/config.ini"
fi
install -m 0755 "$ROOT_DIR/platforms/generic-linux/runtime/tools/"*.sh "$RUNTIME/tools/"
[[ -e "$RUNTIME/roms/demo/Example.rom" ]] || printf 'demo\n' > "$RUNTIME/roms/demo/Example.rom"
cd "$ROOT_DIR"
exec "$BIN" --profile "$PROFILE" --data-root "$RUNTIME" \
  --home "$RUNTIME/forgeshell" --source "$RUNTIME/forgeshell/systems.ini" \
  --windowed "$@"
