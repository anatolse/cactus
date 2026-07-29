# external-runtime-events Specification

## Purpose
Define declaration, visibility, provenance, and runtime injection rules for `extern event` values — events whose payload type is declared in Cactus but whose occurrences may only be injected by the runtime host.

## Requirements

### Requirement: External runtime event declarations
Cactus SHALL accept `[pub] extern event Name` declarations with the same marker or bare-field payload syntax as ordinary events. An external event SHALL declare a typed event value whose occurrences originate outside authored Cactus handlers.

#### Scenario: Runtime frame payload is declared
- **WHEN** `pub extern event frame:` declares `dt: float`
- **THEN** semantic analysis records a public external event named `frame` with an immutable float field named `dt`

#### Scenario: Marker external event is declared
- **WHEN** `extern event resumed` appears without a field block
- **THEN** semantic analysis records a module-private marker external event

### Requirement: Runtime-only event provenance
Only the runtime host SHALL inject occurrences of an external event. Authored `emit` statements and handler `emits` contracts MUST NOT name an external event.

#### Scenario: Authored emit of external event is rejected
- **WHEN** a handler contains `emit frame` and `frame` resolves to an external event
- **THEN** semantic analysis reports that external event `frame` can only be emitted by the runtime

#### Scenario: Runtime injects a declared payload
- **WHEN** the runtime injects `frame` with a float `dt`
- **THEN** the scheduler accepts the occurrence as an activation source using the declared event type

### Requirement: Canonical external event identity
External events SHALL participate in module visibility, duplicate-name validation, canonical symbol resolution, artifacts, and linking as event symbols while preserving an external-provenance flag.

#### Scenario: Imported external event resolves canonically
- **WHEN** a phase in another module references a public external event through its module qualifier
- **THEN** the reference resolves to the external event's canonical event identity
