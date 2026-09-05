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
import dataclasses
import json
import math
import os
import random
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import evolve_notation as ev  # noqa: E402


class ArtifactIsolation(unittest.TestCase):
    def test_scaffolded_seed_loads_separate_typed_artifacts(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "seed"
            ev.scaffold_seed(root)
            seed = ev.Seed.load(root)

            self.assertEqual(seed.format_version, ev.SEED_FORMAT_VERSION)
            self.assertEqual(set(seed.contracts), set(ev.KATA_IDENTS))
            self.assertEqual(set(seed.expressions), set(ev.KATA_IDENTS))
            self.assertEqual(set(seed.provenance), set(ev.KATA_IDENTS))
            self.assertNotIn(seed.contracts["K01"].text,
                             seed.expressions["K01"].text)
            self.assertTrue((root / "seed-manifest.json").exists())

    def test_legacy_combined_seed_is_rejected_with_migration_diagnostic(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "katas").mkdir()
            (root / "notation.md").write_text("notation", encoding="utf-8")
            (root / "ui_measure_arrange.md").write_text("ui", encoding="utf-8")
            (root / "katas" / "K01.md").write_text(
                "## Contract\nsecret\n## Ancestor expression\nanswer",
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "legacy.*scaffold-seed"):
                ev.Seed.load(root)

    def test_seed_load_verifies_the_manifested_genome(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "seed"
            ev.scaffold_seed(root)
            genome_path = root / "genome.json"
            genome_path.write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "hash mismatch.*genome"):
                ev.Seed.load(root)


class PromptIsolation(unittest.TestCase):
    class CaptureClient:
        def __init__(self):
            self.prompts = []
            self.counted = []

        def count_tokens(self, text):
            self.counted.append(text)
            return len(text)

        def structured(self, system, user, schema, **kwargs):
            del kwargs
            self.prompts.append((system, user, schema))
            if schema is ev.GATE_SCHEMA:
                out = _clean_audit()
                out["authored_placement"] = {"found": False, "evidence": ""}
                return out
            if schema is ev.RESIDUE_SCHEMA:
                return {"occurrences": []}
            if "answers" in schema.get("properties", {}):
                return {"answers": [
                    {"kata": k, "described_behavior": f"reader {k}",
                     "confidence": "medium"} for k in ev.COMPREHENSION_SAMPLE
                ]}
            if "graded" in schema.get("properties", {}):
                return {"graded": [
                    {"kata": k, "correct": True, "reason": "ok"}
                    for k in ev.COMPREHENSION_SAMPLE
                ]}
            raise AssertionError("unexpected schema")

    def test_scoring_views_never_mix_contracts_into_expressions(self):
        expressions = {k: f"EXPRESSION-{k}" for k in ev.KATA_IDENTS}
        contracts = {k: f"SECRET-CONTRACT-{k}" for k in ev.KATA_IDENTS}
        client = self.CaptureClient()

        ev.count_kata_tokens(client, expressions)
        ev.run_gate(client, "NOTATION", expressions)
        ev.count_residue(client, "NOTATION", expressions)
        ev.comprehension(client, "NOTATION", expressions, contracts,
                         ev.COMPREHENSION_SAMPLE)

        self.assertEqual(client.counted, list(expressions.values()))
        gate_prompt = client.prompts[0][1]
        residue_prompt = client.prompts[1][1]
        reader_prompt = client.prompts[2][1]
        grader_prompt = client.prompts[3][1]
        for prompt in (gate_prompt, residue_prompt, reader_prompt):
            self.assertNotIn("SECRET-CONTRACT", prompt)
            self.assertIn("EXPRESSION-", prompt)
        self.assertIn("SECRET-CONTRACT", grader_prompt)
        self.assertIn("reader K02", grader_prompt)


class StructuredEvidence(unittest.TestCase):
    def test_exact_set_rejects_missing_duplicate_unknown_and_malformed_rows(self):
        valid = [{"kata": "K01", "value": True},
                 {"kata": "K02", "value": False}]
        self.assertEqual(ev._validate_exact_rows(
            valid, "kata", ("K01", "K02"), "sample", ("value",)), valid)
        bad_rows = (
            valid[:1],
            [valid[0], valid[0]],
            [valid[0], {"kata": "K99", "value": True}],
            [valid[0], {"kata": 2, "value": True}],
            [{"kata": "K01"}, valid[1]],
        )
        for rows in bad_rows:
            with self.subTest(rows=rows), self.assertRaises(ev.EvidenceValidationError):
                ev._validate_exact_rows(rows, "kata", ("K01", "K02"),
                                        "sample", ("value",))

    def test_gate_rejects_incomplete_identity_sets_before_arithmetic(self):
        class AuditClient:
            def structured(self, *args, **kwargs):
                del args, kwargs
                audit = _clean_audit()
                audit["authored_placement"] = {"found": False, "evidence": ""}
                audit["kata_expressible"].pop()
                return audit

        with self.assertRaises(ev.EvidenceValidationError):
            ev.run_gate(AuditClient(), "notation", _clean_katas())

    def test_comprehension_rejects_incomplete_reader_or_grader(self):
        class IncompleteReader:
            calls = 0

            def structured(self, *args, **kwargs):
                del args, kwargs
                self.calls += 1
                return {"answers": []}

        with self.assertRaises(ev.EvidenceValidationError):
            ev.comprehension(IncompleteReader(), "notation", _clean_katas(),
                             {k: "contract" for k in ev.KATA_IDENTS},
                             ev.COMPREHENSION_SAMPLE)

    def test_ui_rejects_missing_fields(self):
        class MissingUi:
            def structured(self, *args, **kwargs):
                del args, kwargs
                return {"lines": 1}

        with self.assertRaises(ev.EvidenceValidationError):
            ev.score_differentiator(MissingUi(), "notation", "```cactus\nx\n```")


class ResidueEvidence(unittest.TestCase):
    def test_quotes_resolve_to_utf8_byte_spans(self):
        out = ev.canonicalize_residue_occurrences(
            [{"pattern": "sentinel", "kata": "K01", "quote": "β", "ordinal": 2}],
            {"K01": "α β β"})
        self.assertEqual(out[0]["start"], len("α β ".encode("utf-8")))
        self.assertEqual(out[0]["end"], len("α β β".encode("utf-8")))

    def test_nonexistent_ambiguous_and_unknown_quotes_are_rejected(self):
        expressions = {"K01": "same same"}
        cases = (
            [{"pattern": "sentinel", "kata": "K01", "quote": "missing", "ordinal": 1}],
            [{"pattern": "sentinel", "kata": "K01", "quote": "same"}],
            [{"pattern": "unknown", "kata": "K01", "quote": "same", "ordinal": 1}],
            [{"pattern": "sentinel", "kata": "K99", "quote": "same", "ordinal": 1}],
        )
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ev.EvidenceValidationError):
                ev.canonicalize_residue_occurrences(rows, expressions)

    def test_duplicate_spans_are_canonicalized_once_and_rescore_deterministically(self):
        row = {"pattern": "sentinel", "kata": "K01", "quote": "same", "ordinal": 1}
        accepted = ev.canonicalize_residue_occurrences([row, dict(row)], {"K01": "same"})
        self.assertEqual(len(accepted), 1)
        self.assertEqual(ev.score_residue_occurrences(accepted),
                         ev.score_residue_occurrences(json.loads(json.dumps(accepted))))
        self.assertEqual(ev.score_residue_occurrences(accepted)["total"], 1)


class UiEvidence(unittest.TestCase):
    class UiClient:
        def __init__(self, cannot_express=False):
            self.cannot_express = cannot_express

        def structured(self, *args, **kwargs):
            del args, kwargs
            return {
                "lines": 999, "max_nesting_depth": 1, "manual_walks": 0,
                "dispatch_flags": 0, "manual_reductions": 0,
                "extern_declarations": 0, "total_declarations": 2,
                "cannot_express": self.cannot_express, "notes": "audit",
            }

    def test_mechanical_fenced_count_is_authoritative(self):
        result = ev.score_differentiator(
            self.UiClient(), "notation", "```cactus\na\nb\n```")
        self.assertEqual(result["mechanical_line_count"], 2)
        self.assertEqual(result["model_line_count"], 999)
        self.assertEqual(result["lines"], 2)
        self.assertTrue(result["line_count_disagreement"])

    def test_inexpressible_ui_is_not_rankable(self):
        result = ev.score_differentiator(
            self.UiClient(True), "notation", "```cactus\na\n```")
        cand = ev.Candidate(genome=ev.ancestor_genome(), gate={"verdict": "pass"},
                            mechanical={"kata_tokens_total": 1,
                                        "residue": {"total": 0}},
                            differentiator=result, comprehension={"score": 1.0})
        self.assertFalse(result["rankable"])
        self.assertEqual(ev.composite(cand, 1500.0, 1500.0), float("-inf"))

    def test_explicit_cannot_express_overrides_a_missed_model_flag(self):
        class MissedDeclarationClient:
            def structured(self, *_args, **_kwargs):
                return {
                    "lines": 1, "max_nesting_depth": 1, "manual_walks": 0,
                    "dispatch_flags": 0, "manual_reductions": 0,
                    "extern_declarations": 0, "total_declarations": 1,
                    "cannot_express": False, "notes": "missed the declaration",
                }

        ui = "```cactus\nmeasure\n```\n\n## Cannot express\nArrange is unavailable."
        result = ev.score_differentiator(MissedDeclarationClient(), "notation", ui)
        self.assertTrue(result["declared_cannot_express"])
        self.assertFalse(result["rankable"])

    def test_offline_ui_rehearsal_is_complete_and_rankable(self):
        result = ev.score_differentiator(
            ev.Client(dry_run=True), "notation", "```cactus\na\nb\n```")
        self.assertFalse(result["cannot_express"])
        self.assertTrue(result["rankable"])


class DevelopmentAudit(unittest.TestCase):
    def _result(self, genome):
        return {
            "notation": "n" * 3000,
            "realizations": [
                {"locus": locus, "status": "inherited", "evidence": "ok"}
                for locus in genome.alleles()
            ],
            "repairs": [],
        }

    def test_every_inherited_locus_is_accounted_exactly_once(self):
        genome = ev.ancestor_genome()
        result = self._result(genome)
        for mutate in (
                lambda rows: rows.pop(),
                lambda rows: rows.append(dict(rows[0])),
                lambda rows: rows.__setitem__(0, rows[0] | {"locus": "G99"})):
            broken = copy.deepcopy(result)
            mutate(broken["realizations"])
            with self.assertRaises(ev.EvidenceValidationError):
                ev.validate_development_result(genome, broken)

    def test_repairs_name_exactly_the_affected_loci(self):
        genome = ev.ancestor_genome()
        result = self._result(genome)
        result["realizations"][0]["status"] = "overridden"
        with self.assertRaises(ev.EvidenceValidationError):
            ev.validate_development_result(genome, result)
        result["repairs"] = [{"loci": [result["realizations"][0]["locus"]],
                              "reason": "coherence"}]
        notation, depth, audit = ev.validate_development_result(genome, result)
        self.assertEqual(notation, result["notation"])
        self.assertEqual(depth, 1)
        self.assertEqual(audit["affected_loci"], result["repairs"][0]["loci"])
        result["repairs"][0]["loci"] = ["G99"]
        with self.assertRaises(ev.EvidenceValidationError):
            ev.validate_development_result(genome, result)


class VersionedCheckpoints(unittest.TestCase):
    def _identity(self, **changes):
        base = {
            "run_mode": "dry", "seed_hash": "seed-a", "genotype_hash": "gene-a",
            "claude_client": "agent-sdk", "claude_model": "claude-test",
            "codex_client": "codex-cli", "codex_model": "codex-test",
            "prompt_hashes": {"development": "p1", "gate": "p2"},
            "schema_hashes": {"development": "s1", "gate": "s2"},
            "scoring_hash": "score-a", "judge_hash": "judge-a",
            "harness_version": ev.HARNESS_VERSION,
        }
        base.update(changes)
        return base

    def _manifest(self):
        seed = ev.Seed(notation="notation", katas=_clean_katas(), ui="ui")
        return ev.build_run_manifest(
            seed, ev.seed_population(random.Random(1), 12), dry_run=True,
            seed_rng=1, generations=0, claude_model="claude-test",
            codex_model="codex-test", workers=2)

    def test_dry_seed_genotype_prompt_schema_scoring_and_judges_are_identity(self):
        base = self._identity()
        changes = (
            {"run_mode": "real"}, {"seed_hash": "seed-b"},
            {"genotype_hash": "gene-b"},
            {"prompt_hashes": base["prompt_hashes"] | {"gate": "changed"}},
            {"schema_hashes": base["schema_hashes"] | {"gate": "changed"}},
            {"scoring_hash": "changed"}, {"judge_hash": "changed"},
        )
        for change in changes:
            with self.subTest(change=change):
                self.assertTrue(ev.identity_mismatches(base, self._identity(**change)))

    def test_stage_records_require_complete_success_evidence(self):
        record = ev.StageRecord(
            stage="gate", dependency_fingerprint="abc", prompt="p",
            raw_response={"raw": True}, validated_value={"verdict": "pass"},
            accounting={"calls": 1}, status="complete", error="")
        self.assertTrue(record.is_reusable("abc"))
        for field in ("prompt", "raw_response", "validated_value", "accounting"):
            broken = dataclasses.replace(record, **{field: None})
            self.assertFalse(broken.is_reusable("abc"), field)
        self.assertFalse(record.is_reusable("different"))

    def test_legacy_directory_is_diagnosed_not_adopted(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "gen00").mkdir()
            result = ev.inspect_checkpoint_directory(root)
            self.assertEqual(result["status"], "non-comparative")
            self.assertEqual(result["classification"], "legacy")

    def test_current_format_dry_directory_is_non_comparative(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            ev._write_json(root / "run-manifest.json", {
                "format_version": ev.RUN_FORMAT_VERSION,
                "harness_version": ev.HARNESS_VERSION,
                "run_mode": "dry",
            })
            result = ev.inspect_checkpoint_directory(root)
            self.assertEqual(result["status"], "non-comparative")
            self.assertEqual(result["classification"], "dry-run")
            self.assertIn("dry", result["reason"])

    def test_incomplete_real_directory_is_not_reported_as_comparative(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            ev._write_json(root / "run-manifest.json", {
                "format_version": ev.RUN_FORMAT_VERSION,
                "harness_version": ev.HARNESS_VERSION,
                "run_mode": "real",
            })
            (root / "gen00").mkdir()
            result = ev.inspect_checkpoint_directory(root)
            self.assertEqual(result["status"], "non-comparative")
            self.assertEqual(result["classification"], "incomplete")

    def test_atomic_stage_resume_reuses_only_matching_completed_stage(self):
        with tempfile.TemporaryDirectory() as raw:
            store = ev.StageStore(Path(raw))
            calls = []

            def produce():
                calls.append("called")
                return ev.StageOutcome("prompt", {"raw": 1}, {"value": 1},
                                       {"calls": 1})

            first = store.run("gate", "fingerprint-a", produce)
            second = store.run("gate", "fingerprint-a", produce)
            third = store.run("gate", "fingerprint-b", produce)
            self.assertEqual(first, second)
            self.assertEqual(third, {"value": 1})
            self.assertEqual(calls, ["called", "called"])
            self.assertFalse(list(Path(raw).glob("*.tmp")))

    def test_scoring_identity_change_reuses_only_upstream_stages(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            original = self._manifest()
            changed = copy.deepcopy(original)
            changed["scoring_hash"] = "changed-scoring"
            ev.ensure_run_manifest(root, original, False)

            mismatches = ev.ensure_run_manifest(root, changed, True)

            self.assertEqual(mismatches, ["scoring_hash"])
            self.assertEqual(
                ev.stage_dependency_fingerprint("development", original),
                ev.stage_dependency_fingerprint("development", changed))
            self.assertNotEqual(
                ev.stage_dependency_fingerprint("gate", original),
                ev.stage_dependency_fingerprint("gate", changed))
            self.assertEqual(len(list(
                (root / "manifest-history").glob("*.json"))), 1)
            self.assertEqual(json.loads(
                (root / "run-manifest.json").read_text(
                    encoding="utf-8"))["scoring_hash"], "changed-scoring")

    def test_judge_identity_change_does_not_invalidate_generation_stages(self):
        original = self._manifest()
        changed = copy.deepcopy(original)
        changed["clients"]["judges"][1]["model"] = "changed-codex"
        self.assertEqual(
            ev.stage_dependency_fingerprint("development", original),
            ev.stage_dependency_fingerprint("development", changed))
        self.assertNotEqual(
            ev.stage_dependency_fingerprint("duel", original),
            ev.stage_dependency_fingerprint("duel", changed))

    def test_prompt_hashes_include_effective_global_prompt_content(self):
        original = self._manifest()
        with mock.patch.object(ev, "DUEL_SYSTEM", ev.DUEL_SYSTEM + " changed"):
            changed_duel = self._manifest()
        with mock.patch.object(ev, "PHILOSOPHY", ev.PHILOSOPHY + " changed"):
            changed_philosophy = self._manifest()
        self.assertNotEqual(original["prompt_hashes"]["duel"],
                            changed_duel["prompt_hashes"]["duel"])
        self.assertNotEqual(original["prompt_hashes"]["development"],
                            changed_philosophy["prompt_hashes"]["development"])

    def test_dry_to_real_manifest_resume_is_rejected(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            original = self._manifest()
            changed = copy.deepcopy(original)
            changed["run_mode"] = "real"
            ev.ensure_run_manifest(root, original, False)
            with self.assertRaisesRegex(
                    ev.CheckpointCompatibilityError, "run_mode"):
                ev.ensure_run_manifest(root, changed, True)

    def test_superseded_stage_evidence_is_preserved(self):
        with tempfile.TemporaryDirectory() as raw:
            store = ev.StageStore(Path(raw))
            store.run("gate", "old", lambda: ev.StageOutcome(
                "old prompt", {"raw": "old"}, {"value": "old"},
                {"calls": 1}))
            store.run("gate", "new", lambda: ev.StageOutcome(
                "new prompt", {"raw": "new"}, {"value": "new"},
                {"calls": 1}))
            archived = Path(raw) / "superseded" / "old" / "gate.json"
            self.assertTrue(archived.exists())
            self.assertEqual(json.loads(archived.read_text(
                encoding="utf-8"))["validated_value"], {"value": "old"})

    def test_interrupted_stage_is_durable_but_not_reusable(self):
        with tempfile.TemporaryDirectory() as raw:
            store = ev.StageStore(Path(raw))
            with self.assertRaises(RuntimeError):
                store.run("gate", "fp", lambda: (_ for _ in ()).throw(RuntimeError("cut")))
            record = json.loads((Path(raw) / "gate.json").read_text(encoding="utf-8"))
            self.assertEqual(record["status"], "error")
            self.assertEqual(store.load("gate", "fp"), None)

    def test_checkpoint_errors_are_credential_redacted(self):
        with tempfile.TemporaryDirectory() as raw:
            store = ev.StageStore(Path(raw))
            with self.assertRaises(RuntimeError):
                store.run(
                    "gate", "fp",
                    lambda: (_ for _ in ()).throw(
                        RuntimeError("token=sk-secret-value")))
            encoded = (Path(raw) / "gate.json").read_text(encoding="utf-8")
            self.assertNotIn("sk-secret-value", encoded)
            self.assertIn("[redacted]", encoded)

    def test_gate_pass_is_not_a_complete_candidate_checkpoint(self):
        gate = _clean_audit()
        gate["authored_placement"] = {"found": False, "evidence": ""}
        candidate = ev.Candidate(
            genome=ev.ancestor_genome(), notation="n" * 2500,
            katas={ident: f"```cactus\n{'x' * 120}\n```"
                   for ident in ev.KATA_IDENTS},
            ui="```cactus\nlayout\n```", gate=gate)
        self.assertFalse(candidate.is_complete)

        candidate.mechanical = {
            "kata_tokens": {ident: 1 for ident in ev.KATA_IDENTS},
            "kata_tokens_total": len(ev.KATA_IDENTS),
            "residue": {
                "counts": {pattern: 0 for pattern, _ in ev.RESIDUE_PATTERNS},
                "total": 0, "occurrences": []},
        }
        candidate.differentiator = {
            "rankable": True, "mechanical_line_count": 1,
        }
        candidate.comprehension = {
            "answers": [{"kata": ident, "described_behavior": "behavior",
                         "confidence": "medium"}
                        for ident in ev.COMPREHENSION_SAMPLE],
            "graded": [{"kata": ident, "correct": True, "reason": "ok"}
                       for ident in ev.COMPREHENSION_SAMPLE],
            "score": 1.0,
        }
        self.assertTrue(candidate.is_complete)

        candidate.mechanical["residue"]["counts"] = {"bad": "not-a-count"}
        self.assertFalse(candidate.is_complete)

    def test_missing_candidate_artifacts_are_not_loadable(self):
        with tempfile.TemporaryDirectory() as raw:
            candidate_dir = Path(raw) / "ancestor"
            candidate_dir.mkdir()
            (candidate_dir / "scores.json").write_text(
                json.dumps({"genome": ev.ancestor_genome().to_dict()}),
                encoding="utf-8")
            self.assertIsNone(ev._load_candidate(Path(raw), "ancestor"))

    def test_generation_zero_manifest_can_resume_to_later_generations(self):
        seed = ev.Seed(notation="notation", katas=_clean_katas(), ui="ui")
        population = ev.seed_population(random.Random(1), 12)
        common = dict(
            dry_run=True, seed_rng=1, claude_model="claude-test",
            codex_model="codex-test", workers=2)
        pilot = ev.build_run_manifest(
            seed, population, generations=0, **common)
        continued = ev.build_run_manifest(
            seed, population, generations=5, **common)
        self.assertEqual(pilot, continued)
        self.assertEqual(pilot["generation"], 0)

    def test_offline_scoring_resumes_after_deliberate_quota_interruption(self):
        class InterruptingClient(ev.Client):
            def __init__(self):
                super().__init__(dry_run=True)
                self.interrupted = False

            def structured(self, system, user, schema, **kwargs):
                if schema is ev.DIFFERENTIATOR_SCHEMA and not self.interrupted:
                    self.interrupted = True
                    error = ev.QuotaExhausted("deliberate offline interruption")
                    self.cancellation.cancel(error)
                    raise error
                return super().structured(system, user, schema, **kwargs)

        with tempfile.TemporaryDirectory() as raw:
            seed = ev.Seed(
                notation="n" * 2500, katas=_clean_katas(),
                ui="```cactus\nlayout\n```")
            genome = ev.ancestor_genome()
            manifest = ev.build_run_manifest(
                seed, [genome], dry_run=True, seed_rng=1, generations=0,
                claude_model="claude-test", codex_model="codex-test", workers=1)
            store = ev.StageStore(Path(raw))
            with self.assertRaises(ev.QuotaExhausted):
                ev.develop_and_score(
                    InterruptingClient(), genome, seed,
                    stage_store=store, manifest=manifest)
            self.assertEqual(json.loads(
                (Path(raw) / "ui_measurement.json").read_text(
                    encoding="utf-8"))["status"], "error")

            resumed_client = ev.Client(dry_run=True)
            candidate = ev.develop_and_score(
                resumed_client, genome, seed,
                stage_store=store, manifest=manifest)
            self.assertTrue(candidate.passed)
            self.assertFalse(candidate.error)
            kinds = [row["kind"] for row in resumed_client.evidence_since(0)]
            self.assertEqual(kinds, ["structured", "structured", "structured"])


class CancellationSafety(unittest.TestCase):
    def test_quota_cancels_queued_parallel_work(self):
        token = ev.CancellationToken()
        started = []

        def work(item):
            started.append(item)
            if item == 0:
                raise ev.QuotaExhausted("rate limit")
            token.check()
            return item

        with self.assertRaises(ev.QuotaExhausted):
            ev._parallel(work, list(range(20)), 2, token)
        self.assertTrue(token.cancelled)
        self.assertLess(len(started), 20)

    def test_model_client_checks_cancellation_before_a_call(self):
        token = ev.CancellationToken()
        token.cancel(ev.QuotaExhausted("quota"))
        client = ev.Client(dry_run=True, cancellation=token)
        with self.assertRaises(ev.QuotaExhausted):
            client.structured([], "x", ev.DUEL_SCHEMA)

    def test_candidate_specific_invalid_output_does_not_cancel_run(self):
        token = ev.CancellationToken()

        def work(item):
            if item == 1:
                raise ev.EvidenceValidationError("bad candidate")
            return item

        def isolated(item):
            try:
                return work(item)
            except ev.EvidenceValidationError as exc:
                return str(exc)

        self.assertEqual(ev._parallel(isolated, [0, 1, 2], 2, token),
                         [0, "bad candidate", 2])
        self.assertFalse(token.cancelled)


class LocalClientOnly(unittest.TestCase):
    def test_help_examples_do_not_offer_an_unqualified_real_run(self):
        self.assertIn("--dry-run", ev.__doc__)
        self.assertIn("--preflight", ev.__doc__)
        self.assertIn("--calibration", ev.__doc__)

    def test_removed_api_backend_is_rejected_by_cli(self):
        parser = ev.build_parser()
        with self.assertRaises(SystemExit):
            parser.parse_args([
                "run", "--seed", "seed", "--out", "out", "--backend", "api"])

    def test_module_has_no_anthropic_sdk_or_api_environment_access(self):
        source = Path(ev.__file__).read_text(encoding="utf-8")
        self.assertNotIn("import anthropic", source)
        self.assertNotIn("ANTHROPIC_API_KEY", source)
        self.assertNotIn("ANTHROPIC_AUTH_TOKEN", source)


class JudgeAdapters(unittest.TestCase):
    def test_family_neutral_result_keeps_audit_fields(self):
        result = ev.JudgeResult(
            family="claude", winner="A", reason="clearer", usage={"calls": 1},
            model="claude-test", presentation_order={"A": "one", "B": "two"},
            raw_evidence={"winner": "A", "reason": "clearer"})
        self.assertEqual(set(dataclasses.asdict(result)), {
            "family", "winner", "reason", "usage", "model",
            "presentation_order", "raw_evidence"})

    def _codex(self, runner, which=lambda _: "codex"):
        return ev.CodexJudge("codex-test", ev.CancellationToken(),
                             runner=runner, which=which)

    def test_claude_client_identity_includes_sdk_and_cli_versions(self):
        def runner(args, **kwargs):
            del kwargs
            return subprocess.CompletedProcess(args, 0, "claude 9.1\n", "")

        judge = ev.ClaudeJudge(
            ev.Client(dry_run=True), runner=runner, which=lambda _: "claude")
        with mock.patch("importlib.metadata.version", return_value="sdk 2.3"):
            self.assertEqual(
                judge.client_version(),
                "claude-agent-sdk sdk 2.3; claude-cli claude 9.1")

    def test_codex_cli_absence_is_actionable(self):
        judge = self._codex(lambda *a, **k: None, which=lambda _: None)
        with self.assertRaisesRegex(RuntimeError, "codex.*PATH"):
            judge.judge("universality", "prompt", {"A": "one", "B": "two"})

    def test_codex_auth_model_and_schema_failures_are_rejected(self):
        for message in ("not logged in", "model unavailable", "output schema invalid"):
            def runner(*args, **kwargs):
                del args, kwargs
                return subprocess.CompletedProcess([], 1, "", message)
            with self.subTest(message=message), self.assertRaisesRegex(RuntimeError, message):
                self._codex(runner).judge(
                    "universality", "prompt", {"A": "one", "B": "two"})

    def test_codex_quota_failure_cancels_the_run(self):
        def runner(*args, **kwargs):
            del args, kwargs
            return subprocess.CompletedProcess([], 1, "", "usage limit reached")

        judge = self._codex(runner)
        with self.assertRaises(ev.QuotaExhausted):
            judge.judge("universality", "prompt", {"A": "one", "B": "two"})
        self.assertTrue(judge.cancellation.cancelled)

    def test_codex_malformed_event_stream_is_rejected(self):
        def runner(args, **kwargs):
            output = Path(args[args.index("--output-last-message") + 1])
            output.write_text('{"winner":"A","reason":"ok"}', encoding="utf-8")
            return subprocess.CompletedProcess(args, 0, "not-json\n", "")

        with self.assertRaisesRegex(RuntimeError, "event"):
            self._codex(runner).judge(
                "universality", "prompt", {"A": "one", "B": "two"})

    def test_codex_valid_schema_constrained_result_is_auditable(self):
        captured = {}

        def runner(args, **kwargs):
            captured["args"] = args
            captured["kwargs"] = kwargs
            output = Path(args[args.index("--output-last-message") + 1])
            output.write_text('{"winner":"B","reason":"broader"}', encoding="utf-8")
            event = json.dumps({"type": "turn.completed",
                                "usage": {"input_tokens": 10, "output_tokens": 2}})
            return subprocess.CompletedProcess(args, 0, event + "\n", "")

        result = self._codex(runner).judge(
            "universality", "same prompt", {"A": "one", "B": "two"})
        self.assertEqual(result.winner, "B")
        self.assertEqual(result.reason, "broader")
        self.assertEqual(result.usage["input_tokens"], 10)
        self.assertIn("--ephemeral", captured["args"])
        self.assertIn("--skip-git-repo-check", captured["args"])
        self.assertIn("read-only", captured["args"])
        self.assertEqual(captured["kwargs"]["input"], "same prompt")
        self.assertNotEqual(Path(captured["kwargs"]["cwd"]), ev.REPO)

    def test_codex_audit_evidence_redacts_credential_shaped_text(self):
        def runner(args, **kwargs):
            del kwargs
            output = Path(args[args.index("--output-last-message") + 1])
            output.write_text(
                '{"winner":"A","reason":"authorization=sk-result-secret"}',
                encoding="utf-8")
            event = json.dumps({"type": "turn.completed",
                                "detail": "token=sk-event-secret",
                                "access_token": "ghp-event-secret",
                                "usage": {"input_tokens": 1,
                                          "authorization": "Bearer other-secret"}})
            return subprocess.CompletedProcess(
                args, 0, event + "\n", "password=sk-stderr-secret")

        result = self._codex(runner).judge(
            "universality", "prompt", {"A": "one", "B": "two"})
        encoded = json.dumps(dataclasses.asdict(result))
        self.assertNotIn("sk-result-secret", encoded)
        self.assertNotIn("sk-event-secret", encoded)
        self.assertNotIn("sk-stderr-secret", encoded)
        self.assertNotIn("ghp-event-secret", encoded)
        self.assertNotIn("other-secret", encoded)
        self.assertIn("[redacted]", encoded)


class DualFamilyTournament(unittest.TestCase):
    class FakeJudge:
        def __init__(self, family, winner="A", fail_after=None, cancellation=None):
            self.family = family
            self.model = family + "-test"
            self.winner = winner
            self.fail_after = fail_after
            self.cancellation = cancellation or ev.CancellationToken()
            self.calls = []

        def judge(self, axis, prompt, presentation_order):
            self.calls.append((axis, prompt, dict(presentation_order)))
            if self.fail_after is not None and len(self.calls) > self.fail_after:
                error = ev.QuotaExhausted("quota")
                self.cancellation.cancel(error)
                raise error
            return ev.JudgeResult(
                self.family, self.winner, f"{self.family} reason", {"calls": 1},
                self.model, dict(presentation_order),
                {"winner": self.winner, "reason": f"{self.family} reason"})

    def _candidates(self):
        ancestor = ev.Candidate(ev.ancestor_genome(), notation="ancestor notation",
                                gate={"verdict": "pass"})
        challenger = ev.Candidate(
            ev.Genome("g0-00", "S1_contract", dict(ev.ANCESTOR_FREE)),
            notation="challenger notation", gate={"verdict": "pass"})
        return [ancestor, challenger]

    def test_families_receive_identical_precomputed_prompt_and_orientation(self):
        with tempfile.TemporaryDirectory() as raw:
            claude = self.FakeJudge("claude", "A")
            codex = self.FakeJudge("codex", "B", cancellation=claude.cancellation)
            report = ev.run_tournament(
                [claude, codex], self._candidates(), random.Random(3), 1,
                Path(raw))
            self.assertTrue(report["complete"])
            self.assertEqual(len(claude.calls), len(ev.DUEL_AXES))
            self.assertEqual(len(codex.calls), len(ev.DUEL_AXES))
            self.assertEqual(claude.calls, codex.calls)
            records = report["records"]
            self.assertEqual({row["family"] for row in records}, {"claude", "codex"})
            self.assertEqual({row["reason"] for row in records},
                             {"claude reason", "codex reason"})
            self.assertEqual(len(list((Path(raw) / "duels").glob("*.json"))), 4)

    def test_quota_during_tournament_keeps_completed_duels_and_no_rating(self):
        with tempfile.TemporaryDirectory() as raw:
            token = ev.CancellationToken()
            claude = self.FakeJudge("claude", cancellation=token)
            codex = self.FakeJudge("codex", fail_after=0, cancellation=token)
            with self.assertRaises(ev.QuotaExhausted):
                ev.run_tournament(
                    [claude, codex], self._candidates(), random.Random(1), 1,
                    Path(raw))
            self.assertTrue(list((Path(raw) / "duels").glob("*.json")))
            self.assertFalse((Path(raw) / "tournament-report.json").exists())

    def test_incomplete_matrix_persists_diagnostics_without_a_rating(self):
        with tempfile.TemporaryDirectory() as raw:
            judges = [self.FakeJudge("claude"), self.FakeJudge("codex")]
            report = ev.run_tournament(
                judges, self._candidates()[:1], random.Random(1), 1, Path(raw))
            self.assertFalse(report["complete"])
            self.assertIsNone(report["ratings"])
            persisted = json.loads((Path(raw) / "tournament-report.json").read_text(
                encoding="utf-8"))
            self.assertFalse(persisted["complete"])


class QualificationCommands(unittest.TestCase):
    class FakeJudge:
        def __init__(self, family, *, winner="A", failure=None,
                     version="client 1.0"):
            self.family = family
            self.model = family + "-test"
            self.winner = winner
            self.failure = failure
            self.version = version
            self.calls = []

        def client_version(self):
            if self.failure == "version":
                raise RuntimeError(f"{self.family} executable is not on PATH")
            return self.version

        def judge(self, axis, prompt, presentation_order):
            self.calls.append((axis, prompt, dict(presentation_order)))
            if isinstance(self.failure, Exception):
                raise self.failure
            return ev.JudgeResult(
                self.family, self.winner, "canary ok", {"calls": 1}, self.model,
                dict(presentation_order),
                {"winner": self.winner, "secret": "sk-test-credential"})

    def test_preflight_uses_one_fixed_canary_per_family_and_no_candidate_work(self):
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "preflight.json"
            judges = [self.FakeJudge("claude"), self.FakeJudge("codex", winner="B")]
            forbidden = ("scaffold_seed", "develop_notation", "run_gate",
                         "develop_and_score", "run_tournament")
            patches = [mock.patch.object(
                ev, name, side_effect=AssertionError(f"called {name}"))
                for name in forbidden]
            with patches[0], patches[1], patches[2], patches[3], patches[4]:
                record = ev.run_preflight(output, judges)

            self.assertEqual([len(judge.calls) for judge in judges], [1, 1])
            self.assertEqual(judges[0].calls, judges[1].calls)
            self.assertEqual(record["status"], "success")
            self.assertEqual({row["family"] for row in record["clients"]},
                             {"claude", "codex"})
            self.assertEqual({row["version"] for row in record["clients"]},
                             {"client 1.0"})
            encoded = output.read_text(encoding="utf-8")
            self.assertNotIn("sk-test-credential", encoded)
            self.assertNotIn("raw_evidence", encoded)

    def test_preflight_rejects_executable_auth_model_quota_and_schema_failures(self):
        failures = (
            "version",
            RuntimeError("not logged in"),
            RuntimeError("model unavailable"),
            ev.QuotaExhausted("usage limit token=sk-secret"),
        )
        with tempfile.TemporaryDirectory() as raw:
            for index, failure in enumerate(failures):
                output = Path(raw) / f"failure-{index}.json"
                judges = [self.FakeJudge("claude", failure=failure),
                          self.FakeJudge("codex")]
                with self.subTest(failure=failure), self.assertRaises(Exception):
                    ev.run_preflight(output, judges)
                record = json.loads(output.read_text(encoding="utf-8"))
                self.assertEqual(record["status"], "failed")
                self.assertNotIn("sk-secret", output.read_text(encoding="utf-8"))

            output = Path(raw) / "invalid-schema.json"
            judges = [self.FakeJudge("claude", winner="C"),
                      self.FakeJudge("codex")]
            with self.assertRaises(ev.EvidenceValidationError):
                ev.run_preflight(output, judges)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["status"],
                             "failed")

    def test_incomplete_successful_qualification_records_are_rejected(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            preflight_path = root / "preflight.json"
            calibration_dir = root / "calibration"
            ev.scaffold_seed(seed_path)
            judges = [self.FakeJudge("claude"), self.FakeJudge("codex")]
            ev.run_preflight(preflight_path, judges)
            preflight = json.loads(preflight_path.read_text(encoding="utf-8"))
            preflight.pop("clients")
            ev._write_json(preflight_path, preflight)
            with self.assertRaisesRegex(
                    ev.CheckpointCompatibilityError, "incomplete"):
                ev.load_compatible_preflight(preflight_path, judges)

            ev.run_preflight(preflight_path, judges)
            ev.run_calibration(
                seed_path, calibration_dir, preflight_path,
                client=ev.Client(dry_run=True), judges=judges)
            calibration_path = calibration_dir / "calibration.json"
            calibration = json.loads(calibration_path.read_text(encoding="utf-8"))
            calibration.pop("measurements")
            ev._write_json(calibration_path, calibration)
            expected = ev.calibration_identity(
                ev.Seed.load(seed_path),
                ev.load_compatible_preflight(preflight_path, judges), judges)
            with self.assertRaisesRegex(
                    ev.CheckpointCompatibilityError, "incomplete"):
                ev.load_compatible_calibration(calibration_path, expected)

    def test_preflight_is_an_explicit_cli_command(self):
        args = ev.build_parser().parse_args([
            "preflight", "--out", "qualification.json",
            "--claude-model", "claude-test", "--codex-model", "codex-test"])
        self.assertEqual(args.cmd, "preflight")
        self.assertEqual(args.claude_model, "claude-test")
        self.assertEqual(args.codex_model, "codex-test")

    def test_calibrate_requires_compatible_preflight_before_scoring(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            ev.scaffold_seed(seed_path)
            client = ev.Client(dry_run=True)
            judges = [self.FakeJudge("claude"), self.FakeJudge("codex")]

            with self.assertRaises(ev.CheckpointCompatibilityError):
                ev.run_calibration(
                    seed_path, root / "calibration", root / "missing.json",
                    client=client, judges=judges)
            self.assertEqual(client.calls, 0)
            self.assertEqual(client.evidence_marker(), 0)

            preflight_path = root / "preflight.json"
            ev.run_preflight(preflight_path, judges)
            judges[1].model = "changed-model"
            with self.assertRaises(ev.CheckpointCompatibilityError):
                ev.run_calibration(
                    seed_path, root / "calibration", preflight_path,
                    client=client, judges=judges)
            self.assertEqual(client.evidence_marker(), 0)

    def test_calibrate_scores_only_ancestor_through_every_non_tournament_stage(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            preflight_path = root / "preflight.json"
            output = root / "calibration"
            ev.scaffold_seed(seed_path)
            judges = [self.FakeJudge("claude"), self.FakeJudge("codex")]
            ev.run_preflight(preflight_path, judges)
            client = ev.Client(dry_run=True)
            forbidden = ("develop_notation", "express_katas", "express_ui",
                         "run_tournament", "seed_population", "crossover", "mutate")
            def forbidden_call(*args, **kwargs):
                del args, kwargs
                raise AssertionError("called forbidden calibration work")

            patches = [mock.patch.object(ev, name, forbidden_call)
                       for name in forbidden]
            with patches[0], patches[1], patches[2], patches[3], patches[4], \
                    patches[5], patches[6]:
                record = ev.run_calibration(
                    seed_path, output, preflight_path,
                    client=client, judges=judges)

            self.assertEqual(record["status"], "success")
            self.assertEqual(record["candidate"], "ancestor")
            self.assertTrue(record["identity_fingerprint"])
            stage_names = {path.stem for path in (output / "stages").glob("*.json")}
            self.assertEqual(stage_names, {
                "development", "katas", "ui_expression", "gate", "tokens",
                "residue", "ui_measurement", "comprehension"})
            self.assertFalse(list((output / "stages").glob("*duel*")))
            self.assertFalse((output / "tournament-report.json").exists())

    def test_calibrate_is_an_explicit_cli_command(self):
        args = ev.build_parser().parse_args([
            "calibrate", "--seed", "seed", "--preflight", "preflight.json",
            "--out", "calibration", "--claude-model", "claude-test",
            "--codex-model", "codex-test"])
        self.assertEqual(args.cmd, "calibrate")
        self.assertEqual(args.seed, "seed")
        self.assertEqual(args.preflight, "preflight.json")

    def test_real_run_requires_current_preflight_and_calibration(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            preflight_path = root / "preflight.json"
            calibration_dir = root / "calibration"
            ev.scaffold_seed(seed_path)
            seed = ev.Seed.load(seed_path)
            judges = [self.FakeJudge("claude"), self.FakeJudge("codex")]
            ev.run_preflight(preflight_path, judges)
            ev.run_calibration(
                seed_path, calibration_dir, preflight_path,
                client=ev.Client(dry_run=True), judges=judges)

            record = ev.require_run_qualifications(
                seed, preflight_path, calibration_dir / "calibration.json",
                judges, dry_run=False)
            self.assertEqual(record["status"], "success")
            judges[0].model = "stale-model"
            with self.assertRaises(ev.CheckpointCompatibilityError):
                ev.require_run_qualifications(
                    seed, preflight_path, calibration_dir / "calibration.json",
                    judges, dry_run=False)

    def test_real_run_rejects_missing_qualification_before_development(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            ev.scaffold_seed(seed_path)
            args = ev.build_parser().parse_args([
                "run", "--seed", str(seed_path), "--out", str(root / "run")])
            with mock.patch.object(
                    ev, "develop_and_score",
                    side_effect=AssertionError("candidate development started")):
                with self.assertRaises(ev.CheckpointCompatibilityError):
                    ev.run(args)

    def test_dry_run_does_not_require_or_create_qualification(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            ev.scaffold_seed(seed_path)
            seed = ev.Seed.load(seed_path)
            self.assertIsNone(ev.require_run_qualifications(
                seed, None, None, [], dry_run=True))
            self.assertEqual(list(root.glob("*qualification*")), [])


class RunPublication(unittest.TestCase):
    def test_incomplete_tournament_writes_no_ranking_or_composite(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            seed_path = root / "seed"
            output = root / "run"
            ev.scaffold_seed(seed_path)
            args = ev.build_parser().parse_args([
                "run", "--seed", str(seed_path), "--out", str(output),
                "--dry-run", "--workers", "1"])
            seed = ev.Seed.load(seed_path)
            population = ev.seed_population(
                random.Random(args.seed_rng), args.population)
            manifest = ev.build_run_manifest(
                seed, population, dry_run=True, seed_rng=args.seed_rng,
                generations=args.generations, claude_model=args.claude_model,
                codex_model=args.codex_model, workers=args.workers)
            incomplete = {"complete": False, "ratings": None,
                          "missing_records": [["claude", "universality",
                                               ["ancestor", "g0-00"]]]}
            with mock.patch.object(
                    ev, "build_run_manifest", return_value=manifest), \
                    mock.patch.object(ev, "run_tournament",
                                      return_value=incomplete):
                ev.run(args)

            self.assertFalse((output / "gen00" / "ranking.json").exists())
            self.assertFalse((output / "history.json").exists())
            self.assertFalse((output / "allele_associations.json").exists())
            self.assertEqual(len(list((output / "gen00").glob("*/scores.json"))),
                             12)

    def test_unavailable_local_cost_is_not_reported_as_zero(self):
        self.assertEqual(ev._format_cost(ev.Client(dry_run=True)), "$0.00")
        real = ev.Client.__new__(ev.Client)
        real.dry_run = False
        real.cost_usd = 0.0
        self.assertEqual(ev._format_cost(real), "unknown")
        real.cost_usd = 1.25
        self.assertEqual(ev._format_cost(real), "$1.25")


class BradleyTerry(unittest.TestCase):
    def _records(self):
        return [
            {"family": "claude", "axis": "universality", "left": "ancestor",
             "right": "b", "winner": "b"},
            {"family": "claude", "axis": "universality", "left": "ancestor",
             "right": "c", "winner": "c"},
            {"family": "claude", "axis": "universality", "left": "b",
             "right": "c", "winner": "b"},
        ]

    def test_fit_is_order_independent_anchored_finite_and_stable(self):
        records = self._records()
        first = ev.fit_bradley_terry(records, ["ancestor", "b", "c"])
        shuffled = list(reversed(records))
        second = ev.fit_bradley_terry(shuffled, ["c", "ancestor", "b"])
        self.assertEqual(list(first["ratings"]), ["ancestor", "b", "c"])
        self.assertEqual(first["ratings"]["ancestor"], ev.START_RATING)
        for ident in first["ratings"]:
            self.assertTrue(math.isfinite(first["ratings"][ident]))
            self.assertAlmostEqual(first["ratings"][ident], second["ratings"][ident],
                                   delta=ev.BT_TOLERANCE)
        self.assertLessEqual(first["max_delta"], ev.BT_TOLERANCE)

    def test_complete_separation_is_regularized_to_finite_values(self):
        records = [
            {"left": "ancestor", "right": "b", "winner": "b"},
            {"left": "ancestor", "right": "c", "winner": "c"},
            {"left": "b", "right": "c", "winner": "c"},
        ]
        result = ev.fit_bradley_terry(records, ["ancestor", "b", "c"])
        self.assertTrue(all(math.isfinite(value)
                            for value in result["utilities"].values()))
        self.assertGreater(result["regularization"], 0)

    def test_incomplete_matrix_is_not_ranked(self):
        report = ev.build_tournament_report(
            self._records()[:1], ["ancestor", "b", "c"],
            ["claude"], ["universality"])
        self.assertFalse(report["complete"])
        self.assertIsNone(report["ratings"])
        self.assertTrue(report["missing_records"])


class AlleleEvidence(unittest.TestCase):
    def _row(self, generation, ident, super_gene, allele, score):
        free = dict(ev.ANCESTOR_FREE)
        free["G7_absence"] = allele
        return {"generation": generation, "ident": ident,
                "super_gene": super_gene, "free": free, "score": score}

    def _three_blocks(self):
        rows = []
        for index, super_gene in enumerate(list(ev.SUPER_GENES)[:3]):
            rows.append(self._row(0, f"{index}a", super_gene, "option / maybe types",
                                  10.0 + index))
            rows.append(self._row(0, f"{index}b", super_gene,
                                  ev.ANCESTOR_FREE["G7_absence"], 5.0 + index))
        return rows

    def test_generation_zero_estimator_pairs_within_super_gene_blocks(self):
        report = ev.allele_associations(self._three_blocks())
        allele = next(row for row in report["generation_zero"]["G7_absence"]["alleles"]
                      if row["allele"] == "option / maybe types")
        self.assertEqual(allele["estimator"], "within_super_gene_pair_difference")
        self.assertEqual(allele["block_count"], 3)
        self.assertEqual(allele["candidate_count"], 6)
        self.assertEqual(allele["super_gene_count"], 3)
        self.assertEqual(allele["generation_count"], 1)
        self.assertEqual(allele["mean_paired_difference"], 5.0)
        self.assertIn("dispersion", allele)
        self.assertEqual(allele["status"], "sufficient")

    def test_fewer_than_three_blocks_is_insufficient_and_not_actionable(self):
        report = ev.allele_associations(self._three_blocks()[:4])
        rows = report["generation_zero"]["G7_absence"]["alleles"]
        allele = next(row for row in rows if row["allele"] == "option / maybe types")
        self.assertEqual(allele["status"], "insufficient_evidence")
        self.assertNotIn(allele["allele"], [row["allele"]
                                           for row in report["actionable"]])

    def test_later_generations_are_separate_descriptive_associations(self):
        history = self._three_blocks()
        history.append(self._row(1, "later", "S0_ancestor",
                                 "option / maybe types", 1000.0))
        report = ev.allele_associations(history)
        allele = next(row for row in report["generation_zero"]["G7_absence"]["alleles"]
                      if row["allele"] == "option / maybe types")
        self.assertEqual(allele["mean_paired_difference"], 5.0)
        self.assertIn("1", report["later_generations"])
        encoded = json.dumps(report).lower()
        self.assertIn("descriptive_association_mean", encoded)
        self.assertNotIn("marginal", encoded)


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

    def test_generation_zero_is_two_balanced_blocks_with_one_ancestor(self):
        pop = ev.seed_population(random.Random(41), 12)
        counts = {name: sum(g.super_gene == name for g in pop)
                  for name in ev.SUPER_GENES}
        self.assertEqual(set(counts.values()), {2})
        self.assertEqual(sum(g.is_ancestor for g in pop), 1)
        free_maps = [g.free for g in pop[1:]]
        self.assertEqual(len({id(value) for value in free_maps}), len(free_maps))

    def test_unsupported_population_size_fails_instead_of_claiming_balance(self):
        for size in (1, 6, 11, 13, 24):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "population 12"):
                ev.seed_population(random.Random(1), size)

    def test_super_gene_jump_is_different_and_registered(self):
        rng = random.Random(43)
        for source in ev.SUPER_GENES:
            for _ in range(50):
                target = ev.mutate_super_gene(source, rng)
                self.assertIn(target, ev.SUPER_GENES)
                self.assertNotEqual(target, source)
                self.assertFalse(target.startswith("S6"))


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

    def test_k05_unbounded_capacity_is_scored_but_not_disqualifying(self):
        contract = next(kata.contract for kata in ev.KATAS if kata.ident == "K05")
        self.assertNotIn("stated bound", contract)
        self.assertTrue(any(fact == "capacity_bounds"
                            for fact, _ in ev.SCORED_FACTS))
        audit = _clean_audit()
        katas = _clean_katas()
        katas["K05"] = """```cactus
rule SpawnOnTimer:
    spawn enemy
rule RemoveAfterDeath:
    destroy enemy
```
"""
        self.assertEqual(ev._decide_verdict(audit, katas)["verdict"], "pass")


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
        numeric = [p for p, s in props if s.get("type") in ("number", "integer")]
        self.assertEqual(numeric, ["ordinal"])
        self.assertEqual({p for p, _ in props},
                         {"occurrences", "pattern", "kata", "quote", "ordinal"})

    def test_a_round_without_the_ancestor_is_not_admitted(self):
        client = ev.Client(dry_run=True)
        challenger = ev.Candidate(genome=ev.Genome("g0-00", "S1_contract",
                                                   dict(ev.ANCESTOR_FREE)))
        challenger.gate = {"verdict": "pass"}
        report = ev.run_tournament(
            [ev.ClaudeJudge(client)], [challenger], random.Random(1), 1)
        self.assertFalse(report["complete"])
        self.assertIsNone(report["ratings"])


class TokenMeasure(unittest.TestCase):
    """The subscription backend counts by probing, so the arithmetic must hold."""

    def _client(self, sizes):
        c = ev.Client(dry_run=True)
        c.dry_run = False
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
