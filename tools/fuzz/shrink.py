"""Shrinks a failing program while it keeps failing the same way.

    python tools/fuzz/shrink.py fuzz-failures/123.cactus [--build-dir build-clang-o3-release]

Removes one rule, entity, other declaration, rule clause, or handler statement
at a time and keeps a removal only when the check still fails with the same
kind (and, for a digest difference, the same pair of handlers). Stops when no
single removal keeps the failure. Writes `<name>.shrunk.cactus` and
`<name>.shrunk.txt` next to the input.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
import sys
from collections.abc import Callable
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run  # noqa: E402

TOP_LEVEL_UNITS = ("rule ", "entity ", "template ", "event ", "trait ")


@dataclasses.dataclass(frozen=True)
class Unit:
    """Lines [first, end) that one removal drops."""

    first: int
    end: int
    kind: str


def indent_of(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def block_end(lines: list[str], first: int) -> int:
    """The end of the line at `first` plus every following line indented deeper."""
    depth = indent_of(lines[first])
    end = first + 1
    while end < len(lines) and (not lines[end].strip() or indent_of(lines[end]) > depth):
        end += 1
    while end > first + 1 and not lines[end - 1].strip():
        end -= 1
    return end


def rule_name(line: str) -> str:
    return line.split()[1].rstrip(":")


def units(lines: list[str], keep_rules: set[str]) -> list[Unit]:
    """Removable units, largest first: rules and entities, then handlers and
    clauses, then statements. The rules in `keep_rules` keep their handlers."""
    top: list[Unit] = []
    middle: list[Unit] = []
    inner: list[Unit] = []
    kept = False
    for index, line in enumerate(lines):
        if not line.strip():
            continue
        depth = indent_of(line)
        unit = Unit(index, block_end(lines, index), "statement")
        if depth == 0:
            kept = line.startswith("rule ") and rule_name(line) in keep_rules
            if line.startswith(TOP_LEVEL_UNITS) and not kept:
                top.append(dataclasses.replace(unit, kind=line.split()[0]))
        elif depth > 4:
            inner.append(unit)
        elif not line.lstrip().startswith("on "):
            middle.append(dataclasses.replace(unit, kind="clause"))
        elif not kept:
            middle.append(dataclasses.replace(unit, kind="handler"))
    return top + middle + inner


def without(lines: list[str], unit: Unit) -> list[str]:
    return lines[: unit.first] + lines[unit.end :]


def pair_rules(outcome: run.Outcome) -> set[str]:
    """The rule names of the failing pair's handlers, which shrinking keeps."""
    if outcome.pair is None:
        return set()
    return {identity.split("/")[0].rsplit(".", 1)[-1] for identity in outcome.pair}


def shrink(
    text: str,
    target: run.Outcome,
    check: Callable[[str], run.Outcome],
    log: Callable[[str], None] = lambda message: None,
) -> str:
    lines = text.split("\n")
    keep = pair_rules(target)
    progress = True
    while progress:
        progress = False
        index = 0
        while index < len(candidates := units(lines, keep)):
            unit = candidates[index]
            trial = without(lines, unit)
            outcome = check("\n".join(trial))
            if outcome.same_failure(target):
                log(f"removed {unit.kind} at line {unit.first + 1}: {lines[unit.first].strip()}")
                lines = trial
                progress = True
                continue
            index += 1
    return re.sub(r"\n{3,}", "\n\n", "\n".join(lines))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("program", type=Path)
    parser.add_argument("--build-dir", type=Path, default=run.REPO / "build-clang-o3-release")
    args = parser.parse_args(argv)
    slot = run.Slot(args.build_dir.resolve())
    text = args.program.read_text(encoding="utf-8")
    target = run.check_program(text, slot)
    if not target.failed:
        print(f"{args.program} does not fail ({target.kind}); nothing to shrink")
        return 1
    print(f"shrinking a {target.kind} failure" + (f" of {target.pair[0]} | {target.pair[1]}" if target.pair else ""))
    shrunk = shrink(text, target, lambda candidate: run.check_program(candidate, slot), print)
    final = run.check_program(shrunk, slot)
    stem = args.program.with_suffix("")
    stem.with_name(stem.name + ".shrunk.cactus").write_text(shrunk, encoding="utf-8", newline="\n")
    stem.with_name(stem.name + ".shrunk.txt").write_text(
        run.report(args.program.name, final), encoding="utf-8", newline="\n"
    )
    print(f"{len(text.splitlines())} lines -> {len(shrunk.splitlines())} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
