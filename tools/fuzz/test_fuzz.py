"""Checks on the fuzzing tools themselves; ctest runs them as FuzzToolTests.

    python tools/fuzz/test_fuzz.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_program  # noqa: E402
import run  # noqa: E402
import shrink  # noqa: E402

GENERATOR = Path(__file__).resolve().parent / "generate_program.py"


def count(pattern: str, text: str) -> int:
    return len(re.findall(pattern, text, flags=re.MULTILINE))


class GeneratorTests(unittest.TestCase):
    def test_two_runs_with_one_seed_write_identical_text(self):
        runs = [
            subprocess.run(
                [sys.executable, str(GENERATOR), "17"], capture_output=True, text=True, encoding="utf-8", check=True
            ).stdout
            for _ in range(2)
        ]
        self.assertEqual(runs[0], runs[1])
        self.assertEqual(runs[0], generate_program.generate(17))

    def test_different_seeds_write_different_programs(self):
        self.assertNotEqual(generate_program.generate(1), generate_program.generate(2))

    def test_the_module_is_named_after_the_seed_unless_given(self):
        self.assertTrue(generate_program.generate(5).startswith("module fuzz_seed_5\n"))
        self.assertTrue(generate_program.generate(5, "fuzz_slot").startswith("module fuzz_slot\n"))

    def test_programs_stay_within_the_design_sizes(self):
        for seed in range(1, 101):
            text = generate_program.generate(seed)
            with self.subTest(seed=seed):
                self.assertTrue(2 <= count(r"^trait ", text) <= 5)
                # One extra phase rule when no rule has a tick handler to emit from.
                self.assertTrue(3 <= count(r"^rule ", text) <= 9)
                self.assertTrue(2 <= count(r"^entity ", text) <= generate_program.MAX_ENTITIES)


FAKE_COMPILER = """
import sys
mode = sys.argv[1]
if mode == "reject":
    print("prog.cactus:3:1: error: unorientable conflict", file=sys.stderr)
    sys.exit(1)
if mode == "silent":
    sys.exit(1)
sys.exit(3)
"""

CONFLICT = """module fuzz_slot

use std.core

trait Score:
    var value: int = 0

entity Board:
    Score

rule First:
    on tick:
        Board[Score].value = 1

rule Second:
    on tick:
        Board[Score].value = 2
"""


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        fake = self.root / "fake_cactus.py"
        fake.write_text(FAKE_COMPILER, encoding="utf-8")
        self.fake = fake

    def tearDown(self):
        self.temp.cleanup()

    def check_with(self, mode: str) -> run.Outcome:
        slot = run.Slot(self.root, compiler=[sys.executable, str(self.fake), mode])
        return run.check_program(generate_program.generate(3), slot)

    def test_a_rejection_with_a_diagnostic_passes(self):
        outcome = self.check_with("reject")
        self.assertEqual(outcome.kind, "rejected")
        self.assertFalse(outcome.failed)

    def test_a_rejection_without_a_diagnostic_fails(self):
        self.assertEqual(self.check_with("silent").kind, "no-diagnostic")

    def test_an_abnormal_compiler_exit_is_a_crash(self):
        outcome = self.check_with("crash")
        self.assertEqual(outcome.kind, "compiler-crash")
        self.assertTrue(outcome.failed)

    def test_the_slot_program_takes_the_slot_module_name(self):
        self.check_with("reject")
        text = (self.root / "fuzz" / "slot" / "fuzz_slot.cactus").read_text(encoding="utf-8")
        self.assertTrue(text.startswith("module fuzz_slot\n"))

    def test_a_digest_failure_names_the_pair_and_frame(self):
        output = (
            "unordered pairs: 4\n"
            "pair swap differs: m.A/on std.core.tick | m.B/on added m.T | frame 3\n"
            "pair swap differs: m.C/on std.core.tick | m.D/on std.core.tick | frame 1\n"
        )
        self.assertEqual(run.parse_pair_failure(output), (("m.A/on std.core.tick", "m.B/on added m.T"), 3))

    def test_a_digest_failure_keeps_the_seed_program_pair_and_frame(self):
        outcome = run.Outcome("digest", pairs=4, pair=("m.A/on std.core.tick", "m.B/on std.core.tick"), frame=3)
        run.keep_failure(self.root / "failures", 42, CONFLICT, outcome)
        self.assertEqual((self.root / "failures" / "42.cactus").read_text(encoding="utf-8"), CONFLICT)
        kept = (self.root / "failures" / "42.txt").read_text(encoding="utf-8")
        self.assertIn("seed: 42", kept)
        self.assertIn("pair: m.A/on std.core.tick | m.B/on std.core.tick", kept)
        self.assertIn("first differing frame: 3", kept)

    def test_a_timeout_passes_only_after_a_multiplying_cycle_warning(self):
        warned = run.timeout_outcome(f"prog.cactus:4:5: warning: {run.MULTIPLYING_CYCLE}: one run of R1 ...")
        self.assertEqual(warned.kind, "runaway")
        self.assertFalse(warned.failed)
        silent = run.timeout_outcome("")
        self.assertEqual(silent.kind, "timeout")
        self.assertTrue(silent.failed)

    def test_digest_failures_match_only_on_the_same_pair(self):
        first = run.Outcome("digest", pair=("a", "b"))
        self.assertTrue(first.same_failure(run.Outcome("digest", pair=("a", "b"), frame=5)))
        self.assertFalse(first.same_failure(run.Outcome("digest", pair=("a", "c"))))
        self.assertFalse(first.same_failure(run.Outcome("cpp-compile")))

    @unittest.skipUnless(os.environ.get("CACTUS_BUILD_DIR"), "needs CACTUS_BUILD_DIR with a built cactus compiler")
    def test_the_compiler_rejects_an_unorientable_conflict_with_a_diagnostic(self):
        slot = run.Slot(
            Path(os.environ["CACTUS_BUILD_DIR"]), program_path=self.root / "scratch" / "fuzz_slot.cactus"
        )
        outcome = run.check_program(CONFLICT, slot)
        self.assertEqual(outcome.kind, "rejected", outcome.log)


SEVEN_RULES = """module fuzz_slot

use std.core

trait T0:
    var f0: int = 0
    var f1: int = 0

entity N0:
    T0

entity N1:
    T0

rule R0:
    on tick:
        N1[T0].f1 += 1
        N1[T0].f1 += 2

rule R1:
    on tick:
        N1[T0].f1 += 3
        N0[T0].f0 += 1
        if N1[T0].f1 > 2:
            N1[T0].f1 += 4

rule R2:
    each:
        T0 as a0
    on tick:
        a0.f1 += 1

rule R3:
    on tick:
        N1[T0].f1 += 5

rule R4:
    after:
        R0
    on tick:
        N1[T0].f1 += 6
        N0[T0].f0 = 2

rule R5:
    on tick:
        N1[T0].f1 += 7

rule R6:
    on tick:
        N1[T0].f1 += 8
"""

GUILTY_PAIR = ("fuzz_slot.R1/on std.core.tick", "fuzz_slot.R4/on std.core.tick")


def fake_check(text: str) -> run.Outcome:
    """Fails like a compiler gap: R1's Accum and R4's Assign on N0[T0].f0 race."""
    lines = text.split("\n")
    for index, line in enumerate(lines):
        body_follows = index + 1 < len(lines) and shrink.indent_of(lines[index + 1]) > 4
        if line.startswith("    on ") and not body_follows:
            return run.Outcome("rejected")
    if "N0[T0].f0 += 1" in text and "N0[T0].f0 = 2" in text and "entity N0:" in text:
        return run.Outcome("digest", pair=GUILTY_PAIR, frame=1)
    return run.Outcome("accepted")


class ShrinkerTests(unittest.TestCase):
    def test_a_seven_rule_counterexample_shrinks_to_its_guilty_pair(self):
        target = fake_check(SEVEN_RULES)
        self.assertEqual(target.kind, "digest")
        shrunk = shrink.shrink(SEVEN_RULES, target, fake_check)

        outcome = fake_check(shrunk)
        self.assertTrue(outcome.same_failure(target))
        self.assertEqual(re.findall(r"^rule (\w+)", shrunk, flags=re.MULTILINE), ["R1", "R4"])
        self.assertNotIn("entity N1", shrunk)
        self.assertNotIn("after:", shrunk)
        self.assertNotIn("+= 3", shrunk)

    def test_no_single_removal_keeps_the_shrunk_failure(self):
        target = fake_check(SEVEN_RULES)
        shrunk = shrink.shrink(SEVEN_RULES, target, fake_check)
        lines = shrunk.split("\n")
        for unit in shrink.units(lines, shrink.pair_rules(target)):
            with self.subTest(unit=unit):
                self.assertFalse(fake_check("\n".join(shrink.without(lines, unit))).same_failure(target))

    def test_the_pair_handlers_are_never_removed(self):
        lines = SEVEN_RULES.split("\n")
        kept = shrink.pair_rules(fake_check(SEVEN_RULES))
        self.assertEqual(kept, {"R1", "R4"})
        removed = [lines[unit.first] for unit in shrink.units(lines, kept) if unit.kind in ("rule", "handler")]
        self.assertNotIn("rule R1:", removed)
        self.assertNotIn("rule R4:", removed)
        self.assertNotIn("    on tick:", [lines[unit.first] for unit in shrink.units(lines, kept) if unit.kind == "clause"])


if __name__ == "__main__":
    unittest.main()
