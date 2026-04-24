# example-entt-mesh-renderer Specification

## Purpose
TBD - created by archiving change add-entt-mesh-renderer-example. Update Purpose after archive.

## Requirements

### Requirement: Mesh renderer example source demonstrates the shipped stdlib mesh-render surface
The repository SHALL provide `examples/mesh-renderer/main.cactus` as a valid Cactus DSL example that imports `std.render.meshes` and `std.transform.volume` and declares at least one renderable mesh entity through the shipped stdlib trait surface.

#### Scenario: Example imports the shipped mesh-render modules
- **WHEN** `examples/mesh-renderer/main.cactus` is read
- **THEN** it imports `std.render.meshes` and `std.transform.volume`

#### Scenario: Example declares a renderable mesh entity
- **WHEN** the example's scene units are declared
- **THEN** at least one unit applies `std.transform.volume` transform data together with `std.render.meshes.Renderer`

### Requirement: Mesh renderer example is compatible with EnTT backend generation
The mesh renderer example SHALL compile with the `cpp-entt` backend without requiring project-local user extern C++ callbacks for mesh rendering.

#### Scenario: Example generates EnTT project glue
- **WHEN** the compiler is invoked with `cactus examples/mesh-renderer/main.cactus --backend cpp-entt --output <generated.cpp>`
- **THEN** code generation succeeds and produces project glue intended to link against the standard cpp-entt runtime/library

#### Scenario: Example relies on recognized stdlib mesh-renderer binding
- **WHEN** generated output from the example is compiled in the curated example build
- **THEN** successful compilation does not require a user-defined `MeshRenderer_update(...)` implementation