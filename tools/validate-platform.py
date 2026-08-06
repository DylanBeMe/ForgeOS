#!/usr/bin/env python3
"""Validate ForgeOS device profiles and their maintenance manifests."""
from __future__ import annotations

import argparse
import configparser
import re
import sys
from pathlib import Path

LABEL_KEYS = {
    "accept", "back", "favorite", "options", "page_left", "page_right",
    "start", "select", "power",
}

REQUIRED = {
    "device": {"id", "name", "family"},
    "ui": {"screen_width", "screen_height", "screen_bpp", "fullscreen"},
    "storage": {"data_root", "rom_root", "home", "tool_root"},
    "launcher": {"provider", "source", "fallback_command", "frontend_value"},
    "maintenance": {"manifest"},
    "performance": {"cpu_helper"},
    "power": {"reboot", "poweroff"},
    "input": {
        "up", "down", "left", "right", "accept", "back", "favorite",
        "options", "page_left", "page_right", "start", "select", "power",
    },
    "capabilities": {
        "battery", "cpu_profiles", "brightness", "volume", "safe_shutdown",
        "storage_health", "system_info",
    },
}
ALLOWED_PROVIDERS = {"gmenu2x", "forge-manifest"}
ALLOWED_BPP = {16, 24, 32}
KNOWN_CAPABILITIES = {
    "always", "battery", "cpu_profiles", "brightness", "volume",
    "safe_shutdown", "storage_health", "system_info",
}
INPUT_ACTIONS = {
    "up", "down", "left", "right", "accept", "back", "favorite",
    "options", "page_left", "page_right", "start", "select", "power",
}
ALLOWED_KEY_ALIASES = {frozenset(("options", "start"))}
VARIABLE_RE = re.compile(r"\$\{([a-z_][a-z0-9_]*)\}")
KNOWN_VARIABLES = {"data_root", "rom_root", "home", "tool_root", "device_id"}
KNOWN_KEY_NAMES = {
    "UP", "DOWN", "LEFT", "RIGHT", "RETURN", "ENTER", "ESCAPE", "ESC",
    "SPACE", "TAB", "BACKSPACE", "LSHIFT", "LCTRL", "LALT", "RCTRL",
}


def parse_profile(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str.lower
    with path.open("r", encoding="utf-8") as handle:
        parser.read_file(handle)
    return parser


def bool_value(value: str, label: str, errors: list[str]) -> bool:
    lowered = value.strip().lower()
    if lowered not in {"0", "1", "yes", "no", "true", "false", "on", "off"}:
        errors.append(f"{label}: expected a boolean, got {value!r}")
    return lowered in {"1", "yes", "true", "on"}


def expand(value: str, values: dict[str, str], errors: list[str], label: str) -> str:
    previous = None
    for _ in range(8):
        if value == previous:
            break
        previous = value
        value = VARIABLE_RE.sub(lambda m: values.get(m.group(1), m.group(0)), value)
    unknown = sorted(set(VARIABLE_RE.findall(value)) - KNOWN_VARIABLES)
    if unknown:
        errors.append(f"{label}: unknown variables: {', '.join(unknown)}")
    unresolved = VARIABLE_RE.findall(value)
    if unresolved:
        errors.append(f"{label}: unresolved variables: {', '.join(sorted(set(unresolved)))}")
    return value


def validate_manifest(path: Path, capabilities: dict[str, bool], errors: list[str]) -> None:
    if not path.exists():
        errors.append(f"maintenance.manifest: file does not exist: {path}")
        return
    ids: set[str] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 5:
            errors.append(f"{path}:{number}: expected five tab-separated fields")
            continue
        tool_id, title, meta, command, requirement = (field.strip() for field in fields)
        if not tool_id or not title or not command:
            errors.append(f"{path}:{number}: id, title, and command are required")
        if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,46}", tool_id) is None:
            errors.append(f"{path}:{number}: invalid tool id {tool_id!r}")
        if tool_id in ids:
            errors.append(f"{path}:{number}: duplicate tool id {tool_id!r}")
        ids.add(tool_id)
        if requirement not in KNOWN_CAPABILITIES:
            errors.append(f"{path}:{number}: unknown capability {requirement!r}")
        if requirement != "always" and requirement not in capabilities:
            errors.append(f"{path}:{number}: capability {requirement!r} is not declared")
        if "\n" in title or "\n" in meta or "\n" in command:
            errors.append(f"{path}:{number}: fields must be single-line")
        unknown_variables = sorted(set(VARIABLE_RE.findall(command)) - {"tool_root", "home"})
        if unknown_variables:
            errors.append(
                f"{path}:{number}: command uses unsupported variables: "
                f"{', '.join(unknown_variables)}"
            )



def validate_systems_manifest(path: Path, errors: list[str]) -> None:
    if not path.exists():
        errors.append(f"forge-manifest provider requires a systems.ini template: {path}")
        return
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str.lower
    try:
        with path.open("r", encoding="utf-8") as handle:
            parser.read_file(handle)
    except (OSError, configparser.Error) as exc:
        errors.append(f"{path}: cannot parse emulator manifest: {exc}")
        return
    system_sections = [section for section in parser.sections() if section.startswith("system.")]
    if not system_sections:
        errors.append(f"{path}: no [system.<id>] sections found")
        return
    ids: set[str] = set()
    required = {"exec"}
    allowed = {"title", "exec", "params", "workdir", "romdir", "selectordir", "romexts", "selectorfilter"}
    for section in system_sections:
        system_id = section[7:].strip()
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,46}", system_id):
            errors.append(f"{path}: invalid system id in [{section}]")
        if system_id in ids:
            errors.append(f"{path}: duplicate system id {system_id!r}")
        ids.add(system_id)
        present = set(parser[section])
        missing = sorted(required - present)
        if not ({"romdir", "selectordir"} & present):
            missing.append("romdir or selectordir")
        unknown_keys = sorted(present - allowed)
        if missing:
            errors.append(f"{path}: [{section}] missing: {', '.join(missing)}")
        if unknown_keys:
            errors.append(f"{path}: [{section}] unknown keys: {', '.join(unknown_keys)}")
        if {"romdir", "selectordir"} <= present:
            errors.append(f"{path}: [{section}] cannot define both romdir and selectordir")
        if {"romexts", "selectorfilter"} <= present:
            errors.append(f"{path}: [{section}] cannot define both romexts and selectorfilter")
        for key, value in parser[section].items():
            unknown = sorted(set(VARIABLE_RE.findall(value)) - KNOWN_VARIABLES)
            if unknown:
                errors.append(f"{path}: [{section}] {key}: unknown variables: {', '.join(unknown)}")


def validate(path: Path, project_root: Path) -> list[str]:
    errors: list[str] = []
    try:
        parser = parse_profile(path)
    except (OSError, configparser.Error) as exc:
        return [f"{path}: cannot parse profile: {exc}"]

    allowed_sections = set(REQUIRED) | {"labels"}
    unknown_sections = sorted(set(parser.sections()) - allowed_sections)
    if unknown_sections:
        errors.append(f"unknown sections: {', '.join(unknown_sections)}")
    for section, keys in REQUIRED.items():
        if not parser.has_section(section):
            errors.append(f"missing section [{section}]")
            continue
        present = set(parser[section])
        missing = sorted(keys - present)
        unknown = sorted(present - keys)
        if missing:
            errors.append(f"[{section}] missing: {', '.join(missing)}")
        if unknown:
            errors.append(f"[{section}] unknown keys: {', '.join(unknown)}")

    if parser.has_section("labels"):
        present = set(parser["labels"])
        missing = sorted(LABEL_KEYS - present)
        unknown = sorted(present - LABEL_KEYS)
        if missing:
            errors.append(f"[labels] missing: {', '.join(missing)}")
        if unknown:
            errors.append(f"[labels] unknown keys: {', '.join(unknown)}")
        for name, value in parser["labels"].items():
            label = value.strip()
            if not label:
                errors.append(f"labels.{name}: label is empty")
            elif len(label.encode("utf-8")) >= 12:
                errors.append(f"labels.{name}: label must be at most 11 UTF-8 bytes")
            elif any(char in label for char in "\r\n\t"):
                errors.append(f"labels.{name}: label must be a single field")

    if errors:
        return errors

    try:
        width = int(parser["ui"]["screen_width"])
        height = int(parser["ui"]["screen_height"])
        bpp = int(parser["ui"]["screen_bpp"])
    except ValueError:
        errors.append("[ui] screen values must be integers")
    else:
        if not 160 <= width <= 3840 or not 120 <= height <= 2160:
            errors.append("[ui] resolution must be between 160x120 and 3840x2160")
        if bpp not in ALLOWED_BPP:
            errors.append("[ui] screen_bpp must be 16, 24, or 32")
    bool_value(parser["ui"]["fullscreen"], "ui.fullscreen", errors)

    provider = parser["launcher"]["provider"].strip().lower()
    if provider not in ALLOWED_PROVIDERS:
        errors.append(f"launcher.provider: unsupported provider {provider!r}")

    values = {
        "device_id": parser["device"]["id"].strip(),
        "data_root": parser["storage"]["data_root"].strip(),
        "rom_root": parser["storage"]["rom_root"].strip(),
        "home": parser["storage"]["home"].strip(),
        "tool_root": parser["storage"]["tool_root"].strip(),
    }
    for _ in range(4):
        for key in ("data_root", "rom_root", "home", "tool_root"):
            values[key] = expand(values[key], values, errors, f"storage.{key}")

    for section in ("launcher", "maintenance", "power"):
        for key, value in parser[section].items():
            expand(value.strip(), values, errors, f"{section}.{key}")

    mapped: dict[str, str] = {}
    for action, value in parser["input"].items():
        normalized = value.strip().upper()
        if not normalized:
            errors.append(f"input.{action}: key is empty")
        elif normalized not in KNOWN_KEY_NAMES:
            try:
                numeric = int(normalized, 10)
            except ValueError:
                errors.append(f"input.{action}: unknown SDL key {value!r}")
            else:
                if not 0 <= numeric <= 65535:
                    errors.append(f"input.{action}: numeric SDL key out of range")
        mapped[action] = normalized
    seen: dict[str, str] = {}
    for action in sorted(INPUT_ACTIONS):
        key = mapped.get(action, "")
        if key in seen:
            pair = frozenset((action, seen[key]))
            if pair not in ALLOWED_KEY_ALIASES:
                errors.append(f"input.{action} shares {key!r} with input.{seen[key]}")
        else:
            seen[key] = action

    capabilities: dict[str, bool] = {}
    for name, value in parser["capabilities"].items():
        capabilities[name] = bool_value(value, f"capabilities.{name}", errors)
    if capabilities.get("safe_shutdown"):
        if not parser["power"]["reboot"].strip() or not parser["power"]["poweroff"].strip():
            errors.append("safe_shutdown requires reboot and poweroff commands")

    target_dir = path.parent
    manifest_hint = target_dir / "tools.tsv"
    if not manifest_hint.exists() and path.parent.name == "profiles":
        manifest_hint = project_root / "platforms" / path.stem / "tools.tsv"
    validate_manifest(manifest_hint, capabilities, errors)

    if provider == "forge-manifest":
        systems_hint = target_dir / "systems.ini"
        if not systems_hint.exists() and path.parent.name == "profiles":
            systems_hint = project_root / "platforms" / path.stem / "systems.ini"
        validate_systems_manifest(systems_hint, errors)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("profiles", nargs="*", type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    profiles = args.profiles or sorted((args.root / "platforms").glob("*/platform.ini"))
    failed = False
    for profile in profiles:
        errors = validate(profile.resolve(), args.root.resolve())
        if errors:
            failed = True
            print(f"FAIL {profile}")
            for error in errors:
                print(f"  - {error}")
        else:
            print(f"OK   {profile}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
