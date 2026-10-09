## 1. Write failing tests

- [x] 1.1 Inspect current parser/list helpers and overlapping exclusive-states work; identify reusable entry parsers and preserve unrelated edits.
- [x] 1.2 Add failing parser tests for contextual `each`/`without`, legacy equivalents, aliases, every inline-list family, qualified references, nested commas/colons, comments, and trailing commas.
- [x] 1.3 Add failing parser tests for compact/mixed pair bindings and `reduce per`, including reducer names such as `per`, legacy grouping, and duplicate header/body grouping.
- [x] 1.4 Add failing malformed-input tests for empty entries, repeated commas, mixed continuations, duplicate canonical clauses, invalid contexts/cardinality/grouping, per-entry locations, forward progress, and recovery to later handlers/declarations.
- [x] 1.5 Add failing semantic equivalence tests for domains, binding scope, contracts, graph edges, ordering, empty reductions, sort/limit rules, and spatial-query plans, ignoring source locations only.
- [x] 1.6 Add failing module-artifact/linking and extern-codegen cases proving compact notation preserves canonical data, explicit capabilities, visibility errors, and command/projection exclusions.
- [x] 1.7 Add `examples/concise_rule_notation.cactus` and `tests/test_concise_rule_notation_headless_behavior.cpp`, register the target, and compare isolated compact/legacy cases for movement, ordered pair events, empty/grouped results, rankings, gates, and kept-trait entry/exit at commit boundaries.
- [x] 1.8 Run the focused new tests and fixture-generation target before implementation; record the expected parser rejection and headless generation/build failure from unsupported notation.

## 2. Implement canonical parsing

- [x] 2.1 Extend or extract a shared inline-or-block list helper using existing typed entry parsers, delimiter handling, entry locations, and progress/recovery rules.
- [x] 2.2 Recognize contextual `each`/`without` at unary clause positions, normalize to existing selection fields, and reject duplicate legacy/canonical clauses while retaining ordinary identifier uses.
- [x] 2.3 Apply inline trait/name lists to unary selection and existing rule, handler, group, and phase ordering positions; preserve each position's reference grammar and leave phase `from:` unchanged.
- [x] 2.4 Parse inline predicate and sort-key lists through existing expression parsers; preserve separate predicates, nested expressions, directions, scope, and clause placement.
- [x] 2.5 Apply the shared list surface to all existing external contract families, including `projects:` and structured `commands:`, without granting implicit component access.
- [x] 2.6 Accept each pair binding's inline or block trait list with existing aliases, cardinality validation, and pair/unary exclusions.
- [x] 2.7 Parse `reduce per binding:` into the existing reduction grouping fields; preserve global/legacy forms and reject duplicated grouping or empty reducer bodies.
- [x] 2.8 Complete source-located error recovery and any needed diagnostic wording updates; keep semantic, scheduler, artifact, and backend representations independent of spelling.

## 3. Prove equivalence

- [x] 3.1 Make parser/semantic cases pass, including contextual identifiers, negative cases, multiple-error recovery, and legacy compatibility.
- [x] 3.2 Make artifact/linking and external-codegen cases pass; confirm canonical identities, contracts, execution graphs, reductions, spatial plans, and native callback capabilities remain equivalent.
- [x] 3.3 Make the headless fixture pass with identical ordered observations and state transitions; explicitly check empty groups, kept-trait removal, and visibility before/after commits.
- [x] 3.4 Inspect representative compact/block generated output for unchanged runtime strategy, allowing location-derived temporary-name differences and requiring no notation-specific runtime path.

## 4. Document and demonstrate

- [x] 4.1 Update `spec/cactus_dsl_spec.md` with contextual aliases, the inline-list grammar/table, pair-binding alternatives, header grouping, malformed-input rules, and unchanged clause placement/timing.
- [x] 4.2 Update the README and a small set of teaching/stdlib examples to canonical spellings, covering unary selection, pair relations, reductions, and external contracts; retain legacy compatibility fixtures.
- [x] 4.3 Link Proposal 010 to this OpenSpec change as its implementation plan, and document retained spellings, no deprecation warnings, and the excluded formatter/clause-reordering work.

## 5. Run implementation checks

- [x] 5.0 Resolve existing runtime, linker, semantic-analyzer, artifact, scheduler, and backend lint failures and compiler narrowing/source-location deserialization defects, preserving behavior (broader fixes approved by the user).

- [x] 5.1 Format changed C++, build focused targets with clang-tidy enabled, and run parser, semantic, artifact, linker, codegen, CLI diagnostic, compact-notation, reduction, keep, and ordering regression checks.
- [x] 5.2 Verify project-owned compiler changes with Clang (user requested Clang-only validation) while generated output stays on C++20, and run the full CTest suite with an explicit build directory matching the configured preset.
- [x] 5.3 Run `openspec validate add-concise-rule-notation --strict` and review requirement/scenario coverage against the implemented diff.

## 6. Verify and clean up

- [x] 6.1 Use `openspec-verify-change` to verify implementation against all artifacts; fix findings and re-verify before starting cleanup.
- [x] 6.2 Simplify the diff for reuse, dead code, and unnecessary complexity without expanding into bug hunting.
- [x] 6.3 Perform a separate correctness review of the simplified code and apply fixes; rerun affected checks and resolve all findings.
- [x] 6.4 Use `openspec-verify-change` again to confirm cleanup preserves artifact requirements and runtime equivalence; fix findings and re-verify before completion.
- [x] 6.5 Run the final coverage gate: `cmake --preset clang-coverage`, then `cmake --build --preset clang-coverage --target coverage_check`. If it fails, fix test failures or add tests for uncovered lines until it passes; never lower `CACTUS_COVERAGE_THRESHOLD`. Re-verify any resulting fixes before archiving under the repository lifecycle.

## Validation notes

- Before implementation, the focused parser test and headless fixture generation failed on the unsupported concise syntax. Recovery tests then exposed discarded handlers and misplaced comma diagnostics; both were fixed and covered.
- The focused Clang build with clang-tidy enabled passed for parser, semantic, module artifact, linker, codegen, CLI diagnostics, concise headless behavior, and runtime targets. Their 8 focused CTest cases passed.
- The full Clang coverage target passed all 98 CTest cases. Gated line coverage was 91.3903% against the unchanged 90% threshold. Generated-output targets remain C++20.
- `openspec validate add-concise-rule-notation --strict` and `openspec validate --specs` passed. The user requested Clang-only validation; MSVC was not run.