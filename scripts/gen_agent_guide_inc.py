#!/usr/bin/env python3
# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0
"""Generate cmd/sqldeep_agent_guide.inc from dist/sqldeep-agents-guide.md.

Usage:
  scripts/gen_agent_guide_inc.py          # write .inc
  scripts/gen_agent_guide_inc.py --check  # exit 1 if .inc would change
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MD = ROOT / "dist" / "sqldeep-agents-guide.md"
INC = ROOT / "cmd" / "sqldeep_agent_guide.inc"


def generate() -> str:
    parts: list[str] = ["static const char sd_agent_guide[] =\n"]
    for line in MD.read_text(encoding="utf-8").splitlines(keepends=True):
        esc = line.replace("\\", "\\\\").replace('"', '\\"')
        if esc.endswith("\n"):
            esc = esc[:-1] + "\\n"
        parts.append(f'    "{esc}"\n')
    parts.append(";\n")
    return "".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="exit 0 if .inc matches md; exit 1 if stale",
    )
    args = parser.parse_args()

    if not MD.is_file():
        print(f"error: missing {MD}", file=sys.stderr)
        return 2

    generated = generate()

    if args.check:
        if not INC.is_file():
            print(f"error: missing {INC}", file=sys.stderr)
            return 1
        if INC.read_text(encoding="utf-8") != generated:
            print(
                f"error: {INC.relative_to(ROOT)} is stale; "
                f"run scripts/gen_agent_guide_inc.py",
                file=sys.stderr,
            )
            return 1
        print(f"ok: {INC.relative_to(ROOT)} matches {MD.relative_to(ROOT)}")
        return 0

    INC.write_text(generated, encoding="utf-8")
    print(f"wrote {INC.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
