# Example authoring rules (`examples/`)

These add to the "Cactus DSL authoring rules" in the root `CLAUDE.md`.

## No duplication inside an example

- If the same entity shape appears more than once, extract a `template` and create
  the instances with `entity Name from Template:` or `spawn Template:`. Use template
  parameters for the values that differ between instances.
- Put shared parts of related templates in a base template and compose it with
  body-level `use`, instead of repeating the same trait entries.
- Don't set a field to its default value. That includes the trait field's declared
  default, a template parameter's default, and a value the entity already gets from
  its template. Write only the values that differ.

## Three kinds of example

The kind follows from the layout:

- **Teaching examples** — single `.cactus` files directly in `examples/`. Each one
  shows how to build a behavior with the core language. Write that behavior by hand;
  don't call a stdlib API that already does it. Stdlib for plumbing the example
  needs but doesn't teach (transform, render, input) is fine.
- **Showcase examples** — other subdirectories (`platformer/`, `shooter-slice/`, …).
  They show a real game and use stdlib freely. Prefer a stdlib primitive over
  hand-rolled logic here.
- **Stdlib fixtures** — `stdlib-fixtures/`. Each one exercises a stdlib API for its
  headless test. Use the API under test directly.

## Keep examples current with the core language

When a change adds a core language feature, update every example where that feature
makes the code shorter or clearer, as part of the same change. Examples should show
the current way to write Cactus, not an older one.

A new stdlib feature needs a stdlib fixture and should reach showcase examples where it
fits. Teaching examples keep their hand-written core-language version.

Exception: an example that is a test fixture for a specific older form or a
diagnostic must keep that form. Check `tests/` for references to the file before
rewriting it.
