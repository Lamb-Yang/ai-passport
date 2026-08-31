#!/usr/bin/env python3
"""Generate C level data for the Camping puzzle from the original web game.

Reads the level JSON embedded in camping-master.html (the source of truth) and
writes:

  main/camping_levels.c      firmware level table (board + clues + counts)
  tests/camping_answer_data.c  host-test-only fixture with the reference answers

Usage: python3 tools/gen_camping_levels.py <camping-master.html>
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SIZE = 6
DIFFICULTIES = ("easy", "medium", "hard")
MAX_TREES = 8
MAX_SINGLES = 8
MAX_DOUBLES = 4

LEVELS_C = """\
// main/camping_levels.c —— 露营达人关卡数据(由 tools/gen_camping_levels.py 生成,勿手改)。
// 来源:原网页版 camping-master.html 内嵌的 level-data JSON。
#include "camping_levels.h"

const camping_level_t *camping_levels_of(camping_diff_t diff)
{{
    static const camping_level_t *const tables[CAMPING_DIFF_COUNT] = {{
        CAMPING_LEVELS_EASY, CAMPING_LEVELS_MEDIUM, CAMPING_LEVELS_HARD,
    }};
    return tables[diff];
}}

{bodies}\
"""

ANSWER_C = """\
// tests/camping_answer_data.c —— 关卡标准答案(仅供宿主机测试验证判胜逻辑)。
// 由 tools/gen_camping_levels.py 生成,勿手改。不参与固件编译。
#include "camping_answer.h"

{bodies}\
"""


def load_levels(html_path: Path) -> dict:
    text = html_path.read_text(encoding="utf-8")
    match = re.search(
        r'<script id="level-data" type="application/json">(.*?)</script>', text, re.S
    )
    if not match:
        sys.exit("level-data script block not found")
    try:
        levels = json.loads(match.group(1))
    except json.JSONDecodeError as exc:
        sys.exit(f"level-data JSON is invalid: {exc}")
    for key in DIFFICULTIES:
        if key not in levels or not levels[key]:
            sys.exit(f"missing or empty difficulty: {key}")
    return levels


def validate(levels: dict) -> None:
    errors: list[str] = []
    for key in DIFFICULTIES:
        for i, lv in enumerate(levels[key]):
            where = f"{key}[{i}]"
            if len(lv["t"]) > MAX_TREES:
                errors.append(f"{where}: too many trees")
            if len(lv["r"]) != SIZE or len(lv["c"]) != SIZE:
                errors.append(f"{where}: clue size")
            if len(lv["a"]["s"]) > MAX_SINGLES:
                errors.append(f"{where}: too many singles")
            if len(lv["a"]["d"]) > MAX_DOUBLES:
                errors.append(f"{where}: too many doubles")
            if len(lv["a"]["s"]) != lv["s"]:
                errors.append(f"{where}: singles mismatch")
            if len(lv["a"]["d"]) != lv["d"]:
                errors.append(f"{where}: doubles mismatch")
            for cell in lv["t"] + lv["a"]["s"]:
                if not 0 <= cell < SIZE * SIZE:
                    errors.append(f"{where}: cell out of range ({cell})")
            for pair in lv["a"]["d"]:
                if len(pair) != 2:
                    errors.append(f"{where}: pair size")
    if errors:
        sys.exit("invalid level data:\n  " + "\n  ".join(errors))


def level_body(key: str, levels: list[dict]) -> str:
    lines = [
        f"const camping_level_t CAMPING_LEVELS_{key.upper()}[{len(levels)}] = {{"
    ]
    for lv in levels:
        trees = ", ".join(str(c) for c in lv["t"])
        rows = ", ".join(str(c) for c in lv["r"])
        cols = ", ".join(str(c) for c in lv["c"])
        lines.append(
            f"    {{ .trees = {{ {trees} }}, .tree_count = {len(lv['t'])},"
            f" .row_clues = {{ {rows} }},"
            f" .col_clues = {{ {cols} }},"
            f" .singles = {lv['s']}, .doubles = {lv['d']} }},"
        )
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def answer_body(key: str, levels: list[dict]) -> str:
    lines = [f"const camping_answer_t CAMPING_ANSWERS_{key.upper()}[{len(levels)}] = {{"]
    for lv in levels:
        singles = ", ".join(str(c) for c in lv["a"]["s"])
        cells = []
        for pair in lv["a"]["d"]:
            cells.append(f"{{ {pair[0]}, {pair[1]} }}")
        pairs = ", ".join(cells)
        lines.append(
            f"    {{ .single_cells = {{ {singles} }},"
            f" .single_count = {len(lv['a']['s'])},"
            f" .double_cells = {{ {pairs} }},"
            f" .double_count = {len(lv['a']['d'])} }},"
        )
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    levels = load_levels(Path(sys.argv[1]))
    validate(levels)

    count = sum(len(levels[k]) for k in DIFFICULTIES)
    bodies = "\n".join(level_body(k, levels[k]) for k in DIFFICULTIES)
    (REPO_ROOT / "main" / "camping_levels.c").write_text(
        LEVELS_C.format(bodies=bodies), encoding="utf-8"
    )

    answer_bodies = "\n".join(answer_body(k, levels[k]) for k in DIFFICULTIES)
    (REPO_ROOT / "tests" / "camping_answer_data.c").write_text(
        ANSWER_C.format(bodies=answer_bodies), encoding="utf-8"
    )
    print(f"wrote {count} levels -> main/camping_levels.c, tests/camping_answer_data.c")


if __name__ == "__main__":
    main()
