#!/usr/bin/env python3
"""Check whether a ForgeOS platform adapter is not only valid, but build-ready."""
from __future__ import annotations

import argparse
import configparser
import re
import subprocess
import sys
from pathlib import Path

ASSIGN_RE = re.compile(r"^([A-Z0-9_]+)=(.*)$")


def read_build_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ASSIGN_RE.match(line)
        if not match:
            continue
        key, value = match.groups()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[key] = value
    return values


def parse_profile(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    parser.optionxform = str.lower
    with path.open("r", encoding="utf-8") as handle:
        parser.read_file(handle)
    return parser


def manifest_commands(path: Path) -> list[str]:
    commands: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) == 5:
            commands.append(fields[3].strip())
    return commands


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("target")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    root = args.root.resolve()
    platform_dir = root / "platforms" / args.target
    profile_path = platform_dir / "platform.ini"
    build_env_path = platform_dir / "build.env"
    errors: list[str] = []
    warnings: list[str] = []

    if not profile_path.is_file():
        print(f"FAIL {args.target}: missing {profile_path}", file=sys.stderr)
        return 1
    validation = subprocess.run(
        [sys.executable, str(root / "tools" / "validate-platform.py"), str(profile_path)],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if validation.returncode != 0:
        print(validation.stdout, end="", file=sys.stderr)
        return validation.returncode

    profile = parse_profile(profile_path)
    if not build_env_path.is_file():
        errors.append("missing build.env")
        build_env: dict[str, str] = {}
    else:
        build_env = read_build_env(build_env_path)
        backend = build_env.get("FORGE_BUILD_BACKEND", "")
        if not backend or backend == "unimplemented":
            errors.append("build backend is not implemented")

    provider = profile["launcher"]["provider"].strip().lower()
    if provider == "forge-manifest" and not (platform_dir / "systems.ini").is_file():
        errors.append("forge-manifest provider has no systems.ini")

    tools_path = platform_dir / "tools.tsv"
    if not tools_path.is_file():
        errors.append("missing tools.tsv")
    else:
        commands = manifest_commands(tools_path)
        overlay_rel = build_env.get("FORGE_OVERLAY_DIR")
        runtime_roots: list[Path] = [platform_dir / "runtime" / "tools"]
        if overlay_rel:
            overlay = root / overlay_rel
            runtime_roots.append(overlay / "main" / "apps" / "forge-tools")
            runtime_manifest = overlay / "main" / "forgeshell" / "tools.tsv"
            runtime_profile = overlay / "main" / "forgeshell" / "device.ini"
            if runtime_manifest.is_file() and runtime_manifest.read_bytes() != tools_path.read_bytes():
                errors.append("runtime tools.tsv differs from canonical platform tools.tsv")
            if runtime_profile.is_file() and runtime_profile.read_bytes() != profile_path.read_bytes():
                errors.append("runtime device.ini differs from canonical platform.ini")
        for command in commands:
            if not command.startswith("${tool_root}/"):
                continue
            basename = command[len("${tool_root}/"):].split()[0]
            candidates = [runtime_root / basename for runtime_root in runtime_roots]
            if not any(candidate.is_file() for candidate in candidates):
                warnings.append(f"tool implementation not found in source tree: {basename}")

    caps = profile["capabilities"]
    if caps.get("cpu_profiles", "0").strip().lower() in {"1", "yes", "true", "on"}:
        helper = profile["performance"].get("cpu_helper", "").strip()
        if not helper:
            errors.append("cpu_profiles is enabled but performance.cpu_helper is empty")

    if errors:
        print(f"FAIL {args.target}: adapter is schema-valid but not build-ready", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        for warning in warnings:
            print(f"  ! {warning}", file=sys.stderr)
        return 1

    print(f"OK   {args.target}: schema-valid and build-ready")
    for warning in warnings:
        print(f"WARN {args.target}: {warning}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
