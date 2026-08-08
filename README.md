# Cactus

A DSL for making games, and a compiler that turns it into native C++ binaries
(EnTT ECS + raylib).

**This is a for-fun side project.** I'm using it to play with AI-assisted /
agentic coding, and to test a personal thesis:

> Can a dead-simple, YAML-like game DSL — the kind you could hand a kid —
> actually compile down to high-performance native game code? Or does
> "simple and expressive" inevitably mean "slow"?

No roadmap, no stability guarantees, no promises it goes anywhere. It's a
testbed for that question, not a product.

## What it looks like

```
entity Player:
    tf.WorldTransform:
        position = vec2(100.0, 300.0)
    PlayerMotion:
        velocity = vec2(0.0, 0.0)
        move_speed = MOVE_SPEED

rule MovementSystem:
    filter:
        tf.WorldTransform
        PlayerMotion

    on tick:
        velocity = vec2(move_axis * move_speed, velocity.y)
        position = vec2(position.x + velocity.x * tick.dt, position.y)
```

Entities, traits (components), and rules (systems) are declared directly —
no loops, no manual memory management, no hand-wired ECS boilerplate. The
compiler generates all of that.

## How it works

```
.cactus source → lexer → parser → semantic analysis → decorated AST
               → C++ codegen (EnTT + raylib) → native binary
```

- `src/frontend` — lexer, parser, semantic analyzer, module system
- `src/backends/cpp-entt` — the only working backend today: generates C++
  using EnTT (ECS) and raylib (rendering / input / audio)
- `src/backends/rust` — planned, not implemented
- `stdlib/` — math, physics, transforms, camera, UI, input, rendering,
  written in Cactus itself
- `examples/` — platformer, twin-stick shooter, split-screen, 3D model
  rendering, editor — real(ish) games doubling as compiler test fixtures

## Building

```
cmake --preset default
cmake --build build
ctest --preset default
```

See `CMakePresets.json` for MSVC / Clang / release presets.

## Status

Early and unstable. Language and compiler are evolving together as I throw
increasingly ambitious example games at it, mostly written pairing with AI.
If it breaks, it breaks — that's kind of the point.
