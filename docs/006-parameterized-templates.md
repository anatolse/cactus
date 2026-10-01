# Proposal 006: Parameterized Entity Templates

Status: implemented — normative text lives in `spec/cactus_dsl_spec.md`  
Kind: compile-time declarative abstraction  
Semantic change: static template specialization with creation-time values

Implemented without the child-declaration shorthand of proposal 004: a child
template application is written `entity Role from Template(...)` inside a
`children:` block, not `Role from Template(...)`. Examples below that use the
shorthand show the shorthand's eventual form, not current syntax.

## Summary

Allow templates to declare typed, immutable parameters used by trait
initializers and descendant templates. Arguments are named at every
instantiation site and enclosed in parentheses.

Parameterized templates remove repeated archetype blocks and keep derived
values, such as a bubble radius and its visual diameter, consistent.

Parentheses represent application of a typed blueprint interface. They do not
turn templates into runtime functions and do not introduce general function
overloading.

## Proposed syntax

```cactus
template BubbleAt(
    position: vec2,
    velocity: vec2,
    radius: float,
    color: color
):
    tf.WorldTransform:
        position = position

    Bubble:
        velocity = velocity
        radius = radius

    shapes.Shape:
        type = shapes.ShapeType.Circle
        size = vec2(radius * 2.0)
        color = color
```

Entity instantiation:

```cactus
entity Bubble1 from BubbleAt(
    position = vec2(250.0, 200.0),
    velocity = vec2(120.0, 90.0),
    radius = 12.0,
    color = #FF6B6BFF
)
```

Runtime spawn:

```cactus
let bullet = spawn Bullet(
    origin = transform.position,
    direction = aim,
    speed = BULLET_SPEED
)
```

The structure of `Bullet` is known and flattened before CIR construction. The
argument expressions are evaluated when this `spawn` executes, each exactly
once, and their resulting values initialize the already-known archetype.

When there is no explicit override body, the declaration or spawn expression
ends after `)`. The existing colon body remains available for explicit
per-instance trait and child overrides after parameter evaluation:

```cactus
entity Boss from Enemy(health = 100):
    EnemyVisual:
        color = #AA22FFFF
```

## Why parentheses

A parameter list is the public input interface of a reusable blueprint:

```text
template Name(parameters):   declares the interface
Name(arguments)              applies the interface
```

Parentheses are preferred because they:

- visually separate template arguments from trait and child overrides;
- use the same form after `from`, `spawn`, body-level `use`, and child template
  references;
- avoid adding an `arguments:` or `with:` block and another indentation level;
- keep nested child declarations compact;
- are already familiar as syntax for supplying typed inputs;
- do not conflict with indexing (`[]`) or possible future generic syntax
  (`<>`).

The resemblance to a function call is intentional only at the interface
boundary: both forms bind typed inputs. Their semantics remain different.

| Form | Result | Evaluation |
| --- | --- | --- |
| `func f(...) T` | a value of type `T` | ordinary runtime expression |
| `Template(...)` | an entity archetype specialization | structure resolved statically; values evaluated in the creation context |

A template application is legal only where the grammar expects a template
reference:

```cactus
entity Bubble1 from BubbleAt(position = start, radius = 12.0)
spawn BubbleAt(position = current, radius = next_radius)
use BubbleAt(position = origin, radius = 8.0)
Child from BubbleAt(position = offset, radius = 4.0)
```

It is not a general expression or a first-class value:

```cactus
let factory = BubbleAt             # error: template is not a value
let value = BubbleAt(radius = 4.0) # error: template application is not an expression
```

## Parameters are not trait overrides

Arguments in parentheses bind only parameters explicitly declared by the
template. The optional body after `:` remains the syntax for overriding traits
and descendants of one concrete instance.

```cactus
template Bullet(origin: vec2, direction: vec2):
    tf.WorldTransform:
        position = origin
    Motion:
        velocity = direction * BULLET_SPEED
    Damage:
        amount = 10

let bullet = spawn Bullet(
    origin = transform.position,
    direction = aim
):
    Damage:
        amount = 25
```

Here `origin` and `direction` form the supported template interface.
`Damage.amount` is a low-level per-instance override. A trait field does not
implicitly become a named template argument.

Consequently, this proposal does not restore flat arbitrary spawn overrides
such as `spawn Bullet(damage = 25)` unless `damage` is an explicitly declared
template parameter.

## Parameter rules

- Parameters are typed and immutable.
- Arguments are named; positional arguments are not supported.
- A parameter may have a default built from constants and earlier parameters
  (see "Defaults" below):

  ```cactus
  template Light(color: color = #FFFFFFFF, intensity: float = 1.0):
  ```

- A required parameter must be supplied exactly once.
- Unknown and duplicate arguments are compile-time errors.
- An argument expression must be pure and valid in its application context.
- A parameter may be referenced by the template root and all descendants.
- A child template may receive an expression derived from a parent parameter.
- A parameter name denotes its bound value, not writable storage.
- Parameters cannot be captured as first-class values or referenced outside
  the applied template tree.

## Hierarchical example

```cactus
template Tree(
    target_scale: float,
    slowdown: float,
    seed: int
):
    TreeGrowth:
        target_scale = target_scale
        slowdown = slowdown
    TreeRng:
        rng = rand.seeded(seed)

    children:
        Crown from TreeCrown(
            target_scale = target_scale,
            slowdown = slowdown,
            seed = seed + 100
        )
```

## Name resolution

Recommended precedence inside a template expression:

1. local template parameter;
2. local declaration symbol;
3. imported or module symbol.

A parameter name that conflicts with a trait alias, child role, or other
parameter in the same template is rejected. Trait fields are always addressed
inside their trait block and therefore do not shadow parameters.

## Semantics and lowering

Parameterized templates are statically specialized blueprints, not runtime
functions. Template expansion fixes the set of entities, traits, fields, and
children before CIR construction. Parameter values may still originate at
runtime when the application occurs in `spawn`.

For every template application, the compiler:

1. Type-check supplied arguments.
2. Bind each parameter reference to a typed argument slot.
3. Expand nested template applications and ordinary template composition.
4. Apply the explicit instance/spawn override body.
5. Flatten the resulting archetype tree and initializer plan.
6. Emit only the resulting creation data and value expressions into CIR.

At a creation site, arguments are evaluated left-to-right in source order,
exactly once, before any root or child trait initializer is committed. Default
arguments are evaluated as if they appeared at the application site after all
explicit arguments have been bound.

Parameter references in multiple field initializers reuse the one bound value;
the source expression is not evaluated again. Parameters do not become entity
storage unless their values initialize ordinary trait fields.

### Declaration-time and runtime applications

The allowed argument expressions depend on when an entity is created:

| Application | Argument availability | Evaluation time |
| --- | --- | --- |
| `entity Name from Template(...)` | constants and other load-time-valid expressions | module or scene load |
| body-level `use Template(...)` | enclosing template parameters and constants | enclosing template application |
| `Child from Template(...)` | enclosing template parameters and constants | creation of the containing tree |
| `spawn Template(...)` | handler locals, selected trait values, event/phase data, and constants | execution of the `spawn` statement |

An `entity` declaration cannot capture handler state. Conversely, permitting a
runtime expression in `spawn` does not make archetype structure dynamic: only
the values of statically known initializers vary.

### Defaults

A default may reference constants and earlier parameters, but not later
parameters:

```cactus
template Projectile(
    speed: float = 10.0,
    velocity: vec2 = vec2(speed, 0.0)
):
```

Defaults are type-checked once in the template declaration and validated again
after binding in each application context. Cyclic default dependencies are
compile-time errors.

## Restrictions

Template bodies remain declarative. They cannot contain:

- handlers or runtime `if` statements;
- mutation;
- event emission;
- spawn/destroy/add/remove/project;
- recursion through template instantiation.

Parameters cannot control structure. In particular, a parameter cannot
conditionally add a trait or child, select a template by value, or determine
how many descendants are created. Conditional structure requires a separate,
explicit language proposal.

Recursive template expansion, including indirect cycles, is a compile-time
error with a template-instantiation path.

## Compatibility

An existing template without a parameter list is equivalent to a template with
zero parameters. Existing `entity Name from Template:` and `spawn Template:`
syntax remains valid.

## Implementation work

- Lexer/parser: parameter lists, named template arguments, and bodyless
  parameterized entity/spawn forms.
- Parser: recognize `Template(...)` only in template-reference positions; do
  not add it to general primary expressions.
- AST: parameter declarations and named arguments on every template-reference
  form.
- Semantic analysis: type checking, defaults, application-context validation,
  name resolution, and cycle detection.
- Template flattener: produce a static archetype plus typed initializer slots;
  do not require runtime values in order to flatten structure.
- CIR lowering: evaluate spawn arguments once in source order and reuse their
  slots throughout root and descendant initialization.
- Module artifacts: serialize public template signatures and argument-bearing
  references; increment artifact version.
- Diagnostics: missing, unknown, duplicate, wrong-type arguments and cycles.
- Tests: root values, descendants, dependent defaults, single evaluation,
  composition, entity, runtime spawn, illegal expression use, and cross-module
  public templates.

## Acceptance criteria

- `radius` can initialize both gameplay radius and derived visual size.
- Parameters are available in nested child template arguments.
- A side-effect-free spawn argument is evaluated once even when its parameter
  initializes several fields or descendants.
- Runtime spawn values do not change the statically flattened entity/trait
  structure.
- A parameter leaves no entity storage unless used in a trait field.
- Invalid argument lists fail during semantic analysis.
- Arbitrary trait fields cannot be passed as arguments unless exposed as
  template parameters.
- Template applications are rejected in ordinary expression positions.
- Public parameterized templates survive module artifact round-tripping.
- Template recursion is rejected deterministically.

## CIR impact

No template object, template-call instruction, overload candidate, or dynamic
archetype reaches CIR. Template structure is fully expanded before CIR.

For runtime `spawn`, CIR contains ordinary evaluated argument temporaries and a
creation command for the statically known flattened archetype. One temporary
may feed several root or descendant field initializers. Load-time entities may
store already evaluated constants directly in their creation data.
