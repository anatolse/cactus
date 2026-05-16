## Context

The current repository already has automated curated example compilation requirements, including coverage for `blue-square`, `mesh-renderer`, and `platformer`. At the same time, some examples appear to contain syntax or comments from earlier language iterations.

The biggest observed drifts are:

- system handler bodies using bare trait fields even though semantic analysis requires `alias.field` or `TraitName.field`,
- comments and examples showing parenthesized dynamic trait initialization such as `add Invincible(duration = 1.5)`,
- comments mentioning removed syntax or migration-era concepts that are no longer part of the current grammar,
- examples that are valuable but should still be made truthful to the current spec and implementation.

## Goals / Non-Goals

**Goals:**
- Fix existing examples in place so they use currently accepted grammar and semantics.
- Update comments so they describe the implemented current language surface.
- Keep the compilation test suite aligned with the examples it already validates.
- Make example drift easy to detect in CI.

**Non-Goals:**
- Implementing template composition, placement, UI, or platformer controller features.
- Rewriting every example into a polished tutorial immediately.
- Moving examples into new directories or introducing canonical/exploratory classification.
- Removing examples solely because they are broad or advanced.

## Proposed Approach

Do not separate examples into categories as part of this change. Instead, update the existing files that are intended to remain in the repository so they correspond to the current spec and implementation.

The work is primarily an in-place cleanup pass:

```text
examples/platformer/platformer.cactus
    fix field access and comments to match current semantic rules

examples/*
    keep paths stable; fix syntax/semantic drift where found
```

`examples/dsl_showcase.cactus` was removed during this change rather than retained as a broad mixed-current/future showcase. The remaining example cleanup should keep existing paths stable.

## Example Conformance Rules

Maintained examples should obey these rules:

1. **Current syntax only.** No legacy `apply:`, `config:`, parenthesized `add`, parenthesized `emit`, handler parameters, or other removed syntax.
2. **Current semantic profile only.** System trait fields use `alias.field` or `TraitName.field`; event fields use event names or handler aliases.
3. **No unimplemented syntax presented as current.** If a comment discusses future syntax, it must say it is future-facing rather than implemented.
4. **Backend expectations are explicit.** If an example depends on cpp-entt stdlib bindings, that dependency is documented and covered by tests.
5. **Teaching comments match the code.** Comments should explain accepted syntax, not historical migration notes unless the file is explicitly a migration guide.

## Design Decision: Fix examples in place

This change intentionally avoids moving files into `supported/`, `advanced/`, or `exploratory/` directories. Stable paths matter because tests, docs, and developer habits already reference existing examples.

If a section in a maintained example cannot be made to match the current implementation, that section should be rewritten as prose about future direction or removed until the corresponding feature exists.

The important point is that examples should not teach syntax that the current compiler rejects.

## Risks / Trade-offs

- **Large cleanup surface:** Broad examples may contain many small drifts. Mitigation: prioritize curated examples and obviously stale syntax first.
- **Future-looking examples become less ambitious:** Some snippets may need to be removed or rewritten as prose. Mitigation: capture desired future syntax in OpenSpec changes instead of presenting it as implemented.
- **Implementation/spec mismatch may be discovered:** Example cleanup can reveal places where the spec and implementation disagree. Mitigation: decide case-by-case whether to fix the example, implementation, or spec in a follow-up change.

## Open Questions

- Should every `.cactus` example be parsed/semantically checked, or only the curated set used for backend compilation?
- Should broad showcase material be rebuilt later as strict validation once the language surface stabilizes, or remain documentation-oriented?
- Should future syntax sketches live in OpenSpec design docs instead of `.cactus` example files?
