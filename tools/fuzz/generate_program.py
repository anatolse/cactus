"""Writes one small, well-typed Cactus program from a seed.

    python tools/fuzz/generate_program.py SEED [--output PATH] [--module NAME]

The program uses traits with int/float/bool fields, unary or selectionless
rules with each:, without:, where: and when:, phase, event and lifecycle
handlers, Accum, Join and Assign writes, if, emit with and without `to`, spawn,
destroy, add, remove and set, plus the entities that start the world. The same
seed always gives the same text: only random.Random(seed) chooses.
"""

from __future__ import annotations

import argparse
import dataclasses
import random
import sys
from pathlib import Path

MAX_STATEMENTS = 6
COMMUTING_KINDS = {"accum", "join max", "join min", "join or", "join and"}
MAX_ENTITIES = 8
TYPES = ("int", "float", "bool")
FLOAT_LITERALS = ("0.0", "0.5", "1.0", "1.5", "2.0", "2.25", "3.0", "4.5")


@dataclasses.dataclass
class Field:
    name: str
    type: str
    default: str
    # The write kind every handler uses on this field, or None for any kind.
    kind: str | None


@dataclasses.dataclass(eq=False)
class Trait:
    name: str
    fields: list[Field]


@dataclasses.dataclass
class Event:
    name: str
    fields: list[Field]


@dataclasses.dataclass
class Template:
    name: str
    traits: list[Trait]


@dataclasses.dataclass
class Entity:
    name: str
    traits: list[Trait]


@dataclasses.dataclass
class Rule:
    name: str
    each: list[Trait]
    without: list[Trait]
    trigger: tuple[str, object]
    lines: list[str] = dataclasses.field(default_factory=list)
    after: list[str] = dataclasses.field(default_factory=list)
    where: list[str] = dataclasses.field(default_factory=list)
    when: list[str] = dataclasses.field(default_factory=list)
    # Field keys `T.f` and membership keys `+T`, for choosing `after:` lines;
    # each write key maps to its write kinds.
    reads: set[str] = dataclasses.field(default_factory=set)
    writes: dict[str, set[str]] = dataclasses.field(default_factory=dict)
    # The traits whose membership and the fields `T.f` this rule mostly
    # touches, so two rules often touch disjoint data and stay unordered.
    focus: list[Trait] = dataclasses.field(default_factory=list)
    focus_fields: set[str] = dataclasses.field(default_factory=set)

    def write(self, key: str, kind: str) -> None:
        self.writes.setdefault(key, set()).add(kind)

    def commutes_on(self, other: "Rule", key: str) -> bool:
        kinds = self.writes[key] | other.writes[key]
        return len(kinds) == 1 and kinds <= COMMUTING_KINDS and key not in self.reads | other.reads

    def conflicts(self, other: "Rule") -> bool:
        shared = self.writes.keys() & other.writes.keys()
        if any(not self.commutes_on(other, key) for key in shared):
            return True
        return bool(self.writes.keys() & other.reads or other.writes.keys() & self.reads)


@dataclasses.dataclass
class Scope:
    """What a handler body can read: aliases, locals, the event payload."""

    rule: Rule
    aliases: list[tuple[str, Trait]]
    locals: list[tuple[str, str]]
    event: Event | None
    depth: int = 0


def literal(rng: random.Random, type_name: str) -> str:
    if type_name == "int":
        return str(rng.randint(0, 5))
    if type_name == "float":
        return rng.choice(FLOAT_LITERALS)
    return rng.choice(("true", "false"))


class Generator:
    def __init__(self, seed: int, module: str) -> None:
        self.module = module
        self.rng = random.Random(seed)
        self.traits: list[Trait] = []
        self.events: list[Event] = []
        self.templates: list[Template] = []
        self.entities: list[Entity] = []
        self.rules: list[Rule] = []

    # ── Declarations ────────────────────────────────────────────────────────

    def declare_traits(self) -> None:
        rng = self.rng
        for index in range(rng.randint(2, 5)):
            fields = []
            if index == 0 or rng.random() < 0.8:
                for field_index in range(rng.randint(1, 3)):
                    type_name = rng.choice(TYPES)
                    fields.append(
                        Field(f"f{field_index}", type_name, literal(rng, type_name), self.pick_field_kind(type_name))
                    )
            self.traits.append(Trait(f"T{index}", fields))

    def pick_field_kind(self, type_name: str) -> str | None:
        roll = self.rng.random()
        if roll < 0.1:
            return None
        if type_name == "bool":
            return self.rng.choice(("join or", "join and")) if roll < 0.7 else "assign"
        if roll < 0.6:
            return "accum"
        return self.rng.choice(("join max", "join min")) if roll < 0.75 else "assign"

    def declare_events(self) -> None:
        rng = self.rng
        for index in range(rng.randint(0, 2)):
            fields = []
            for field_index in range(rng.randint(0, 2)):
                type_name = rng.choice(TYPES)
                fields.append(Field(f"v{field_index}", type_name, literal(rng, type_name), None))
            self.events.append(Event(f"E{index}", fields))

    def trait_subset(self, minimum: int) -> list[Trait]:
        count = self.rng.randint(minimum, min(3, len(self.traits)))
        return sorted(self.rng.sample(self.traits, count), key=lambda trait: trait.name)

    def declare_world(self) -> None:
        rng = self.rng
        for index in range(rng.randint(0, 2)):
            self.templates.append(Template(f"P{index}", self.trait_subset(1)))
        for index in range(rng.randint(2, MAX_ENTITIES)):
            self.entities.append(Entity(f"N{index}", self.trait_subset(1)))

    # ── Rules ───────────────────────────────────────────────────────────────

    def declare_rules(self) -> None:
        rng = self.rng
        for index in range(rng.randint(3, 8)):
            each: list[Trait] = []
            without: list[Trait] = []
            if rng.random() < 0.75:
                archetype = rng.choice(self.entities).traits
                each = sorted(rng.sample(archetype, rng.randint(1, min(2, len(archetype)))), key=lambda t: t.name)
                # Excludes a trait some entity matching `each` carries, but this one doesn't.
                others = [
                    trait
                    for trait in self.traits
                    if trait not in archetype
                    and any(set(each) | {trait} <= set(entity.traits) for entity in self.entities)
                ]
                if others and rng.random() < 0.3:
                    without = [rng.choice(others)]
            trigger = self.pick_trigger()
            if trigger[1] in without:
                without = []
            if trigger[0] == "removed" and trigger[1] in each:
                each = [trait for trait in each if trait is not trigger[1]]
            rule = Rule(f"R{index}", each, without, trigger)
            rule.reads.update(f"+{trait.name}" for trait in each + without)
            self.rules.append(rule)
        for rule in self.rules:
            self.fill_rule(rule)
        self.connect_triggers()
        self.order_some_conflicts()

    def order_some_conflicts(self) -> None:
        """Orders most pairs that touch one key, so most programs are accepted."""
        for index, rule in enumerate(self.rules):
            for earlier in self.rules[:index]:
                if earlier.name not in rule.after and rule.conflicts(earlier) and self.rng.random() < 0.85:
                    rule.after.append(earlier.name)

    def pick_trigger(self) -> tuple[str, object]:
        rng = self.rng
        roll = rng.random()
        if self.events and roll < 0.2:
            return ("event", rng.choice(self.events))
        if roll < 0.28:
            return ("added", rng.choice(self.traits))
        if roll < 0.32:
            return ("removed", rng.choice(self.traits))
        if roll < 0.35:
            return ("phase", "load")
        return ("phase", "tick")

    def fill_rule(self, rule: Rule) -> None:
        rng = self.rng
        rule.focus = list(rule.each) + [rng.choice(self.traits)]
        keys = sorted(f"{trait.name}.{field.name}" for trait in self.traits for field in trait.fields)
        rule.focus_fields = set(rng.sample(keys, max(1, round(len(keys) * 0.2)))) if keys else set()
        aliases = [(f"a{index}", trait) for index, trait in enumerate(rule.each)]
        event = rule.trigger[1] if rule.trigger[0] == "event" else None
        scope = Scope(rule, aliases, [], event)  # type: ignore[arg-type]
        if aliases and rng.random() < 0.3:
            rule.where.append(self.expression(Scope(rule, aliases, [], None), "bool", pure=True))
        if rng.random() < 0.1:
            rule.when.append(self.expression(Scope(rule, [], [], None), "bool", pure=True))
        rule.lines = self.block(scope, rng.randint(1, MAX_STATEMENTS))

    def connect_triggers(self) -> None:
        """Every event a rule handles is emitted, and every lifecycle trigger fires."""
        rng = self.rng
        sources = [rule for rule in self.rules if rule.trigger == ("phase", "tick")]
        if not sources:
            rule = Rule(f"R{len(self.rules)}", [], [], ("phase", "tick"))
            self.rules.append(rule)
            self.fill_rule(rule)
            sources = [rule]
        for rule in list(self.rules):
            kind, target = rule.trigger
            source = rng.choice(sources)
            if kind == "event" and not self.starts_with(sources, f"emit {target.name}"):  # type: ignore[union-attr]
                source.lines.append(self.emit_line(Scope(source, [], [], None), target, "    "))  # type: ignore[arg-type]
            if kind in ("added", "removed"):
                self.ensure_lifecycle(kind, target, source, sources)  # type: ignore[arg-type]

    @staticmethod
    def starts_with(rules: list[Rule], prefix: str) -> bool:
        return any(line.strip().startswith(prefix) for rule in rules for line in rule.lines)

    def ensure_lifecycle(self, kind: str, trait: Trait, source: Rule, sources: list[Rule]) -> None:
        verb = "add" if kind == "added" else "remove"
        if self.starts_with(sources, f"{verb} {trait.name}"):
            return
        carriers = [entity for entity in self.entities if (trait in entity.traits) == (kind == "removed")]
        if not carriers:
            return
        target = self.rng.choice(carriers).name
        source.write(f"+{trait.name}", "general")
        if kind == "added":
            source.lines.append(f"    add {trait.name} to {target}")
        else:
            source.lines.append(f"    remove {trait.name} from {target}")

    # ── Statements ──────────────────────────────────────────────────────────

    def block(self, scope: Scope, count: int) -> list[str]:
        lines: list[str] = []
        for _ in range(count):
            lines.extend(self.statement(scope))
        return lines

    def statement(self, scope: Scope) -> list[str]:
        rng = self.rng
        indent = "    " * (scope.depth + 1)
        unary = bool(scope.rule.each)
        weights = {"write": 12, "let": 3, "if": 4, "emit": 2, "spawn": 1, "add": 1, "remove": 1, "set": 1}
        if unary:
            weights["destroy"] = 1
        kind = rng.choices(list(weights), weights=list(weights.values()))[0]
        if kind == "if" and scope.depth < 2:
            condition = self.expression(scope, "bool")
            inner = Scope(scope.rule, scope.aliases, list(scope.locals), scope.event, scope.depth + 1)
            return [f"{indent}if {condition}:"] + self.block(inner, rng.randint(1, 2))
        if kind == "let":
            name = f"x{len(scope.locals)}_{scope.depth}"
            type_name = rng.choice(TYPES)
            line = f"{indent}let {name} = {self.expression(scope, type_name)}"
            scope.locals.append((name, type_name))
            return [line]
        if kind == "emit" and self.events:
            return [self.emit_line(scope, rng.choice(self.events), indent)]
        if kind == "spawn" and self.templates:
            return [f"{indent}spawn {rng.choice(self.templates).name}()"]
        if kind == "destroy":
            for trait in scope.rule.each:
                scope.rule.write(f"+{trait.name}", "general")
            return [f"{indent}destroy"]
        if kind in ("add", "remove", "set"):
            lines = self.structural(scope, kind, indent)
            if lines:
                return lines
        return self.write(scope, indent)

    def focused(self, scope: Scope, candidates: list, key_of) -> list:
        if self.rng.random() < 0.05:
            return candidates
        return [item for item in candidates if key_of(item) is None or key_of(item) in scope.rule.focus_fields]

    def touched(self, scope: Scope, traits: list[Trait]) -> list[Trait]:
        if self.rng.random() < 0.1:
            return traits
        return [trait for trait in traits if trait in scope.rule.focus]

    def target(self, scope: Scope) -> str:
        rng = self.rng
        if scope.rule.each and rng.random() < 0.6:
            return "self"
        return rng.choice(self.entities).name

    def structural(self, scope: Scope, kind: str, indent: str) -> list[str]:
        rng = self.rng
        trait = rng.choice(self.touched(scope, self.traits) or self.traits)
        target = self.target(scope)
        scope.rule.write(f"+{trait.name}", "general")
        if kind == "remove":
            return [f"{indent}remove {trait.name} from {target}"]
        if kind == "add":
            lines = [f"{indent}add {trait.name} to {target}"]
            if trait.fields and rng.random() < 0.5:
                lines[0] += ":"
                field = rng.choice(trait.fields)
                lines.append(f"{indent}    {field.name} = {self.expression(scope, field.type)}")
            return lines
        if not trait.fields:
            return []
        field = rng.choice(trait.fields)
        scope.rule.write(f"{trait.name}.{field.name}", "general")
        return [f"{indent}set {trait.name} on {target}:", f"{indent}    {field.name} = {self.expression(scope, field.type)}"]

    def emit_line(self, scope: Scope, event: Event, indent: str) -> str:
        rng = self.rng
        head = f"{indent}emit {event.name}"
        if rng.random() < 0.4:
            head += f" to {self.target(scope)}"
        if not event.fields:
            return head
        payload = "\n".join(
            f"{indent}    {field.name} = {self.expression(scope, field.type)}" for field in event.fields
        )
        return f"{head}:\n{payload}"

    def write(self, scope: Scope, indent: str) -> list[str]:
        rng = self.rng
        places = [(f"{alias}.{field.name}", trait, field) for alias, trait in scope.aliases for field in trait.fields]
        for entity in self.entities:
            for trait in entity.traits:
                places.extend((f"{entity.name}[{trait.name}].{field.name}", trait, field) for field in trait.fields)
        places = self.focused(scope, places, lambda place: f"{place[1].name}.{place[2].name}")
        if not places:
            return [f"{indent}let w{rng.randint(0, 999)} = {literal(rng, 'int')}"]
        place, trait, field = rng.choice(places)
        key = f"{trait.name}.{field.name}"
        kind = field.kind or self.pick_field_kind(field.type) or "assign"
        value = self.expression(scope, field.type)
        scope.rule.write(key, kind)
        if kind == "accum":
            return [f"{indent}{place} {rng.choice(('+=', '-='))} {value}"]
        # `x or not x` is valid Cactus, but clang rejects its C++ under -Werror.
        if kind == "join or":
            return [f"{indent}{place} = " + ("true" if place in value else f"{place} or {value}")]
        if kind == "join and":
            return [f"{indent}{place} = " + ("false" if place in value else f"{place} and {value}")]
        if kind.startswith("join "):
            return [f"{indent}{place} = math.{kind[5:]}({place}, {value})"]
        return [f"{indent}{place} = {value}"]

    # ── Expressions ─────────────────────────────────────────────────────────

    def reads(self, scope: Scope, type_name: str, pure: bool) -> list[tuple[str, str | None]]:
        found: list[tuple[str, str | None]] = [
            (name, None) for name, local_type in scope.locals if local_type == type_name
        ]
        for alias, trait in scope.aliases:
            found.extend(
                (f"{alias}.{field.name}", f"{trait.name}.{field.name}")
                for field in trait.fields
                if field.type == type_name
            )
        if scope.event is not None and not pure:
            found.extend(
                (f"{scope.event.name}.{field.name}", None) for field in scope.event.fields if field.type == type_name
            )
        for entity in self.entities:
            for trait in entity.traits:
                found.extend(
                    (f"{entity.name}[{trait.name}].{field.name}", f"{trait.name}.{field.name}")
                    for field in trait.fields
                    if field.type == type_name
                )
        return self.focused(scope, found, lambda read: read[1])

    def expression(self, scope: Scope, type_name: str, depth: int = 0, pure: bool = False) -> str:
        rng = self.rng
        roll = rng.random()
        if depth >= 2 or roll < 0.3:
            return self.leaf(scope, type_name, pure)
        if type_name == "bool":
            if roll < 0.7:
                operand = rng.choice(("int", "float"))
                left = self.expression(scope, operand, depth + 1, pure)
                right = self.distinct(left, self.expression(scope, operand, depth + 1, pure), operand)
                return f"{left} {rng.choice(('<', '<=', '>', '>=', '==', '!='))} {right}"
            if roll < 0.85:
                return f"not {self.leaf(scope, 'bool', pure)}"
            left = self.expression(scope, "bool", depth + 1, pure)
            right = self.distinct(left, self.expression(scope, "bool", depth + 1, pure), "bool")
            return f"({left} {rng.choice(('and', 'or'))} {right})"
        left = self.expression(scope, type_name, depth + 1, pure)
        right = self.expression(scope, type_name, depth + 1, pure)
        return f"({left} {rng.choice(('+', '-', '*'))} {right})"

    def distinct(self, left: str, right: str, type_name: str) -> str:
        """Clang rejects a self-comparison or `a || !a` under -Werror."""
        if left == right or right == f"not {left}" or left == f"not {right}":
            return literal(self.rng, type_name)
        return right

    def leaf(self, scope: Scope, type_name: str, pure: bool) -> str:
        reads = self.reads(scope, type_name, pure)
        if reads and self.rng.random() < 0.4:
            text, key = self.rng.choice(reads)
            if key is not None:
                scope.rule.reads.add(key)
            return text
        return literal(self.rng, type_name)

    # ── Text ────────────────────────────────────────────────────────────────

    def generate(self) -> str:
        self.declare_traits()
        self.declare_events()
        self.declare_world()
        self.declare_rules()
        return self.render()

    def render(self) -> str:
        out = [f"module {self.module}", "", "use std.core", "use std.math as math", ""]
        for trait in self.traits:
            out.append(f"trait {trait.name}" + (":" if trait.fields else ""))
            out.extend(f"    var {field.name}: {field.type} = {field.default}" for field in trait.fields)
            out.append("")
        for event in self.events:
            out.append(f"event {event.name}" + (":" if event.fields else ""))
            out.extend(f"    {field.name}: {field.type}" for field in event.fields)
            out.append("")
        for template in self.templates:
            out.append(f"template {template.name}:")
            out.extend(f"    {trait.name}" for trait in template.traits)
            out.append("")
        for entity in self.entities:
            out.append(f"entity {entity.name}:")
            out.extend(f"    {trait.name}" for trait in entity.traits)
            out.append("")
        for rule in self.rules:
            out.extend(self.render_rule(rule))
        return "\n".join(out)

    def render_rule(self, rule: Rule) -> list[str]:
        out = [f"rule {rule.name}:"]
        if rule.each:
            out.append("    each:")
            # A fieldless trait takes no alias: an alias of one emits C++ that does not compile.
            out.extend(
                f"        {trait.name}" + (f" as a{index}" if trait.fields else "") for index, trait in enumerate(rule.each)
            )
        if rule.without:
            out.append("    without:")
            out.extend(f"        {trait.name}" for trait in rule.without)
        for clause, lines in (("after", rule.after), ("where", rule.where), ("when", rule.when)):
            if lines:
                out.append(f"    {clause}:")
                out.extend(f"        {line}" for line in lines)
        kind, target = rule.trigger
        if kind == "phase" or kind == "event":
            name = target if isinstance(target, str) else target.name  # type: ignore[union-attr]
            out.append(f"    on {name}:")
        else:
            out.append(f"    on {kind} {target.name}:")  # type: ignore[union-attr]
        out.extend(f"    {line}" for body in rule.lines for line in body.split("\n"))
        out.append("")
        return out


def generate(seed: int, module: str | None = None) -> str:
    return Generator(seed, module or f"fuzz_seed_{seed}").generate()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("seed", type=int)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--module", help="module name; the default is fuzz_seed_SEED")
    args = parser.parse_args(argv)
    text = generate(args.seed, args.module)
    if args.output is None:
        sys.stdout.write(text)
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
        args.output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
