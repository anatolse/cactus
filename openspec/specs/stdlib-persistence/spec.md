## Purpose

Provide a small gameplay request surface and an external storage adapter contract for format-independent world save operations.

## Requirements

### Requirement: Typed save requests and runtime outcomes
The std.persistence module SHALL expose an ordinary `SaveRequested` event with `slot: string` and `request_id: int` fields. It SHALL expose public extern `SaveCompleted` and `SaveFailed` events carrying the same correlation fields, with `SaveFailed` additionally carrying `code: string` and `message: string`. Each accepted request SHALL produce exactly one outcome. Request IDs SHALL be caller correlation values: repeated IDs SHALL remain distinct requests and SHALL NOT deduplicate. Standard error codes SHALL cover unavailable adapter, I/O failure, unsupported value, and resource limits. Existing external-event provenance restrictions SHALL apply.

#### Scenario: Author requests a save
- **WHEN** a handler emits `SaveRequested` with a slot constant and request ID
- **THEN** the runtime captures the world and reports one correlated outcome carrying the same slot and request ID

#### Scenario: Adapter unavailable
- **WHEN** a save request is processed with no registered adapter
- **THEN** `SaveFailed` identifies adapter unavailability and the world remains unchanged

#### Scenario: Repeated request ID stays distinct
- **WHEN** two save requests carry the same request ID
- **THEN** each produces its own outcome

### Requirement: Save operations occur at deterministic boundaries
Persistence requests SHALL be ordered by emission order and processed after the originating activation's complete handler and event cascade and structural and lifecycle commit drain. Accepted boundary requests SHALL be processed as a fixed ordered batch, frozen when the boundary opens. Outcome events SHALL be subsequent external activations delivered in operation order after that batch, never reentrant callbacks into a running operation. Requests emitted by outcome handlers SHALL belong to a later boundary. The initial adapter contract SHALL be synchronous. Scene transitions SHALL obey the same boundary ordering when combined with persistence requests.

#### Scenario: Outcomes follow the batch
- **WHEN** two save requests are accepted at the same boundary
- **THEN** both captures and writes run before either outcome event is delivered
- **AND** the outcomes arrive in request order

#### Scenario: Outcome handler request defers to a later boundary
- **WHEN** a `SaveCompleted` handler emits another `SaveRequested`
- **THEN** the new request is processed at a later boundary, not inside the current batch

### Requirement: Storage adapters own format and location
An external adapter SHALL receive the generated schema descriptor and an owned typed snapshot to write, and SHALL return an owned typed snapshot or an explicit error to read. It SHALL choose encoding and storage without changing gameplay declarations. It SHALL NOT receive unrestricted registry access, spawn capabilities, or ownership of entity reconstruction. A successful write SHALL mean a completed storage commit; a reported write failure SHALL preserve the previously committed slot contents. A read SHALL reject a document whose schema descriptor is incompatible with the running program's descriptor. Document validation SHALL remain runtime-owned even when adapters validate their own encoding.

#### Scenario: Same game uses two formats
- **WHEN** two adapters encode the same generated snapshot using different representations
- **THEN** each reads back a document equal to the one it was given, without changing Cactus traits or handlers

#### Scenario: Partial file write fails safely
- **WHEN** the example file adapter fails before committing replacement output
- **THEN** it reports failure and the previous save slot remains readable

#### Scenario: Incompatible baseline is rejected on read
- **WHEN** a stored document was written against a different archetype default descriptor
- **THEN** reading it reports incompatible schema

### Requirement: Saving works without host C++ integration
Generated `main()` SHALL register the example file adapter by default so that an author who only writes Cactus can request a save and get a file. Hosts that supply their own `main()` SHALL be able to register a different adapter through the public generated API. Adapter selection SHALL be host or build integration and SHALL NEVER appear as a device or backend marker in Cactus declarations.

#### Scenario: Pure Cactus program saves
- **WHEN** a program built with the default generated entry point emits `SaveRequested`
- **THEN** a file is written by the example file adapter and `SaveCompleted` is delivered

#### Scenario: Host replaces the adapter
- **WHEN** a host provides its own entry point and registers a custom adapter
- **THEN** the same gameplay declarations save through that adapter instead

### Requirement: Persistence stays separate from scene control
Save SHALL require neither a format declaration in Cactus nor a persist modifier on templates. Existing `load` scene semantics and `std.core.Persistent` scene-survival semantics SHALL remain separate from snapshot eligibility. Documentation SHALL include an author fixture and a custom adapter example.

#### Scenario: Scene survivor is not automatically saved
- **WHEN** an entity has `std.core.Persistent` but no persistence eligibility
- **THEN** it remains excluded from a world snapshot
