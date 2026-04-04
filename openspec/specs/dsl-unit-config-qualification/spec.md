## REMOVED Requirements

### Requirement: Optional `as` alias in `apply:` blocks of units and templates
**Reason**: Units and templates no longer use `apply:` blocks. Nested trait blocks identify the owning trait directly, making archetype aliases unnecessary.
**Migration**: Replace `apply:` entries and any `as` alias usage with direct nested trait blocks under the trait name.

### Requirement: Qualified `TraitName.field` and `alias.field` keys in `config:` blocks
**Reason**: `config:` blocks are removed from units and templates. Nested trait blocks eliminate the need for qualified config keys.
**Migration**: Move each config assignment under its owning trait block in the `unit` or `template` body.

### Requirement: Qualified field keys in `spawn()` override arguments
**Reason**: `spawn()` override arguments are replaced by nested trait override blocks.
**Migration**: Rewrite `spawn Foo(Trait.field = value)` and `spawn Foo(alias.field = value)` as `spawn Foo:` with an indented `Trait:` block containing `field = value`.
