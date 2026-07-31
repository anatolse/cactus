# targeted-event-delivery Specification

## Purpose
Define the semantics of recipient-targeted event delivery: how a targeted occurrence captures and retains its recipient across queueing and cascade deferral, how total entity semantics govern delivery to a stale recipient, how target routing constrains selectionless, unary, and pair event consumers, and how targeted delivery preserves the same graph event-flow and activation-commit semantics as broadcast delivery.

## Requirements

### Requirement: Targeted event occurrences retain their recipient
An `emit Event to target` statement SHALL evaluate `target` exactly once and SHALL store the resulting `entity_id` with the queued occurrence. Recipient identity SHALL survive queueing, bounded cascade deferral, and later delivery. An emit without `to` SHALL queue an occurrence with no recipient.

#### Scenario: Recipient survives deferred cascade delivery
- **WHEN** a targeted occurrence exceeds the current cascade depth and is deferred
- **THEN** its original recipient is retained when it is delivered in the later activation

#### Scenario: Broadcast occurrence has no recipient
- **WHEN** a handler emits `Contact` without `to`
- **THEN** delivery uses ordinary full consumer-domain cardinality

### Requirement: Targeted delivery obeys total entity semantics
A targeted occurrence whose recipient is stale at delivery SHALL be dropped before any consumer executes. No handler, command, or effect SHALL run for that occurrence.

#### Scenario: Recipient destroyed before delivery
- **WHEN** a queued targeted occurrence names an entity that is no longer live when delivery begins
- **THEN** the occurrence is silently dropped

### Requirement: Target routing constrains relation consumers
For a live targeted occurrence, a selectionless consumer SHALL execute once; a unary consumer SHALL execute at most once for the recipient and only if it satisfies that consumer's selection; and a pair consumer SHALL execute only ordinary relation tuples in which at least one binding equals the recipient. An untargeted occurrence SHALL use each consumer's full ordinary domain.

#### Scenario: Unary target receives one event
- **WHEN** `emit Contact to body` is delivered to a unary ResolveContact handler and `body` satisfies its filter
- **THEN** ResolveContact executes once for `body` and not for other matching entities

#### Scenario: Unary target failing filter receives nothing
- **WHEN** a live recipient does not satisfy a unary consumer's filter
- **THEN** that consumer does not execute for the targeted occurrence

#### Scenario: Pair target routes to incident tuples
- **WHEN** a targeted occurrence is delivered to a pair consumer
- **THEN** only snapshotted tuples whose left or right binding equals the recipient execute

#### Scenario: Selectionless observer receives targeted occurrence once
- **WHEN** a selectionless handler consumes a targeted event
- **THEN** it executes once without acquiring an implicit current entity

### Requirement: Target routing preserves graph event semantics
Targeted occurrences SHALL use the same stable consumer graph order, bounded cascade rules, and activation command buffer as broadcast occurrences. Targeted delivery MUST NOT invoke consumers immediately at the emit site.

#### Scenario: Targeted consumer commands join activation commit
- **WHEN** a targeted event consumer queues a structural command
- **THEN** the command commits at the enclosing activation boundary after the event cascade
