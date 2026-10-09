## Context

See proposal.md - Why and `docs/010-concise-domains-and-contracts.md`. Current parser entry points already share common regular/extern rule clauses and ordering/name-block helpers, but unary trait lists and pair-binding trait lists repeat related parsing. Predicate, sort-key, and external-contract entry parsers already define the required semantics.

Existing AST fields represent positive/negative selection, pair bindings, reduction grouping, ordering, and handler contracts. Semantic analysis, module artifacts, scheduling, and cpp-entt consume those structures. This change should end at that boundary. The separately planned CIR implementation is not a prerequisite.

The work overlaps parser files touched by the active exclusive-states change. During implementation, inspect current changes and integrate the shared clause parser without replacing unrelated state grammar or tests.

## Goals / Non-Goals

**Goals:** Normalize surface alternatives as they are parsed; share list handling without duplicating expression parsing; retain accurate source locations and error recovery; prove both frontend and runtime equivalence.

**Non-Goals:** No notation flags downstream, new backend branches, general grammar cleanup, or requirement to migrate all maintained examples. Proposal non-goals apply.

## Decisions

### 1. Contextual aliases normalize at unary clause recognition

Recognize `each` and `without` only as identifier-plus-colon at the existing unary clause positions. Feed them into the same positive/negative selection fields as `filter` and `exclude`. Keep legacy tokens unchanged and recognize duplicate aliases as duplicates of the same canonical clause.

This avoids globally reserving new words or expanding semantic analysis. Existing uses of the words as traits, fields, aliases, binding names, and locals remain valid in their identifier positions. Treating the new words as lexer keywords would unnecessarily break those uses; preserving spelling flags would force downstream consumers to distinguish equivalent programs.

### 2. Share list structure and retain typed entry parsers

Extend or extract a frontend-local inline-or-block list helper from the existing list/block helpers. Select a block when the colon is followed by NEWLINE and INDENT; otherwise parse a nonempty inline list through the clause's existing entry parser. Share delimiter, trailing-comma, location, progress, and recovery handling across trait entries, names, predicates, sort keys, and contract capabilities.

Calls, list literals, constructors, and conditional expressions retain the normal expression parser. Only an enclosing list consumes its separator comma. Inline predicates stay separate expressions rather than becoming a synthetic `and` expression, so predicate-level purity, grouping classification, and spatial recognition see the same structure as before. Source-text splitting would lose nested expression boundaries and locations; separate full list implementations would repeat error handling.

Apply the shared surface to every already-valid `after:` position, including leading handler dependencies, group dependencies, and phase dependencies. Preserve each position's own name grammar and lookup rules. Phase `from:` and scalar clauses remain outside the new list surface.

### 3. Compact pairs reuse existing binding records

After a pair binding's name and colon, parse the same positive trait entries as unary selection, retaining binding-local aliases. Keep the surrounding two-binding `pairs:` structure. Each binding independently chooses inline or block form.

Cardinality, namespaces, writability, membership snapshots, and acceleration continue through existing validation and lowering. Collapsing both bindings into one inline `pairs:` expression is excluded because it would obscure the existing entity roles and require another grammar.

### 4. Header grouping lowers to the existing reduction binding

After `reduce`, recognize optional contextual `per` plus a binding name before the colon. Populate the existing grouping field and its location. Preserve body-level `per:` on legacy blocks; reject a second grouping declaration if the header already supplied one. `per = count()` remains a reducer name, not a grouping declaration.

Keep reducer declarations as a nonempty block. Existing reduction validation continues to own binding checks, eliminated scope, purity, identities, numeric edge cases, retained-binding writes, aggregate ranking, and `keep` restrictions. A second reduction representation would duplicate this logic.

### 5. Preserve declaration placement and runtime strategy

Add accepted list layouts within existing clause positions. Keep the current query clause order, including sorting after reduction, and retain leading-only handler dependencies. Existing forbidden contexts still parse enough to diagnose and recover, without admitting the clause semantically.

No scheduler, spatial planner, module format, or backend change is expected. If frontend diagnostic spelling must change, describe both accepted positive spellings without altering lookup. Existing source-derived temporary names can differ because locations differ; generated C++ text need not be byte-identical. Observable order and backend strategy must be equivalent.

### 6. Verify meaning at both compilation and execution boundaries

Start with failing parser tests covering each list family and malformed-input recovery. Compare compact/block sources with the same module and declaration identities, disregarding locations, to check domains, aliases, contracts, graph edges, reduction plans, and spatial recognition. Round-trip the same canonical data through module artifacts and linking; external codegen tests compare granted capabilities.

Add `examples/concise_rule_notation.cactus` and a headless behavior test before implementation. Use isolated compact and legacy cases so their traits and event observations cannot interact, and ensure kept traits have only one producer per trait. Drive identical scripted activations and compare movement, ordered pair events, grouped totals including empty groups, ranked/limited results, gates, and kept-trait entry/exit. Assertions on the kept value during the phase and its lifecycle after commit protect timing, not just final values.

Unit tests cover valid input not exercised by that fixture, including contextual identifiers, header/body duplicate grouping, per-item diagnostics, and inline contracts. Existing semantic/runtime suites remain the regression contract. Benchmarks are unnecessary: parser normalization must select existing runtime paths.

## Risks / Trade-offs

- Nested commas or conditional colons mistaken for list syntax → parse complete entries with existing typed parsers and test nested expressions.
- Recovery consumes the next binding or handler → recover with the owning list's indentation and clause boundaries, and require token progress after errors.
- Aliases bypass existing duplicate or domain checks → canonicalize before validation and test mixed legacy/canonical spellings.
- Header grouping changes scope or empty results → reuse the existing reduction fields and exercise empty groups and retained writes.
- Compact predicates change query recognition → retain separate predicate entries and compare resolved spatial plans.
- Unrelated in-progress parser edits are overwritten → integrate against current files and preserve unrelated tests.
- Two accepted spellings increase temporary surface area → teach one canonical spelling, retain compatibility without warnings, and defer removal to a separate decision.

## Migration Plan

Land tests and parsing support together, then document canonical spellings and update the README plus a small set of unary, pair, reduction, and external-contract examples. Retain legacy fixtures to exercise compatibility. Link Proposal 010 to this change during implementation so it no longer serves as a separate implementation plan.

Run focused checks, compiler builds with clang-tidy, and the full required coverage gate. Complete verification, simplification, correctness review, re-verification, and archive under the repository lifecycle. Existing projects need no source migration. A rollback restores migrated examples to block/legacy forms before reverting parser acceptance; no saved-world or module-schema conversion is needed.

## Verification scope extension

The user approved fixing existing runtime, linker, semantic-analyzer, artifact, scheduler, and backend lint failures exposed by the required verification gate, including the MSVC narrowing warning. These are behavior-preserving refactors, covered by the existing regression suites; language semantics and generated runtime strategy remain unchanged.

Cross-compiler verification also exposed a source-location deserialization failure under MSVC. Read the filename, line, and column sequentially before constructing SourceLocation, preserving the artifact format and intended values.

The same gate also requires smaller backend emission helpers, explicit fallible moves, and normal Catch2 registration macros during static analysis. Preserve generated output, keep all lint checks enabled, and compare generated fixtures before and after these refactors.
