## Why

Cactus examples are part of the authoring experience and are already used as integration inputs. Some current examples contain syntax or comments that no longer correspond to the current spec and implementation, such as bare trait-field access in system handlers or legacy parenthesized `add` forms.

Rather than reorganizing or separating examples, this change focuses on fixing the existing examples in place so they are truthful references for the language as it works today.

## What Changes

- Audit existing examples for drift against the current parser, semantic specs, and implemented backend behavior.
- Fix examples in their existing locations so demonstrated syntax is accepted by the current implementation.
- Update stale teaching comments that describe removed or unsupported syntax.
- Keep curated example compilation coverage aligned with the fixed examples.
- Avoid introducing new example categories, directory splits, or broad reclassification in this change.

## Capabilities

### New Capabilities
- `example-spec-conformance`: maintained examples remain aligned with the current spec and implementation.

### Modified Capabilities
- `example-cpp-compilation-tests`: curated compilation coverage reports example drift clearly.
- `language-philosophy`: examples reinforce the gameplay-core profile by showing implemented current syntax.

## Impact

- Primarily affects examples, documentation, and tests.
- Does not add new Cactus language syntax.
- Does not move or separate existing examples.
- Improves confidence before larger language additions such as template composition and content placement.
