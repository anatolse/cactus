## ADDED Requirements

### Requirement: Code generation for `order by:` clause
When a `SystemNode` has a non-empty `order_by` field, the EnTT backend SHALL emit a `registry.sort<T>(comparator)` call before the view iteration loop for each handler. `T` is the component type corresponding to the first sort key's alias. The comparator lambda implements the full multi-key lexicographic comparison.

Generated pattern for `order by: s.layer asc, p.pos.y desc`:

```cpp
registry.sort<Sprite>([&](const entt::entity a, const entt::entity b) {
    const auto& sa = registry.get<Sprite>(a);
    const auto& sb = registry.get<Sprite>(b);
    if (sa.layer != sb.layer)
        return sa.layer < sb.layer;
    const auto& pa = registry.get<Position>(a);
    const auto& pb = registry.get<Position>(b);
    return pa.pos.y > pb.pos.y;
});
```

The comparator is placed immediately before the `auto view = registry.view<...>()` call for the handler.

#### Scenario: Single sort key generates single-component comparator
- **WHEN** `order by: s.layer asc` is compiled
- **THEN** the generated comparator compares only `Sprite.layer` with `<`

#### Scenario: Multi-key sort generates if-chain comparator
- **WHEN** `order by: s.layer asc, p.pos.y desc` is compiled
- **THEN** the generated comparator uses `if (sa.layer != sb.layer) return sa.layer < sb.layer; return pa.pos.y > pb.pos.y;`

#### Scenario: desc direction reverses comparison
- **WHEN** a sort key has `desc` direction
- **THEN** the comparator uses `>` for that key's comparison

#### Scenario: asc direction uses less-than comparison
- **WHEN** a sort key has `asc` direction (explicit or default)
- **THEN** the comparator uses `<` for that key's comparison

#### Scenario: No order by generates no sort call
- **WHEN** a system has no `order by:` clause
- **THEN** no `registry.sort()` call is generated; the view iterates in default order
