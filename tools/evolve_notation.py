"""Evolutionary search over Cactus notation designs.

Implements the experiment described in
``openspec/changes/evolve-cactus-notation/{proposal,design}.md``:

    genome (locus/allele vector)
        -> development   (LLM grows genotype into a notation + expressed katas)
        -> gate          (information-sufficiency, pass/fail)
        -> mechanical    (token count, imperative residue, std.ui metrics)
        -> tournament    (dual-family batch fit, ancestor always present)

Crossover and mutation are mechanical and fully deterministic under ``--seed-rng``.
Development and evaluation call the model. Note that Claude Opus 5 rejects
``temperature`` outright, so LLM calls cannot be made bit-reproducible; the
determinism guarantees here are (a) the GA is reproducible given the same
scores, and (b) every prompt, response, and score is checkpointed to disk.

Usage
-----
    # rehearse without model calls or qualification evidence
    python tools/evolve_notation.py run --seed experiments/notation-v2-seed \
        --out experiments/notation-v2-offline --dry-run

    # paid runs require current outputs from the separate qualification commands
    python tools/evolve_notation.py run --seed experiments/notation-v2-seed \
        --out experiments/notation-v2-pilot --generations 0 --population 12 \
        --preflight experiments/notation-v2-preflight.json \
        --calibration experiments/notation-v2-calibration/calibration.json

See ``tools/evolve_notation_runbook.md`` for the full safe workflow.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import hashlib
import inspect
import itertools
import json
import math
import os
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Callable, Iterable

try:  # absence is reported by preflight; offline commands remain available
    import claude_agent_sdk as _agent_sdk
except ImportError:  # pragma: no cover - import guard
    _agent_sdk = None


MODEL = "claude-opus-5"
CODEX_MODEL = "gpt-5.3-codex"
REPO = Path(__file__).resolve().parent.parent
SEED_FORMAT_VERSION = 2
RUN_FORMAT_VERSION = 2
HARNESS_VERSION = "2.0"
QUALIFICATION_FORMAT_VERSION = 1


# --------------------------------------------------------------------------
# Genome
# --------------------------------------------------------------------------
# The six linked loci (effect declaration, ordering, aggregation, cross-entity
# write, built-in extension, authoring tiers) travel as one indivisible
# super-gene: choosing the declarative allele at G1 makes the other five nearly
# free, so uniform crossover across them produces designs whose parts
# contradict. Everything else recombines 50/50.

SUPER_GENES: dict[str, dict[str, str]] = {
    "S0_ancestor": {
        "G1_effects": "inferred from imperative handler body",
        "G2_ordering": "author names other rules (`after:`) plus contract inference",
        "G3_aggregation": "hand-rolled reduce across rules, sentinel for 'no value'",
        "G4_cross_entity_write": "emit + apply continuation pair",
        "G5_extension": "all-or-nothing: reimplement the built-in or take it whole",
        "G6_tiers": "three (author rule / stdlib rule / extern rule)",
    },
    "S1_contract": {
        "G1_effects": "declared per rule, checked against the body",
        "G2_ordering": "derived from declared contracts; no author-written ordering",
        "G3_aggregation": "hand-rolled, unchanged from the ancestor",
        "G4_cross_entity_write": "direct join-write, legal because contracts prove non-interference",
        "G5_extension": "override a built-in by declaring a subsuming contract",
        "G6_tiers": "two (author / extern collapses into declared)",
    },
    "S2_relational": {
        "G1_effects": "structurally impossible; rules are relations, not actions",
        "G2_ordering": "stratified dataflow over relation dependencies",
        "G3_aggregation": "fold-over-join as a language primitive",
        "G4_cross_entity_write": "relation update",
        "G5_extension": "open: add rules to an existing relation",
        "G6_tiers": "one uniform tier",
    },
    "S3_dataflow": {
        "G1_effects": "declared as signal dependencies",
        "G2_ordering": "topological over the signal graph",
        "G3_aggregation": "fold over streams",
        "G4_cross_entity_write": "derived signal reading across entities",
        "G5_extension": "override or replace an individual signal",
        "G6_tiers": "one uniform tier",
    },
    "S4_synchronous": {
        "G1_effects": "declared write sets per equation",
        "G2_ordering": "phases plus explicit clock domains",
        "G3_aggregation": "fold primitive over the current instant",
        "G4_cross_entity_write": "direct write inside a double-buffered transaction",
        "G5_extension": "layered override of an equation",
        "G6_tiers": "two (equations / foreign)",
    },
    "S5_constraint": {
        "G1_effects": "invariants, not effects",
        "G2_ordering": "determined by the solver",
        "G3_aggregation": "implicit in constraint satisfaction",
        "G4_cross_entity_write": "implicit; constraints span entities",
        "G5_extension": "refine a constraint set",
        "G6_tiers": "one uniform tier",
    },
}

# S5 is carried deliberately: it is expected to FAIL the information-sufficiency
# gate (solver-determined ordering is not statically derivable). If it passes,
# the gate is broken -- that is the point of including it.

FREE_LOCI: dict[str, list[str]] = {
    "G7_absence": [
        "totality with sentinel values standing in for 'no result'",
        "option / maybe types",
        "observable no-op (the failure is recorded, not silent)",
        "failure as control flow",
    ],
    "G8_capacity": [
        "unbounded spawn",
        "declared pool capacity per template",
        "capacity inferred from spawn sites",
    ],
    "G9_bulk_data": [
        "none; repeated entity declarations written by hand",
        "table literals",
        "declaration-time comprehension",
        "generator functions evaluated at compile time",
    ],
    "G10_selection": [
        "separate filter / pairs / where clauses",
        "one unified query form",
        "relational join syntax",
        "comprehension syntax",
    ],
    "G11_trait_reference": [
        "module-qualified path plus explicit `as` alias",
        "auto-alias by last path segment",
        "destructuring bind of the fields actually used",
        "structural: match on field shape, not name",
    ],
    "G12_numeric": [
        "raw float, constructor-call vector literals",
        "vector literal syntax with swizzles",
        "units attached to numeric types",
        "frames of reference in the type (world / local / view)",
    ],
    "G13_time": [
        "discrete named phases",
        "continuous time with sampling",
        "temporal operators (since, until, for)",
        "explicit clock domains",
    ],
    "G14_mutation": [
        "imperative field assignment",
        "next-value equations",
        "declarative patches applied at a commit point",
        "transactional blocks",
    ],
    "G15_composition": [
        "template + use + children + from",
        "prototype cloning",
        "mixins",
        "set algebra over trait sets",
    ],
    "G16_persistence": [
        "field modifiers that are declared but unrealized",
        "explicit schema with migrations",
        "replication policy per field",
        "CRDT-backed convergent state",
    ],
    # G17 covers UI/layout only. Drawing stages are G21: the ancestor has no
    # special support for the former and a dedicated mechanism for the latter,
    # so one locus could not represent both without misdescribing it.
    "G17_presentation": [
        "ordinary traits and rules, no special support",
        "a first-class UI tier",
        "retained tree with automatic invalidation",
    ],
    "G21_gpu_stages": [
        "render-pass phases whose vertex and fragment stages are ordinary rules",
        "a separate shader language embedded in the program",
        "none; drawing is entirely a stdlib and backend concern",
        "compute kernels derived from ordinary rules with no pass declaration",
    ],
    "G18_determinism": [
        "unspecified",
        "declared per phase",
        "enforced by the backend",
        "replayable: same inputs reproduce the same frame",
    ],
    "G19_control_flow": [
        "if / return / bounded for",
        "total pattern match, no fallthrough",
        "guarded rules with no imperative body",
        "none; the rule head is the whole condition",
    ],
    "G20_metaprogramming": [
        "none",
        "comprehension over declarations",
        "staged evaluation",
        "macros",
    ],
}

ANCESTOR_FREE = {
    "G7_absence": FREE_LOCI["G7_absence"][0],
    "G8_capacity": FREE_LOCI["G8_capacity"][0],
    "G9_bulk_data": FREE_LOCI["G9_bulk_data"][0],
    "G10_selection": FREE_LOCI["G10_selection"][0],
    "G11_trait_reference": FREE_LOCI["G11_trait_reference"][0],
    "G12_numeric": FREE_LOCI["G12_numeric"][0],
    "G13_time": FREE_LOCI["G13_time"][0],
    "G14_mutation": FREE_LOCI["G14_mutation"][0],
    "G15_composition": FREE_LOCI["G15_composition"][0],
    "G16_persistence": FREE_LOCI["G16_persistence"][0],
    "G17_presentation": FREE_LOCI["G17_presentation"][0],
    "G21_gpu_stages": FREE_LOCI["G21_gpu_stages"][0],
    "G18_determinism": FREE_LOCI["G18_determinism"][0],
    "G19_control_flow": FREE_LOCI["G19_control_flow"][0],
    "G20_metaprogramming": FREE_LOCI["G20_metaprogramming"][0],
}

DONOR_PANEL = [
    "Verse (Epic): transactional semantics, failure as control flow",
    "Bevy / Flecs: query syntax, system ordering, archetype thinking",
    "Datalog and differential dataflow: joins as selection, incremental recompute",
    "Elm / FRP: signals, derived state, no manual ordering",
    "SQL: declarative selection with order by and limit",
    "Houdini VEX and shader graphs: per-element parallel semantics",
    "Erlang: targeted delivery, supervision, distribution",
    "Inform 7 / PuzzleScript: rule readability at the extreme",
    "Spreadsheets: dependency-ordered recomputation with no scheduling syntax",
    "Verilog / HDL: phases and clock domains as first-class structure",
]

MUTATION_P_FREE = 0.10
MUTATION_P_SUPER = 0.03


@dataclasses.dataclass
class Genome:
    """A locus/allele vector. Never prose -- that is what makes crossover exact."""

    ident: str
    super_gene: str
    free: dict[str, str]
    parents: tuple[str, ...] = ()
    repair_depth: int | None = None

    @property
    def is_ancestor(self) -> bool:
        return self.ident == "ancestor"

    def alleles(self) -> dict[str, str]:
        merged = dict(SUPER_GENES[self.super_gene])
        merged.update(self.free)
        return merged

    def describe(self) -> str:
        lines = [f"Super-gene: {self.super_gene}"]
        for locus, allele in SUPER_GENES[self.super_gene].items():
            lines.append(f"  {locus}: {allele}")
        lines.append("Free loci:")
        for locus in FREE_LOCI:
            lines.append(f"  {locus}: {self.free[locus]}")
        return "\n".join(lines)

    def to_dict(self) -> dict[str, Any]:
        return dataclasses.asdict(self)

    @classmethod
    def from_dict(cls, raw: dict[str, Any]) -> "Genome":
        return cls(
            ident=raw["ident"],
            super_gene=raw["super_gene"],
            free=dict(raw["free"]),
            parents=tuple(raw.get("parents", ())),
            repair_depth=raw.get("repair_depth"),
        )


def ancestor_genome() -> Genome:
    return Genome(ident="ancestor", super_gene="S0_ancestor", free=dict(ANCESTOR_FREE))


def seed_population(rng: random.Random, size: int) -> list[Genome]:
    """Generation 0: every super-gene represented, free loci drawn independently.

    A population descended from one ancestor has nothing to recombine, so
    diversity is injected here rather than evolved.
    """
    if size != 12:
        raise ValueError("the balanced generation-zero design requires population 12")
    out = [ancestor_genome()]
    supers = ["S0_ancestor"] + [name for name in SUPER_GENES
                                for _ in range(2) if name != "S0_ancestor"]
    for i, sg in enumerate(supers):
        free = {locus: rng.choice(alleles) for locus, alleles in FREE_LOCI.items()}
        out.append(Genome(ident=f"g0-{i:02d}", super_gene=sg, free=free))
    return out


def crossover(a: Genome, b: Genome, rng: random.Random, ident: str) -> Genome:
    """Super-gene inherited whole from one parent; free loci uniform 50/50."""
    sg = a.super_gene if rng.random() < 0.5 else b.super_gene
    free = {
        locus: (a.free[locus] if rng.random() < 0.5 else b.free[locus])
        for locus in FREE_LOCI
    }
    return Genome(ident=ident, super_gene=sg, free=free, parents=(a.ident, b.ident))


def mutate(g: Genome, rng: random.Random) -> Genome:
    free = dict(g.free)
    for locus, alleles in FREE_LOCI.items():
        if rng.random() < MUTATION_P_FREE:
            free[locus] = rng.choice([a for a in alleles if a != free[locus]])
    sg = g.super_gene
    if rng.random() < MUTATION_P_SUPER:
        sg = mutate_super_gene(sg, rng)
    return dataclasses.replace(g, super_gene=sg, free=free)


def mutate_super_gene(source: str, rng: random.Random) -> str:
    if source not in SUPER_GENES:
        raise ValueError(f"unknown super-gene {source!r}")
    return rng.choice([name for name in SUPER_GENES if name != source])


# --------------------------------------------------------------------------
# Kata suite -- the gate. Contracts are notation-independent by construction.
# --------------------------------------------------------------------------

ARENA = "examples/first-person-arena/main.cactus"
PARTICLES = "examples/particle-burst/particle_burst.cactus"
GRADIENT = "examples/gradient-square/gradient_square.cactus"


@dataclasses.dataclass(frozen=True)
class Kata:
    ident: str
    title: str
    stresses: str
    contract: str
    arena_ranges: tuple[tuple[int, int], ...]
    source: str = ARENA


KATAS: tuple[Kata, ...] = (
    Kata("K01", "Look and planar movement from input",
         "G11 trait reference, G12 numeric surface, G19 control flow",
         "Mouse motion turns the player's heading and pitch, pitch clamped to a "
         "fixed range. Four directional inputs move the player across the ground "
         "plane at a constant speed relative to the current heading, with no "
         "vertical component. No movement or look occurs once the game is over.",
         ((598, 610), (628, 650))),
    Kata("K02", "Gravity, ground detection, step-up",
         "G3 aggregation, G7 absence",
         "Each step an actor accelerates downward to a terminal speed. An actor "
         "standing over one or more walkable surfaces rests on the HIGHEST such "
         "surface whose top is within step height of its feet and not far below "
         "them, and is then considered grounded with zero vertical speed. An actor "
         "over no qualifying surface keeps falling. Walking off a ledge must fall "
         "rather than snap to whatever lies below.",
         ((668, 673), (675, 692), (746, 792))),
    Kata("K03", "Actor to solid contact resolution",
         "G4 cross-entity write",
         "When an actor's body overlaps a solid box, the actor is displaced by the "
         "minimum separation that ends the overlap. Multiple simultaneous overlaps "
         "each contribute.",
         ((718, 744),)),
    Kata("K04", "Symmetric actor separation",
         "G4 cross-entity write, G10 selection",
         "Two distinct living actors whose bodies overlap are each pushed apart by "
         "half the overlap. The relation is symmetric and must not double-apply or "
         "pair an actor with itself.",
         ((694, 716),)),
    Kata("K05", "Timed spawning and destruction",
         "G8 capacity, G15 composition",
         "A spawn point emits one enemy every fixed interval. An enemy that has "
         "finished dying is removed from the world.",
         ((852, 923), (1217, 1241))),
    Kata("K06", "Projectile lifecycle",
         "G4 cross-entity write, G13 time",
         "Firing is rate-limited by a cooldown. A projectile travels at constant "
         "velocity, expires after a fixed lifetime, and on first contact with a "
         "solid or an enemy is consumed -- exactly once, even when it contacts "
         "several things in the same step. Contact with an enemy kills that enemy.",
         ((1076, 1177),)),
    Kata("K07", "Camera rig pose composition",
         "G2 ordering, G15 composition",
         "A camera rig's world pose is derived each frame from the player's "
         "position offset to eye height, and from the player's heading and pitch "
         "composed into a rotation. The derivation runs after the player's position "
         "is final for the frame.",
         ((794, 817),)),
    Kata("K08", "Timed death transition",
         "G13 time, G14 mutation, G19 control flow",
         "A killed enemy switches to its death animation, stops seeking, tips over "
         "and fades out over a fixed duration, then is removed. Progress is a "
         "normalized 0..1 value driving both the tilt and the fade.",
         ((1179, 1249),)),
    Kata("K09", "Game over and world restart",
         "G14 mutation, G15 composition",
         "An enemy reaching the player ends the game: input stops affecting the "
         "player, the crosshair hides, a game-over label shows. A restart input "
         "then removes every enemy and projectile, resets every spawn timer, "
         "restores the HUD, and returns the player to the starting state.",
         ((1251, 1374),)),
    Kata("K10", "Obstacle-aware steering with vaulting",
         "G5 extension, G19 control flow",
         "An enemy heads toward the player. If the direct heading is blocked by a "
         "solid within a probe distance, it tries fixed deflections in order and "
         "takes the first unblocked one. If the blocking obstacle is short enough "
         "to clear, it jumps instead of steering around, playing a jump animation "
         "for the duration of the vault.",
         ((925, 1074),)),
    Kata("K11", "A drawing stage authored as ordinary rules",
         "G21 GPU stages, G12 numeric surface",
         "A single screen-space quad of fixed size is drawn centred on an "
         "entity's position. Each of its four corners is assigned a different "
         "colour, and the surface between them shows a smooth two-dimensional "
         "blend of those four colours. Per-corner work and per-pixel work are "
         "separately expressible, and neither is written in a different language "
         "from the rest of the program.",
         ((1, 40),), GRADIENT),
    Kata("K12", "One selection driving both simulation and drawing",
         "G21 GPU stages, G10 selection, G13 time",
         "A burst of entities is created at the pointer on click, each on a "
         "distinct radial heading. Each step they accelerate downward, move, and "
         "age; one that reaches the end of its life is removed. Every live entity "
         "-- and only a live one -- is drawn as a soft round dot: within its quad, "
         "pixels beyond a fixed radius from the centre are fully transparent and "
         "the rest fade linearly to that boundary. The set that gets drawn is "
         "determined by the same description of what an entity must carry that "
         "the movement step uses, and nothing in the source states which work "
         "happens on which device.",
         ((1, 83),), PARTICLES),
)

KATA_IDENTS = [k.ident for k in KATAS]

UI_BASELINE = {
    "source": "stdlib/std/ui.cactus",
    "measure_range": (181, 389),
    "arrange_range": (390, 692),
    "total_lines": 512,
    "max_nesting_depth": 8,
    # The scored scope is the two layout handlers, and inside them the ancestor
    # declares no externs at all: all 512 lines are expressed in the notation.
    # `ui.cactus` as a whole declares 7, but no candidate re-expresses the whole
    # module, so that figure has nothing comparable to be measured against.
    "extern_escape_ratio": 0.0,
    "module_extern_declarations": 7,
    "project_nesting_rewrite_trigger": 4,
}

GATE_FACTS = [
    ("access_sets", "per-rule trait read and write sets, exactly, without whole-program inference over imperative statement sequences"),
    ("effect_domains", "which external effect domains each rule touches"),
    ("ordering", "a total execution order that never requires an author to name another rule"),
    ("iteration_bounds", "a finite bound on every iteration construct"),
    ("lifetime_bounds", "when entities and handles cease to be valid"),
]

SCORED_FACTS = [
    ("capacity_bounds", "a declared bound on live spawned entities, so the backend can pool"),
    ("aliasing", "provable non-interference between rules that could run concurrently"),
    ("layout", "co-access or access-frequency information usable for data layout"),
]

RESIDUE_PATTERNS = [
    ("sentinel", "a literal value standing in for 'no result yet' because the notation has no vocabulary for absence"),
    ("reset_accumulate", "a rule that clears state so a later rule can accumulate into it"),
    ("cps_split", "one operation split across two rules connected by an event, solely because the first rule cannot write its result directly"),
    ("ordering_by_name", "execution order established by naming another rule"),
    ("provisional_write", "a value written and then corrected later within the same activation"),
    ("read_modify_write", "read-modify-write on state shared with another rule"),
    ("statement_order_dependent", "a handler body whose meaning depends on the order of its statements"),
]


# --------------------------------------------------------------------------
# Model client
# --------------------------------------------------------------------------

class QuotaExhausted(RuntimeError):
    """Raised when the backend refuses on quota rather than on content.

    Deliberately not caught by the per-candidate error trap: a quota wall applies
    to the whole run, so trapping it per candidate would grind through the rest
    of the population, bill for every attempt, and exit 0 reporting a full
    generation of "errored" candidates instead of one exhausted account.
    """


class CancellationToken:
    def __init__(self) -> None:
        self._event = threading.Event()
        self._lock = threading.Lock()
        self._error: QuotaExhausted | None = None

    @property
    def cancelled(self) -> bool:
        return self._event.is_set()

    def cancel(self, error: QuotaExhausted) -> None:
        with self._lock:
            if self._error is None:
                self._error = error
        self._event.set()

    def check(self) -> None:
        if self._event.is_set():
            raise self._error or QuotaExhausted("model work cancelled after quota failure")


_QUOTA_MARKERS = ("session limit", "usage limit", "rate limit", "quota")


def _quota_wall(text: str) -> bool:
    low = (text or "").lower()
    return any(marker in low for marker in _QUOTA_MARKERS)


def _flatten(system: list[dict[str, Any]]) -> str:
    return "\n\n".join(block["text"] for block in system)


class Client:
    """Claude Code Agent SDK transport using the operator's local login."""

    #: Claude Code's own preamble is ~27k tokens and would be re-billed on every
    #: call. Supplying our own system prompt and no tools removes it entirely.
    SUBSCRIPTION_BASE = dict(tools=[], setting_sources=[], max_turns=1)

    def __init__(self, model: str = MODEL, dry_run: bool = False,
                 cancellation: CancellationToken | None = None) -> None:
        self.model = model
        self.dry_run = dry_run
        self.cancellation = cancellation or CancellationToken()
        self.calls = 0
        self.input_tokens = 0
        self.output_tokens = 0
        self.cost_usd = 0.0
        self._client = None
        self._token_baseline: int | None = None
        self._baseline_lock = threading.Lock()
        self._thread_evidence = threading.local()
        if not dry_run:
            self._preflight()

    # -- credentials ------------------------------------------------------
    def _stop_for_quota(self, text: str) -> None:
        error = QuotaExhausted(text.strip() or "quota exhausted")
        self.cancellation.cancel(error)
        raise error

    def _evidence_log(self) -> list[dict[str, Any]]:
        if not hasattr(self._thread_evidence, "rows"):
            self._thread_evidence.rows = []
        return self._thread_evidence.rows

    def evidence_marker(self) -> int:
        return len(self._evidence_log())

    def evidence_since(self, marker: int) -> list[dict[str, Any]]:
        return self._evidence_log()[marker:]

    def _record_evidence(self, kind: str, prompt: Any, response: Any) -> None:
        self._evidence_log().append({
            "kind": kind, "prompt": prompt, "raw_response": response,
            "accounting": {"calls": self.calls,
                           "input_tokens": self.input_tokens,
                           "output_tokens": self.output_tokens,
                           "cost_usd": self.cost_usd or None},
        })

    def _preflight(self) -> None:
        """Resolve credentials before generation 0, not at the first call.

        Either SDK constructs happily with nothing and fails only when a request
        goes out -- which lands inside the per-candidate error trap, so an
        unauthenticated run grinds through the whole population and reports a
        generation of errored candidates instead of one missing credential.
        """
        if _agent_sdk is None:
            raise RuntimeError(
                "Claude work needs the Claude Code Agent SDK.\n"
                "Run: python -m pip install claude-agent-sdk\n"
                "Nothing has been spent.")
        if shutil.which("claude") is None:
            raise RuntimeError(
                "Claude work needs the `claude` CLI on PATH, which is "
                "what carries the subscription credential.\nInstall Claude Code, "
                "Nothing has been spent.")

    # -- generation -------------------------------------------------------
    def text(self, system: list[dict[str, Any]], user: str, *,
             max_tokens: int = 32000, effort: str = "high") -> str:
        return self.text_series(system, [user], max_tokens=max_tokens,
                                effort=effort)[0][0]

    def text_series(self, system: list[dict[str, Any]], users: list[str], *,
                    max_tokens: int = 32000,
                    effort: str = "high") -> list[tuple[str, int]]:
        """Run several prompts against one shared prefix.

        Returns (text, output_tokens) per prompt. Prompts share a single session
        so the prefix is cache-written once and read thereafter.
        """
        self.cancellation.check()
        if self.dry_run:
            result = [(f"```cactus\n# dry-run program for a prompt of {len(u)} chars\n"
                       + "rule DryRun:\n    value = 1\n" * 8 + "```\n",
                       len(u) // 4) for u in users]
            self._record_evidence("text_series", {"system": system, "users": users},
                                  result)
            return result
        result = self._sub_series(_flatten(system), users, effort)
        self._record_evidence("text_series", {"system": system, "users": users},
                              result)
        return result

    # -- scoring ----------------------------------------------------------
    def structured(self, system: list[dict[str, Any]], user: str,
                   schema: dict[str, Any], *, max_tokens: int = 16000,
        effort: str = "high") -> dict[str, Any]:
        self.cancellation.check()
        if self.dry_run:
            if schema is RESIDUE_SCHEMA:
                result = {"occurrences": []}
            elif schema is DEVELOPMENT_SCHEMA:
                result = _dry_run_instance(schema)
                result["notation"] = "# Dry-run notation\n" + "construct dry {}\n" * 200
            elif schema is DIFFERENTIATOR_SCHEMA:
                result = _dry_run_instance(schema)
                result["cannot_express"] = False
            else:
                result = _dry_run_instance(schema)
            self._record_evidence(
                "structured", {"system": system, "user": user, "schema": schema},
                result)
            return result
        # A fresh session prevents one candidate's answer entering the next call.
        result = self._sub_structured(_flatten(system), user, schema, effort)
        self._record_evidence(
            "structured", {"system": system, "user": user, "schema": schema}, result)
        return result

    #: A probe that carries the text to be measured and generates almost nothing
    #: back. Must stay byte-identical across calls or the baseline is invalid.
    _PROBE_SYSTEM = "Reply with the single character x and nothing else."
    _PROBE_TAIL = "\n\nReply with the single character x and nothing else."

    def count_tokens(self, text: str) -> int | None:
        """Exact count from the local model's own tokenizer."""
        self.cancellation.check()
        if self.dry_run:
            result = len(text) // 4
            self._record_evidence("token_count", text, result)
            return result
        # The local path has no counting endpoint, but a request reports
        # the input tokens it consumed. Measuring a fixed empty probe once gives
        # the constant overhead to subtract, leaving the text's own token count.
        with self._baseline_lock:
            if self._token_baseline is None:
                self._token_baseline = self._sub_probe_tokens("")
        result = max(0, self._sub_probe_tokens(text) - self._token_baseline)
        self._record_evidence("token_count", text, result)
        return result

    def _sub_probe_tokens(self, body: str) -> int:
        async def go():
            opts = _agent_sdk.ClaudeAgentOptions(
                model=self.model, system_prompt=self._PROBE_SYSTEM,
                # A TypedDict, not a dataclass: the SDK subscripts it.
                effort="low", thinking={"type": "disabled"},
                **self.SUBSCRIPTION_BASE)
            async with _agent_sdk.ClaudeSDKClient(options=opts) as c:
                await c.query(body + self._PROBE_TAIL)
                async for message in c.receive_response():
                    if isinstance(message, _agent_sdk.ResultMessage):
                        self._account_sub(message)
                        usage = message.usage or {}
                        if _quota_wall(getattr(message, "result", "")):
                            self._stop_for_quota(message.result or "")
                        return (usage.get("input_tokens", 0)
                                + usage.get("cache_read_input_tokens", 0)
                                + usage.get("cache_creation_input_tokens", 0))
            raise RuntimeError("token probe returned no result")
        return _agent_sdk_run(go)

    # -- local Agent SDK transport -----------------------------------------
    def _sub_series(self, system: str, users: list[str],
                    effort: str) -> list[tuple[str, int]]:
        async def go():
            opts = _agent_sdk.ClaudeAgentOptions(
                model=self.model, system_prompt=system, effort=effort,
                **self.SUBSCRIPTION_BASE)
            out = []
            async with _agent_sdk.ClaudeSDKClient(options=opts) as c:
                for user in users:
                    await c.query(user)
                    out.append(await self._sub_drain(c))
            return out
        return _agent_sdk_run(go)

    def _sub_structured(self, system: str, user: str, schema: dict[str, Any],
                        effort: str) -> dict[str, Any]:
        async def go():
            opts = _agent_sdk.ClaudeAgentOptions(
                model=self.model, system_prompt=system, effort=effort,
                output_format={"type": "json_schema", "schema": schema},
                **self.SUBSCRIPTION_BASE)
            async with _agent_sdk.ClaudeSDKClient(options=opts) as c:
                await c.query(user)
                async for message in c.receive_response():
                    if isinstance(message, _agent_sdk.ResultMessage):
                        self._account_sub(message)
                        parsed = getattr(message, "structured_output", None)
                        if parsed is None:
                            result = getattr(message, "result", "") or ""
                            if _quota_wall(result):
                                self._stop_for_quota(result)
                            raise RuntimeError(f"no structured output: {result!r}")
                        return parsed
            raise RuntimeError("session ended before returning a result")
        return _agent_sdk_run(go)

    async def _sub_drain(self, c: Any) -> tuple[str, int]:
        chunks: list[str] = []
        async for message in c.receive_response():
            if isinstance(message, _agent_sdk.AssistantMessage):
                chunks += [b.text for b in message.content
                           if isinstance(b, _agent_sdk.TextBlock)]
            elif isinstance(message, _agent_sdk.ResultMessage):
                self._account_sub(message)
                text = "".join(chunks)
                # The wall can arrive AS the assistant text, not only as an empty
                # turn with the notice on the result. Checking only the empty case
                # let the refusal string be stored as a candidate's notation and
                # all twelve of its katas, and be reported as recoverable.
                if _quota_wall(text) or _quota_wall(getattr(message, "result", "")):
                    self._stop_for_quota(text or message.result or "")
                return text, (message.usage or {}).get("output_tokens", 0)
        raise RuntimeError("session ended before returning a result")

    # -- accounting -------------------------------------------------------
    def _account_sub(self, message: Any) -> None:
        self.calls += 1
        usage = message.usage or {}
        # Cache creation is billed (and on a fresh session it is most of the
        # input), so leaving it out makes a run look nearly free.
        self.input_tokens += (usage.get("input_tokens", 0)
                              + usage.get("cache_read_input_tokens", 0)
                              + usage.get("cache_creation_input_tokens", 0))
        self.output_tokens += usage.get("output_tokens", 0)
        self.cost_usd += getattr(message, "total_cost_usd", 0.0) or 0.0


def _agent_sdk_run(coro_fn: Callable) -> Any:
    """Run one Agent SDK session. Safe from worker threads: each gets its loop."""
    import anyio
    return anyio.run(coro_fn)


def _dry_run_instance(schema: dict[str, Any]) -> Any:
    """Schema-shaped placeholder so --dry-run exercises the whole loop.

    Arrays whose element carries an enumerated key (kata, gate fact, residue
    pattern) expand over that enum: a one-element stub would slip past the
    completeness checks the gate is built on and leave them unexercised.
    """
    kind = schema.get("type")
    if kind == "object":
        return {k: _dry_run_instance(v) for k, v in schema.get("properties", {}).items()}
    if kind == "array":
        item = schema["items"]
        key, values = _enum_key(item)
        if key is None:
            return []
        return [_dry_run_instance(item) | {key: v} for v in values]
    if kind == "boolean":
        return True
    if kind == "integer":
        return 1
    if kind == "number":
        return 1.0
    if "enum" in schema:
        return schema["enum"][0]
    return "dry-run"


def _enum_key(item: dict[str, Any]) -> tuple[str | None, list[Any]]:
    """First enumerated property of an array element -- its identity axis."""
    for name, prop in item.get("properties", {}).items():
        if "enum" in prop:
            return name, prop["enum"]
    return None, []


def cached(text: str) -> dict[str, Any]:
    """A system block marked for prompt caching (stable prefix)."""
    return {"type": "text", "text": text, "cache_control": {"type": "ephemeral"}}


class EvidenceValidationError(ValueError):
    """A structured result cannot be admitted as experiment evidence."""


def _validate_exact_rows(rows: Any, identity: str, expected: Iterable[str],
                         label: str, required: Iterable[str] = ()) -> list[dict[str, Any]]:
    if not isinstance(rows, list):
        raise EvidenceValidationError(f"{label}: expected a list")
    expected_set = set(expected)
    seen: set[str] = set()
    validated: list[dict[str, Any]] = []
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise EvidenceValidationError(f"{label}[{index}]: expected an object")
        if identity not in row or not isinstance(row[identity], str):
            raise EvidenceValidationError(
                f"{label}[{index}]: missing or malformed {identity}")
        ident = row[identity]
        if ident not in expected_set:
            raise EvidenceValidationError(f"{label}: unknown {identity} {ident!r}")
        if ident in seen:
            raise EvidenceValidationError(f"{label}: duplicate {identity} {ident!r}")
        missing = [name for name in required if name not in row]
        if missing:
            raise EvidenceValidationError(
                f"{label} {ident}: missing fields {', '.join(missing)}")
        seen.add(ident)
        validated.append(row)
    missing_identities = expected_set - seen
    if missing_identities:
        raise EvidenceValidationError(
            f"{label}: missing identities {', '.join(sorted(missing_identities))}")
    return validated


def _validate_structured(value: Any, schema: dict[str, Any], path: str) -> None:
    kind = schema.get("type")
    expected_types = {
        "object": dict, "array": list, "string": str,
        "boolean": bool, "integer": int, "number": (int, float),
    }
    if kind in expected_types and not isinstance(value, expected_types[kind]):
        raise EvidenceValidationError(f"{path}: expected {kind}")
    if kind == "integer" and isinstance(value, bool):
        raise EvidenceValidationError(f"{path}: expected integer")
    if "enum" in schema and value not in schema["enum"]:
        raise EvidenceValidationError(f"{path}: unexpected value {value!r}")
    if kind == "object":
        required = schema.get("required", [])
        missing = [name for name in required if name not in value]
        if missing:
            raise EvidenceValidationError(
                f"{path}: missing fields {', '.join(missing)}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            unknown = set(value) - set(properties)
            if unknown:
                raise EvidenceValidationError(
                    f"{path}: unknown fields {', '.join(sorted(unknown))}")
        for name, item in value.items():
            if name in properties:
                _validate_structured(item, properties[name], f"{path}.{name}")
    if kind == "array":
        for index, item in enumerate(value):
            _validate_structured(item, schema["items"], f"{path}[{index}]")


def count_kata_tokens(client: "Client", expressions: dict[str, str]) -> dict[str, int]:
    """Measure expression text only, in stable kata order."""
    _validate_exact_rows([{"kata": ident} for ident in expressions], "kata",
                         KATA_IDENTS, "token expressions")
    measured = {ident: client.count_tokens(expressions[ident]) for ident in KATA_IDENTS}
    if any(value is None for value in measured.values()) or sum(measured.values()) <= 0:
        raise RuntimeError("no usable kata token measure")
    return {ident: int(value) for ident, value in measured.items() if value is not None}


# --------------------------------------------------------------------------
# Prompts
# --------------------------------------------------------------------------

PHILOSOPHY = """\
You are working on Cactus, a declarative gameplay-description language that
compiles to native C++ (EnTT ECS + raylib). Its goals, in priority order:

1. The notation must carry enough information for a backend to generate fast
   code -- access sets, effect domains, execution order, iteration bounds,
   lifetime bounds -- WITHOUT whole-program inference over imperative bodies.
2. Operations are total: no author-side validity checks, no nulls, no
   out-of-bounds, no manual memory management.
3. Execution is predictable: an author can reason about when every statement
   runs without consulting backend internals.
4. It is declarative. Where an author would encode a SEQUENCE, the notation
   should instead offer a way to encode a RELATIONSHIP.
5. It is teachable: the gameplay core must be explainable to a beginner.

Performance itself is the backend's obligation, not the author's. The
notation's obligation is to be ANALYZABLE.
"""


INHERITED_LOCI = tuple(SUPER_GENES["S0_ancestor"]) + tuple(FREE_LOCI)
DEVELOPMENT_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["notation", "realizations", "repairs"],
    "properties": {
        "notation": {"type": "string"},
        "realizations": {
            "type": "array",
            "items": {
                "type": "object", "additionalProperties": False,
                "required": ["locus", "status", "evidence"],
                "properties": {
                    "locus": {"type": "string", "enum": list(INHERITED_LOCI)},
                    "status": {"type": "string", "enum": [
                        "inherited", "overridden", "omitted", "merged"]},
                    "evidence": {"type": "string"},
                },
            },
        },
        "repairs": {
            "type": "array",
            "items": {
                "type": "object", "additionalProperties": False,
                "required": ["loci", "reason"],
                "properties": {
                    "loci": {"type": "array", "items": {
                        "type": "string", "enum": list(INHERITED_LOCI)}},
                    "reason": {"type": "string"},
                },
            },
        },
    },
}


def validate_development_result(
        genome: Genome, result: dict[str, Any]) -> tuple[str, int, dict[str, Any]]:
    _validate_structured(result, DEVELOPMENT_SCHEMA, "development")
    expected = tuple(genome.alleles())
    realizations = _validate_exact_rows(
        result["realizations"], "locus", expected, "development realizations",
        ("status", "evidence"))
    changed = {row["locus"] for row in realizations
               if row["status"] != "inherited"}
    repaired: set[str] = set()
    for index, repair in enumerate(result["repairs"]):
        if not repair["loci"]:
            raise EvidenceValidationError(
                f"development repairs[{index}]: loci must not be empty")
        for locus in repair["loci"]:
            if locus not in expected:
                raise EvidenceValidationError(
                    f"development repairs[{index}]: unknown locus {locus!r}")
            repaired.add(locus)
    if repaired != changed:
        missing = sorted(changed - repaired)
        invented = sorted(repaired - changed)
        detail = []
        if missing:
            detail.append(f"repairs omit {', '.join(missing)}")
        if invented:
            detail.append(f"repairs invent {', '.join(invented)}")
        raise EvidenceValidationError("development audit: " + "; ".join(detail))
    audit = {
        "realizations": realizations,
        "repairs": result["repairs"],
        "affected_loci": sorted(repaired),
    }
    return result["notation"], len(repaired), audit


def develop_notation(client: Client, g: Genome) -> tuple[str, int, dict[str, Any]]:
    """Genotype -> phenotype. Records how much repair the genome needed."""
    system = [cached(PHILOSOPHY)]
    donors = "\n".join(f"- {d}" for d in DONOR_PANEL)
    user = f"""\
Grow this genome into a concrete notation design.

{g.describe()}

Prior art you may borrow ALLELES from (not designs to imitate):
{donors}

Return a structured notation design with:
  1. A one-paragraph statement of the design's central idea.
  2. The concrete syntax for each locus above, with a small example of each.
  3. How a backend derives, from the notation alone: per-rule access sets,
     effect domains, total execution order, iteration bounds, lifetime bounds.
  4. Exactly one realization record for every inherited locus. Mark a locus
     inherited, overridden, omitted, or merged and cite its realization.
  5. A repair record naming every and only locus whose realization was
     overridden, omitted, or merged. An empty repair list is a real answer.

Do not evaluate or defend the design. Just specify it."""
    result = client.structured(
        system, user, DEVELOPMENT_SCHEMA, max_tokens=40000)
    return validate_development_result(g, result)


def express_katas(client: Client, notation: str,
                  katas: Iterable[Kata]) -> tuple[dict[str, str], dict[str, int]]:
    """Express every kata in the candidate notation, sharing one cached prefix.

    Also returns generation output-token accounting for each expression. The
    ranking measure is computed later from each expression's text.
    """
    system = [cached(PHILOSOPHY), cached(f"# Notation reference\n\n{notation}")]
    katas = list(katas)
    prompts = []
    for kata in katas:
        prompts.append(f"""\
Express this behavior in the notation reference above. Use ONLY constructs the
reference defines. If the notation cannot express some part of it, say so
explicitly under a "## Cannot express" heading rather than inventing syntax.

## {kata.ident} -- {kata.title}

{kata.contract}

Output the program in a fenced code block, then (if needed) the
"## Cannot express" section. No other commentary.""")
    results = client.text_series(system, prompts, max_tokens=16000)
    texts = {k.ident: r[0] for k, r in zip(katas, results)}
    tokens = {k.ident: r[1] for k, r in zip(katas, results)}
    return texts, tokens


def express_ui(client: Client, notation: str, ancestor_ui: str) -> str:
    system = [cached(PHILOSOPHY), cached(f"# Notation reference\n\n{notation}")]
    user = f"""\
Below is the measure/arrange layout implementation from the ancestor notation's
standard library: {UI_BASELINE['total_lines']} lines across two handler bodies,
reaching control-flow nesting depth {UI_BASELINE['max_nesting_depth']}.

What it computes is two tree traversals over a UI node hierarchy:
  - MEASURE: bottom-up. Each node's desired size is its intrinsic size (text,
    image, button label) combined with its children's desired sizes according
    to its container kind (stack along an axis with gap and padding; grid with
    column count, spans and cell size); leaves fall back to a preferred minimum.
  - ARRANGE: top-down. Each node receives a slot from its parent and computes
    its final position and size from anchors, pivot, offset and margins, then
    passes slots to its children. Effective visibility, enabled state, opacity
    and clip rect accumulate down the tree.

Express BOTH passes in the notation reference above.

```
{ancestor_ui}
```

Output the two passes in fenced code blocks. If the notation cannot express
part of it, say so under "## Cannot express" rather than inventing syntax."""
    return client.text(system, user, max_tokens=32000)


GATE_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["kata_expressible", "disqualifying", "scored", "verdict", "rationale"],
    "properties": {
        "kata_expressible": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["kata", "expressible", "evidence"],
                "properties": {
                    "kata": {"type": "string", "enum": KATA_IDENTS},
                    "expressible": {"type": "boolean"},
                    "evidence": {"type": "string"},
                },
            },
        },
        "disqualifying": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["fact", "status", "evidence"],
                "properties": {
                    "fact": {"type": "string", "enum": [f for f, _ in GATE_FACTS]},
                    "status": {"type": "string", "enum": ["satisfied", "partial", "absent"]},
                    "evidence": {"type": "string"},
                },
            },
        },
        "scored": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["fact", "status", "evidence"],
                "properties": {
                    "fact": {"type": "string", "enum": [f for f, _ in SCORED_FACTS]},
                    "status": {"type": "string", "enum": ["satisfied", "partial", "absent"]},
                    "evidence": {"type": "string"},
                },
            },
        },
        "authored_placement": {
            "type": "object",
            "additionalProperties": False,
            "required": ["found", "evidence"],
            "properties": {
                "found": {"type": "boolean"},
                "evidence": {"type": "string"},
            },
        },
        "verdict": {"type": "string", "enum": ["pass", "fail"]},
        "rationale": {"type": "string"},
    },
}
GATE_SCHEMA["required"].append("authored_placement")


def run_gate(client: Client, notation: str, katas: dict[str, str]) -> dict[str, Any]:
    dq = "\n".join(f"- {name}: {desc}" for name, desc in GATE_FACTS)
    sc = "\n".join(f"- {name}: {desc}" for name, desc in SCORED_FACTS)
    body = "\n\n".join(f"### {k}\n\n{v}" for k, v in sorted(katas.items()))
    system = [cached(PHILOSOPHY), cached(
        "You are auditing a proposed notation against a hard gate. Be strict and "
        "literal. 'partial' means a fact is only recoverable by analyzing "
        "imperative statement sequences, or requires the author to supply "
        "ordering by hand. Cite the specific construct that supplies each fact.")]
    user = f"""\
# Notation reference

{notation}

# Expressed katas

{body}

# Gate

Disqualifying facts -- a backend must derive each of these statically, from the
notation alone:
{dq}

Scored but not disqualifying:
{sc}

A kata is not expressible if the expression above contains a "Cannot express"
section covering a load-bearing part of its contract, or invents syntax the
notation reference does not define.

Report every kata and every disqualifying fact -- an omitted one counts against
the candidate. A "partial" on a disqualifying fact does not by itself fail the
gate, but say so plainly in the rationale.

`ordering` is at most "partial" if any expressed kata above establishes
execution order by naming another rule. That a total order also *exists* by
some fallback -- declaration order, a scheduler tiebreak -- does not make it
"satisfied": the author reached for the name because the derived order was not
the one the program needed. Naming a phase, a stage, or a clock is not this.

Separately, set `authored_placement.found` if the notation makes the AUTHOR state
where work runs: any marker, keyword or annotation on a declaration naming a
device, execution target, or pipeline stage kind -- a `gpu:`, `shader:`,
`target:` or `kind:` clause, or an equivalent. A descriptor field naming a
rendering *pipeline shape* is not this; a field naming the hardware or code path
that realizes it is. Quote the construct as evidence, or leave evidence empty."""
    result = client.structured(system, user, GATE_SCHEMA, max_tokens=20000)
    _validate_structured(result, GATE_SCHEMA, "information gate")
    _validate_exact_rows(
        result["kata_expressible"], "kata", KATA_IDENTS,
        "information gate katas", ("expressible", "evidence"))
    _validate_exact_rows(
        result["disqualifying"], "fact", (fact for fact, _ in GATE_FACTS),
        "information gate disqualifying facts", ("status", "evidence"))
    _validate_exact_rows(
        result["scored"], "fact", (fact for fact, _ in SCORED_FACTS),
        "information gate scored facts", ("status", "evidence"))
    return _decide_verdict(result, katas)


def _decide_verdict(result: dict[str, Any], katas: dict[str, str]) -> dict[str, Any]:
    """The verdict is arithmetic over the reported facts, never the model's call.

    A gate whose pass/fail is itself a model opinion is not a gate: the same
    prompt that produces the evidence would also grade it.
    """
    failures: list[str] = []
    reported = {row["kata"]: row for row in result.get("kata_expressible", [])}
    for kata in KATAS:
        row = reported.get(kata.ident)
        reason = _inexpressible_reason(katas.get(kata.ident, ""))
        if row is None:
            failures.append(f"{kata.ident}: not reported by the audit")
        elif not row["expressible"]:
            failures.append(f"{kata.ident}: audited as inexpressible")
        elif reason:
            failures.append(f"{kata.ident}: {reason}")

    seen = {row["fact"]: row["status"] for row in result.get("disqualifying", [])}
    for fact, _ in GATE_FACTS:
        status = seen.get(fact)
        if status is None:
            failures.append(f"{fact}: not reported by the audit")
        elif status == "absent":
            failures.append(f"{fact}: absent")

    result["verdict"] = "fail" if failures else "pass"
    result["gate_failures"] = failures
    return result


def reconcile_ordering(gate: dict[str, Any], residue: dict[str, Any]) -> dict[str, Any]:
    """Floor `ordering` at `partial` when the katas order rules by naming them.

    The gate and the residue extractor answer the same question in separate
    calls, and on the ancestor they contradicted each other: `ordering` came
    back "satisfied" while five `after: <RuleName>` blocks were quoted verbatim
    out of the same katas. A quote is checkable and a status is an opinion, so
    the quotes decide. `partial` is not disqualifying, so this can only correct
    the record -- it can never fail a candidate the audit passed.
    """
    named = sum(1 for occ in residue.get("occurrences", [])
                if occ.get("pattern") == "ordering_by_name")
    if not named:
        return gate
    for row in gate.get("disqualifying", []):
        if row.get("fact") != "ordering" or row.get("status") != "satisfied":
            continue
        row["status"] = "partial"
        row["evidence"] = (
            f"[reconciled] downgraded from satisfied: residue extraction quotes "
            f"{named} place(s) where the expressed katas establish execution "
            f"order by naming another rule.\n\n" + row.get("evidence", ""))
        gate["ordering_reconciled"] = named
    return gate


def _inexpressible_reason(text: str) -> str:
    """Judge-free evidence that an expression does not stand up as a program."""
    if not text.strip():
        return "expression is empty"
    if not re.search(r"```[^\n]*\n.*?```", text, re.S):
        return "expression contains no program"
    m = re.search(r"^#+[ \t]*Cannot express\b(.*)", text, re.M | re.I | re.S)
    if m and m.group(1).strip():
        return "expression declares it cannot express part of the contract"
    return ""


RESIDUE_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["occurrences"],
    "properties": {
        "occurrences": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["pattern", "kata", "quote", "ordinal"],
                "properties": {
                    "pattern": {"type": "string", "enum": [p for p, _ in RESIDUE_PATTERNS]},
                    "kata": {"type": "string", "enum": KATA_IDENTS},
                    "quote": {"type": "string"},
                    "ordinal": {"type": "integer"},
                },
            },
        }
    },
}


def count_residue(client: Client, notation: str, katas: dict[str, str]) -> dict[str, Any]:
    """Imperative residue: sequence encoded where relationship was meant.

    The model only EXTRACTS occurrences with citations; the count itself is
    arithmetic here, so the same extraction always yields the same number.
    """
    patterns = "\n".join(f"- {p}: {d}" for p, d in RESIDUE_PATTERNS)
    body = "\n\n".join(f"### {k}\n\n{v}" for k, v in sorted(katas.items()))
    system = [cached(
        "Extract occurrences only. Do not judge, rank, or summarize. Every "
        "occurrence must quote the exact text it refers to and give its 1-based "
        "ordinal among identical quotes in that kata. If a construct is "
        "ambiguous, do not report it.")]
    user = f"""\
# Notation reference

{notation}

# Expressed katas

{body}

# Patterns to extract

{patterns}

List every occurrence in the expressed katas. Quote exactly and identify the
1-based ordinal of that literal quote within its kata."""
    result = client.structured(system, user, RESIDUE_SCHEMA, max_tokens=20000, effort="medium")
    _validate_structured(result, RESIDUE_SCHEMA, "residue")
    accepted = canonicalize_residue_occurrences(result["occurrences"], katas)
    return score_residue_occurrences(accepted)


def canonicalize_residue_occurrences(
        occurrences: Any, expressions: dict[str, str]) -> list[dict[str, Any]]:
    if not isinstance(occurrences, list):
        raise EvidenceValidationError("residue occurrences: expected a list")
    patterns = {pattern for pattern, _ in RESIDUE_PATTERNS}
    canonical: dict[tuple[str, str, int, int], dict[str, Any]] = {}
    for index, occurrence in enumerate(occurrences):
        if not isinstance(occurrence, dict):
            raise EvidenceValidationError(
                f"residue occurrences[{index}]: expected an object")
        missing = {"pattern", "kata", "quote"} - set(occurrence)
        if missing:
            raise EvidenceValidationError(
                f"residue occurrences[{index}]: missing {', '.join(sorted(missing))}")
        pattern = occurrence["pattern"]
        kata = occurrence["kata"]
        quote = occurrence["quote"]
        if pattern not in patterns:
            raise EvidenceValidationError(f"residue: unknown pattern {pattern!r}")
        if kata not in expressions:
            raise EvidenceValidationError(f"residue: unknown kata {kata!r}")
        if not isinstance(quote, str) or not quote:
            raise EvidenceValidationError("residue: quote must be non-empty text")
        text = expressions[kata]
        matches = [match.start() for match in re.finditer(re.escape(quote), text)]
        if not matches:
            raise EvidenceValidationError(
                f"residue: quote is not present in {kata}: {quote!r}")
        ordinal = occurrence.get("ordinal")
        if ordinal is None and len(matches) > 1:
            raise EvidenceValidationError(
                f"residue: repeated quote in {kata} requires an ordinal")
        if ordinal is None:
            ordinal = 1
        if not isinstance(ordinal, int) or isinstance(ordinal, bool) \
                or ordinal < 1 or ordinal > len(matches):
            raise EvidenceValidationError(
                f"residue: ordinal {ordinal!r} is invalid for quote in {kata}")
        char_start = matches[ordinal - 1]
        char_end = char_start + len(quote)
        start = len(text[:char_start].encode("utf-8"))
        end = len(text[:char_end].encode("utf-8"))
        key = (pattern, kata, start, end)
        canonical[key] = {
            "pattern": pattern, "kata": kata, "quote": quote,
            "ordinal": ordinal, "start": start, "end": end,
        }
    return [canonical[key] for key in sorted(canonical)]


def score_residue_occurrences(accepted: list[dict[str, Any]]) -> dict[str, Any]:
    counts = {p: 0 for p, _ in RESIDUE_PATTERNS}
    for occurrence in accepted:
        pattern = occurrence.get("pattern")
        if pattern not in counts:
            raise EvidenceValidationError(f"residue: unknown pattern {pattern!r}")
        counts[pattern] += 1
    return {"counts": counts, "total": sum(counts.values()),
            "occurrences": accepted}


DIFFERENTIATOR_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["lines", "max_nesting_depth", "manual_walks", "dispatch_flags",
                 "manual_reductions", "extern_declarations", "total_declarations",
                 "cannot_express", "notes"],
    "properties": {
        "lines": {"type": "integer"},
        "max_nesting_depth": {"type": "integer"},
        "manual_walks": {"type": "integer"},
        "dispatch_flags": {"type": "integer"},
        "manual_reductions": {"type": "integer"},
        "extern_declarations": {"type": "integer"},
        "total_declarations": {"type": "integer"},
        "cannot_express": {"type": "boolean"},
        "notes": {"type": "string"},
    },
}


def score_differentiator(client: Client, notation: str, ui: str) -> dict[str, Any]:
    system = [cached(
        "Count constructs. Do not judge quality. A 'manual walk' is an explicit "
        "traversal construct the author wrote to visit a tree. A 'dispatch flag' "
        "is a boolean used to emulate mutually exclusive branches. A 'manual "
        "reduction' is a fold written by hand out of an accumulator plus a "
        "comparison or accumulation step. Count only executable control flow for "
        "nesting depth -- declarative or structural nesting does not count.")]
    user = f"""\
# Notation reference

{notation}

# Candidate measure/arrange implementation

{ui}

Count. `lines` counts only lines inside the two passes' code blocks, and
`extern_declarations` / `total_declarations` count only declarations written
inside those same code blocks -- the ancestor writes 2 declarations there and 0
externs, so the two numbers have to be measured in the same scope to compare.
An `extern_declaration` is any declaration that names layout work done outside
the notation. If the implementation says it cannot express part of the layout,
set cannot_express."""
    metrics = client.structured(system, user, DIFFERENTIATOR_SCHEMA, max_tokens=8000, effort="medium")
    _validate_structured(metrics, DIFFERENTIATOR_SCHEMA, "ui measurement")
    model_lines = metrics["lines"]
    mechanical_lines = _fenced_line_count(ui)
    model_cannot_express = metrics["cannot_express"]
    declared_cannot_express = bool(re.search(
        r"^#+[ \t]*Cannot express\b.*\S", ui, re.M | re.I | re.S))
    metrics["model_line_count"] = model_lines
    metrics["mechanical_line_count"] = mechanical_lines
    metrics["line_count_disagreement"] = model_lines != mechanical_lines
    metrics["lines"] = mechanical_lines
    metrics["model_cannot_express"] = model_cannot_express
    metrics["declared_cannot_express"] = declared_cannot_express
    metrics["cannot_express"] = model_cannot_express or declared_cannot_express
    metrics["rankable"] = not metrics["cannot_express"] and mechanical_lines > 0
    return metrics


def _fenced_line_count(text: str) -> int:
    return sum(len(block.strip("\n").splitlines())
               for block in re.findall(r"```[^\n]*\n(.*?)```", text, re.S))


COMPREHENSION_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["answers"],
    "properties": {
        "answers": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["kata", "described_behavior", "confidence"],
                "properties": {
                    "kata": {"type": "string", "enum": KATA_IDENTS},
                    "described_behavior": {"type": "string"},
                    "confidence": {"type": "string", "enum": ["low", "medium", "high"]},
                },
            },
        }
    },
}

GRADE_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["graded"],
    "properties": {
        "graded": {
            "type": "array",
            "items": {
                "type": "object",
                "additionalProperties": False,
                "required": ["kata", "correct", "reason"],
                "properties": {
                    "kata": {"type": "string", "enum": KATA_IDENTS},
                    "correct": {"type": "boolean"},
                    "reason": {"type": "string"},
                },
            },
        }
    },
}


def comprehension(client: Client, notation: str, expressions: dict[str, str],
                  contracts: dict[str, str], sample: tuple[str, ...]) -> dict[str, Any]:
    """Blind read: a cold agent gets the reference and a program, nothing else.

    This is the anti-golfing guard. A notation that wins on token count and
    fails here is correctly punished, which token count alone cannot do.
    """
    subset = {k: expressions[k] for k in sample if k in expressions}
    body = "\n\n".join(f"### {k}\n\n{v}" for k, v in sorted(subset.items()))
    read_system = [cached(
        "You have never seen this notation before. You are given only its "
        "reference and some programs written in it. Say what each program does, "
        "in plain prose, in terms of observable behavior. Do not guess from the "
        "identifier names alone -- read the constructs.")]
    reader_schema = json.loads(json.dumps(COMPREHENSION_SCHEMA))
    reader_schema["properties"]["answers"]["items"]["properties"]["kata"][
        "enum"] = list(sample)
    answers = client.structured(
        read_system,
        f"# Notation reference\n\n{notation}\n\n# Programs\n\n{body}",
        reader_schema, max_tokens=12000)
    _validate_structured(answers, reader_schema, "comprehension reader")
    answer_rows = _validate_exact_rows(
        answers["answers"], "kata", sample, "comprehension answers",
        ("described_behavior", "confidence"))

    pairs = "\n\n".join(
        f"### {a['kata']}\n\nIntended:\n{contracts.get(a['kata'], '(unknown)')}\n\n"
        f"Reader said:\n{a['described_behavior']}"
        for a in answer_rows)
    grade_system = [cached(
        "Grade whether the reader's description matches the intended behavior on "
        "the load-bearing points. Wording may differ freely. Mark incorrect only "
        "if the reader missed or inverted something that changes what the program "
        "observably does.")]
    grader_schema = json.loads(json.dumps(GRADE_SCHEMA))
    grader_schema["properties"]["graded"]["items"]["properties"]["kata"][
        "enum"] = list(sample)
    graded = client.structured(grade_system, pairs, grader_schema,
                               max_tokens=8000, effort="medium")
    _validate_structured(graded, grader_schema, "comprehension grader")
    rows = _validate_exact_rows(
        graded["graded"], "kata", sample, "comprehension grades",
        ("correct", "reason"))
    correct = sum(1 for r in rows if r["correct"])
    return {
        "answers": answer_rows,
        "graded": rows,
        "score": correct / len(rows) if rows else 0.0,
    }


DUEL_SCHEMA = {
    "type": "object",
    "additionalProperties": False,
    "required": ["winner", "reason"],
    "properties": {
        "winner": {"type": "string", "enum": ["A", "B"]},
        "reason": {"type": "string"},
    },
}

DUEL_AXES = {
    "universality": (
        "Which notation would extend more cleanly BEYOND first-person action "
        "games -- to 2D games, UI layout, particle and VFX systems, standalone "
        "physics simulation, saving and reloading world state, and replicating "
        "state across a network? Judge the concepts, not the spelling."),
    "declarativeness": (
        "In which notation does the author more often encode a RELATIONSHIP "
        "rather than a SEQUENCE? Penalize designs that require the author to "
        "order operations by hand, to reset state before accumulating into it, "
        "or to split one operation across two rules."),
}


DUEL_SYSTEM = (
    "Compare two notation designs on one axis. You do not know which is "
    "which; neither is an incumbent. Prefer the design that better serves "
    "the stated axis. Answer with a winner and one paragraph of reason.")


def duel_prompt(axis: str, a_text: str, b_text: str) -> str:
    return f"""\
# Axis

{DUEL_AXES[axis]}

# Candidate A

{a_text}

# Candidate B

{b_text}"""


@dataclasses.dataclass(frozen=True)
class JudgeResult:
    family: str
    winner: str
    reason: str
    usage: dict[str, Any]
    model: str
    presentation_order: dict[str, str]
    raw_evidence: Any


class ClaudeJudge:
    family = "claude"

    def __init__(self, client: Client, *,
                 runner: Callable[..., Any] = subprocess.run,
                 which: Callable[[str], str | None] = shutil.which) -> None:
        self.client = client
        self.model = client.model
        self.cancellation = client.cancellation
        self._runner = runner
        self._which = which

    def judge(self, axis: str, prompt: str,
              presentation_order: dict[str, str]) -> JudgeResult:
        del axis
        marker = self.client.evidence_marker()
        raw = self.client.structured(
            [cached(DUEL_SYSTEM)], prompt, DUEL_SCHEMA, max_tokens=6000)
        _validate_structured(raw, DUEL_SCHEMA, "Claude duel")
        evidence = self.client.evidence_since(marker)
        usage = evidence[-1]["accounting"] if evidence else {}
        return JudgeResult(
            self.family, raw["winner"], raw["reason"], usage, self.model,
            dict(presentation_order), {"result": raw, "calls": evidence})

    def client_version(self) -> str:
        try:
            import importlib.metadata
            sdk_version = importlib.metadata.version("claude-agent-sdk")
        except importlib.metadata.PackageNotFoundError:
            sdk_version = "unavailable"
        executable = self._which("claude")
        if executable is None:
            raise RuntimeError("claude executable is not on PATH")
        completed = self._runner(
            [executable, "--version"], capture_output=True, text=True, check=False)
        if completed.returncode:
            raise RuntimeError(
                f"cannot read Claude client version: {completed.stderr.strip()}")
        cli_version = completed.stdout.strip()
        return f"claude-agent-sdk {sdk_version}; claude-cli {cli_version}"


class CodexJudge:
    family = "codex"

    def __init__(self, model: str, cancellation: CancellationToken,
                 *, runner: Callable[..., Any] = subprocess.run,
                 which: Callable[[str], str | None] = shutil.which,
                 dry_run: bool = False) -> None:
        self.model = model
        self.cancellation = cancellation
        self._runner = runner
        self._which = which
        self.dry_run = dry_run

    def _executable(self) -> str:
        executable = self._which("codex")
        if executable is None:
            raise RuntimeError("codex executable is not on PATH")
        return executable

    def client_version(self) -> str:
        executable = self._executable()
        completed = self._runner(
            [executable, "--version"], capture_output=True, text=True, check=False)
        if completed.returncode:
            raise RuntimeError(
                f"cannot read Codex client version: {completed.stderr.strip()}")
        return completed.stdout.strip()

    def judge(self, axis: str, prompt: str,
              presentation_order: dict[str, str]) -> JudgeResult:
        del axis
        self.cancellation.check()
        if self.dry_run:
            winner = "A" if int(_stable_hash(prompt)[0], 16) % 2 == 0 else "B"
            result = {"winner": winner, "reason": "dry-run deterministic judge"}
            return JudgeResult(
                self.family, winner, result["reason"], {"calls": 0}, self.model,
                dict(presentation_order), {"result": result, "dry_run": True})
        executable = self._executable()
        with tempfile.TemporaryDirectory(prefix="cactus-codex-judge-") as raw_dir:
            work = Path(raw_dir)
            schema_path = work / "duel-schema.json"
            output_path = work / "duel-result.json"
            schema_path.write_text(json.dumps(DUEL_SCHEMA), encoding="utf-8")
            args = [
                executable, "exec", "--skip-git-repo-check", "--ephemeral",
                "--sandbox", "read-only",
                "--model", self.model, "--json", "--output-schema",
                str(schema_path), "--output-last-message", str(output_path), "-",
            ]
            try:
                completed = self._runner(
                    args, input=prompt, cwd=str(work), capture_output=True,
                    text=True, check=False)
            except FileNotFoundError as exc:
                raise RuntimeError("codex executable is not available") from exc
            combined = f"{completed.stdout}\n{completed.stderr}"
            if _quota_wall(combined):
                error = QuotaExhausted(combined.strip())
                self.cancellation.cancel(error)
                raise error
            if completed.returncode:
                raise RuntimeError(completed.stderr.strip() or "codex exec failed")
            events = []
            for line in completed.stdout.splitlines():
                if not line.strip():
                    continue
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError as exc:
                    raise RuntimeError(f"malformed Codex JSON event: {line!r}") from exc
            if not output_path.exists():
                raise RuntimeError("Codex produced no schema-constrained result")
            try:
                result = json.loads(output_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                raise RuntimeError("Codex returned malformed schema output") from exc
            _validate_structured(result, DUEL_SCHEMA, "Codex duel")
            usage = _public_usage(_codex_usage(events))
            return JudgeResult(
                self.family, result["winner"], _redact_text(result["reason"]), usage,
                self.model, dict(presentation_order),
                _redact_evidence({
                    "events": events, "result": result,
                    "stderr": completed.stderr}))


def _codex_usage(events: list[dict[str, Any]]) -> dict[str, Any]:
    usage: dict[str, Any] = {}
    for event in events:
        if isinstance(event.get("usage"), dict):
            usage.update(event["usage"])
    return usage


_CANARY_AXIS = "universality"
_CANARY_ORDER = {"A": "canary-one", "B": "canary-two"}
_CANARY_PROMPT = duel_prompt(
    _CANARY_AXIS,
    "A notation where behavior is declared as relations.",
    "A notation where behavior is declared as ordered steps.")


def _redact_text(value: Any) -> str:
    text = str(value)
    text = re.sub(r"(?i)\b(sk-[A-Za-z0-9_-]+)\b", "[redacted]", text)
    return re.sub(
        r"(?i)\b(api[_ -]?key|authorization|password|token)\s*[=:]\s*\S+",
        lambda match: f"{match.group(1)}=[redacted]", text)


def _redact_evidence(value: Any) -> Any:
    if isinstance(value, str):
        return _redact_text(value)
    if isinstance(value, list):
        return [_redact_evidence(item) for item in value]
    if isinstance(value, dict):
        return {
            key: ("[redacted]" if isinstance(key, str) and re.search(
                r"(?i)(?:api[_ -]?key|authorization|password|token)", key)
                  else _redact_evidence(item))
            for key, item in value.items()
        }
    return value


def _validate_judge_result(result: JudgeResult, judge: Any) -> None:
    if not isinstance(result, JudgeResult):
        raise EvidenceValidationError("judge returned no auditable result")
    _validate_structured(
        {"winner": result.winner, "reason": result.reason}, DUEL_SCHEMA,
        f"{judge.family} preflight")
    if result.family != judge.family or result.model != judge.model:
        raise EvidenceValidationError("judge result identity does not match adapter")
    if result.presentation_order != _CANARY_ORDER:
        raise EvidenceValidationError("judge changed the fixed canary order")


def _public_usage(usage: dict[str, Any]) -> dict[str, Any]:
    return {
        key: value for key, value in sorted(usage.items())
        if isinstance(value, (int, float)) and not isinstance(value, bool)
    }


def preflight_identity(judges: list[Any],
                       versions: list[str] | None = None) -> dict[str, Any]:
    resolved_versions = versions or [judge.client_version() for judge in judges]
    return {
        "harness_version": HARNESS_VERSION,
        "schema_hash": _stable_hash(DUEL_SCHEMA),
        "prompt_hash": _stable_hash(_CANARY_PROMPT),
        "clients": [{
            "family": judge.family, "model": judge.model, "version": version,
        } for judge, version in zip(judges, resolved_versions)],
    }


def run_preflight(output: Path, judges: list[Any] | None = None, *,
                  claude_model: str = MODEL,
                  codex_model: str = CODEX_MODEL) -> dict[str, Any]:
    """Qualify both local judge transports without touching candidate work."""
    try:
        if judges is None:
            cancellation = CancellationToken()
            client = Client(model=claude_model, cancellation=cancellation)
            judges = [ClaudeJudge(client), CodexJudge(codex_model, cancellation)]
        if [judge.family for judge in judges] != ["claude", "codex"]:
            raise EvidenceValidationError(
                "preflight requires Claude then Codex judge families")

        versions = [judge.client_version() for judge in judges]
        results = [
            judge.judge(_CANARY_AXIS, _CANARY_PROMPT, dict(_CANARY_ORDER))
            for judge in judges
        ]
        for judge, result in zip(judges, results):
            _validate_judge_result(result, judge)

        clients = [{
            "family": judge.family,
            "model": judge.model,
            "version": version,
            "usage": _public_usage(result.usage),
            "winner": result.winner,
            "reason": _redact_text(result.reason),
            "response_hash": _stable_hash({
                "winner": result.winner, "reason": result.reason}),
        } for judge, version, result in zip(judges, versions, results)]
        identity = preflight_identity(judges, versions)
        record = {
            "format_version": QUALIFICATION_FORMAT_VERSION,
            "status": "success",
            "identity": identity,
            "identity_fingerprint": _stable_hash(identity),
            "canary": {
                "axis": _CANARY_AXIS,
                "presentation_order": dict(_CANARY_ORDER),
                "prompt_hash": identity["prompt_hash"],
            },
            "clients": clients,
        }
        _write_json(output, record)
        return record
    except Exception as exc:
        record = {
            "format_version": QUALIFICATION_FORMAT_VERSION,
            "status": "failed",
            "error_type": type(exc).__name__,
            "error": _redact_text(exc),
        }
        _write_json(output, record)
        raise


def duel(client: Client, axis: str, a_text: str, b_text: str) -> str:
    order = {"A": "candidate-a", "B": "candidate-b"}
    return ClaudeJudge(client).judge(
        axis, duel_prompt(axis, a_text, b_text), order).winner


# --------------------------------------------------------------------------
# Batch ratings
# --------------------------------------------------------------------------

START_RATING = 1500.0
BT_REGULARIZATION = 0.01
BT_TOLERANCE = 1e-9
BT_MAX_ITERATIONS = 200


def _solve_linear(matrix: list[list[float]], vector: list[float]) -> list[float]:
    size = len(vector)
    augmented = [row[:] + [vector[index]] for index, row in enumerate(matrix)]
    for column in range(size):
        pivot = max(range(column, size),
                    key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-15:
            raise RuntimeError("singular Bradley-Terry system")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(size):
            if row == column:
                continue
            factor = augmented[row][column]
            if factor:
                augmented[row] = [
                    left - factor * right
                    for left, right in zip(augmented[row], augmented[column])]
    return [augmented[index][-1] for index in range(size)]


def fit_bradley_terry(records: list[dict[str, Any]], candidates: Iterable[str],
                       *, regularization: float = BT_REGULARIZATION,
                       tolerance: float = BT_TOLERANCE,
                       max_iterations: int = BT_MAX_ITERATIONS) -> dict[str, Any]:
    names = sorted(set(candidates), key=lambda name: (name != "ancestor", name))
    if "ancestor" not in names:
        raise ValueError("Bradley-Terry fit requires the ancestor anchor")
    unknown = {row.get(key) for row in records for key in ("left", "right")} - set(names)
    if unknown:
        raise ValueError(f"unknown comparison candidates: {sorted(unknown)}")
    variables = [name for name in names if name != "ancestor"]
    positions = {name: index for index, name in enumerate(variables)}
    utility = {name: 0.0 for name in names}
    ordered_records = sorted(
        records, key=lambda row: (row["left"], row["right"], row["winner"],
                                  row.get("family", ""), row.get("axis", "")))
    max_delta = float("inf")
    iterations = 0
    for iterations in range(1, max_iterations + 1):
        gradient = [0.0] * len(variables)
        information = [[0.0] * len(variables) for _ in variables]
        for row in ordered_records:
            left, right = row["left"], row["right"]
            difference = max(-40.0, min(40.0, utility[left] - utility[right]))
            probability = 1.0 / (1.0 + math.exp(-difference))
            outcome = 1.0 if row["winner"] == left else 0.0
            residual = outcome - probability
            weight = probability * (1.0 - probability)
            if left != "ancestor":
                i = positions[left]
                gradient[i] += residual
                information[i][i] += weight
            if right != "ancestor":
                j = positions[right]
                gradient[j] -= residual
                information[j][j] += weight
            if left != "ancestor" and right != "ancestor":
                i, j = positions[left], positions[right]
                information[i][j] -= weight
                information[j][i] -= weight
        for name, index in positions.items():
            gradient[index] -= regularization * utility[name]
            information[index][index] += regularization
        delta = _solve_linear(information, gradient) if variables else []
        max_delta = max((abs(value) for value in delta), default=0.0)
        for name, index in positions.items():
            utility[name] += delta[index]
        utility["ancestor"] = 0.0
        if max_delta <= tolerance:
            break
    if max_delta > tolerance:
        raise RuntimeError(
            f"Bradley-Terry fit did not converge within {max_iterations} iterations")
    scale = 400.0 / math.log(10.0)
    ratings = {name: START_RATING + scale * utility[name] for name in names}
    return {
        "ratings": ratings, "utilities": utility, "iterations": iterations,
        "max_delta": max_delta, "tolerance": tolerance,
        "regularization": regularization,
    }


def build_tournament_report(records: list[dict[str, Any]], candidates: Iterable[str],
                            families: Iterable[str], axes: Iterable[str]) -> dict[str, Any]:
    names = sorted(set(candidates), key=lambda name: (name != "ancestor", name))
    family_names = sorted(set(families))
    axis_names = sorted(set(axes))
    pairs = [tuple(sorted(pair)) for pair in itertools.combinations(names, 2)]
    expected = {(family, axis, pair) for family in family_names
                for axis in axis_names for pair in pairs}
    observed: dict[tuple[str, str, tuple[str, str]], dict[str, Any]] = {}
    duplicates = []
    for row in records:
        key = (row["family"], row["axis"],
               tuple(sorted((row["left"], row["right"]))))
        if key in observed:
            duplicates.append(key)
        observed[key] = row
    missing = sorted(expected - set(observed))
    complete = not missing and not duplicates and bool(expected)
    base = {
        "complete": complete, "comparison_count": len(records),
        "expected_comparison_count": len(expected),
        "missing_records": [list(key[:2]) + [list(key[2])] for key in missing],
        "duplicate_records": [list(key[:2]) + [list(key[2])]
                              for key in duplicates],
        "settings": {"method": "regularized_bradley_terry",
                     "regularization": BT_REGULARIZATION,
                     "tolerance": BT_TOLERANCE,
                     "max_iterations": BT_MAX_ITERATIONS},
        "records": sorted(records, key=lambda row: (
            row["family"], row["axis"], row["left"], row["right"])),
    }
    if not complete:
        return base | {"ratings": None, "family_ratings": None,
                       "agreement": _judge_agreement(records, axis_names)}
    family_ratings = {
        family: fit_bradley_terry(
            [row for row in records if row["family"] == family], names)
        for family in family_names
    }
    combined = fit_bradley_terry(records, names)
    return base | {
        "ratings": combined["ratings"], "fit": combined,
        "family_ratings": {family: fit["ratings"]
                           for family, fit in family_ratings.items()},
        "family_fits": family_ratings,
        "agreement": _judge_agreement(records, axis_names),
    }


def _judge_agreement(records: list[dict[str, Any]],
                     axes: Iterable[str]) -> dict[str, Any]:
    grouped: dict[tuple[str, tuple[str, str]], list[str]] = {}
    for row in records:
        key = (row["axis"], tuple(sorted((row["left"], row["right"]))))
        grouped.setdefault(key, []).append(row["winner"])
    per_axis = {}
    all_decisions = []
    for axis in sorted(axes):
        decisions = [winners for (row_axis, _), winners in grouped.items()
                     if row_axis == axis and len(winners) == 2]
        agreements = sum(winners[0] == winners[1] for winners in decisions)
        per_axis[axis] = {
            "compared": len(decisions), "agreed": agreements,
            "rate": agreements / len(decisions) if decisions else None,
        }
        all_decisions.extend(decisions)
    agreements = sum(winners[0] == winners[1] for winners in all_decisions)
    return {
        "per_axis": per_axis, "overall": {
            "compared": len(all_decisions), "agreed": agreements,
            "rate": agreements / len(all_decisions) if all_decisions else None,
        },
    }


# --------------------------------------------------------------------------
# Evaluation pipeline
# --------------------------------------------------------------------------

COMPREHENSION_SAMPLE = ("K02", "K06", "K09")


@dataclasses.dataclass
class Candidate:
    genome: Genome
    notation: str = ""
    katas: dict[str, str] = dataclasses.field(default_factory=dict)
    ui: str = ""
    gate: dict[str, Any] = dataclasses.field(default_factory=dict)
    mechanical: dict[str, Any] = dataclasses.field(default_factory=dict)
    differentiator: dict[str, Any] = dataclasses.field(default_factory=dict)
    comprehension: dict[str, Any] = dataclasses.field(default_factory=dict)
    development_audit: dict[str, Any] = dataclasses.field(default_factory=dict)
    #: Output tokens of the call that produced each kata. Persisted even when a
    #: later stage fails, so a resume can re-score without re-generating.
    expressed_tokens: dict[str, int] = dataclasses.field(default_factory=dict)
    error: str = ""

    #: Below this a "notation reference" or "kata expression" is not one. A
    #: backend refusal is ~60 bytes; the shortest real notation measured is 17k
    #: and the shortest real kata ~400.
    MIN_NOTATION_BYTES = 2000
    MIN_KATA_BYTES = 120

    @property
    def has_expression(self) -> bool:
        """Generation finished and produced something that could be scored.

        Length is checked, not just presence: a checkpoint written before the
        quota detection was tightened can hold a refusal notice where its
        notation should be, and resume would otherwise reuse and score it.
        """
        if not (self.notation and self.katas and self.ui):
            return False
        if set(self.katas) != set(KATA_IDENTS):
            return False
        if _quota_wall(self.notation) or len(self.notation) < self.MIN_NOTATION_BYTES:
            return False
        return all(v and not _quota_wall(v) and len(v) >= self.MIN_KATA_BYTES
                   for v in self.katas.values())

    @property
    def is_complete(self) -> bool:
        if self.error or not self.has_expression:
            return False
        try:
            gate_evidence = {
                name: self.gate[name] for name in GATE_SCHEMA["properties"]
                if name in self.gate}
            _validate_structured(gate_evidence, GATE_SCHEMA, "checkpoint gate")
            _validate_exact_rows(
                self.gate["kata_expressible"], "kata", KATA_IDENTS,
                "checkpoint gate katas", ("expressible", "evidence"))
            _validate_exact_rows(
                self.gate["disqualifying"], "fact",
                (fact for fact, _ in GATE_FACTS),
                "checkpoint gate disqualifying", ("status", "evidence"))
            _validate_exact_rows(
                self.gate["scored"], "fact", (fact for fact, _ in SCORED_FACTS),
                "checkpoint gate scored", ("status", "evidence"))
        except (EvidenceValidationError, KeyError, TypeError):
            return False
        if _decide_verdict(dict(self.gate), self.katas)["verdict"] \
                != self.gate.get("verdict"):
            return False
        if self.gate.get("verdict") == "fail":
            return True
        if self.gate.get("verdict") != "pass":
            return False
        try:
            tokens = self.mechanical.get("kata_tokens")
            residue = self.mechanical.get("residue")
            if (not isinstance(tokens, dict) or set(tokens) != set(KATA_IDENTS)
                    or not all(isinstance(value, int)
                               and not isinstance(value, bool) and value >= 0
                               for value in tokens.values())
                    or self.mechanical.get("kata_tokens_total") != sum(tokens.values())
                    or not isinstance(residue, dict)
                    or not isinstance(residue.get("occurrences"), list)
                    or not isinstance(residue.get("counts"), dict)
                    or set(residue["counts"]) != {
                        pattern for pattern, _ in RESIDUE_PATTERNS}
                    or not all(isinstance(value, int)
                               and not isinstance(value, bool) and value >= 0
                               for value in residue["counts"].values())
                    or residue.get("total") != sum(residue["counts"].values())):
                return False
            if (not isinstance(self.differentiator.get("rankable"), bool)
                    or not isinstance(
                        self.differentiator.get("mechanical_line_count"), int)
                    or self.differentiator["rankable"] != (
                        not self.differentiator.get("cannot_express", False)
                        and self.differentiator["mechanical_line_count"] > 0)):
                return False
            _validate_structured(
                {"answers": self.comprehension["answers"]},
                COMPREHENSION_SCHEMA, "checkpoint comprehension reader")
            _validate_structured(
                {"graded": self.comprehension["graded"]},
                GRADE_SCHEMA, "checkpoint comprehension grader")
            _validate_exact_rows(
                self.comprehension["answers"], "kata", COMPREHENSION_SAMPLE,
                "checkpoint comprehension answers",
                ("described_behavior", "confidence"))
            _validate_exact_rows(
                self.comprehension["graded"], "kata", COMPREHENSION_SAMPLE,
                "checkpoint comprehension grades", ("correct", "reason"))
        except (AttributeError, EvidenceValidationError, KeyError, TypeError):
            return False
        score = self.comprehension.get("score")
        correct = sum(row["correct"] for row in self.comprehension["graded"])
        return (isinstance(score, (int, float)) and not isinstance(score, bool)
                and score == correct / len(self.comprehension["graded"]))

    @property
    def passed(self) -> bool:
        return self.gate.get("verdict") == "pass" and not self.error

    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        d["genome"] = self.genome.to_dict()
        return d


def develop_and_score(client: Client, genome: Genome, seed: "Seed",
                      prior: Candidate | None = None,
                      checkpoint: Callable[[Candidate], None] | None = None,
                      stage_store: "StageStore | None" = None,
                      manifest: dict[str, Any] | None = None,
                      ) -> Candidate:
    """Evaluate one genome, reusing `prior`'s generated text when it has some.

    A candidate that reached the gate and then died -- to a quota wall, a schema
    fault, anything -- has already paid for its notation, twelve katas and UI
    rewrite. Re-scoring those costs a fraction of regenerating them.
    """
    cand = Candidate(genome=genome)

    def staged(name: str, dependencies: Any, producer: Callable[[], Any]) -> Any:
        if stage_store is None or manifest is None:
            return producer()
        fingerprint = stage_dependency_fingerprint(
            name, manifest, genome, _stable_hash(dependencies))
        marker = client.evidence_marker()

        def capture() -> StageOutcome:
            value = producer()
            evidence = client.evidence_since(marker)
            prompt = [row["prompt"] for row in evidence] or {
                "kind": "mechanical", "stage": name}
            raw = [row["raw_response"] for row in evidence] or {
                "kind": "mechanical", "value_hash": _stable_hash(value)}
            accounting = [row["accounting"] for row in evidence] or {"calls": 0}
            return StageOutcome(prompt, raw, value, accounting)

        return stage_store.run(name, fingerprint, capture)

    try:
        if genome.is_ancestor:
            source = staged("development", seed.fingerprint, lambda: {
                "notation": seed.notation, "repair_depth": 0,
                "audit": {"source": "seed", "affected_loci": []}})
            cand.notation = source["notation"]
            genome.repair_depth = source["repair_depth"]
            cand.development_audit = source["audit"]
            cand.katas = staged("katas", cand.notation, lambda: dict(seed.katas))
            cand.ui = staged("ui_expression", cand.notation, lambda: seed.ui)
        elif prior is not None and prior.has_expression:
            cand.notation, cand.katas, cand.ui = (prior.notation, dict(prior.katas),
                                                  prior.ui)
            cand.expressed_tokens = dict(prior.expressed_tokens)
            genome.repair_depth = prior.genome.repair_depth
            cand.development_audit = dict(prior.development_audit)
        else:
            developed = staged("development", genome.to_dict(), lambda: dict(zip(
                ("notation", "repair_depth", "audit"),
                develop_notation(client, genome))))
            cand.notation = developed["notation"]
            genome.repair_depth = developed["repair_depth"]
            cand.development_audit = developed["audit"]
            expressed = staged("katas", cand.notation, lambda: dict(zip(
                ("expressions", "tokens"), express_katas(client, cand.notation, KATAS))))
            cand.katas = expressed["expressions"]
            cand.expressed_tokens = expressed["tokens"]
            cand.ui = staged(
                "ui_expression", cand.notation,
                lambda: express_ui(client, cand.notation, seed.ui))

        # Persist the expensive half before scoring begins. A quota wall during
        # scoring otherwise discards a completed notation, twelve katas and a UI
        # rewrite -- and on a fixed plan that quota cannot be re-bought, only
        # waited out.
        if checkpoint is not None:
            checkpoint(cand)

        cand.gate = staged(
            "gate", {"notation": cand.notation, "katas": cand.katas},
            lambda: run_gate(client, cand.notation, cand.katas))
        if not cand.passed:
            return cand

        # Use the model's own tokenizer applied to the
        # kata text. Deliberately not the expressing call's output tokens, which
        # would include reasoning and so partly track how hard the notation was
        # to write in rather than how long the result is.
        tokens = staged("tokens", cand.katas,
                        lambda: count_kata_tokens(client, cand.katas))
        token_source = "count_tokens"
        # A missing measure must not read as an infinitely terse notation: the
        # expressiveness term divides by this, so an empty result would hand the
        # candidate an enormous bonus.
        residue = staged(
            "residue", {"notation": cand.notation, "katas": cand.katas},
            lambda: count_residue(client, cand.notation, cand.katas))
        reconcile_ordering(cand.gate, residue)
        cand.mechanical = {
            "kata_tokens": tokens,
            "kata_tokens_total": sum(tokens.values()),
            "kata_token_source": token_source,
            "residue": residue,
        }
        cand.differentiator = staged(
            "ui_measurement", {"notation": cand.notation, "ui": cand.ui},
            lambda: score_differentiator(client, cand.notation, cand.ui))
        cand.comprehension = staged(
            "comprehension",
            {"notation": cand.notation, "katas": cand.katas,
             "contracts": seed.contract_texts},
            lambda: comprehension(
                client, cand.notation, cand.katas, seed.contract_texts,
                COMPREHENSION_SAMPLE))
    except QuotaExhausted:
        if checkpoint is not None and cand.has_expression:
            cand.error = "interrupted by quota during scoring"
            checkpoint(cand)
        raise  # applies to the whole run, not this candidate
    except Exception as exc:  # keep one bad candidate from killing a generation
        cand.error = _redact_text(f"{type(exc).__name__}: {exc}")
    return cand


def run_tournament(judges: list[Any], cands: list[Candidate], rng: random.Random,
                   workers: int, checkpoint_dir: Path | None = None,
                   manifest: dict[str, Any] | None = None) -> dict[str, Any]:
    """Persist a complete, dual-family blinded batch before fitting ratings."""
    def persist(report: dict[str, Any]) -> dict[str, Any]:
        if checkpoint_dir is not None:
            _write_json(checkpoint_dir / "tournament-report.json", report)
        return report

    alive = [c for c in cands if c.passed]
    if not any(c.genome.is_ancestor for c in alive):
        report = build_tournament_report(
            [], [c.genome.ident for c in alive],
            [judge.family for judge in judges], DUEL_AXES)
        return persist(report)
    if len(alive) < 2:
        report = build_tournament_report(
            [], [c.genome.ident for c in alive],
            [judge.family for judge in judges], DUEL_AXES)
        return persist(report)
    base_jobs: list[dict[str, Any]] = []
    for axis in DUEL_AXES:
        for a, b in itertools.combinations(alive, 2):
            left, right = (b, a) if rng.random() < 0.5 else (a, b)
            order = {"A": left.genome.ident, "B": right.genome.ident}
            base_jobs.append({
                "axis": axis, "left": left, "right": right, "order": order,
                "prompt": duel_prompt(axis, left.notation, right.notation),
            })
    rng.shuffle(base_jobs)
    jobs = [(judge, job) for job in base_jobs for judge in judges]
    token = judges[0].cancellation if judges else CancellationToken()
    store = StageStore((checkpoint_dir or Path.cwd()) / "duels")

    def play(item: tuple[Any, dict[str, Any]]) -> dict[str, Any]:
        judge, job = item
        token.check()
        left = job["left"].genome.ident
        right = job["right"].genome.ident
        stage = f"{judge.family}__{job['axis']}__{min(left, right)}__{max(left, right)}"
        fingerprint = _stable_hash({
            "format_version": RUN_FORMAT_VERSION, "family": judge.family,
            "model": judge.model, "axis": job["axis"], "prompt": job["prompt"],
            "presentation_order": job["order"], "schema": DUEL_SCHEMA,
            "identity": stage_dependency_fingerprint("duel", manifest)
            if manifest is not None else None,
        })

        def call() -> StageOutcome:
            result = judge.judge(job["axis"], job["prompt"], job["order"])
            winner = left if result.winner == "A" else right
            row = {
                "family": result.family, "model": result.model,
                "axis": job["axis"], "left": left, "right": right,
                "presentation_order": result.presentation_order,
                "winner_slot": result.winner, "winner": winner,
                "reason": result.reason, "usage": result.usage,
                "raw_evidence": result.raw_evidence,
            }
            return StageOutcome(job["prompt"], result.raw_evidence, row, result.usage)

        return store.run(stage, fingerprint, call)

    records = _parallel(play, jobs, workers, token)
    report = build_tournament_report(
        records, [candidate.genome.ident for candidate in alive],
        [judge.family for judge in judges], DUEL_AXES)
    return persist(report)


def _parallel(fn: Callable, items: list, workers: int,
              cancellation: CancellationToken | None = None) -> list:
    token = cancellation or CancellationToken()
    if workers <= 1:
        out = []
        for item in items:
            token.check()
            try:
                out.append(fn(item))
            except QuotaExhausted as exc:
                token.cancel(exc)
                raise
        return out
    results: list[Any] = [None] * len(items)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        pending: dict[concurrent.futures.Future, int] = {}
        next_index = 0
        while next_index < len(items) and len(pending) < workers:
            pending[pool.submit(fn, items[next_index])] = next_index
            next_index += 1
        while pending:
            done, _ = concurrent.futures.wait(
                pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                index = pending.pop(future)
                try:
                    results[index] = future.result()
                except QuotaExhausted as exc:
                    token.cancel(exc)
                    for queued in pending:
                        queued.cancel()
                    raise
                if next_index < len(items) and not token.cancelled:
                    pending[pool.submit(fn, items[next_index])] = next_index
                    next_index += 1
    return results


COMPREHENSION_WEIGHT = 40.0

#: Per-fact credit for the three scored-but-not-disqualifying gate facts, which
#: were collected and written to disk but never read by `composite` -- the same
#: dead-measure family as the extern penalty. Deliberately below the 40 carried by
#: comprehension and the judge: design.md D1 calls these "capping, not zeroing",
#: so supplying them should move a ranking without rivalling the primary axes.
#: 30 points available in total.
SCORED_FACT_WEIGHT = 10.0
#: A fact recoverable only in part earns part of the credit -- the same
#: three-valued reading the gate already applies to the disqualifying facts.
SCORED_FACT_CREDIT = {"satisfied": 1.0, "partial": 0.5, "absent": 0.0}


def scored_facts_credit(gate: dict[str, Any]) -> float:
    """Credit for capacity bounds, provable non-interference, and layout info.

    An unreported fact earns nothing, matching how the gate treats an omitted
    disqualifying fact: silence is not evidence that the notation supplies it.
    """
    reported = {f.get("fact"): f.get("status") for f in gate.get("scored", [])}
    return SCORED_FACT_WEIGHT * sum(
        SCORED_FACT_CREDIT.get(reported.get(name), 0.0) for name, _ in SCORED_FACTS)


#: The two judged axes are universality and declarativeness -- universality being
#: one of the three criteria this experiment exists to measure -- so they cannot
#: be worth ~3% of the score range, as `(elo - ancestor_elo) / 10.0` made them.
#: They are still model preference over prose, so they must not be able to
#: overturn counted evidence either. Calibration: in a twelve-candidate field
#: every candidate plays 22 duels, which leaves the strongest and weakest roughly
#: 130-205 Elo from a mid-field ancestor, so 200 is where the term saturates
#: rather than an arbitrary round number. At full stretch it is worth exactly the
#: comprehension term and about an eighth of the mechanical span (~300 points on
#: the measured ancestor): enough to reorder near-ties, and nothing more -- two
#: candidates further apart than 2 x JUDGED_WEIGHT mechanically cannot be swapped
#: by judgement at all.
JUDGED_WEIGHT = 40.0
JUDGED_ELO_SPAN = 200.0


def judged_term(elo: float, ancestor_elo: float) -> float:
    """Elo relative to the ancestor, in score points, bounded both ways."""
    return JUDGED_WEIGHT * max(-1.0, min(1.0, (elo - ancestor_elo) / JUDGED_ELO_SPAN))


def extern_escape_ratio(d: dict[str, Any]) -> float:
    """Share of the layout expression's declarations that are external escapes.

    Measured in the same scope as the ancestor's 0.0 -- the two layout handlers
    -- rather than against a whole-module extern count no candidate re-expresses.
    """
    externs = max(0, d.get("extern_declarations") or 0)
    total = max(d.get("total_declarations") or 0, externs, 1)
    return externs / total


def composite(cand: Candidate, elo: float, ancestor_elo: float) -> float:
    """Selection score. Mechanical terms dominate; the judge reorders near-ties."""
    if not cand.passed or cand.differentiator.get("rankable") is False:
        return float("-inf")
    m = cand.mechanical
    d = cand.differentiator
    tokens = max(m.get("kata_tokens_total", 1), 1)
    ui_lines = max(d.get("mechanical_line_count") or d.get("lines", 1), 1)
    score = 0.0
    # Line count bought by pushing layout behind an external boundary is not a
    # line count, so the escape ratio discounts exactly the credit it would have
    # earned: a fully externalized rewrite gets none of it, however short.
    score += (30.0 * math.log(1 + UI_BASELINE["total_lines"] / ui_lines)
              * (1.0 - extern_escape_ratio(d)))
    score += 20.0 * math.log(1 + 6000 / tokens)
    score -= 4.0 * m.get("residue", {}).get("total", 0)
    score -= 6.0 * max(0, (d.get("max_nesting_depth", 0)
                           - UI_BASELINE["project_nesting_rewrite_trigger"]))
    score -= 3.0 * (d.get("manual_walks", 0) + d.get("dispatch_flags", 0)
                    + d.get("manual_reductions", 0))
    score += COMPREHENSION_WEIGHT * cand.comprehension.get("score", 0.0)
    score += scored_facts_credit(cand.gate)
    score -= 2.0 * (cand.genome.repair_depth or 0)
    # `language-philosophy` disallows the author stating where work runs. The
    # spec says to record it rather than disqualify, so it is a penalty large
    # enough to outweigh any terseness such a marker could buy.
    if cand.gate.get("authored_placement", {}).get("found"):
        score -= 60.0
    score += judged_term(elo, ancestor_elo)
    return score


# --------------------------------------------------------------------------
# Seed folder
# --------------------------------------------------------------------------

@dataclasses.dataclass(frozen=True)
class ContractRecord:
    ident: str
    title: str
    stresses: str
    text: str


@dataclasses.dataclass(frozen=True)
class ExpressionRecord:
    ident: str
    text: str


@dataclasses.dataclass(frozen=True)
class ProvenanceRecord:
    ident: str
    source: str
    ranges: tuple[tuple[int, int], ...]


@dataclasses.dataclass(init=False)
class Seed:
    notation: str
    contracts: dict[str, ContractRecord]
    expressions: dict[str, ExpressionRecord]
    provenance: dict[str, ProvenanceRecord]
    ui: str
    format_version: int

    def __init__(self, notation: str, katas: dict[str, str] | None = None,
                 ui: str = "", *, contracts: dict[str, ContractRecord] | None = None,
                 expressions: dict[str, ExpressionRecord] | None = None,
                 provenance: dict[str, ProvenanceRecord] | None = None,
                 format_version: int = SEED_FORMAT_VERSION) -> None:
        """Create a typed seed.

        ``katas`` remains an in-memory test convenience. Disk seeds must use the
        versioned, separated format and are never inferred from combined files.
        """
        self.notation = notation
        if expressions is None:
            expressions = {ident: ExpressionRecord(ident, text)
                           for ident, text in (katas or {}).items()}
        if contracts is None:
            known = {kata.ident: kata for kata in KATAS}
            contracts = {
                ident: ContractRecord(ident, known[ident].title,
                                      known[ident].stresses, known[ident].contract)
                for ident in expressions if ident in known
            }
        self.contracts = contracts
        self.expressions = expressions
        self.provenance = provenance or {}
        self.ui = ui
        self.format_version = format_version

    @property
    def katas(self) -> dict[str, str]:
        return {ident: record.text for ident, record in self.expressions.items()}

    @property
    def contract_texts(self) -> dict[str, str]:
        return {ident: record.text for ident, record in self.contracts.items()}

    @property
    def fingerprint(self) -> str:
        """Identifies the seed a checkpoint was produced against.

        Every candidate depends on the seed -- the ancestor *is* it, and each
        challenger's UI rewrite is prompted with `seed.ui` -- so a changed seed
        makes every stored result stale, including gate verdicts, which are
        legitimate results rather than errors and would otherwise be reused.
        """
        h = hashlib.sha256()
        h.update(str(self.format_version).encode("ascii"))
        h.update(self.notation.encode("utf-8"))
        h.update(self.ui.encode("utf-8"))
        for ident in sorted(self.expressions):
            h.update(ident.encode("utf-8"))
            h.update(self.expressions[ident].text.encode("utf-8"))
            h.update(self.contracts[ident].text.encode("utf-8"))
            provenance = self.provenance.get(ident)
            if provenance is not None:
                h.update(json.dumps(dataclasses.asdict(provenance), sort_keys=True)
                         .encode("utf-8"))
        return h.hexdigest()[:16]

    @classmethod
    def load(cls, path: Path) -> "Seed":
        manifest_path = path / "seed-manifest.json"
        if not manifest_path.exists():
            legacy = path / "katas"
            if legacy.exists():
                raise ValueError(
                    "legacy combined seed format is not admissible; create a fresh "
                    "versioned seed with scaffold-seed")
            raise ValueError(
                f"seed folder is missing {manifest_path}; run scaffold-seed")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("format_version") != SEED_FORMAT_VERSION:
            raise ValueError(
                f"unsupported seed format {manifest.get('format_version')!r}; "
                "run scaffold-seed into a fresh directory")

        def checked_text(entry: dict[str, Any]) -> str:
            artifact = path / entry["path"]
            text = _require(artifact)
            digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            if digest != entry["sha256"]:
                raise ValueError(f"seed artifact hash mismatch: {artifact}")
            return text

        notation = checked_text(manifest["notation"])
        ui = checked_text(manifest["ui"])
        try:
            genome = Genome.from_dict(json.loads(checked_text(manifest["genome"])))
        except (json.JSONDecodeError, KeyError, TypeError) as exc:
            raise ValueError("seed genome artifact is malformed") from exc
        if genome.to_dict() != ancestor_genome().to_dict():
            raise ValueError("seed genome is not the registered ancestor")
        contracts: dict[str, ContractRecord] = {}
        expressions: dict[str, ExpressionRecord] = {}
        provenance: dict[str, ProvenanceRecord] = {}
        rows = manifest.get("katas", [])
        _validate_exact_rows(rows, "ident", KATA_IDENTS, "seed manifest katas",
                             ("contract", "expression", "provenance"))
        for row in rows:
            ident = row["ident"]
            contract_meta = json.loads(checked_text(row["contract"]))
            expression = checked_text(row["expression"])
            provenance_meta = json.loads(checked_text(row["provenance"]))
            if (contract_meta.get("ident") != ident
                    or provenance_meta.get("ident") != ident):
                raise ValueError(f"seed artifact identity mismatch for {ident}")
            contracts[ident] = ContractRecord(
                ident, contract_meta["title"], contract_meta["stresses"],
                contract_meta["contract"])
            expressions[ident] = ExpressionRecord(ident, expression)
            provenance[ident] = ProvenanceRecord(
                ident, provenance_meta["source"],
                tuple(tuple(pair) for pair in provenance_meta["ranges"]))
        return cls(notation=notation, ui=ui, contracts=contracts,
                   expressions=expressions, provenance=provenance,
                   format_version=manifest["format_version"])


def _require(p: Path) -> str:
    if not p.exists():
        sys.exit(f"seed folder is missing {p}. Run: scaffold-seed")
    return p.read_text(encoding="utf-8")


def _slice(path: Path, ranges: Iterable[tuple[int, int]]) -> str:
    lines = path.read_text(encoding="utf-8").splitlines()
    chunks = []
    for lo, hi in ranges:
        chunks.append("\n".join(lines[lo - 1:hi]))
    return "\n\n# ---\n\n".join(chunks)


def scaffold_seed(out: Path) -> None:
    """Extract the ancestor phenotype mechanically from the repo. No invention."""
    arena = REPO / "examples" / "first-person-arena" / "main.cactus"
    ui_src = REPO / UI_BASELINE["source"]
    spec = REPO / "spec" / "cactus_dsl_spec.md"
    for p in (arena, ui_src, spec):
        if not p.exists():
            sys.exit(f"cannot scaffold: missing {p}")

    (out / "contracts").mkdir(parents=True, exist_ok=True)
    (out / "expressions").mkdir(parents=True, exist_ok=True)
    (out / "provenance").mkdir(parents=True, exist_ok=True)
    (out / "genome.json").write_text(
        json.dumps(ancestor_genome().to_dict(), indent=2), encoding="utf-8")

    # `spec/cactus_dsl_spec.md` alone is not the whole notation: it documents no
    # render-pass stage handlers, though two shipped examples use them. Left out,
    # the ancestor fails its own drawing katas for using syntax its reference
    # never defines -- a false negative against the incumbent.
    passes_spec = REPO / "openspec" / "specs" / "dsl-render-passes" / "spec.md"
    passes_lib = REPO / "stdlib" / "std" / "render" / "passes.cactus"
    parts = [
        "# Ancestor notation reference\n",
        "Assembled verbatim from the repo's normative sources.\n",
        "\n## From `spec/cactus_dsl_spec.md`\n\n" + spec.read_text(encoding="utf-8"),
    ]
    for path, heading in ((passes_spec, "openspec/specs/dsl-render-passes/spec.md"),
                          (passes_lib, "stdlib/std/render/passes.cactus")):
        if not path.exists():
            sys.exit(f"cannot scaffold: missing {path}")
        body = path.read_text(encoding="utf-8")
        if path.suffix == ".cactus":
            body = f"```cactus\n{body}\n```"
        parts.append(f"\n## From `{heading}`\n\n{body}")
    (out / "notation.md").write_text("\n".join(parts), encoding="utf-8")

    (out / "ui_measure_arrange.md").write_text(
        f"# Ancestor measure/arrange\n\n"
        f"From `{UI_BASELINE['source']}` lines "
        f"{UI_BASELINE['measure_range'][0]}-{UI_BASELINE['arrange_range'][1]}.\n\n"
        "```cactus\n"
        + _slice(ui_src, (UI_BASELINE["measure_range"], UI_BASELINE["arrange_range"]))
        + "\n```\n", encoding="utf-8")

    manifest_katas = []
    for kata in KATAS:
        src = REPO / kata.source
        if not src.exists():
            sys.exit(f"cannot scaffold {kata.ident}: missing {src}")
        contract_path = out / "contracts" / f"{kata.ident}.json"
        expression_path = out / "expressions" / f"{kata.ident}.cactus.md"
        provenance_path = out / "provenance" / f"{kata.ident}.json"
        contract_path.write_text(json.dumps({
            "ident": kata.ident, "title": kata.title, "stresses": kata.stresses,
            "contract": kata.contract,
        }, indent=2), encoding="utf-8")
        expression_path.write_text(
            "```cactus\n" + _slice(src, kata.arena_ranges) + "\n```\n",
            encoding="utf-8")
        provenance_path.write_text(json.dumps({
            "ident": kata.ident, "source": kata.source,
            "ranges": kata.arena_ranges,
        }, indent=2), encoding="utf-8")
        manifest_katas.append({
            "ident": kata.ident,
            "contract": _artifact_manifest_entry(out, contract_path),
            "expression": _artifact_manifest_entry(out, expression_path),
            "provenance": _artifact_manifest_entry(out, provenance_path),
        })

    manifest = {
        "format_version": SEED_FORMAT_VERSION,
        "notation": _artifact_manifest_entry(out, out / "notation.md"),
        "ui": _artifact_manifest_entry(out, out / "ui_measure_arrange.md"),
        "genome": _artifact_manifest_entry(out, out / "genome.json"),
        "katas": manifest_katas,
    }
    _write_json(out / "seed-manifest.json", manifest)

    (out / "README.md").write_text(
        "# Ancestor phenotype (generation 0, individual `ancestor`)\n\n"
        "Every file here was extracted mechanically from the repo -- nothing was\n"
        "written by hand or by a model. Review the kata contracts in `contracts/`\n"
        "against your own intuition BEFORE running the search: wrong katas will\n"
        "not show up as variance, and every downstream measurement inherits the\n"
        "error silently.\n\n"
        f"Baseline: measure/arrange is {UI_BASELINE['total_lines']} lines at "
        f"nesting depth {UI_BASELINE['max_nesting_depth']}, declaring no externs "
        f"of its own -- an extern escape ratio of "
        f"{UI_BASELINE['extern_escape_ratio']:.1f}, which is what a candidate's "
        f"layout rewrite is scored against. (`{UI_BASELINE['source']}` as a whole "
        f"declares {UI_BASELINE['module_extern_declarations']}, but no candidate "
        f"re-expresses the whole module.)\n",
        encoding="utf-8")
    print(f"scaffolded ancestor phenotype into {out}")


def _artifact_manifest_entry(root: Path, path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    return {
        "path": path.relative_to(root).as_posix(),
        "sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
    }


# --------------------------------------------------------------------------
# Orchestration
# --------------------------------------------------------------------------

def _stable_hash(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         default=str).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def identity_mismatches(existing: dict[str, Any], requested: dict[str, Any],
                        prefix: str = "") -> list[str]:
    mismatches: list[str] = []
    keys = sorted(set(existing) | set(requested))
    for key in keys:
        name = f"{prefix}.{key}" if prefix else key
        left = existing.get(key, object())
        right = requested.get(key, object())
        if isinstance(left, dict) and isinstance(right, dict):
            mismatches.extend(identity_mismatches(left, right, name))
        elif left != right:
            mismatches.append(name)
    return mismatches


@dataclasses.dataclass(frozen=True)
class StageOutcome:
    prompt: Any
    raw_response: Any
    validated_value: Any
    accounting: Any


@dataclasses.dataclass(frozen=True)
class StageRecord:
    stage: str
    dependency_fingerprint: str
    prompt: Any
    raw_response: Any
    validated_value: Any
    accounting: Any
    status: str
    error: str

    def is_reusable(self, fingerprint: str) -> bool:
        evidence = (self.prompt, self.raw_response, self.validated_value,
                    self.accounting)
        return (self.status == "complete"
                and self.dependency_fingerprint == fingerprint
                and all(value is not None for value in evidence)
                and not self.error)

    @classmethod
    def from_dict(cls, raw: dict[str, Any]) -> "StageRecord":
        required = {field.name for field in dataclasses.fields(cls)}
        if set(raw) != required:
            raise EvidenceValidationError("stage record is incomplete")
        return cls(**raw)


class StageStore:
    def __init__(self, root: Path) -> None:
        self.root = root

    def _path(self, stage: str) -> Path:
        safe = re.sub(r"[^A-Za-z0-9_.-]", "_", stage)
        return self.root / f"{safe}.json"

    def load(self, stage: str, fingerprint: str) -> Any | None:
        path = self._path(stage)
        if not path.exists():
            return None
        try:
            record = StageRecord.from_dict(
                json.loads(path.read_text(encoding="utf-8")))
        except (json.JSONDecodeError, KeyError, TypeError, EvidenceValidationError):
            return None
        return record.validated_value if record.is_reusable(fingerprint) else None

    def run(self, stage: str, fingerprint: str,
            producer: Callable[[], StageOutcome]) -> Any:
        cached_value = self.load(stage, fingerprint)
        if cached_value is not None:
            return cached_value
        path = self._path(stage)
        if path.exists():
            try:
                existing = json.loads(path.read_text(encoding="utf-8"))
                old_fingerprint = existing.get("dependency_fingerprint", "invalid")
                archive = self.root / "superseded" / old_fingerprint / path.name
                if not archive.exists():
                    _write_json(archive, existing)
            except (json.JSONDecodeError, OSError):
                pass
        try:
            outcome = producer()
            record = StageRecord(
                stage, fingerprint, outcome.prompt, outcome.raw_response,
                outcome.validated_value, outcome.accounting, "complete", "")
            _write_json(self._path(stage), dataclasses.asdict(record))
            return outcome.validated_value
        except Exception as exc:
            record = StageRecord(
                stage, fingerprint, "", "", None, {}, "error",
                _redact_text(f"{type(exc).__name__}: {exc}"))
            _write_json(self._path(stage), dataclasses.asdict(record))
            raise


class CheckpointCompatibilityError(ValueError):
    pass


def _source_hash(*objects: Any) -> str:
    return _stable_hash([inspect.getsource(obj) for obj in objects])


def _prompt_hash(function: Callable, *effective_inputs: Any) -> str:
    return _stable_hash({
        "builder_source": inspect.getsource(function),
        "effective_inputs": effective_inputs,
    })


def _prompt_hashes() -> dict[str, str]:
    return {
        "development": _prompt_hash(
            develop_notation, PHILOSOPHY, DONOR_PANEL, SUPER_GENES, FREE_LOCI),
        "katas": _prompt_hash(express_katas, PHILOSOPHY, [
            (kata.ident, kata.title, kata.contract) for kata in KATAS]),
        "ui_expression": _prompt_hash(
            express_ui, PHILOSOPHY, UI_BASELINE),
        "gate": _prompt_hash(
            run_gate, PHILOSOPHY, GATE_FACTS, SCORED_FACTS),
        "residue": _prompt_hash(count_residue, RESIDUE_PATTERNS),
        "ui_measurement": _prompt_hash(score_differentiator),
        "comprehension": _prompt_hash(
            comprehension, COMPREHENSION_SAMPLE),
        "duel": _prompt_hash(duel_prompt, DUEL_SYSTEM, DUEL_AXES),
    }


def build_run_manifest(seed: Seed, population: list[Genome], *, dry_run: bool,
                       seed_rng: int, generations: int, claude_model: str,
                       codex_model: str, workers: int) -> dict[str, Any]:
    del generations  # The terminal generation may increase on compatible resume.
    schemas = {
        "development": DEVELOPMENT_SCHEMA, "gate": GATE_SCHEMA,
        "residue": RESIDUE_SCHEMA, "ui": DIFFERENTIATOR_SCHEMA,
        "comprehension": COMPREHENSION_SCHEMA, "grade": GRADE_SCHEMA,
        "duel": DUEL_SCHEMA,
    }
    return {
        "format_version": RUN_FORMAT_VERSION,
        "harness_version": HARNESS_VERSION,
        "run_mode": "dry" if dry_run else "real",
        "random_seed": seed_rng,
        "population": len(population),
        "generation": 0,
        "artifact_hashes": {"seed": seed.fingerprint},
        "genotype_hashes": {
            genome.ident: _stable_hash(genome.to_dict()) for genome in population},
        "clients": {
            "generation": {"family": "claude-agent-sdk", "model": claude_model},
            "judges": [
                {"family": "claude", "model": claude_model},
                {"family": "codex", "model": codex_model},
            ],
        },
        "prompt_hashes": _prompt_hashes(),
        "schema_hashes": {name: _stable_hash(schema) for name, schema in schemas.items()},
        "scoring_hash": _source_hash(
            composite, scored_facts_credit, extern_escape_ratio,
            count_kata_tokens, _decide_verdict, reconcile_ordering,
            canonicalize_residue_occurrences, score_residue_occurrences,
            _fenced_line_count),
        "judge_hash": _source_hash(duel, run_tournament),
        "judge_configuration": {
            "families": ["claude", "codex"],
            "axes": list(DUEL_AXES), "workers": workers,
        },
    }


_IMMUTABLE_RUN_IDENTITY = {
    "format_version", "harness_version", "run_mode", "random_seed",
    "population", "generation", "artifact_hashes", "genotype_hashes",
}


def ensure_run_manifest(path: Path, requested: dict[str, Any],
                        resume: bool) -> list[str]:
    manifest_path = path / "run-manifest.json"
    if not manifest_path.exists():
        if resume and any(path.iterdir()):
            raise CheckpointCompatibilityError(
                "legacy checkpoint directory has no run manifest; it is "
                "non-comparative and cannot be resumed")
        _write_json(manifest_path, requested)
        return []
    try:
        existing = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CheckpointCompatibilityError(
            f"run manifest is invalid: {exc}") from exc
    mismatches = identity_mismatches(existing, requested)
    if not mismatches:
        return []
    immutable = [name for name in mismatches
                 if name.split(".", 1)[0] in _IMMUTABLE_RUN_IDENTITY]
    if not resume or immutable:
        raise CheckpointCompatibilityError(
            "run manifest is incompatible: " + ", ".join(mismatches))
    history_path = (path / "manifest-history"
                    / f"{_stable_hash(existing)}.json")
    if not history_path.exists():
        _write_json(history_path, existing)
    _write_json(manifest_path, requested)
    return mismatches


_STAGE_IDENTITY_FIELDS = {
    "development": ("run_mode", "artifact_hashes", "clients", "harness_version"),
    "katas": ("run_mode", "artifact_hashes", "clients", "harness_version"),
    "ui_expression": ("run_mode", "artifact_hashes", "clients", "harness_version"),
    "gate": ("run_mode", "artifact_hashes", "clients", "schema_hashes",
             "scoring_hash", "harness_version"),
    "tokens": ("run_mode", "artifact_hashes", "clients", "scoring_hash",
               "harness_version"),
    "residue": ("run_mode", "artifact_hashes", "clients", "schema_hashes",
                "scoring_hash", "harness_version"),
    "ui_measurement": ("run_mode", "artifact_hashes", "clients", "schema_hashes",
                       "scoring_hash", "harness_version"),
    "comprehension": ("run_mode", "artifact_hashes", "clients", "schema_hashes",
                      "scoring_hash", "harness_version"),
    "duel": ("run_mode", "clients", "prompt_hashes", "schema_hashes",
             "judge_hash", "judge_configuration", "harness_version"),
}


def stage_dependency_fingerprint(stage: str, manifest: dict[str, Any],
                                 genome: Genome | None = None,
                                 dependencies: Any = None) -> str:
    fields = _STAGE_IDENTITY_FIELDS.get(stage, tuple(sorted(manifest)))
    identity = {field: manifest[field] for field in fields}
    if "clients" in identity:
        identity["clients"] = (
            {"judges": manifest["clients"]["judges"]}
            if stage == "duel"
            else {"generation": manifest["clients"]["generation"]})
    if stage in manifest.get("prompt_hashes", {}):
        identity["prompt_hash"] = manifest["prompt_hashes"][stage]
    if stage in manifest.get("schema_hashes", {}):
        identity["schema_hash"] = manifest["schema_hashes"][stage]
    if genome is not None:
        identity["genotype"] = genome.to_dict()
    identity["dependencies"] = dependencies
    return _stable_hash(identity)


def inspect_checkpoint_directory(path: Path) -> dict[str, Any]:
    manifest_path = path / "run-manifest.json"
    if not manifest_path.exists():
        return {
            "status": "non-comparative", "classification": "legacy",
            "reason": "missing versioned run manifest",
        }
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return {
            "status": "non-comparative", "classification": "invalid",
            "reason": f"invalid manifest: {exc}",
        }
    if manifest.get("format_version") != RUN_FORMAT_VERSION:
        return {
            "status": "non-comparative", "classification": "legacy",
            "reason": "incompatible run format",
            "format_version": manifest.get("format_version"),
        }
    if manifest.get("harness_version") != HARNESS_VERSION:
        return {
            "status": "non-comparative", "classification": "stale-harness",
            "reason": "incompatible harness version", "manifest": manifest,
        }
    if manifest.get("run_mode") == "dry":
        return {
            "status": "non-comparative", "classification": "dry-run",
            "reason": "dry-run evidence cannot be used for real comparison",
            "manifest": manifest,
        }
    generation_dirs = sorted(path.glob("gen[0-9][0-9]"))
    missing = []
    for generation_dir in generation_dirs:
        report_path = generation_dir / "tournament" / "tournament-report.json"
        ranking_path = generation_dir / "ranking.json"
        if not report_path.exists() or not ranking_path.exists():
            missing.append(generation_dir.name)
            continue
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            missing.append(generation_dir.name)
            continue
        if not report.get("complete") or report.get("missing_records"):
            missing.append(generation_dir.name)
    if (not generation_dirs or missing or not (path / "history.json").exists()
            or not (path / "allele_associations.json").exists()):
        return {
            "status": "non-comparative", "classification": "incomplete",
            "reason": "one or more required generation reports are incomplete",
            "incomplete_generations": missing, "manifest": manifest,
        }
    return {"status": "current", "comparative": True, "manifest": manifest}


def load_compatible_preflight(path: Path, judges: list[Any]) -> dict[str, Any]:
    if not path.exists():
        raise CheckpointCompatibilityError(
            f"missing preflight qualification: {path}")
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CheckpointCompatibilityError(
            f"invalid preflight qualification: {exc}") from exc
    if record.get("status") != "success":
        raise CheckpointCompatibilityError(
            "preflight qualification is not successful")
    required = {"format_version", "status", "identity",
                "identity_fingerprint", "canary", "clients"}
    clients = record.get("clients")
    if (not required.issubset(record)
            or record.get("format_version") != QUALIFICATION_FORMAT_VERSION
            or not isinstance(clients, list)
            or len(clients) != len(judges)
            or not all(isinstance(row, dict) for row in clients)
            or {row.get("family") for row in clients if isinstance(row, dict)}
            != {judge.family for judge in judges}
            or any(not {"family", "model", "version", "usage", "winner",
                        "reason", "response_hash"}.issubset(row)
                   for row in clients if isinstance(row, dict))):
        raise CheckpointCompatibilityError(
            "preflight qualification is incomplete")
    expected = preflight_identity(judges)
    mismatches = identity_mismatches(record.get("identity", {}), expected)
    if (mismatches
            or record.get("identity_fingerprint") != _stable_hash(expected)):
        detail = ", ".join(mismatches) if mismatches else "identity fingerprint"
        raise CheckpointCompatibilityError(
            f"preflight qualification is incompatible: {detail}")
    return record


def calibration_identity(seed: Seed, preflight: dict[str, Any],
                         judges: list[Any]) -> dict[str, Any]:
    by_family = {judge.family: judge for judge in judges}
    manifest = build_run_manifest(
        seed, [ancestor_genome()], dry_run=False, seed_rng=0, generations=0,
        claude_model=by_family["claude"].model,
        codex_model=by_family["codex"].model, workers=1)
    fields = (
        "harness_version", "artifact_hashes", "genotype_hashes", "clients",
        "prompt_hashes", "schema_hashes", "scoring_hash", "judge_hash",
        "judge_configuration",
    )
    return {
        "experiment": {field: manifest[field] for field in fields},
        "preflight_fingerprint": preflight["identity_fingerprint"],
    }


def load_compatible_calibration(path: Path,
                                expected_identity: dict[str, Any]) -> dict[str, Any]:
    if not path.exists():
        raise CheckpointCompatibilityError(
            f"missing ancestor calibration: {path}")
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise CheckpointCompatibilityError(
            f"invalid ancestor calibration: {exc}") from exc
    if record.get("status") != "success":
        raise CheckpointCompatibilityError("ancestor calibration is not successful")
    required = {"format_version", "status", "identity", "identity_fingerprint",
                "candidate", "seed_fingerprint", "measurements"}
    measurements = record.get("measurements")
    if (not required.issubset(record)
            or record.get("format_version") != QUALIFICATION_FORMAT_VERSION
            or record.get("candidate") != "ancestor"
            or not isinstance(record.get("seed_fingerprint"), str)
            or not isinstance(measurements, dict)
            or not {"gate", "mechanical", "ui", "comprehension"}.issubset(
                measurements)):
        raise CheckpointCompatibilityError(
            "ancestor calibration is incomplete")
    mismatches = identity_mismatches(record.get("identity", {}), expected_identity)
    if (mismatches
            or record.get("identity_fingerprint") != _stable_hash(expected_identity)):
        detail = ", ".join(mismatches) if mismatches else "identity fingerprint"
        raise CheckpointCompatibilityError(
            f"ancestor calibration is incompatible: {detail}")
    return record


def run_calibration(seed_path: Path, output: Path, preflight_path: Path, *,
                    claude_model: str = MODEL, codex_model: str = CODEX_MODEL,
                    client: Client | None = None,
                    judges: list[Any] | None = None) -> dict[str, Any]:
    """Measure only the repository-derived ancestor, with no tournament."""
    if not preflight_path.exists():
        raise CheckpointCompatibilityError(
            f"missing preflight qualification: {preflight_path}")
    if judges is None:
        cancellation = CancellationToken()
        client = Client(model=claude_model, cancellation=cancellation)
        judges = [ClaudeJudge(client), CodexJudge(codex_model, cancellation)]
    if client is None:
        client = getattr(judges[0], "client", None)
    if client is None:
        raise ValueError("calibration needs the Claude scoring client")

    preflight = load_compatible_preflight(preflight_path, judges)
    seed = Seed.load(seed_path)
    identity = calibration_identity(seed, preflight, judges)
    output.mkdir(parents=True, exist_ok=True)
    final_path = output / "calibration.json"
    if final_path.exists():
        try:
            return load_compatible_calibration(final_path, identity)
        except CheckpointCompatibilityError:
            pass

    manifest = identity["experiment"] | {
        "format_version": RUN_FORMAT_VERSION,
        "run_mode": "real",
    }
    candidate = develop_and_score(
        client, ancestor_genome(), seed,
        checkpoint=lambda cand: _dump_candidate(output, cand, seed.fingerprint),
        stage_store=StageStore(output / "stages"), manifest=manifest)
    _dump_candidate(output, candidate, seed.fingerprint)
    if candidate.error or not candidate.passed:
        error = candidate.error or "ancestor failed the information gate"
        record = {
            "format_version": QUALIFICATION_FORMAT_VERSION,
            "status": "failed", "identity": identity,
            "identity_fingerprint": _stable_hash(identity),
            "error": _redact_text(error),
        }
        _write_json(final_path, record)
        raise RuntimeError(error)

    record = {
        "format_version": QUALIFICATION_FORMAT_VERSION,
        "status": "success",
        "identity": identity,
        "identity_fingerprint": _stable_hash(identity),
        "candidate": candidate.genome.ident,
        "seed_fingerprint": seed.fingerprint,
        "measurements": {
            "gate": candidate.gate,
            "mechanical": candidate.mechanical,
            "ui": candidate.differentiator,
            "comprehension": candidate.comprehension,
        },
    }
    _write_json(final_path, record)
    return record


def require_run_qualifications(
        seed: Seed, preflight_path: Path | None, calibration_path: Path | None,
        judges: list[Any], *, dry_run: bool) -> dict[str, Any] | None:
    if dry_run:
        return None
    if preflight_path is None:
        raise CheckpointCompatibilityError(
            "real run requires --preflight from the current preflight command")
    if calibration_path is None:
        raise CheckpointCompatibilityError(
            "real run requires --calibration from the current calibrate command")
    preflight = load_compatible_preflight(preflight_path, judges)
    expected = calibration_identity(seed, preflight, judges)
    return load_compatible_calibration(calibration_path, expected)


def _write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.{threading.get_ident()}.tmp")
    temporary.write_text(json.dumps(obj, indent=2, default=str), encoding="utf-8")
    os.replace(temporary, path)


def _archive_publication(path: Path) -> None:
    if not path.exists():
        return
    content = path.read_bytes()
    digest = hashlib.sha256(content).hexdigest()[:16]
    archive = path.parent / "superseded" / f"{path.stem}-{digest}{path.suffix}"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        os.replace(path, archive)
        return
    path.unlink()


def _format_cost(client: Client) -> str:
    if client.dry_run or client.cost_usd > 0:
        return f"${client.cost_usd:.2f}"
    return "unknown"


def _dump_candidate(gen_dir: Path, cand: Candidate, seed_fingerprint: str = "") -> None:
    d = gen_dir / cand.genome.ident
    d.mkdir(parents=True, exist_ok=True)
    (d / "notation.md").write_text(cand.notation, encoding="utf-8")
    (d / "ui_measure_arrange.md").write_text(cand.ui, encoding="utf-8")
    kdir = d / "katas"
    kdir.mkdir(exist_ok=True)
    for ident, text in cand.katas.items():
        (kdir / f"{ident}.md").write_text(text, encoding="utf-8")
    _write_json(d / "scores.json", {
        "genome": cand.genome.to_dict(),
        "seed_fingerprint": seed_fingerprint,
        "expressed_tokens": cand.expressed_tokens,
        "gate": cand.gate,
        "mechanical": cand.mechanical,
        "differentiator": cand.differentiator,
        "comprehension": cand.comprehension,
        "development_audit": cand.development_audit,
        "error": cand.error,
    })


def _load_candidate(gen_dir: Path, ident: str) -> tuple[Candidate, str] | None:
    d = gen_dir / ident
    if not (d / "scores.json").exists():
        return None
    try:
        raw = json.loads((d / "scores.json").read_text(encoding="utf-8"))
        katas = ({p.stem: p.read_text(encoding="utf-8")
                  for p in sorted((d / "katas").glob("*.md"))}
                 if (d / "katas").exists() else {})
        return Candidate(
            genome=Genome.from_dict(raw["genome"]),
            notation=(d / "notation.md").read_text(encoding="utf-8"),
            katas=katas,
            ui=(d / "ui_measure_arrange.md").read_text(encoding="utf-8"),
            gate=raw.get("gate", {}), mechanical=raw.get("mechanical", {}),
            differentiator=raw.get("differentiator", {}),
            comprehension=raw.get("comprehension", {}),
            development_audit=raw.get("development_audit", {}),
            expressed_tokens=raw.get("expressed_tokens", {}),
            error=raw.get("error", ""),
        ), raw.get("seed_fingerprint", "")
    except (json.JSONDecodeError, KeyError, OSError, TypeError, ValueError):
        return None


def allele_associations(history: list[dict[str, Any]]) -> dict[str, Any]:
    usable = [row for row in history if row.get("score") != float("-inf")]
    generation_zero = [row for row in usable if row.get("generation") == 0]
    blocks: dict[str, list[dict[str, Any]]] = {}
    for row in generation_zero:
        blocks.setdefault(row["super_gene"], []).append(row)

    primary: dict[str, Any] = {}
    actionable: list[dict[str, Any]] = []
    for locus, catalog in FREE_LOCI.items():
        allele_rows = []
        for allele in catalog:
            differences: list[float] = []
            candidate_ids: set[str] = set()
            super_genes: set[str] = set()
            for super_gene, pair in blocks.items():
                if len(pair) != 2:
                    continue
                carriers = [row for row in pair if row["free"].get(locus) == allele]
                controls = [row for row in pair if row["free"].get(locus) != allele]
                if len(carriers) != 1 or len(controls) != 1:
                    continue
                differences.append(carriers[0]["score"] - controls[0]["score"])
                candidate_ids.update((carriers[0]["ident"], controls[0]["ident"]))
                super_genes.add(super_gene)
            if not differences:
                continue
            status = "sufficient" if len(super_genes) >= 3 else "insufficient_evidence"
            row = {
                "allele": allele,
                "estimator": "within_super_gene_pair_difference",
                "sample_count": len(differences),
                "block_count": len(differences),
                "candidate_count": len(candidate_ids),
                "super_gene_count": len(super_genes),
                "generation_count": 1,
                "mean_paired_difference": sum(differences) / len(differences),
                "dispersion": statistics.stdev(differences)
                if len(differences) > 1 else 0.0,
                "status": status,
            }
            allele_rows.append(row)
            if status == "sufficient":
                actionable.append({"locus": locus, **row})
        allele_rows.sort(key=lambda row: (
            row["status"] != "sufficient", -row["mean_paired_difference"],
            row["allele"]))
        primary[locus] = {"alleles": allele_rows}

    later: dict[str, Any] = {}
    for generation in sorted({row["generation"] for row in usable
                              if row.get("generation", 0) > 0}):
        rows = [row for row in usable if row["generation"] == generation]
        loci = {}
        for locus in FREE_LOCI:
            by_allele: dict[str, list[float]] = {}
            for row in rows:
                by_allele.setdefault(row["free"][locus], []).append(row["score"])
            associations = [{
                "allele": allele, "sample_count": len(scores),
                "descriptive_association_mean": sum(scores) / len(scores),
                "frequency": len(scores) / len(rows),
                "fixed": len(scores) == len(rows),
            } for allele, scores in sorted(by_allele.items())]
            loci[locus] = {"associations": associations}
        later[str(generation)] = {
            "generation": generation, "candidate_count": len(rows), "loci": loci}

    actionable.sort(key=lambda row: (-row["mean_paired_difference"],
                                     row["locus"], row["allele"]))
    return {
        "generation_zero": primary,
        "actionable": actionable,
        "later_generations": later,
        "method_note": (
            "Generation zero uses within-super-gene paired associations; later "
            "generations are selection-confounded descriptive associations."),
    }


def run(args: argparse.Namespace) -> None:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    seed = Seed.load(Path(args.seed))
    rng = random.Random(args.seed_rng)
    preflight_path = (Path(args.preflight)
                      if getattr(args, "preflight", None) else None)
    calibration_path = (Path(args.calibration)
                        if getattr(args, "calibration", None) else None)
    if not args.dry_run and (preflight_path is None or calibration_path is None):
        require_run_qualifications(
            seed, preflight_path, calibration_path, [], dry_run=False)
    cancellation = CancellationToken()
    client = Client(model=args.claude_model, dry_run=args.dry_run,
                    cancellation=cancellation)
    judges = [
        ClaudeJudge(client),
        CodexJudge(args.codex_model, cancellation, dry_run=args.dry_run),
    ]
    require_run_qualifications(
        seed, preflight_path, calibration_path, judges, dry_run=args.dry_run)
    if not args.dry_run:
        print("Claude: local Claude Code session (bills subscription quota)")

    population = seed_population(rng, args.population)
    manifest = build_run_manifest(
        seed, population, dry_run=args.dry_run, seed_rng=args.seed_rng,
        generations=args.generations, claude_model=args.claude_model,
        codex_model=getattr(args, "codex_model", CODEX_MODEL), workers=args.workers)
    manifest_mismatches = ensure_run_manifest(out, manifest, args.resume)
    if manifest_mismatches:
        print("resume identity changed; revalidating affected stages: "
              + ", ".join(manifest_mismatches))
    generation_identity_changed = any(
        name.startswith((
            "clients.generation", "prompt_hashes.development",
            "prompt_hashes.katas", "prompt_hashes.ui_expression",
            "schema_hashes.development"))
        for name in manifest_mismatches)
    history: list[dict[str, Any]] = []
    started = time.time()

    for gen in range(args.generations + 1):
        gen_dir = out / f"gen{gen:02d}"
        gen_dir.mkdir(parents=True, exist_ok=True)
        print(f"\n=== generation {gen} ({len(population)} individuals) ===")

        def evaluate(g: Genome) -> Candidate:
            prior = None
            if args.resume:
                loaded = _load_candidate(gen_dir, g.ident)
                cached_cand, stamp = loaded if loaded else (None, "")
                if cached_cand is not None and stamp != seed.fingerprint:
                    print(f"  {g.ident}: checkpoint predates the current seed, re-evaluating")
                    cached_cand = None
                # Checkpoints are keyed by ident, which repeats across runs, so
                # a checkpoint from a different --seed-rng must not be adopted.
                same = (cached_cand is not None
                        and cached_cand.genome.free == g.free
                        and cached_cand.genome.super_gene == g.super_gene)
                # Only a checkpoint that actually finished scoring is adoptable.
                # A post-generation checkpoint carries no error and no gate, so
                # an error-only test would adopt it as complete and score it -inf.
                finished = (same and not manifest_mismatches
                            and cached_cand.is_complete)
                if finished:
                    print(f"  {g.ident}: resumed from checkpoint")
                    return cached_cand
                if (same and cached_cand.has_expression
                        and not generation_identity_changed):
                    # Generation already succeeded and was paid for; re-score only.
                    prior = cached_cand
                    print(f"  {g.ident}: reusing generated text, re-scoring")
                elif same:
                    print(f"  {g.ident}: checkpoint has no usable generated text, redoing")
                elif cached_cand is not None:
                    print(f"  {g.ident}: checkpoint is a different genotype, re-evaluating")
            cand = develop_and_score(
                client, g, seed, prior,
                lambda c: _dump_candidate(gen_dir, c, seed.fingerprint),
                StageStore(gen_dir / g.ident / "stages"), manifest)
            _dump_candidate(gen_dir, cand, seed.fingerprint)
            status = "error" if cand.error else cand.gate.get("verdict", "?")
            print(f"  {g.ident}: {g.super_gene} -> {status}")
            return cand

        try:
            cands = _parallel(evaluate, population, args.workers, cancellation)
            tournament = run_tournament(
                judges, cands, rng, args.workers, gen_dir / "tournament", manifest)
        except QuotaExhausted as exc:
            print(f"\nstopped: a model client is out of quota -- {exc}")
            print(f"spent so far: calls={client.calls} cost={_format_cost(client)}")
            saved = sorted(d.name for d in gen_dir.iterdir()
                           if (d / "scores.json").exists()) if gen_dir.exists() else []
            if saved:
                print(f"recoverable in {gen_dir}: {', '.join(saved)}")
                print("Re-run the same command with --resume once quota returns; "
                      "completed stages and duels are reused.")
            else:
                print(f"no candidate reached a checkpoint in {gen_dir}.")
            sys.exit(2)

        if not tournament["complete"]:
            print("  tournament incomplete: no rating published")
            _archive_publication(gen_dir / "ranking.json")
            if not history:
                _archive_publication(out / "history.json")
                _archive_publication(out / "allele_associations.json")
            print(f"  tournament diagnostics: {gen_dir / 'tournament' / 'tournament-report.json'}")
            return
        ratings = tournament["ratings"]
        if not isinstance(ratings, dict):
            raise RuntimeError("complete tournament has no ratings")
        anc_elo = ratings.get("ancestor", START_RATING)

        scored: list[tuple[Candidate, float]] = []
        for c in cands:
            s = composite(c, ratings.get(c.genome.ident, START_RATING), anc_elo)
            scored.append((c, s))
            history.append({
                "generation": gen, "ident": c.genome.ident,
                "super_gene": c.genome.super_gene, "free": c.genome.free,
                "score": s, "elo": ratings.get(c.genome.ident),
                "gate": c.gate.get("verdict"), "error": c.error,
                "repair_depth": c.genome.repair_depth,
                "kata_tokens": c.mechanical.get("kata_tokens_total"),
                "residue": c.mechanical.get("residue", {}).get("total"),
                "ui_lines": c.differentiator.get("lines"),
                "ui_depth": c.differentiator.get("max_nesting_depth"),
                "comprehension": c.comprehension.get("score"),
            })
        scored.sort(key=lambda t: t[1], reverse=True)

        _write_json(gen_dir / "ranking.json", [
            {"ident": c.genome.ident, "super_gene": c.genome.super_gene,
             "score": s, "elo": ratings.get(c.genome.ident)}
            for c, s in scored])
        _write_json(out / "history.json", history)
        _write_json(out / "allele_associations.json", allele_associations(history))

        print("  ranking:")
        for c, s in scored:
            mark = " (ancestor)" if c.genome.is_ancestor else ""
            print(f"    {s:9.2f}  {c.genome.ident:<10} {c.genome.super_gene}{mark}")

        if gen == args.generations:
            break

        survivors = [c.genome for c, s in scored if s != float("-inf")]
        if len(survivors) < 2:
            print("  stopping: fewer than two viable parents")
            break
        breeding = survivors[:max(2, len(survivors) // 2)]
        nxt = [ancestor_genome()]
        for i in range(args.population - 1):
            a, b = rng.sample(breeding, 2) if len(breeding) > 1 else (breeding[0], breeding[0])
            child = mutate(crossover(a, b, rng, f"g{gen + 1}-{i:02d}"), rng)
            nxt.append(child)
        population = nxt

    elapsed = time.time() - started
    print(f"\ncalls={client.calls} in={client.input_tokens} out={client.output_tokens} "
          f"cost={_format_cost(client)} elapsed={elapsed / 60:.1f}min")
    print(f"results in {out}")
    print(f"per-allele readout: {out / 'allele_associations.json'}")


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("scaffold-seed", help="extract the ancestor phenotype from the repo")
    s.add_argument("--out", required=True)

    p = sub.add_parser(
        "preflight", help="qualify the local Claude and Codex judge clients")
    p.add_argument("--out", required=True, help="qualification JSON file")
    p.add_argument("--claude-model", default=MODEL)
    p.add_argument("--codex-model", default=CODEX_MODEL)

    c = sub.add_parser(
        "calibrate", help="score only the current repository-derived ancestor")
    c.add_argument("--seed", required=True)
    c.add_argument("--preflight", required=True)
    c.add_argument("--out", required=True, help="dedicated calibration directory")
    c.add_argument("--claude-model", default=MODEL)
    c.add_argument("--codex-model", default=CODEX_MODEL)

    r = sub.add_parser("run", help="run the search")
    r.add_argument("--seed", required=True, help="folder with the initial candidate")
    r.add_argument("--out", required=True)
    r.add_argument("--generations", type=int, default=0,
                   help="0 = generation-0 pilot only")
    r.add_argument("--population", type=int, default=12)
    r.add_argument("--workers", type=int, default=4)
    r.add_argument("--seed-rng", type=int, default=1)
    r.add_argument("--claude-model", default=MODEL)
    r.add_argument("--codex-model", default=CODEX_MODEL)
    r.add_argument("--preflight", help="successful preflight JSON for a real run")
    r.add_argument("--calibration",
                   help="successful calibration JSON for a real run")
    r.add_argument("--dry-run", action="store_true",
                   help="exercise the whole loop with no model calls")
    r.add_argument("--resume", action="store_true",
                   help="reuse checkpointed candidates instead of re-evaluating")

    return ap


def main() -> None:
    args = build_parser().parse_args()
    if args.cmd == "scaffold-seed":
        scaffold_seed(Path(args.out))
    elif args.cmd == "preflight":
        run_preflight(
            Path(args.out), claude_model=args.claude_model,
            codex_model=args.codex_model)
    elif args.cmd == "calibrate":
        run_calibration(
            Path(args.seed), Path(args.out), Path(args.preflight),
            claude_model=args.claude_model, codex_model=args.codex_model)
    else:
        run(args)


if __name__ == "__main__":
    main()
