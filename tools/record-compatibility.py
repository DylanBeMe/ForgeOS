#!/usr/bin/env python3
"""Append one validated emulator test result to a ForgeOS compatibility matrix."""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path

HEADER = ["tested_at", "firmware", "device", "system", "game", "emulator",
          "bios", "boot", "gameplay", "frame_pacing", "audio", "sram",
          "save_states", "exit_return", "cpu_profile", "frameskip", "aspect",
          "notes"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("matrix", type=Path)
    for field in HEADER[1:]:
        parser.add_argument(f"--{field.replace('_', '-')}", required=field in {"system", "game", "emulator"})
    args = parser.parse_args()
    values = vars(args)
    matrix: Path = values.pop("matrix")
    matrix.parent.mkdir(parents=True, exist_ok=True)
    new_file = not matrix.exists() or matrix.stat().st_size == 0
    with matrix.open("a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=HEADER)
        if new_file:
            writer.writeheader()
        row = {key: (value or "") for key, value in values.items()}
        row["tested_at"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
        writer.writerow(row)
    print(matrix)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
