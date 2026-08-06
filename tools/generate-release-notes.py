#!/usr/bin/env python3
"""Extract the current VERSION section from CHANGELOG.md for GitHub Releases."""
from __future__ import annotations
import argparse
import re
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repository", default="OWNER/REPOSITORY")
    args = parser.parse_args()
    version = (args.root / "VERSION").read_text(encoding="utf-8").strip()
    text = (args.root / "CHANGELOG.md").read_text(encoding="utf-8")
    pattern = re.compile(rf"^## {re.escape(version)}\b.*?$\n(?P<body>.*?)(?=^## |\Z)", re.M | re.S)
    match = pattern.search(text)
    if not match:
        raise SystemExit(f"CHANGELOG.md has no section for {version}")
    body = match.group("body").strip()
    notes = f"# ForgeOS {version}\n\n> Beta firmware: use a spare microSD card and retain a known-good recovery card.\n\n{body}\n\n## Verification\n\nVerify downloads with `sha256sum -c SHA256SUMS`. GitHub-hosted builds may also be verified with `gh attestation verify <asset> --repo {args.repository}`.\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notes, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
