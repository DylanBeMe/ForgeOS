#!/usr/bin/env python3
"""Generate a deterministic emulator-version report from a MiyooCFW tree."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

SELECTED_RE = re.compile(
    r"^BR2_PACKAGE_(RETROARCH|LIBRETRO_[A-Z0-9_]+|IPK_[A-Z0-9_]+)=y$"
)
ASSIGN_RE = re.compile(r"^([A-Z0-9_]+)_(VERSION|SITE)\s*[:?+]?=\s*(.*?)\s*$")

# These are RetroArch data/support packages, not runnable emulator cores.
LIBRETRO_SUPPORT = {
    "LIBRETRO_ASSETS",
    "LIBRETRO_CORE_INFO",
    "LIBRETRO_DATABASE",
}


@dataclass(frozen=True)
class PackageInfo:
    symbol: str
    kind: str
    version: str
    site: str
    source_file: str


def normalize_name(symbol: str) -> str:
    if symbol == "RETROARCH":
        return "RetroArch"
    if symbol.startswith("LIBRETRO_"):
        return symbol.removeprefix("LIBRETRO_").lower().replace("_", "-")
    if symbol.startswith("IPK_"):
        return symbol.removeprefix("IPK_").lower().replace("_", "-")
    return symbol.lower().replace("_", "-")


def kind_for(symbol: str) -> str:
    if symbol == "RETROARCH":
        return "frontend"
    if symbol.startswith("LIBRETRO_"):
        return "core"
    return "standalone"


def selected_symbols(defconfig: Path) -> list[str]:
    selected: set[str] = set()
    for raw in defconfig.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SELECTED_RE.match(raw.strip())
        if not match:
            continue
        symbol = match.group(1)
        if symbol not in LIBRETRO_SUPPORT:
            selected.add(symbol)
    return sorted(selected)


def parse_makefiles(tree: Path) -> dict[str, list[tuple[Path, dict[str, str]]]]:
    index: dict[str, list[tuple[Path, dict[str, str]]]] = {}
    package_root = tree / "package"
    if not package_root.is_dir():
        return index

    for mk in sorted(package_root.rglob("*.mk")):
        assignments: dict[str, dict[str, str]] = {}
        try:
            lines = mk.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for raw in lines:
            match = ASSIGN_RE.match(raw.strip())
            if not match:
                continue
            prefix, field, value = match.groups()
            assignments.setdefault(prefix, {})[field.lower()] = value
        for prefix, values in assignments.items():
            index.setdefault(prefix, []).append((mk, values))
    return index


def package_candidates(symbol: str) -> list[str]:
    candidates = [symbol]
    if symbol == "RETROARCH":
        candidates.insert(0, "LIBRETRO_RETROARCH")
    elif symbol.startswith("IPK_"):
        # Miyoo IPK wrappers are not completely uniform: some .mk files use
        # IPK_FOO_VERSION while others retain the underlying FOO_VERSION.
        candidates.append(symbol.removeprefix("IPK_"))
    return list(dict.fromkeys(candidates))


def choose_package(
    tree: Path, symbol: str, index: dict[str, list[tuple[Path, dict[str, str]]]]
) -> PackageInfo:
    matches: list[tuple[int, Path, dict[str, str]]] = []
    for candidate_rank, candidate in enumerate(package_candidates(symbol)):
        for mk, values in index.get(candidate, []):
            matches.append((candidate_rank, mk, values))

    if matches:
        # Prefer exact variable-name matches, Miyoo-specific package definitions,
        # then the shortest deterministic path. This avoids a generic Buildroot
        # package winning over the Q90-patched package.
        matches.sort(
            key=lambda item: (
                item[0],
                0 if "package/miyoo/" in item[1].as_posix() else 1,
                len(item[1].parts),
                item[1].as_posix(),
            )
        )
        _, mk, values = matches[0]
        return PackageInfo(
            symbol=symbol,
            kind=kind_for(symbol),
            version=values.get("version", "unknown"),
            site=values.get("site", "unknown"),
            source_file=mk.relative_to(tree).as_posix(),
        )

    return PackageInfo(
        symbol=symbol,
        kind=kind_for(symbol),
        version="unknown",
        site="unknown",
        source_file="not found",
    )


def section(title: str, packages: list[PackageInfo]) -> list[str]:
    lines = [f"{title} ({len(packages)})"]
    if not packages:
        lines.append("  none")
        return lines
    for pkg in sorted(packages, key=lambda item: normalize_name(item.symbol)):
        lines.append(
            f"  {normalize_name(pkg.symbol):24} {pkg.version:40} {pkg.source_file}"
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tree", type=Path, required=True)
    parser.add_argument("--forge-version", required=True)
    parser.add_argument("--upstream-ref", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    tree = args.tree.resolve()
    defconfig = tree / "configs" / "miyoo_uclibc_defconfig"
    if not defconfig.is_file():
        raise SystemExit(f"missing MiyooCFW defconfig: {defconfig}")

    symbols = selected_symbols(defconfig)
    index = parse_makefiles(tree)
    packages = [choose_package(tree, symbol, index) for symbol in symbols]

    frontends = [pkg for pkg in packages if pkg.kind == "frontend"]
    cores = [pkg for pkg in packages if pkg.kind == "core"]
    standalone = [pkg for pkg in packages if pkg.kind == "standalone"]
    unresolved = [pkg for pkg in packages if pkg.version == "unknown"]

    lines = [
        "ForgeOS Q90 Emulator Manifest",
        f"ForgeOS version: {args.forge_version}",
        f"MiyooCFW Buildroot: {args.upstream_ref}",
        "Policy: newest Q90-integrated compatibility snapshot; no untested nightly binaries",
        f"Selected packages: {len(packages)}",
        f"Version metadata unresolved: {len(unresolved)}",
        "",
    ]
    lines.extend(section("FRONTEND", frontends))
    lines.append("")
    lines.extend(section("LIBRETRO CORES", cores))
    lines.append("")
    lines.extend(section("STANDALONE EMULATOR PACKAGES", standalone))
    if unresolved:
        lines.append("")
        lines.append("UNRESOLVED VERSION METADATA")
        for pkg in unresolved:
            lines.append(f"  {pkg.symbol} ({pkg.source_file})")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
