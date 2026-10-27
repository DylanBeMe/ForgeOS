#!/usr/bin/env python3
"""Summarize ForgeOS game compatibility results and optionally enforce a release gate."""
from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

OUTCOME_FIELDS = (
    "boot", "gameplay", "frame_pacing", "audio", "sram", "save_states", "exit_return"
)
VALID_OUTCOMES = {"pass", "minor", "fail", "na"}
PLAYABLE_REQUIRED = ("boot", "gameplay", "exit_return")
PLAYABLE_QUALITY = ("frame_pacing", "audio")


def normalize(value: str | None) -> str:
    return (value or "").strip().lower()


def completed(row: dict[str, str]) -> bool:
    return all(normalize(row.get(field)) in VALID_OUTCOMES for field in PLAYABLE_REQUIRED)


def playable(row: dict[str, str]) -> bool:
    if not completed(row):
        return False
    if any(normalize(row.get(field)) != "pass" for field in PLAYABLE_REQUIRED):
        return False
    return all(normalize(row.get(field)) not in {"fail"} for field in PLAYABLE_QUALITY)


def full_feature(row: dict[str, str]) -> bool:
    if not playable(row):
        return False
    applicable = [normalize(row.get(field)) for field in OUTCOME_FIELDS]
    return all(value in {"pass", "na"} for value in applicable if value)


def rate(numerator: int, denominator: int) -> float:
    return 0.0 if denominator == 0 else 100.0 * numerator / denominator


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("matrix", type=Path)
    parser.add_argument("--minimum-games", type=int, default=20,
                        help="minimum completed game rows required before enforcing")
    parser.add_argument("--minimum-playable", type=float, default=90.0,
                        help="minimum playable percentage when --enforce is used")
    parser.add_argument("--enforce", action="store_true",
                        help="fail if coverage or playable-rate target is not met")
    args = parser.parse_args()

    if args.minimum_games < 1 or not 0.0 <= args.minimum_playable <= 100.0:
        parser.error("minimum-games must be positive and minimum-playable must be 0..100")
    if not args.matrix.is_file():
        raise SystemExit(f"missing compatibility matrix: {args.matrix}")

    with args.matrix.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        missing = sorted(set(OUTCOME_FIELDS) - set(reader.fieldnames or ()))
        if missing:
            raise SystemExit(f"matrix is missing outcome columns: {', '.join(missing)}")
        rows = list(reader)

    invalid: list[str] = []
    finished: list[dict[str, str]] = []
    for number, row in enumerate(rows, 2):
        for field in OUTCOME_FIELDS:
            value = normalize(row.get(field))
            if value and value not in VALID_OUTCOMES:
                invalid.append(f"line {number} {field}={value!r}")
        if completed(row):
            finished.append(row)
    if invalid:
        raise SystemExit("invalid compatibility outcomes: " + "; ".join(invalid))

    playable_count = sum(playable(row) for row in finished)
    full_count = sum(full_feature(row) for row in finished)
    print("ForgeOS compatibility report")
    print(f"Completed game tests: {len(finished)}")
    if not finished:
        print("Playable rate: n/a (no completed game tests)")
        print("Full-feature rate: n/a (no completed game tests)")
        if args.enforce:
            print(f"GATE FAIL: need at least {args.minimum_games} completed game tests")
            return 1
        return 0

    print(f"Playable rate: {rate(playable_count, len(finished)):.1f}% ({playable_count}/{len(finished)})")
    print(f"Full-feature rate: {rate(full_count, len(finished)):.1f}% ({full_count}/{len(finished)})")

    systems: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in finished:
        systems[(row.get("system") or "unknown").strip() or "unknown"].append(row)
    print("Per-system playable rate:")
    for system in sorted(systems, key=str.casefold):
        system_rows = systems[system]
        count = sum(playable(row) for row in system_rows)
        print(f"  {system}: {rate(count, len(system_rows)):.1f}% ({count}/{len(system_rows)})")

    if args.enforce:
        if len(finished) < args.minimum_games:
            print(f"GATE FAIL: need at least {args.minimum_games} completed game tests")
            return 1
        actual = rate(playable_count, len(finished))
        if actual + 1e-9 < args.minimum_playable:
            print(f"GATE FAIL: playable rate {actual:.1f}% is below {args.minimum_playable:.1f}%")
            return 1
        print("GATE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
