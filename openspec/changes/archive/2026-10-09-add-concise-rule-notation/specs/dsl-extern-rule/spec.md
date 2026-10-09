## ADDED Requirements

### Requirement: Compact external selection and contracts
Extern rules SHALL accept `each:` / `without:` and inline selection lists with the same meaning as their legacy forms. Their handlers SHALL accept inline `reads:`, `writes:`, `projects:`, `emits:`, `commands:`, and `effects:` lists. Entry resolution, duplicate checks, capability exclusions, access/effect scheduling, and generated callback capabilities SHALL remain identical to the equivalent block contracts. Selecting a trait SHALL NOT implicitly grant component read or write access.

#### Scenario: Inline explicit contract grants the same capabilities
- **WHEN** a native handler uses `reads: Position, Velocity` and `commands: spawn Bullet, set Health`
- **THEN** its capabilities and scheduling equal the corresponding block contract

#### Scenario: Selection does not grant read access
- **WHEN** an extern rule selects a trait with `each:` but does not contract access to it
- **THEN** the native handler gains membership selection without implicit component access

#### Scenario: Inline projection retains capability exclusions
- **WHEN** a handler lists the same trait under inline `writes:` and `projects:`
- **THEN** compilation rejects it under the existing durable-write/projection exclusion

#### Scenario: Compact contracts preserve structural command meanings
- **WHEN** a handler contracts inline `set Health` without `reads: Health` or `writes: Health`
- **THEN** it gains only the existing queue-only patch capability
