# hierarchy-forest Specification

## Purpose
Keeps the `Parent` relation a forest in every reachable world state, defines reparenting as a commit command that can never form a cycle, and records what each program does with `Parent` so a backend can choose its hierarchy strategy.
## Requirements
### Requirement: The hierarchy is a forest
In every world state a program can reach, following `Parent.parent` from any entity SHALL end, after finitely many steps, at an entity without `Parent` or at a stale handle. `std.core.Parent.parent` SHALL be a `let` field, so it changes only when `Parent` is added. Creating an entity with a parent (a `children:` node, a `Parent` entry in an entity or template body, or a `spawn` override), `remove Parent` and `destroy` SHALL be accepted as they are, because none of them can close a cycle.

#### Scenario: Field write to Parent is rejected
- **WHEN** a handler with `each: Parent as p` writes `p.parent = other`
- **THEN** compilation fails with the `let` field diagnostic naming `Parent.parent`

#### Scenario: Set of Parent is rejected
- **WHEN** a handler writes `set Parent on e: parent = other`
- **THEN** compilation fails with the `let` field diagnostic naming `Parent.parent`

#### Scenario: Spawn with a parent compiles
- **WHEN** a handler spawns a template with an override `Parent: parent = self`
- **THEN** compilation succeeds and the new entity's parent is the spawning entity

### Requirement: Declared entities form a forest and load parent-first
When a declared entity's `Parent` entry names another declared entity, the named entity SHALL be created before it at load, whatever their declaration order; declared entities whose `Parent` entries do not name each other keep their existing load order relative to one another. A cycle among declared entities' `Parent` entries, including an entity that names itself, SHALL be a compile error naming every entity in the cycle.

#### Scenario: Parent declared later loads first
- **WHEN** `entity Sword` declares `Parent: parent = Hand` and `entity Hand` is declared after it
- **THEN** compilation succeeds, `Hand` is created before `Sword`, and `Sword`'s parent is `Hand`

#### Scenario: Declared cycle is rejected
- **WHEN** `entity A` declares `Parent: parent = B` and `entity B` declares `Parent: parent = A`
- **THEN** compilation fails with a diagnostic naming `A` and `B`

### Requirement: Reparenting is a commit command that never forms a cycle
`add Parent: parent = p` on an existing entity `e` SHALL reparent `e` when its command applies at the activation commit, in command order. The command SHALL do nothing when `p` is `e` or a descendant of `e` in the world state at that command's position; such a command SHALL fire no lifecycle trigger and change no field. Otherwise it SHALL follow the ordinary `add` rules: an entity that gains `Parent` fires `on added Parent`, and one that already carried it fires nothing. A stale `p` SHALL make `e` a root that carries `Parent`, as before.

#### Scenario: Valid reparent applies at commit
- **WHEN** a handler adds `Parent: parent = hand` to a root entity `sword`, and `hand` is not a descendant of `sword`
- **THEN** after the commit `sword`'s parent is `hand`, `on added Parent` fires for `sword`, and handlers earlier in the same activation saw `sword` as a root

#### Scenario: Reparent under own descendant does nothing
- **WHEN** entity A is the parent of B, and a handler adds `Parent: parent = B` to A
- **THEN** after the commit A's parent is unchanged and no lifecycle trigger fires for A

#### Scenario: Two reparents that would form a cycle
- **WHEN** A and B are roots, and one commit applies `add Parent: parent = B` to A and then `add Parent: parent = A` to B
- **THEN** A's parent is B, and the second command does nothing, so B stays a root

#### Scenario: Reparent of a carried Parent fires nothing
- **WHEN** a child C of A is reparented to B, and B is not a descendant of C
- **THEN** C's parent is B and no `on added Parent` or `on removed Parent` fires for C

### Requirement: A self-parent the program pins down is a compile error
An `add Parent` command whose target and `parent` value the program text pins to the same entity SHALL be a compile error naming the statement, because it can never act. This covers `add Parent: parent = self` with no `to`, `add Parent to self: parent = self`, and `add Parent to b: parent = b` for the same pair binding or local `b`.

#### Scenario: Self-parent is rejected
- **WHEN** a handler writes `add Parent to self: parent = self`
- **THEN** compilation fails with a diagnostic that the entity can't be its own parent

#### Scenario: Unpinned target compiles
- **WHEN** a handler writes `add Parent to target: parent = self` where `target` is an event field
- **THEN** compilation succeeds, and the commit check decides at runtime

### Requirement: Extern rules reparent only through commands
An `extern rule` handler SHALL NOT list `std.core.Parent` in `writes:` or as a `set` command capability; either is a compile error that names the handler and points to the `add Parent` command capability. An `add Parent` command capability SHALL be applied with the same commit check as an authored `add Parent`.

#### Scenario: Extern write of Parent is rejected
- **WHEN** an extern rule handler declares `writes: core.Parent`
- **THEN** compilation fails naming the handler and suggesting an `add Parent` command

#### Scenario: Extern add command is checked at commit
- **WHEN** an extern handler queues `add Parent` that would make an entity its own ancestor
- **THEN** the command does nothing at commit

### Requirement: Parent sources are classified
After linking, the compiler SHALL classify every reachable source of a `Parent` value as one of: **structural** (a `children:` node), **attached** (a `Parent` entry in an entity or template body, or in a `spawn` override block), **detach** (`remove Parent`), or **reparent** (an `add Parent` statement or an extern `add Parent` command capability, whatever its target). The classification SHALL depend only on canonical identities, never on declaration or link order.

#### Scenario: Children-only program
- **WHEN** every `Parent` in a program comes from `children:` blocks
- **THEN** every source is structural and no reparent is reachable

#### Scenario: Add Parent is a reparent
- **WHEN** a reachable handler contains `add Parent to item: parent = self`
- **THEN** the program has a reachable reparent source

#### Scenario: Unreachable stdlib reparent is ignored
- **WHEN** an `add Parent` appears only in a stdlib rule recorded as unreachable
- **THEN** the program has no reachable reparent source

### Requirement: Hierarchy facts are part of the compiled program
The linked program SHALL record, for backends: the classified `Parent` sources; whether any reparent source is reachable; and the depth bound, which SHALL be the greatest `children:` nesting depth over reachable archetypes when every source is structural or detach, and unbounded otherwise. When no reparent source is reachable, every parent SHALL have been created before each of its children, so creation order is a topological order of the hierarchy. A backend MAY use these facts to choose its strategy; doing so SHALL NOT change any observable behavior.

#### Scenario: Static depth is recorded
- **WHEN** the deepest reachable `children:` nesting is a root with a child with a grandchild, and every source is structural
- **THEN** the linked program records no reparent and depth bound 2

#### Scenario: Attached parent makes the depth unbounded
- **WHEN** a handler spawns a `Segment` with `Parent: parent = last`
- **THEN** the linked program records no reparent and an unbounded depth

#### Scenario: Parent created before child
- **WHEN** a program has no reachable reparent source
- **THEN** in every reachable world state each entity's live parent has a smaller creation ordinal than the entity

