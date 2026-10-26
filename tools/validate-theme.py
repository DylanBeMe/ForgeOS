#!/usr/bin/env python3
"""Validate ForgeShell's shared visual theme and console approximation."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COLOR_KEYS = (
    "background", "panel", "panel_alt", "accent", "accent_soft",
    "text", "muted", "danger", "border",
)
INT_RANGES = {
    "radius": (0, 12),
    "font_small": (8, 16),
    "font_body": (10, 20),
    "font_title": (14, 26),
}
PATH_KEYS = ("font_regular", "font_bold")
REQUIRED_DIALOG_KEYS = {
    "use_shadow", "use_colors", "screen_color", "dialog_color", "title_color",
    "border_color", "button_active_color", "button_inactive_color",
    "button_label_active_color", "button_label_inactive_color", "menubox_color",
    "menubox_border_color", "item_color", "item_selected_color",
    "position_indicator_color",
}


def fail(message: str) -> None:
    raise ValueError(message)


def parse_assignments(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(("#", ";", "[")):
            continue
        if "=" not in line:
            fail(f"{path}:{number}: expected key=value")
        key, value = (part.strip() for part in line.split("=", 1))
        if not key or not value:
            fail(f"{path}:{number}: empty key or value")
        if key in values:
            fail(f"{path}:{number}: duplicate key {key}")
        values[key] = value
    return values


def relative_luminance(value: str) -> float:
    channels = [int(value[index:index + 2], 16) / 255.0 for index in (1, 3, 5)]
    linear = [channel / 12.92 if channel <= 0.04045 else
              ((channel + 0.055) / 1.055) ** 2.4 for channel in channels]
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


def contrast_ratio(left: str, right: str) -> float:
    high, low = sorted((relative_luminance(left), relative_luminance(right)), reverse=True)
    return (high + 0.05) / (low + 0.05)


def validate_contrast(values: dict[str, str], path: Path) -> None:
    pairs = (
        ("text", "background", 7.0),
        ("text", "panel", 7.0),
        ("muted", "background", 4.5),
        ("muted", "panel", 4.5),
        ("accent", "panel", 4.5),
        ("text", "accent_soft", 4.5),
        ("danger", "background", 4.5),
    )
    for foreground, background, minimum in pairs:
        ratio = contrast_ratio(values[foreground], values[background])
        if ratio < minimum:
            fail(f"{path}: {foreground}/{background} contrast {ratio:.2f}:1 is below {minimum:.1f}:1")


def validate_theme(path: Path) -> dict[str, str]:
    values = parse_assignments(path)
    allowed = set(COLOR_KEYS) | set(INT_RANGES) | set(PATH_KEYS)
    missing = sorted(allowed - values.keys())
    unknown = sorted(values.keys() - allowed)
    if missing:
        fail(f"{path}: missing keys: {', '.join(missing)}")
    if unknown:
        fail(f"{path}: unknown keys: {', '.join(unknown)}")
    for key in COLOR_KEYS:
        if re.fullmatch(r"#[0-9A-Fa-f]{6}", values[key]) is None:
            fail(f"{path}: {key} must be #RRGGBB")
    for key, (minimum, maximum) in INT_RANGES.items():
        try:
            value = int(values[key], 10)
        except ValueError as exc:
            raise ValueError(f"{path}: {key} must be an integer") from exc
        if not minimum <= value <= maximum:
            fail(f"{path}: {key} must be between {minimum} and {maximum}")
    for key in PATH_KEYS:
        if not values[key].startswith("/") or "\n" in values[key]:
            fail(f"{path}: {key} must be an absolute single-line path")
    validate_contrast(values, path)
    return values


def validate_c_defaults(path: Path, theme: dict[str, str]) -> None:
    source = path.read_text(encoding="utf-8")
    for key in COLOR_KEYS:
        match = re.search(rf"theme->{re.escape(key)}\s*=\s*0x([0-9A-Fa-f]{{6}})U", source)
        if match is None:
            fail(f"{path}: missing C default for {key}")
        if f"#{match.group(1).upper()}" != theme[key].upper():
            fail(f"{path}: C default for {key} differs from theme.ini")
    for key in INT_RANGES:
        match = re.search(rf"theme->{re.escape(key)}\s*=\s*([0-9]+);", source)
        if match is None or int(match.group(1), 10) != int(theme[key], 10):
            fail(f"{path}: C default for {key} differs from theme.ini")
    for key in PATH_KEYS:
        match = re.search(
            rf"fs_copy\(theme->{re.escape(key)},[^\n]+\n\s*\"([^\"]+)\"\);",
            source,
        )
        if match is None or match.group(1) != theme[key]:
            fail(f"{path}: C default for {key} differs from theme.ini")


def validate_dialogrc(path: Path) -> None:
    values = parse_assignments(path)
    missing = sorted(REQUIRED_DIALOG_KEYS - values.keys())
    if missing:
        fail(f"{path}: missing dialog style keys: {', '.join(missing)}")
    if values.get("use_colors") != "ON" or values.get("use_shadow") != "OFF":
        fail(f"{path}: Midnight Mint dialogs require colors on and shadows off")
    for key in REQUIRED_DIALOG_KEYS - {"use_colors", "use_shadow"}:
        if re.fullmatch(r"\([A-Z]+,[A-Z]+,(?:ON|OFF)\)", values[key]) is None:
            fail(f"{path}: invalid dialog color tuple for {key}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--theme", type=Path,
                        default=ROOT / "overlay/board/miyoo/main/forgeshell/theme.ini")
    parser.add_argument("--defaults", type=Path,
                        default=ROOT / "src/forgeshell/src/core/config.c")
    parser.add_argument("--dialogrc", type=Path,
                        default=ROOT / "overlay/board/miyoo/main/apps/forge-tools/dialogrc")
    args = parser.parse_args()
    try:
        theme = validate_theme(args.theme)
        validate_c_defaults(args.defaults, theme)
        validate_dialogrc(args.dialogrc)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"OK   {args.theme}")
    print(f"OK   {args.dialogrc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
