"""Evolutionary search over Cactus notation designs.

Implements the experiment described in
``openspec/changes/evolve-cactus-notation/{proposal,design}.md``:

    genome (locus/allele vector)
        -> development   (LLM grows genotype into a notation + expressed katas)
        -> gate          (information-sufficiency, pass/fail)
        -> mechanical    (token count, imperative residue, std.ui metrics)
        -> tournament    (pairwise Elo, blinded, ancestor always present)

Crossover and mutation are mechanical and fully deterministic under ``--seed-rng``.
Development and evaluation call the model. Note that Claude Opus 5 rejects
``temperature`` outright, so LLM calls cannot be made bit-reproducible; the
determinism guarantees here are (a) the GA is reproducible given the same
scores, and (b) every prompt, response, and score is checkpointed to disk.

Usage
-----
    # one-time: build the ancestor phenotype out of the repo
    python tools/evolve_notation.py scaffold-seed --out experiments/seed

    # pilot: generation 0 only
    python tools/evolve_notation.py run --seed experiments/seed \
        --out experiments/run-01 --generations 0 --population 12

    # full run, resuming from checkpoints
    python tools/evolve_notation.py run --seed experiments/seed \
        --out experiments/run-01 --generations 5 --population 12 --resume
"""

from __future__ import annotations

import argparse
import concurrent.futures
import dataclasses
import itertools
import json
import math
import random
import re
import sys
import time
from pathlib import Path
from typing import Any, Callable, Iterable

try:
    import anthropic
except ImportError:  # pragma: no cover - import guard
    sys.exit("anthropic SDK not installed. Run: python -m pip install anthropic")

try:  # only needed by --backend subscription; absence is reported at preflight
    import claude_agent_sdk as _agent_sdk
except ImportError:  # pragma: no cover - import guard
    _agent_sdk = None


MODEL = "claude-opus-5"
REPO = Path(__file__).resolve().parent.parent


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
    out = [ancestor_genome()]
    supers = list(SUPER_GENES)
    for i in range(size - 1):
        sg = supers[i % len(supers)]
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
        sg = rng.choice([s for s in SUPER_GENES if s != sg])
    return dataclasses.replace(g, super_gene=sg, free=free)


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
         "finished dying is removed from the world. The number of live enemies has "
         "a stated bound.",
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


_QUOTA_MARKERS = ("session limit", "usage limit", "rate limit", "quota")


def _quota_wall(text: str) -> bool:
    low = (text or "").lower()
    return any(marker in low for marker in _QUOTA_MARKERS)


def _flatten(system: list[dict[str, Any]]) -> str:
    return "\n\n".join(block["text"] for block in system)


class Client:
    """Two backends behind one interface.

    `api` bills API credits and can count tokens exactly. `subscription` drives
    the Claude Code CLI through the Agent SDK and bills subscription quota; it
    has no token-counting endpoint, so kata size is measured from the expressing
    call's own output tokens instead (see `text_series`).
    """

    #: Claude Code's own preamble is ~27k tokens and would be re-billed on every
    #: call. Supplying our own system prompt and no tools removes it entirely.
    SUBSCRIPTION_BASE = dict(tools=[], setting_sources=[], max_turns=1)

    def __init__(self, model: str = MODEL, dry_run: bool = False,
                 backend: str = "subscription") -> None:
        self.model = model
        self.dry_run = dry_run
        self.backend = backend
        self.calls = 0
        self.input_tokens = 0
        self.output_tokens = 0
        self.cost_usd = 0.0
        self._client = None
        self._token_baseline: int | None = None
        import threading
        self._baseline_lock = threading.Lock()
        if not dry_run:
            self._preflight()

    # -- credentials ------------------------------------------------------
    def _preflight(self) -> None:
        """Resolve credentials before generation 0, not at the first call.

        Either SDK constructs happily with nothing and fails only when a request
        goes out -- which lands inside the per-candidate error trap, so an
        unauthenticated run grinds through the whole population and reports a
        generation of errored candidates instead of one missing credential.
        """
        if self.backend == "api":
            try:
                self._client = anthropic.Anthropic()
            except Exception as exc:
                sys.exit(f"cannot reach the Anthropic API: {type(exc).__name__}: {exc}")
            if not (self._client.api_key or self._client.auth_token):
                sys.exit(
                    "no Anthropic credential resolved: set ANTHROPIC_API_KEY (or "
                    "ANTHROPIC_AUTH_TOKEN).\nNothing has been spent. To use a "
                    "Claude Code subscription instead, pass --backend subscription; "
                    "to exercise the loop with no credential at all, --dry-run.")
            return
        if _agent_sdk is None:
            sys.exit(
                "backend 'subscription' needs the Claude Code Agent SDK.\n"
                "Run: python -m pip install claude-agent-sdk\n"
                "Nothing has been spent.")
        import shutil
        if shutil.which("claude") is None:
            sys.exit(
                "backend 'subscription' needs the `claude` CLI on PATH, which is "
                "what carries the subscription credential.\nInstall Claude Code, "
                "or pass --backend api with ANTHROPIC_API_KEY set.\n"
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

        Returns (text, output_tokens) per prompt. On `subscription` the prompts
        share a single session so the prefix is cache-written once and read
        thereafter; on `api` the same amortization comes from prompt caching.
        """
        if self.dry_run:
            return [(f"```\n# dry-run program for a prompt of {len(u)} chars\n```\n"
                     "\n## Conflicts resolved\n\n- none\n", len(u) // 4)
                    for u in users]
        if self.backend == "subscription":
            return self._sub_series(_flatten(system), users, effort)
        out = []
        for user in users:
            # Thinking is on by default on Opus 5 and shares max_tokens with the
            # response text, so this needs headroom; stream to dodge timeouts.
            with self._client.messages.stream(
                model=self.model, max_tokens=max_tokens, system=system,
                output_config={"effort": effort},
                messages=[{"role": "user", "content": user}],
            ) as stream:
                message = stream.get_final_message()
            self._account_api(message)
            out.append(("".join(b.text for b in message.content if b.type == "text"),
                        message.usage.output_tokens))
        return out

    # -- scoring ----------------------------------------------------------
    def structured(self, system: list[dict[str, Any]], user: str,
                   schema: dict[str, Any], *, max_tokens: int = 16000,
                   effort: str = "high") -> dict[str, Any]:
        if self.dry_run:
            return _dry_run_instance(schema)
        if self.backend == "subscription":
            # A fresh session per scoring call: sharing one would leave the
            # previous candidate's answer in context while judging the next.
            return self._sub_structured(_flatten(system), user, schema, effort)
        message = self._client.messages.create(
            model=self.model, max_tokens=max_tokens, system=system,
            output_config={"effort": effort,
                           "format": {"type": "json_schema", "schema": schema}},
            messages=[{"role": "user", "content": user}],
        )
        self._account_api(message)
        if message.stop_reason == "refusal":
            raise RuntimeError(f"model declined: {message.stop_details}")
        return json.loads(next(b.text for b in message.content if b.type == "text"))

    #: A probe that carries the text to be measured and generates almost nothing
    #: back. Must stay byte-identical across calls or the baseline is invalid.
    _PROBE_SYSTEM = "Reply with the single character x and nothing else."
    _PROBE_TAIL = "\n\nReply with the single character x and nothing else."

    def count_tokens(self, text: str) -> int | None:
        """Exact count from the model's own tokenizer, on either backend."""
        if self.dry_run:
            return len(text) // 4
        if self.backend == "api":
            return self._client.messages.count_tokens(
                model=self.model, messages=[{"role": "user", "content": text}],
            ).input_tokens
        # The subscription path has no counting endpoint, but a request reports
        # the input tokens it consumed. Measuring a fixed empty probe once gives
        # the constant overhead to subtract, leaving the text's own token count.
        with self._baseline_lock:
            if self._token_baseline is None:
                self._token_baseline = self._sub_probe_tokens("")
        return max(0, self._sub_probe_tokens(text) - self._token_baseline)

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
                            raise QuotaExhausted((message.result or "").strip())
                        return (usage.get("input_tokens", 0)
                                + usage.get("cache_read_input_tokens", 0)
                                + usage.get("cache_creation_input_tokens", 0))
            raise RuntimeError("token probe returned no result")
        return _agent_sdk_run(go)

    # -- subscription backend ---------------------------------------------
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
                                raise QuotaExhausted(result.strip())
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
                    raise QuotaExhausted((text or message.result or "").strip())
                return text, (message.usage or {}).get("output_tokens", 0)
        raise RuntimeError("session ended before returning a result")

    # -- accounting -------------------------------------------------------
    def _account_api(self, message: Any) -> None:
        self.calls += 1
        usage = message.usage
        self.input_tokens += usage.input_tokens + (usage.cache_read_input_tokens or 0)
        self.output_tokens += usage.output_tokens

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
            return [_dry_run_instance(item)]
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


def develop_notation(client: Client, g: Genome) -> tuple[str, int]:
    """Genotype -> phenotype. Records how much repair the genome needed."""
    system = [cached(PHILOSOPHY)]
    donors = "\n".join(f"- {d}" for d in DONOR_PANEL)
    user = f"""\
Grow this genome into a concrete notation design.

{g.describe()}

Prior art you may borrow ALLELES from (not designs to imitate):
{donors}

Write a notation reference with:
  1. A one-paragraph statement of the design's central idea.
  2. The concrete syntax for each locus above, with a small example of each.
  3. How a backend derives, from the notation alone: per-rule access sets,
     effect domains, total execution order, iteration bounds, lifetime bounds.
  4. A "Conflicts resolved" section listing every place two inherited alleles
     contradicted each other and which one you overrode to make the design
     coherent. Be exhaustive and honest here -- an empty list is a real answer
     if the genome was already coherent.

Do not evaluate or defend the design. Just specify it.
Output Markdown only, no preamble."""
    text = client.text(system, user, max_tokens=40000)
    repair = _count_conflicts(text)
    return text, repair


def _count_conflicts(notation: str) -> int:
    m = re.search(r"##+\s*Conflicts resolved(.*?)(?=\n##\s|\Z)", notation, re.S | re.I)
    if not m:
        return 0
    return len(re.findall(r"^\s*[-*\d]+[.)]?\s+\S", m.group(1), re.M))


def express_katas(client: Client, notation: str,
                  katas: Iterable[Kata]) -> tuple[dict[str, str], dict[str, int]]:
    """Express every kata in the candidate notation, sharing one cached prefix.

    Also returns each expression's own output-token count, which is the only
    token measure available on the subscription backend.
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
                "required": ["pattern", "kata", "quote"],
                "properties": {
                    "pattern": {"type": "string", "enum": [p for p, _ in RESIDUE_PATTERNS]},
                    "kata": {"type": "string", "enum": KATA_IDENTS},
                    "quote": {"type": "string"},
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
        "occurrence must quote the exact text it refers to. If a construct is "
        "ambiguous, do not report it.")]
    user = f"""\
# Notation reference

{notation}

# Expressed katas

{body}

# Patterns to extract

{patterns}

List every occurrence in the expressed katas. Quote exactly."""
    result = client.structured(system, user, RESIDUE_SCHEMA, max_tokens=20000, effort="medium")
    counts = {p: 0 for p, _ in RESIDUE_PATTERNS}
    for occ in result.get("occurrences", []):
        if occ["pattern"] in counts:
            counts[occ["pattern"]] += 1
    return {"counts": counts, "total": sum(counts.values()), "occurrences": result.get("occurrences", [])}


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
    metrics["mechanical_line_count"] = _fenced_line_count(ui)
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


def comprehension(client: Client, notation: str, katas: dict[str, str],
                  sample: tuple[str, ...]) -> dict[str, Any]:
    """Blind read: a cold agent gets the reference and a program, nothing else.

    This is the anti-golfing guard. A notation that wins on token count and
    fails here is correctly punished, which token count alone cannot do.
    """
    subset = {k: katas[k] for k in sample if k in katas}
    body = "\n\n".join(f"### {k}\n\n{v}" for k, v in sorted(subset.items()))
    read_system = [cached(
        "You have never seen this notation before. You are given only its "
        "reference and some programs written in it. Say what each program does, "
        "in plain prose, in terms of observable behavior. Do not guess from the "
        "identifier names alone -- read the constructs.")]
    answers = client.structured(
        read_system,
        f"# Notation reference\n\n{notation}\n\n# Programs\n\n{body}",
        COMPREHENSION_SCHEMA, max_tokens=12000)

    contracts = {k.ident: k.contract for k in KATAS}
    pairs = "\n\n".join(
        f"### {a['kata']}\n\nIntended:\n{contracts.get(a['kata'], '(unknown)')}\n\n"
        f"Reader said:\n{a['described_behavior']}"
        for a in answers.get("answers", []))
    grade_system = [cached(
        "Grade whether the reader's description matches the intended behavior on "
        "the load-bearing points. Wording may differ freely. Mark incorrect only "
        "if the reader missed or inverted something that changes what the program "
        "observably does.")]
    graded = client.structured(grade_system, pairs, GRADE_SCHEMA,
                               max_tokens=8000, effort="medium")
    rows = graded.get("graded", [])
    correct = sum(1 for r in rows if r["correct"])
    return {
        "answers": answers.get("answers", []),
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


def duel(client: Client, axis: str, a_text: str, b_text: str) -> str:
    system = [cached(
        "Compare two notation designs on one axis. You do not know which is "
        "which; neither is an incumbent. Prefer the design that better serves "
        "the stated axis. Answer with a winner and one paragraph of reason.")]
    user = f"""\
# Axis

{DUEL_AXES[axis]}

# Candidate A

{a_text}

# Candidate B

{b_text}"""
    return client.structured(system, user, DUEL_SCHEMA, max_tokens=6000)["winner"]


# --------------------------------------------------------------------------
# Elo
# --------------------------------------------------------------------------

K_FACTOR = 24.0
START_RATING = 1500.0


def elo_update(ratings: dict[str, float], winner: str, loser: str) -> None:
    ra, rb = ratings.get(winner, START_RATING), ratings.get(loser, START_RATING)
    expected = 1.0 / (1.0 + math.pow(10.0, (rb - ra) / 400.0))
    ratings[winner] = ra + K_FACTOR * (1.0 - expected)
    ratings[loser] = rb - K_FACTOR * (1.0 - expected)


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
        if _quota_wall(self.notation) or len(self.notation) < self.MIN_NOTATION_BYTES:
            return False
        return all(v and not _quota_wall(v) and len(v) >= self.MIN_KATA_BYTES
                   for v in self.katas.values())

    @property
    def passed(self) -> bool:
        return self.gate.get("verdict") == "pass" and not self.error

    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        d["genome"] = self.genome.to_dict()
        return d


def develop_and_score(client: Client, genome: Genome, seed: "Seed",
                      prior: Candidate | None = None,
                      checkpoint: Callable[[Candidate], None] | None = None
                      ) -> Candidate:
    """Evaluate one genome, reusing `prior`'s generated text when it has some.

    A candidate that reached the gate and then died -- to a quota wall, a schema
    fault, anything -- has already paid for its notation, twelve katas and UI
    rewrite. Re-scoring those costs a fraction of regenerating them.
    """
    cand = Candidate(genome=genome)
    try:
        if genome.is_ancestor:
            cand.notation, cand.katas, cand.ui = seed.notation, dict(seed.katas), seed.ui
            genome.repair_depth = 0
        elif prior is not None and prior.has_expression:
            cand.notation, cand.katas, cand.ui = (prior.notation, dict(prior.katas),
                                                  prior.ui)
            cand.expressed_tokens = dict(prior.expressed_tokens)
            genome.repair_depth = prior.genome.repair_depth
        else:
            cand.notation, repair = develop_notation(client, genome)
            genome.repair_depth = repair
            cand.katas, cand.expressed_tokens = express_katas(
                client, cand.notation, KATAS)
            cand.ui = express_ui(client, cand.notation, seed.ui)

        # Persist the expensive half before scoring begins. A quota wall during
        # scoring otherwise discards a completed notation, twelve katas and a UI
        # rewrite -- and on a fixed plan that quota cannot be re-bought, only
        # waited out.
        if checkpoint is not None:
            checkpoint(cand)

        cand.gate = run_gate(client, cand.notation, cand.katas)
        if not cand.passed:
            return cand

        # One measure on both backends: the model's own tokenizer applied to the
        # kata text. Deliberately not the expressing call's output tokens, which
        # would include reasoning and so partly track how hard the notation was
        # to write in rather than how long the result is.
        tokens = {k: client.count_tokens(v) for k, v in cand.katas.items()}
        token_source = "count_tokens"
        # A missing measure must not read as an infinitely terse notation: the
        # expressiveness term divides by this, so an empty result would hand the
        # candidate an enormous bonus.
        if not tokens or any(v is None for v in tokens.values()) \
                or sum(tokens.values()) <= 0:
            raise RuntimeError(
                f"no usable kata token measure for {genome.ident} "
                f"(backend={client.backend})")
        residue = count_residue(client, cand.notation, cand.katas)
        reconcile_ordering(cand.gate, residue)
        cand.mechanical = {
            "kata_tokens": tokens,
            "kata_tokens_total": sum(tokens.values()),
            "kata_token_source": token_source,
            "residue": residue,
        }
        cand.differentiator = score_differentiator(client, cand.notation, cand.ui)
        cand.comprehension = comprehension(client, cand.notation, cand.katas,
                                           COMPREHENSION_SAMPLE)
    except QuotaExhausted:
        if checkpoint is not None and cand.has_expression:
            cand.error = "interrupted by quota during scoring"
            checkpoint(cand)
        raise  # applies to the whole run, not this candidate
    except Exception as exc:  # keep one bad candidate from killing a generation
        cand.error = f"{type(exc).__name__}: {exc}"
    return cand


def run_tournament(client: Client, cands: list[Candidate], rng: random.Random,
                   workers: int) -> dict[str, float]:
    """Pairwise, blinded, order-randomized. The ancestor is always present.

    Absolute LLM self-rating is deliberately not a ranking input: it drifts
    between prompts and carries a familiarity prior.
    """
    alive = [c for c in cands if c.passed]
    if not any(c.genome.is_ancestor for c in alive):
        # A round assembled without the calibration anchor is not admissible.
        return {}
    ratings: dict[str, float] = {c.genome.ident: START_RATING for c in alive}
    jobs: list[tuple[str, Candidate, Candidate, bool]] = []
    for axis in DUEL_AXES:
        for a, b in itertools.combinations(alive, 2):
            jobs.append((axis, a, b, rng.random() < 0.5))
    rng.shuffle(jobs)

    def play(job):
        axis, a, b, flip = job
        left, right = (b, a) if flip else (a, b)
        winner_slot = duel(client, axis, left.notation, right.notation)
        won = left if winner_slot == "A" else right
        lost = right if won is left else left
        return won.genome.ident, lost.genome.ident

    for winner, loser in _parallel(play, jobs, workers):
        elo_update(ratings, winner, loser)
    return ratings


def _parallel(fn: Callable, items: list, workers: int) -> list:
    if workers <= 1:
        return [fn(i) for i in items]
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        return [f.result() for f in
                [pool.submit(fn, i) for i in items]]


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
    if not cand.passed:
        return float("-inf")
    m = cand.mechanical
    d = cand.differentiator
    tokens = max(m.get("kata_tokens_total", 1), 1)
    ui_lines = max(d.get("lines") or d.get("mechanical_line_count", 1), 1)
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

@dataclasses.dataclass
class Seed:
    notation: str
    katas: dict[str, str]
    ui: str

    @property
    def fingerprint(self) -> str:
        """Identifies the seed a checkpoint was produced against.

        Every candidate depends on the seed -- the ancestor *is* it, and each
        challenger's UI rewrite is prompted with `seed.ui` -- so a changed seed
        makes every stored result stale, including gate verdicts, which are
        legitimate results rather than errors and would otherwise be reused.
        """
        import hashlib
        h = hashlib.sha256()
        h.update(self.notation.encode("utf-8"))
        h.update(self.ui.encode("utf-8"))
        for ident in sorted(self.katas):
            h.update(ident.encode("utf-8"))
            h.update(self.katas[ident].encode("utf-8"))
        return h.hexdigest()[:16]

    @classmethod
    def load(cls, path: Path) -> "Seed":
        notation = _require(path / "notation.md")
        ui = _require(path / "ui_measure_arrange.md")
        katas = {}
        for kata in KATAS:
            katas[kata.ident] = _require(path / "katas" / f"{kata.ident}.md")
        return cls(notation=notation, katas=katas, ui=ui)


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

    (out / "katas").mkdir(parents=True, exist_ok=True)
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

    for kata in KATAS:
        src = REPO / kata.source
        if not src.exists():
            sys.exit(f"cannot scaffold {kata.ident}: missing {src}")
        ranges = ", ".join(f"{lo}-{hi}" for lo, hi in kata.arena_ranges)
        (out / "katas" / f"{kata.ident}.md").write_text(
            f"# {kata.ident} -- {kata.title}\n\n"
            f"Stresses: {kata.stresses}\n\n"
            f"## Contract\n\n{kata.contract}\n\n"
            f"## Ancestor expression\n\n"
            f"From `{kata.source}` lines {ranges}.\n\n"
            "```cactus\n" + _slice(src, kata.arena_ranges) + "\n```\n",
            encoding="utf-8")

    (out / "README.md").write_text(
        "# Ancestor phenotype (generation 0, individual `ancestor`)\n\n"
        "Every file here was extracted mechanically from the repo -- nothing was\n"
        "written by hand or by a model. Review the kata contracts in `katas/`\n"
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


# --------------------------------------------------------------------------
# Orchestration
# --------------------------------------------------------------------------

def _write_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, default=str), encoding="utf-8")


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
        "error": cand.error,
    })


def _load_candidate(gen_dir: Path, ident: str) -> tuple[Candidate, str] | None:
    d = gen_dir / ident
    if not (d / "scores.json").exists():
        return None
    raw = json.loads((d / "scores.json").read_text(encoding="utf-8"))
    katas = {p.stem: p.read_text(encoding="utf-8")
             for p in sorted((d / "katas").glob("*.md"))} if (d / "katas").exists() else {}
    return Candidate(
        genome=Genome.from_dict(raw["genome"]),
        notation=(d / "notation.md").read_text(encoding="utf-8"),
        katas=katas,
        ui=(d / "ui_measure_arrange.md").read_text(encoding="utf-8"),
        gate=raw.get("gate", {}), mechanical=raw.get("mechanical", {}),
        differentiator=raw.get("differentiator", {}),
        comprehension=raw.get("comprehension", {}),
        expressed_tokens=raw.get("expressed_tokens", {}),
        error=raw.get("error", ""),
    ), raw.get("seed_fingerprint", "")


def allele_marginals(history: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    """Per-allele mean composite, across super-genes.

    This is the actionable readout: alleles that score well regardless of
    paradigm are candidates for standalone proposals against today's Cactus.
    """
    buckets: dict[str, dict[str, list[float]]] = {}
    for row in history:
        if row["score"] == float("-inf"):
            continue
        alleles = dict(SUPER_GENES[row["super_gene"]])
        alleles.update(row["free"])
        alleles["SUPER"] = row["super_gene"]
        for locus, allele in alleles.items():
            buckets.setdefault(locus, {}).setdefault(allele, []).append(row["score"])
    out: dict[str, dict[str, Any]] = {}
    for locus, by_allele in buckets.items():
        rows = [{"allele": a, "n": len(v), "mean": sum(v) / len(v)}
                for a, v in by_allele.items()]
        rows.sort(key=lambda r: r["mean"], reverse=True)
        out[locus] = {"alleles": rows}
    return out


def run(args: argparse.Namespace) -> None:
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    seed = Seed.load(Path(args.seed))
    rng = random.Random(args.seed_rng)
    client = Client(model=args.model, dry_run=args.dry_run, backend=args.backend)
    if not args.dry_run:
        print(f"backend: {args.backend}"
              + ("  (bills Claude Code subscription quota)"
                 if args.backend == "subscription" else "  (bills API credits)"))

    population = seed_population(rng, args.population)
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
                finished = (same and not cached_cand.error
                            and cached_cand.gate.get("verdict"))
                if finished:
                    print(f"  {g.ident}: resumed from checkpoint")
                    return cached_cand
                if same and cached_cand.has_expression:
                    # Generation already succeeded and was paid for; re-score only.
                    prior = cached_cand
                    print(f"  {g.ident}: reusing generated text, re-scoring")
                elif same:
                    print(f"  {g.ident}: checkpoint has no usable generated text, redoing")
                elif cached_cand is not None:
                    print(f"  {g.ident}: checkpoint is a different genotype, re-evaluating")
            cand = develop_and_score(
                client, g, seed, prior,
                lambda c: _dump_candidate(gen_dir, c, seed.fingerprint))
            _dump_candidate(gen_dir, cand, seed.fingerprint)
            status = "error" if cand.error else cand.gate.get("verdict", "?")
            print(f"  {g.ident}: {g.super_gene} -> {status}")
            return cand

        try:
            cands = _parallel(evaluate, population, args.workers)
        except QuotaExhausted as exc:
            print(f"\nstopped: the backend is out of quota -- {exc}")
            print(f"spent so far: calls={client.calls} cost=${client.cost_usd:.2f}")
            saved = sorted(d.name for d in gen_dir.iterdir()
                           if (d / "scores.json").exists()) if gen_dir.exists() else []
            if saved:
                print(f"recoverable in {gen_dir}: {', '.join(saved)}")
                print("Re-run the same command with --resume once quota returns; "
                      "generated text is reused and only scoring is re-paid.")
            else:
                print(f"nothing reached a checkpoint -- {gen_dir} is empty and this "
                      f"quota is unrecoverable.")
            sys.exit(2)

        ratings = run_tournament(client, cands, rng, args.workers)
        if not ratings:
            print("  tournament skipped: ancestor absent or no candidate passed")
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
        _write_json(out / "allele_marginals.json", allele_marginals(history))

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
          f"cost=${client.cost_usd:.2f} elapsed={elapsed / 60:.1f}min")
    print(f"results in {out}")
    print(f"per-allele readout: {out / 'allele_marginals.json'}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("scaffold-seed", help="extract the ancestor phenotype from the repo")
    s.add_argument("--out", required=True)

    r = sub.add_parser("run", help="run the search")
    r.add_argument("--seed", required=True, help="folder with the initial candidate")
    r.add_argument("--out", required=True)
    r.add_argument("--generations", type=int, default=0,
                   help="0 = generation-0 pilot only")
    r.add_argument("--population", type=int, default=12)
    r.add_argument("--workers", type=int, default=4)
    r.add_argument("--seed-rng", type=int, default=1)
    r.add_argument("--model", default=MODEL)
    r.add_argument("--backend", choices=("subscription", "api"),
                   default="subscription",
                   help="subscription drives the Claude Code CLI via the Agent "
                        "SDK and bills subscription quota; api uses "
                        "ANTHROPIC_API_KEY and can count tokens exactly")
    r.add_argument("--dry-run", action="store_true",
                   help="exercise the whole loop with no API calls")
    r.add_argument("--resume", action="store_true",
                   help="reuse checkpointed candidates instead of re-evaluating")

    args = ap.parse_args()
    if args.cmd == "scaffold-seed":
        scaffold_seed(Path(args.out))
    else:
        run(args)


if __name__ == "__main__":
    main()
