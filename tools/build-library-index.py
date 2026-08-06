#!/usr/bin/env python3
"""Build ForgeShell metadata and per-game override indexes from a desktop CSV."""
from __future__ import annotations

import argparse
import csv
import shutil
import sys
from pathlib import Path

FIELDS = (
    "path", "title", "artwork", "emulator_id", "cpu_profile", "aspect",
    "scaling", "frameskip", "bios",
)
CPU = {"default", "eco", "balanced", "performance"}
ASPECT = {"default", "original", "4:3", "fullscreen"}
SCALING = {"default", "nearest", "smooth"}
FRAMESKIP = {-1, 0, 1, 2, 5}


def clean(value: str, field: str) -> str:
    value = value.strip()
    if any(ch in value for ch in "\t\r\n"):
        raise ValueError(f"{field} contains a tab or newline")
    return value


def prepare_art(source: Path, output_dir: Path, index: int) -> str:
    if not source.is_file():
        raise ValueError(f"artwork does not exist: {source}")
    output_dir.mkdir(parents=True, exist_ok=True)
    target = output_dir / f"game-{index:04d}.bmp"
    try:
        from PIL import Image, ImageOps  # type: ignore
    except ImportError:
        if source.suffix.lower() != ".bmp":
            raise ValueError("Pillow is required to convert non-BMP artwork")
        shutil.copyfile(source, target)
        return target.as_posix()
    with Image.open(source) as image:
        image = image.convert("RGB")
        image = ImageOps.fit(image, (64, 80), method=Image.Resampling.LANCZOS)
        image.save(target, format="BMP")
    return target.as_posix()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create ForgeShell metadata.tsv and game-overrides.tsv files."
    )
    parser.add_argument("csv_file", type=Path, help="CSV with a header using ForgeShell fields")
    parser.add_argument("--output", type=Path, required=True, help="Output directory copied to /mnt/forgeshell")
    parser.add_argument(
        "--device-art-root", default="/mnt/forgeshell/library/art",
        help="Path written into metadata for generated artwork",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.csv_file.is_file():
        print(f"error: CSV not found: {args.csv_file}", file=sys.stderr)
        return 2
    args.output.mkdir(parents=True, exist_ok=True)
    art_output = args.output / "library" / "art"
    metadata_rows: list[tuple[str, str, str]] = []
    override_rows: list[tuple[str, str, str, str, str, int, str]] = []
    seen: set[str] = set()
    try:
        with args.csv_file.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            if reader.fieldnames is None or "path" not in reader.fieldnames:
                raise ValueError("CSV must include a path column")
            for number, raw in enumerate(reader, start=2):
                row = {field: clean(raw.get(field, "") or "", field) for field in FIELDS}
                path = row["path"]
                if not path:
                    continue
                if path in seen:
                    raise ValueError(f"row {number}: duplicate path: {path}")
                seen.add(path)
                title = row["title"]
                art_device = ""
                if row["artwork"]:
                    generated = prepare_art(Path(row["artwork"]).expanduser(), art_output, len(metadata_rows))
                    art_device = f"{args.device_art_root.rstrip('/')}/{Path(generated).name}"
                if title or art_device:
                    metadata_rows.append((path, title, art_device))
                cpu = row["cpu_profile"] or "default"
                aspect = row["aspect"] or "default"
                scaling = row["scaling"] or "default"
                frameskip = int(row["frameskip"] or "-1")
                if cpu not in CPU:
                    raise ValueError(f"row {number}: invalid cpu_profile {cpu!r}")
                if aspect not in ASPECT:
                    raise ValueError(f"row {number}: invalid aspect {aspect!r}")
                if scaling not in SCALING:
                    raise ValueError(f"row {number}: invalid scaling {scaling!r}")
                if frameskip not in FRAMESKIP:
                    raise ValueError(f"row {number}: frameskip must be -1, 0, 1, 2, or 5")
                if any((row["emulator_id"], cpu != "default", aspect != "default",
                        scaling != "default", frameskip != -1, row["bios"])):
                    override_rows.append((path, row["emulator_id"], cpu, aspect,
                                          scaling, frameskip, row["bios"]))
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    metadata_path = args.output / "library" / "metadata.tsv"
    override_path = args.output / "state" / "game-overrides.tsv"
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    override_path.parent.mkdir(parents=True, exist_ok=True)
    with metadata_path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# ForgeShell metadata v1\n")
        for row in metadata_rows:
            handle.write("\t".join(row) + "\n")
    with override_path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# ForgeShell per-game overrides v1\n")
        for path, emulator, cpu, aspect, scaling, frameskip, bios in override_rows:
            handle.write(f"{path}\t{emulator}\t{cpu}\t{aspect}\t{scaling}\t{frameskip}\t{bios}\n")
    print(f"Wrote {len(metadata_rows)} metadata rows to {metadata_path}")
    print(f"Wrote {len(override_rows)} override rows to {override_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
