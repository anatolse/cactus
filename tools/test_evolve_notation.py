"""Checks on the notation-search harness itself.

    python tools/test_evolve_notation.py

Stdlib unittest only -- these have to run anywhere the harness runs, with no
API key and no test dependency. What they cover: the genetic operators are
mechanically sound and reproducible, the kata line ranges still name the rules
their contracts describe, the gate actually rejects, and no absolute model
self-rating can reach the ranking.
"""

from __future__ import annotations

import copy
import random
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import evolve_notation as ev  # noqa: E402


# --------------------------------------------------------------------------
# 1.6 -- genetic operators
# --------------------------------------------------------------------------

def _random_genome(rng: random.Random, ident: str) -> ev.Genome:
    return ev.Genome(
        ident=ident,
        super_gene=rng.choice(list(ev.SUPER_GENES)),
        free={locus: rng.choice(alleles) for locus, alleles in ev.FREE_LOCI.items()},
    )


class GeneticOperators(unittest.TestCase):
    def test_crossover_never_invents_a_super_gene(self):
        rng = random.Random(7)
        for i in range(400):
            a, b = _random_genome(rng, "a"), _random_genome(rng, "b")
            child = ev.crossover(a, b, rng, f"c{i}")
            self.assertIn(child.super_gene, {a.super_gene, b.super_gene})

    def test_crossover_never_invents_a_free_allele(self):
        rng = random.Random(11)
        for i in range(400):
            a, b = _random_genome(rng, "a"), _random_genome(rng, "b")
            child = ev.crossover(a, b, rng, f"c{i}")
            self.assertEqual(set(child.free), set(ev.FREE_LOCI))
            for locus, allele in child.free.items():
                self.assertIn(allele, {a.free[locus], b.free[locus]})
            self.assertEqual(child.parents, (a.ident, b.ident))

    def test_mutation_changes_only_to_a_different_valid_allele(self):
        rng = random.Random(13)
        for i in range(400):
            g = _random_genome(rng, f"g{i}")
            m = ev.mutate(g, rng)
            self.assertIn(m.super_gene, ev.SUPER_GENES)
            for locus, allele in m.free.items():
                self.assertIn(allele, ev.FREE_LOCI[locus])
                if allele != g.free[locus]:
                    self.assertNotEqual(allele, g.free[locus])

    def test_mutation_at_certainty_moves_every_locus(self):
        rng = random.Random(17)
        g = _random_genome(rng, "g")
        free_p, super_p = ev.MUTATION_P_FREE, ev.MUTATION_P_SUPER
        ev.MUTATION_P_FREE = ev.MUTATION_P_SUPER = 1.0
        try:
            m = ev.mutate(g, rng)
        finally:
            ev.MUTATION_P_FREE, ev.MUTATION_P_SUPER = free_p, super_p
        self.assertNotEqual(m.super_gene, g.super_gene)
        for locus in ev.FREE_LOCI:
            self.assertNotEqual(m.free[locus], g.free[locus], locus)

    def test_mutation_at_zero_probability_is_identity(self):
        rng = random.Random(19)
        g = _random_genome(rng, "g")
        free_p, super_p = ev.MUTATION_P_FREE, ev.MUTATION_P_SUPER
        ev.MUTATION_P_FREE = ev.MUTATION_P_SUPER = 0.0
        try:
            m = ev.mutate(g, rng)
        finally:
            ev.MUTATION_P_FREE, ev.MUTATION_P_SUPER = free_p, super_p
        self.assertEqual(m.super_gene, g.super_gene)
        self.assertEqual(m.free, g.free)

    def test_seed_rng_reproduces_generation_zero(self):
        first = ev.seed_population(random.Random(1), 12)
        again = ev.seed_population(random.Random(1), 12)
        self.assertEqual([g.to_dict() for g in first], [g.to_dict() for g in again])
        other = ev.seed_population(random.Random(2), 12)
        self.assertNotEqual([g.to_dict() for g in first], [g.to_dict() for g in other])

    def test_seed_population_covers_every_super_gene_and_the_ancestor(self):
        pop = ev.seed_population(random.Random(1), 12)
        self.assertEqual(len(pop), 12)
        self.assertTrue(pop[0].is_ancestor)
        self.assertEqual(pop[0].free, ev.ANCESTOR_FREE)
        self.assertEqual({g.super_gene for g in pop[1:]}, set(ev.SUPER_GENES))
        self.assertEqual(len({g.ident for g in pop}), 12)


# --------------------------------------------------------------------------
# 2.5 -- the kata line ranges still name the rules their contracts describe
# --------------------------------------------------------------------------

ARENA = ev.REPO / "examples" / "first-person-arena" / "main.cactus"

EXPECTED_DECLARATIONS = {
    "K01": ["ReadPlayerLook", "MovePlayer"],
    "K02": ["BeginGrounding", "IntegrateVerticalMotion",
            "DetectGroundCandidate", "ApplyGroundCandidate"],
    "K03": ["DetectActorSolidContact", "ResolveActorSolidContact"],
    "K04": ["DetectEnemySeparation"],
    "K05": ["SpawnRobots", "SpawnKnights", "AnimateEnemyDeath"],
    "K06": ["CooldownShooter", "FireBullet", "MoveBullets",
            "DetectBulletSolidContact", "DetectBulletEnemyContact",
            "ConsumeBulletContact"],
    "K07": ["ComposeCameraPose", "ApplyCameraPose"],
    "K08": ["BeginRobotDeath", "BeginKnightDeath", "ApplySetEnemyClip",
            "AnimateEnemyDeath", "ApplyEnemyDeathProgress"],
    "K09": ["DetectPlayerContact", "EndGame", "HideCrosshairOnGameOver",
            "ShowGameOverLabel", "RestartOnGameOver", "RestartClearsEnemy",
            "RestartClearsBullet", "RestartResetsRobotSpawnPoint",
            "RestartResetsKnightSpawnPoint", "ShowCrosshairOnRestart",
            "HideGameOverLabelOnRestart"],
    "K10": ["is_probe_blocked", "SeekPlayer"],
    "K11": ["SquareVertex", "SquareFragment"],
    "K12": ["EmitParticleBurst", "SimulateParticles",
            "ParticleVertex", "ParticleFragment"],
}


def _source_lines(kata):
    return (ev.REPO / kata.source).read_text(encoding="utf-8").splitlines()


class KataSlices(unittest.TestCase):
    def test_every_kata_has_expected_declarations(self):
        self.assertEqual(sorted(EXPECTED_DECLARATIONS), sorted(ev.KATA_IDENTS))
        for kata in ev.KATAS:
            text = ev._slice(ev.REPO / kata.source, kata.arena_ranges)
            found = re.findall(r"^(?:rule|func) (\w+)", text, re.M)
            self.assertEqual(found, EXPECTED_DECLARATIONS[kata.ident], kata.ident)

    def test_every_kata_source_exists(self):
        for kata in ev.KATAS:
            self.assertTrue((ev.REPO / kata.source).exists(), kata.source)

    def test_ranges_start_and_end_on_declaration_boundaries(self):
        """No off-by-one: a range never clips a doc comment or a rule body."""
        for kata in ev.KATAS:
            lines = _source_lines(kata)
            for lo, hi in kata.arena_ranges:
                where = f"{kata.ident} {lo}-{hi}"
                self.assertLessEqual(1, lo, where)
                self.assertLess(lo, hi, where)
                self.assertLessEqual(hi, len(lines), where)
                self.assertTrue(lines[lo - 1].strip(), where + " starts blank")
                self.assertTrue(lines[hi - 1].strip(), where + " ends blank")
                self.assertEqual(lines[lo - 1][0], lines[lo - 1].lstrip()[0],
                                 where + " starts indented")
                if lo > 1:
                    self.assertFalse(lines[lo - 2].strip(),
                                     where + " clips the preceding comment")
                if hi < len(lines):
                    self.assertFalse(lines[hi].strip(),
                                     where + " ends mid-declaration")

    def test_ranges_within_a_kata_are_ordered_and_disjoint(self):
        for kata in ev.KATAS:
            end = 0
            for lo, hi in kata.arena_ranges:
                self.assertGreater(lo, end, kata.ident)
                end = hi

    def test_ui_baseline_ranges_cover_exactly_the_two_layout_handlers(self):
        lines = (ev.REPO / ev.UI_BASELINE["source"]).read_text(
            encoding="utf-8").splitlines()
        for name, key in (("MeasureUi", "measure_range"), ("ArrangeUi", "arrange_range")):
            lo, hi = ev.UI_BASELINE[key]
            self.assertEqual(lines[lo - 1], f"rule {name}:")
            body = lines[lo:hi]
            self.assertFalse([l for l in body if re.match(r"^(rule|func|pub) ", l)],
                             f"{name} range spans into another declaration")
        measure, arrange = ev.UI_BASELINE["measure_range"], ev.UI_BASELINE["arrange_range"]
        self.assertEqual(measure[1] + 1, arrange[0])
        self.assertEqual(ev.UI_BASELINE["total_lines"],
                         (measure[1] - measure[0] + 1) + (arrange[1] - arrange[0] + 1))


# --------------------------------------------------------------------------
# 3.4 -- the gate rejects
# --------------------------------------------------------------------------

def _clean_audit() -> dict:
    return {
        "kata_expressible": [{"kata": k, "expressible": True, "evidence": "ok"}
                             for k in ev.KATA_IDENTS],
        "disqualifying": [{"fact": f, "status": "satisfied", "evidence": "ok"}
                          for f, _ in ev.GATE_FACTS],
        "scored": [{"fact": f, "status": "absent", "evidence": "ok"}
                   for f, _ in ev.SCORED_FACTS],
        "verdict": "pass",
        "rationale": "",
    }


def _clean_katas() -> dict[str, str]:
    return {k: f"```cactus\nrule {k}:\n    pass\n```\n" for k in ev.KATA_IDENTS}


class Gate(unittest.TestCase):
    def test_a_clean_candidate_passes(self):
        out = ev._decide_verdict(_clean_audit(), _clean_katas())
        self.assertEqual(out["verdict"], "pass")
        self.assertEqual(out["gate_failures"], [])

    def test_truncated_kata_expression_fails(self):
        katas = _clean_katas()
        katas["K06"] = "The notation has no vocabulary for this."
        out = ev._decide_verdict(_clean_audit(), katas)
        self.assertEqual(out["verdict"], "fail")
        self.assertIn("K06: expression contains no program", out["gate_failures"])

    def test_declared_cannot_express_fails(self):
        katas = _clean_katas()
        katas["K02"] += "\n## Cannot express\n\nNo way to fold over a join.\n"
        out = ev._decide_verdict(_clean_audit(), katas)
        self.assertEqual(out["verdict"], "fail")
        self.assertTrue(any("K02" in f for f in out["gate_failures"]))

    def test_empty_and_missing_kata_expressions_fail(self):
        katas = _clean_katas()
        katas["K01"] = "   \n"
        del katas["K09"]
        out = ev._decide_verdict(_clean_audit(), katas)
        self.assertEqual(out["verdict"], "fail")
        self.assertIn("K01: expression is empty", out["gate_failures"])
        self.assertTrue(any("K09" in f for f in out["gate_failures"]))

    def test_model_verdict_cannot_overrule_the_facts(self):
        audit = _clean_audit()
        audit["kata_expressible"][3]["expressible"] = False
        audit["verdict"] = "pass"
        out = ev._decide_verdict(audit, _clean_katas())
        self.assertEqual(out["verdict"], "fail")

    def test_absent_or_unreported_disqualifying_fact_fails(self):
        audit = _clean_audit()
        audit["disqualifying"][2]["status"] = "absent"
        self.assertEqual(ev._decide_verdict(audit, _clean_katas())["verdict"], "fail")

        audit = _clean_audit()
        dropped = audit["disqualifying"].pop()["fact"]
        out = ev._decide_verdict(audit, _clean_katas())
        self.assertEqual(out["verdict"], "fail")
        self.assertIn(f"{dropped}: not reported by the audit", out["gate_failures"])

    def test_partial_facts_do_not_fail_the_gate(self):
        """The ancestor is partial on access sets and ordering; it must survive."""
        audit = _clean_audit()
        for row in audit["disqualifying"]:
            row["status"] = "partial"
        self.assertEqual(ev._decide_verdict(audit, _clean_katas())["verdict"], "pass")

    def test_a_failed_gate_scores_negative_infinity(self):
        cand = ev.Candidate(genome=ev.ancestor_genome())
        cand.gate = ev._decide_verdict(_clean_audit(), {})
        self.assertFalse(cand.passed)
        self.assertEqual(ev.composite(cand, 1600.0, 1500.0), float("-inf"))


# --------------------------------------------------------------------------
# 5.5 -- no absolute model self-rating reaches the ranking
# --------------------------------------------------------------------------

RATING_NAME = re.compile(r"score|rating|rank|quality|grade|stars|points", re.I)


def _schemas() -> dict[str, dict]:
    return {n: v for n, v in vars(ev).items()
            if n.endswith("_SCHEMA") and isinstance(v, dict)}


def _properties(schema, out=None):
    out = [] if out is None else out
    if isinstance(schema, dict):
        for name, prop in schema.get("properties", {}).items():
            out.append((name, prop))
            _properties(prop, out)
        if "items" in schema:
            _properties(schema["items"], out)
    return out


class RankingInputs(unittest.TestCase):
    def test_no_schema_asks_a_model_for_a_numeric_rating(self):
        for name, schema in _schemas().items():
            for prop, spec in _properties(schema):
                if RATING_NAME.search(prop):
                    self.assertNotIn(spec.get("type"), ("number", "integer"),
                                     f"{name}.{prop} is a model-supplied rating")

    def test_the_judge_can_only_pick_a_side(self):
        """Pairwise only: the judge has no way to express a magnitude."""
        props = ev.DUEL_SCHEMA["properties"]
        self.assertEqual(set(props), {"winner", "reason"})
        self.assertEqual(props["winner"]["enum"], ["A", "B"])
        self.assertFalse([p for p, s in _properties(ev.DUEL_SCHEMA)
                          if s.get("type") in ("number", "integer")])

    def test_duel_yields_only_a_side(self):
        client = ev.Client(dry_run=True)
        self.assertIn(ev.duel(client, "universality", "a", "b"), ("A", "B"))

    def test_comprehension_self_confidence_does_not_move_the_score(self):
        """`confidence` is the one self-rating the harness collects at all."""
        base = ev.Candidate(genome=ev.ancestor_genome())
        base.gate = {"verdict": "pass"}
        base.mechanical = {"kata_tokens_total": 6000, "residue": {"total": 2}}
        base.differentiator = {"lines": 200, "max_nesting_depth": 3,
                               "manual_walks": 0, "dispatch_flags": 0,
                               "manual_reductions": 0, "extern_declarations": 0,
                               "total_declarations": 2}
        base.comprehension = {
            "answers": [{"kata": "K02", "described_behavior": "x", "confidence": "low"}],
            "graded": [{"kata": "K02", "correct": True, "reason": ""}],
            "score": 1.0,
        }
        loud = copy.deepcopy(base)
        loud.comprehension["answers"][0]["confidence"] = "high"
        self.assertEqual(ev.composite(base, 1500.0, 1500.0),
                         ev.composite(loud, 1500.0, 1500.0))

    def test_comprehension_score_is_arithmetic_over_booleans(self):
        rows = [{"kata": "K02", "correct": True, "reason": ""},
                {"kata": "K06", "correct": False, "reason": ""}]
        self.assertEqual(sum(1 for r in rows if r["correct"]) / len(rows), 0.5)
        self.assertEqual(
            ev.GRADE_SCHEMA["properties"]["graded"]["items"]["properties"]["correct"]["type"],
            "boolean")

    def test_residue_total_is_arithmetic_over_extracted_quotes(self):
        """Reproducible without a judge: the model never supplies the count."""
        props = _properties(ev.RESIDUE_SCHEMA)
        self.assertFalse([p for p, s in props if s.get("type") in ("number", "integer")])
        self.assertEqual({p for p, _ in props}, {"occurrences", "pattern", "kata", "quote"})

    def test_a_round_without_the_ancestor_is_not_admitted(self):
        client = ev.Client(dry_run=True)
        challenger = ev.Candidate(genome=ev.Genome("g0-00", "S1_contract",
                                                   dict(ev.ANCESTOR_FREE)))
        challenger.gate = {"verdict": "pass"}
        self.assertEqual(
            ev.run_tournament(client, [challenger], random.Random(1), 1), {})


class TokenMeasure(unittest.TestCase):
    """The subscription backend counts by probing, so the arithmetic must hold."""

    def _client(self, sizes):
        c = ev.Client(dry_run=True)
        c.dry_run, c.backend = False, "subscription"
        c.probes = []

        def probe(body):
            c.probes.append(body)
            return sizes.get(body, 0)

        c._sub_probe_tokens = probe
        return c

    def test_baseline_is_subtracted_from_the_measurement(self):
        c = self._client({"": 300, "hello": 312})
        self.assertEqual(c.count_tokens("hello"), 12)

    def test_baseline_is_measured_once_and_reused(self):
        c = self._client({"": 300, "a": 305, "b": 310})
        c.count_tokens("a")
        c.count_tokens("b")
        self.assertEqual(c.probes.count(""), 1, "baseline re-probed")

    def test_a_measurement_never_goes_negative(self):
        c = self._client({"": 300, "x": 290})
        self.assertGreaterEqual(c.count_tokens("x"), 0)

    def test_an_unmeasurable_candidate_is_refused_not_rewarded(self):
        """An empty measure would divide into an enormous expressiveness bonus."""
        class NoCount(ev.Client):
            def __init__(self):
                super().__init__(dry_run=True)

            def count_tokens(self, text):
                return None

        katas = {k: f"```cactus\nrule {k}:\n    pass\n```\n" for k in ev.KATA_IDENTS}
        seed = ev.Seed(notation="n", katas=katas, ui="u")
        cand = ev.develop_and_score(NoCount(), ev.ancestor_genome(), seed)
        self.assertEqual(cand.gate.get("verdict"), "pass", "gate blocked the test")
        self.assertIn("no usable kata token measure", cand.error)


class AuthoredPlacement(unittest.TestCase):
    """`language-philosophy` forbids the author stating where work runs."""

    def _scored(self, placement):
        cand = ev.Candidate(genome=ev.ancestor_genome())
        cand.gate = {"verdict": "pass", "authored_placement": placement}
        cand.mechanical = {"kata_tokens_total": 6000, "residue": {"total": 0}}
        cand.differentiator = {"lines": 512, "max_nesting_depth": 8}
        cand.comprehension = {"score": 1.0}
        return ev.composite(cand, 1500.0, 1500.0)

    def test_authoring_placement_is_penalised(self):
        clean = self._scored({"found": False, "evidence": ""})
        marked = self._scored({"found": True, "evidence": "gpu: true"})
        self.assertLess(marked, clean)

    def test_the_penalty_outweighs_plausible_terseness_gains(self):
        """Otherwise a `gpu:` marker could pay for itself in tokens saved."""
        clean = self._scored({"found": False, "evidence": ""})
        marked = self._scored({"found": True, "evidence": "target: gpu"})
        self.assertGreater(clean - marked, 40.0)

    def test_the_gate_must_report_the_field(self):
        self.assertIn("authored_placement", ev.GATE_SCHEMA["required"])


class OrderingReconciliation(unittest.TestCase):
    """8.3b -- the gate and the residue counter cannot contradict each other.

    On the anchor run the gate rated `ordering` satisfied while the residue
    extractor quoted five `after: <RuleName>` blocks out of the same katas. The
    quotes are verbatim repo text (see `test_ancestor_katas_order_by_naming_rules`
    below), so the extractor was right and the gate's status was wrong.
    """

    def _residue(self, named: int) -> dict:
        return {"occurrences": [{"pattern": "ordering_by_name", "kata": "K02",
                                 "quote": "after:\n    MovePlayer"}] * named
                               + [{"pattern": "cps_split", "kata": "K03",
                                   "quote": "emit X to y"}]}

    def _gate(self, ordering_status: str) -> dict:
        audit = _clean_audit()
        for row in audit["disqualifying"]:
            if row["fact"] == "ordering":
                row["status"] = ordering_status
        return ev._decide_verdict(audit, _clean_katas())

    def _ordering(self, gate: dict) -> dict:
        return next(r for r in gate["disqualifying"] if r["fact"] == "ordering")

    def test_author_named_ordering_downgrades_a_satisfied_gate(self):
        gate = ev.reconcile_ordering(self._gate("satisfied"), self._residue(5))
        self.assertEqual(self._ordering(gate)["status"], "partial")
        self.assertEqual(gate["ordering_reconciled"], 5)
        self.assertIn("naming another rule", self._ordering(gate)["evidence"])

    def test_reconciliation_never_fails_a_candidate_the_audit_passed(self):
        gate = ev.reconcile_ordering(self._gate("satisfied"), self._residue(5))
        self.assertEqual(gate["verdict"], "pass")
        cand = ev.Candidate(genome=ev.ancestor_genome(), gate=gate)
        self.assertTrue(cand.passed)

    def test_no_named_ordering_leaves_the_gate_untouched(self):
        gate = ev.reconcile_ordering(self._gate("satisfied"), self._residue(0))
        self.assertEqual(self._ordering(gate)["status"], "satisfied")
        self.assertNotIn("ordering_reconciled", gate)

    def test_reconciliation_only_ever_downgrades(self):
        for status in ("partial", "absent"):
            gate = ev.reconcile_ordering(self._gate(status), self._residue(5))
            self.assertEqual(self._ordering(gate)["status"], status)
            self.assertNotIn("ordering_reconciled", gate)

    def test_other_residue_patterns_do_not_touch_ordering(self):
        residue = {"occurrences": [{"pattern": p, "kata": "K02", "quote": "x"}
                                   for p, _ in ev.RESIDUE_PATTERNS
                                   if p != "ordering_by_name"]}
        gate = ev.reconcile_ordering(self._gate("satisfied"), residue)
        self.assertEqual(self._ordering(gate)["status"], "satisfied")

    def test_ancestor_katas_order_by_naming_rules(self):
        """The evidence that decided it: `after:` blocks naming declared rules.

        Every quote the extractor returned is verbatim repo text, so `ordering`
        cannot be `satisfied` for the ancestor -- design.md predicts `partial`.
        """
        named = 0
        for kata in ev.KATAS:
            source = (ev.REPO / kata.source).read_text(encoding="utf-8")
            rules = set(re.findall(r"^(?:pub )?rule (\w+)", source, re.M))
            text = ev._slice(ev.REPO / kata.source, kata.arena_ranges)
            for block in re.findall(r"^\s*after:\n((?:\s+\w+\n)+)", text, re.M):
                entries = block.split()
                self.assertTrue(entries)
                if set(entries) <= rules:
                    named += 1
                else:  # `after: render` names a phase, not a rule
                    self.assertFalse(set(entries) & rules, entries)
        self.assertEqual(named, 6, "arena's six author-named orderings moved")


class ExternEscape(unittest.TestCase):
    """8.3b -- the extern penalty was structurally dead.

    It compared externs counted inside the two extracted handlers (0) against a
    baseline counted over all of `ui.cactus` (7), so it could never fire.
    """

    def _scored(self, lines, externs, total_decls):
        cand = ev.Candidate(genome=ev.ancestor_genome())
        cand.gate = {"verdict": "pass"}
        cand.mechanical = {"kata_tokens_total": 6000, "residue": {"total": 0}}
        cand.differentiator = {"lines": lines, "max_nesting_depth": 0,
                               "manual_walks": 0, "dispatch_flags": 0,
                               "manual_reductions": 0,
                               "extern_declarations": externs,
                               "total_declarations": total_decls}
        cand.comprehension = {"score": 1.0}
        return ev.composite(cand, 1500.0, 1500.0)

    def test_baseline_is_measured_in_the_scored_scope(self):
        """The ancestor's layout expression declares no externs of its own."""
        self.assertEqual(ev.UI_BASELINE["extern_escape_ratio"], 0.0)
        self.assertEqual(ev.extern_escape_ratio(
            {"extern_declarations": 0, "total_declarations": 2}), 0.0)

    def test_the_ratio_is_bounded_and_survives_missing_counts(self):
        self.assertEqual(ev.extern_escape_ratio({}), 0.0)
        self.assertEqual(ev.extern_escape_ratio(
            {"extern_declarations": 3, "total_declarations": 0}), 1.0)
        self.assertEqual(ev.extern_escape_ratio(
            {"extern_declarations": None, "total_declarations": None}), 0.0)
        for externs in range(0, 9):
            r = ev.extern_escape_ratio({"extern_declarations": externs,
                                        "total_declarations": 4})
            self.assertGreaterEqual(r, 0.0)
            self.assertLessEqual(r, 1.0)

    def test_a_single_extern_already_costs_something(self):
        """The dead term needed 8 externs to fire; the ancestor's scope has 0."""
        self.assertLess(self._scored(200, 1, 4), self._scored(200, 0, 4))

    def test_externalizing_does_not_buy_a_line_count_improvement(self):
        """The spec scenario: shrinking lines behind externs is not an improvement."""
        honest = self._scored(512, 0, 2)
        externalized = self._scored(20, 4, 4)
        self.assertLessEqual(externalized, honest)

    def test_an_honest_shorter_rewrite_still_wins(self):
        self.assertGreater(self._scored(200, 0, 6), self._scored(512, 0, 2))


class JudgedAxisWeight(unittest.TestCase):
    """8.3b -- the judged axis was ~3% of the score range.

    Universality is one of the three criteria the experiment measures, so the
    duels have to be able to reorder a ranking; they are still model preference,
    so they must not be able to overturn counted evidence.
    """

    def _cand(self, residue):
        cand = ev.Candidate(genome=ev.ancestor_genome())
        cand.gate = {"verdict": "pass"}
        cand.mechanical = {"kata_tokens_total": 6000, "residue": {"total": residue}}
        cand.differentiator = {"lines": 512, "max_nesting_depth": 0,
                               "manual_walks": 0, "dispatch_flags": 0,
                               "manual_reductions": 0, "extern_declarations": 0,
                               "total_declarations": 2}
        cand.comprehension = {"score": 1.0}
        return cand

    def test_the_judged_axis_is_worth_the_comprehension_term_at_full_stretch(self):
        self.assertEqual(ev.JUDGED_WEIGHT, ev.COMPREHENSION_WEIGHT)
        self.assertEqual(ev.judged_term(1500.0 + ev.JUDGED_ELO_SPAN, 1500.0),
                         ev.JUDGED_WEIGHT)
        self.assertEqual(ev.judged_term(1500.0 - ev.JUDGED_ELO_SPAN, 1500.0),
                         -ev.JUDGED_WEIGHT)

    def test_the_judged_axis_is_bounded_however_lopsided_the_tournament(self):
        for gap in (0.0, 250.0, 1000.0, -1000.0, float("inf")):
            self.assertLessEqual(abs(ev.judged_term(1500.0 + gap, 1500.0)),
                                 ev.JUDGED_WEIGHT)

    def test_a_realistic_elo_lead_is_no_longer_negligible(self):
        """A 12-candidate field leaves the leader ~150 Elo above a mid-field
        ancestor. Under the old `/10.0` that was 15 points of a ~300 range."""
        self.assertGreaterEqual(ev.judged_term(1650.0, 1500.0), 25.0)

    def test_the_judge_reorders_a_near_tie(self):
        close, ahead = self._cand(1), self._cand(0)   # 4 points apart mechanically
        self.assertLess(ev.composite(close, 1500.0, 1500.0),
                        ev.composite(ahead, 1500.0, 1500.0))
        self.assertGreater(ev.composite(close, 1700.0, 1500.0),
                           ev.composite(ahead, 1300.0, 1500.0))

    def test_the_judge_cannot_overturn_counted_evidence(self):
        """Beyond 2 x JUDGED_WEIGHT of mechanical gap, judgement cannot swap."""
        gap = int(2 * ev.JUDGED_WEIGHT / 4.0) + 1     # residue is 4 points each
        weak, strong = self._cand(gap), self._cand(0)
        self.assertGreater(ev.composite(strong, 1000.0, 1500.0),
                           ev.composite(weak, 2000.0, 1500.0))

    def test_mechanical_terms_still_span_far_more_than_the_judged_one(self):
        """The ancestor measured -236; the judged band is 80 points wide."""
        span = abs(ev.composite(self._cand(80), 1500.0, 1500.0)
                   - ev.composite(self._cand(0), 1500.0, 1500.0))
        self.assertGreater(span, 3 * (2 * ev.JUDGED_WEIGHT))


class ScoredFacts(unittest.TestCase):
    """Scored-but-not-disqualifying facts must actually reach the score."""

    def _gate(self, *statuses):
        return {"verdict": "pass",
                "scored": [{"fact": name, "status": s, "evidence": ""}
                           for (name, _), s in zip(ev.SCORED_FACTS, statuses)]}

    def test_omitting_a_scored_fact_reduces_the_score(self):
        full = ev.scored_facts_credit(self._gate("satisfied", "satisfied", "satisfied"))
        one_absent = ev.scored_facts_credit(self._gate("absent", "satisfied", "satisfied"))
        self.assertLess(one_absent, full)
        self.assertEqual(full - one_absent, ev.SCORED_FACT_WEIGHT)

    def test_partial_earns_part_of_the_credit(self):
        partial = ev.scored_facts_credit(self._gate("partial", "absent", "absent"))
        self.assertEqual(partial, ev.SCORED_FACT_WEIGHT * 0.5)

    def test_an_unreported_fact_earns_nothing(self):
        """Silence is not evidence the notation supplies it."""
        self.assertEqual(ev.scored_facts_credit({"scored": []}), 0.0)
        self.assertEqual(ev.scored_facts_credit({}), 0.0)

    def test_scored_facts_cannot_rival_the_primary_axes(self):
        available = ev.SCORED_FACT_WEIGHT * len(ev.SCORED_FACTS)
        self.assertLess(available, ev.COMPREHENSION_WEIGHT)
        self.assertLess(available, ev.JUDGED_WEIGHT)

    def test_the_credit_reaches_composite(self):
        def scored(gate):
            c = ev.Candidate(genome=ev.ancestor_genome())
            c.gate = gate
            c.mechanical = {"kata_tokens_total": 6000, "residue": {"total": 0}}
            c.differentiator = {"lines": 512, "max_nesting_depth": 8}
            c.comprehension = {"score": 1.0}
            return ev.composite(c, 1500.0, 1500.0)

        rich = scored(self._gate("satisfied", "satisfied", "satisfied"))
        poor = scored(self._gate("absent", "absent", "absent"))
        self.assertAlmostEqual(rich - poor,
                               ev.SCORED_FACT_WEIGHT * len(ev.SCORED_FACTS))


class QuotaWall(unittest.TestCase):
    """A quota refusal must stop the run, not become 12 errored candidates."""

    def test_backend_quota_messages_are_recognised(self):
        for text in ("You've hit your session limit — resets 3:10pm",
                     "Usage limit reached for this account",
                     "rate limit exceeded, retry later",
                     "monthly quota exhausted"):
            self.assertTrue(ev._quota_wall(text), text)

    def test_ordinary_refusals_are_not_mistaken_for_quota(self):
        for text in ("I cannot express this kata in the given notation.",
                     "The notation has no construct for bounded iteration.",
                     ""):
            self.assertFalse(ev._quota_wall(text), text)

    def test_a_refusal_returned_as_assistant_text_is_still_a_wall(self):
        """It arrived as content, not as an empty turn, and was stored as one."""
        self.assertTrue(ev._quota_wall(
            "You've hit your session limit · resets 1:20am (Europe/Riga)"))

    def test_a_refusal_stored_as_a_notation_is_not_reusable(self):
        refusal = "You've hit your session limit · resets 1:20am"
        c = ev.Candidate(genome=ev.ancestor_genome(), notation=refusal,
                         katas={k: refusal for k in ev.KATA_IDENTS}, ui=refusal)
        self.assertFalse(c.has_expression, "resume would score a refusal string")

    def test_an_implausibly_short_notation_is_not_reusable(self):
        c = ev.Candidate(genome=ev.ancestor_genome(), notation="# Notation\n",
                         katas={k: "x" * 500 for k in ev.KATA_IDENTS}, ui="u" * 500)
        self.assertFalse(c.has_expression)

    def test_a_real_generation_is_reusable(self):
        c = ev.Candidate(genome=ev.ancestor_genome(), notation="n" * 20000,
                         katas={k: "k" * 500 for k in ev.KATA_IDENTS}, ui="u" * 500)
        self.assertTrue(c.has_expression)

    def test_generation_survives_a_wall_during_scoring(self):
        """The costly half must be on disk before scoring can lose it."""
        class WallAtGate(ev.Client):
            def __init__(self):
                super().__init__(dry_run=True)

            def structured(self, *a, **k):
                raise ev.QuotaExhausted("session limit")

        saved = []
        # Sizes must clear the has_expression floors, or this asserts nothing.
        seed = ev.Seed(notation="# Notation\n" + "n" * 20000,
                       katas={k: "```cactus\n" + "x" * 500 + "\n```"
                              for k in ev.KATA_IDENTS},
                       ui="```cactus\n" + "u" * 500 + "\n```")
        with self.assertRaises(ev.QuotaExhausted):
            ev.develop_and_score(WallAtGate(), ev.ancestor_genome(), seed,
                                 checkpoint=saved.append)

        self.assertTrue(saved, "nothing was checkpointed before scoring")
        self.assertTrue(saved[-1].has_expression,
                        "checkpoint carries no generated text")

    def test_quota_escapes_the_per_candidate_trap(self):
        """Every other exception is trapped; this one must propagate."""
        class Boom(ev.Client):
            def __init__(self, exc):
                super().__init__(dry_run=True)
                self.exc = exc

            def structured(self, *a, **k):
                raise self.exc

        seed = ev.Seed(notation="n", katas={k: "x" for k in ev.KATA_IDENTS}, ui="u")
        genome = ev.ancestor_genome()

        trapped = ev.develop_and_score(Boom(RuntimeError("schema mismatch")),
                                       genome, seed)
        self.assertIn("schema mismatch", trapped.error)

        with self.assertRaises(ev.QuotaExhausted):
            ev.develop_and_score(Boom(ev.QuotaExhausted("session limit")),
                                 genome, seed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
