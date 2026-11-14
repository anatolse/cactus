"""Checks generated programs for schedule independence, one at a time.

    python tools/fuzz/run.py --from 1 --to 500 [--build-dir build-clang-o3-release]

For each seed: compile the program. A rejection with diagnostics passes. An
accepted program is built into the `fuzz_schedule_slot` target (configured by
tests/CMakeLists.txt with LINEARIZATION_HARNESS) and run with every unordered
pair of handlers in both orders. A compiler crash, a rejection without a
diagnostic, generated C++ that does not compile, or a digest difference is a
failure: the program and a report go to `fuzz-failures/<seed>.cactus` and
`fuzz-failures/<seed>.txt`.
"""

from __future__ import annotations

import argparse
import dataclasses
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_program  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
SLOT_TARGET = "fuzz_schedule_slot"
PAIR_TAG = "[linearization-pairs]"
COMPILE_TIMEOUT = 600.0
HARNESS_TIMEOUT = 120.0
MULTIPLYING_CYCLE = "this cycle can multiply its work each activation"
PASSING_KINDS = ("accepted", "rejected", "runaway")


@dataclasses.dataclass
class Outcome:
    # Failing kinds: compiler-crash, no-diagnostic, cpp-compile, timeout, digest.
    kind: str
    pairs: int = 0
    # For a digest difference: the first failing pair and frame.
    pair: tuple[str, str] | None = None
    frame: int | None = None
    log: str = ""

    @property
    def failed(self) -> bool:
        return self.kind not in PASSING_KINDS

    def same_failure(self, other: "Outcome") -> bool:
        return self.kind == other.kind and (self.kind != "digest" or self.pair == other.pair)


@dataclasses.dataclass
class Slot:
    build_dir: Path
    # Replace the build's cactus compiler and slot program, for testing this runner.
    compiler: list[str] | None = None
    program_path: Path | None = None

    @property
    def program(self) -> Path:
        return self.program_path or self.build_dir / "fuzz" / "slot" / "fuzz_slot.cactus"

    @property
    def compile_command(self) -> list[str]:
        if self.compiler is not None:
            return self.compiler
        return [str(self.build_dir / ("cactus.exe" if sys.platform == "win32" else "cactus"))]

    @property
    def runner(self) -> Path:
        name = SLOT_TARGET + (".exe" if sys.platform == "win32" else "")
        return self.build_dir / "tests" / name


def run(command: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, cwd=REPO
    )


def parse_pair_failure(output: str) -> tuple[tuple[str, str] | None, int | None]:
    found = re.search(r"^pair swap differs: (.+?) \| (.+?) \| frame (\d+)$", output, flags=re.MULTILINE)
    if found is None:
        return None, None
    return (found.group(1), found.group(2)), int(found.group(3))


def timeout_outcome(compiler_log: str) -> Outcome:
    """A harness run that did not finish is expected only for a warned multiplying cycle."""
    kind = "runaway" if MULTIPLYING_CYCLE in compiler_log else "timeout"
    return Outcome(kind, log=f"harness timed out after {HARNESS_TIMEOUT:.0f} s\n{compiler_log}")


def check_program(text: str, slot: Slot) -> Outcome:
    """Compiles `text`; when accepted, builds the slot and runs every pair swap."""
    text = re.sub(r"\Amodule \w+", f"module {slot.program.stem}", text)
    slot.program.parent.mkdir(parents=True, exist_ok=True)
    if not slot.program.exists() or slot.program.read_text(encoding="utf-8") != text:
        slot.program.write_text(text, encoding="utf-8", newline="\n")
    scratch = slot.program.with_suffix(".check.cpp")
    try:
        compiled = run(
            [*slot.compile_command, str(slot.program), "--backend", "cpp-entt", "--output", str(scratch)], COMPILE_TIMEOUT
        )
    except subprocess.TimeoutExpired:
        return Outcome("compiler-crash", log="compiler timed out")
    compiler_log = compiled.stdout + compiled.stderr
    if compiled.returncode not in (0, 1):
        return Outcome("compiler-crash", log=f"exit {compiled.returncode}\n{compiler_log}")
    if compiled.returncode == 1:
        return Outcome("rejected" if "error:" in compiler_log else "no-diagnostic", log=compiler_log)
    built = run(["cmake", "--build", str(slot.build_dir), "--target", SLOT_TARGET], COMPILE_TIMEOUT)
    if built.returncode != 0:
        return Outcome("cpp-compile", log=built.stdout[-20000:] + built.stderr[-5000:])
    try:
        ran = run([str(slot.runner), PAIR_TAG], HARNESS_TIMEOUT)
    except subprocess.TimeoutExpired:
        return timeout_outcome(compiler_log)
    output = ran.stdout + ran.stderr
    pairs = sum(int(count) for count in re.findall(r"unordered pairs: (\d+)", output))
    if ran.returncode == 0:
        return Outcome("accepted", pairs=pairs, log=output)
    pair, frame = parse_pair_failure(output)
    return Outcome("digest", pairs=pairs, pair=pair, frame=frame, log=output)


def report(seed: int | str, outcome: Outcome) -> str:
    lines = [f"seed: {seed}", f"failure: {outcome.kind}"]
    if outcome.pair is not None:
        lines.append(f"pair: {outcome.pair[0]} | {outcome.pair[1]}")
    if outcome.frame is not None:
        lines.append(f"first differing frame: {outcome.frame}")
    lines += ["", outcome.log]
    return "\n".join(lines)


def keep_failure(failures: Path, seed: int, text: str, outcome: Outcome) -> None:
    failures.mkdir(parents=True, exist_ok=True)
    (failures / f"{seed}.cactus").write_text(text, encoding="utf-8", newline="\n")
    (failures / f"{seed}.txt").write_text(report(seed, outcome), encoding="utf-8", newline="\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--from", dest="first", type=int, required=True)
    parser.add_argument("--to", dest="last", type=int, required=True)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build-clang-o3-release")
    parser.add_argument("--failures", type=Path, default=REPO / "fuzz-failures")
    args = parser.parse_args(argv)
    slot = Slot(args.build_dir.resolve())
    counts: dict[str, int] = {}
    pair_counts: list[int] = []
    any_failed = False
    for seed in range(args.first, args.last + 1):
        started = time.monotonic()
        text = generate_program.generate(seed)
        outcome = check_program(text, slot)
        counts[outcome.kind] = counts.get(outcome.kind, 0) + 1
        if outcome.kind == "accepted":
            pair_counts.append(outcome.pairs)
        detail = f"{outcome.pairs} pairs" if outcome.kind in ("accepted", "digest") else ""
        print(f"seed {seed}: {outcome.kind} {detail} ({time.monotonic() - started:.0f} s)", flush=True)
        if outcome.failed:
            any_failed = True
            keep_failure(args.failures, seed, text, outcome)
    total = args.last - args.first + 1
    accepted = sum(counts.get(kind, 0) for kind in ("accepted", "runaway", "timeout", "digest"))
    print(f"checked {total}: " + ", ".join(f"{kind} {count}" for kind, count in sorted(counts.items())))
    print(f"accept rate {accepted / total:.0%}")
    if pair_counts:
        print(f"unordered pairs per passing program: mean {sum(pair_counts) / len(pair_counts):.1f}, "
              f"zero in {pair_counts.count(0)}")
    return 1 if any_failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
