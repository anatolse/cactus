// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "persistence_test_adapter.hpp"

namespace {

namespace backend = cactus::runtime::entt_backend;

using Cell      = cactus_gen::stdlib::voxel::volume::Cell;
using Volume    = cactus_gen::stdlib::voxel::volume::Volume;
using CellProbe = cactus_gen::stdlib::voxel::volume::CellProbe;
using RayProbe  = cactus_gen::stdlib::voxel::volume::RayProbe;
using Solid     = cactus_gen::stdlib::physics::volume::Solid;
using RayHit    = cactus_gen::stdlib::collision::volume::RayHit;

const auto& slots() {
    return cactus_gen::detail::generated_named_slots();
}

struct World {
    entt::registry registry;

    World() {
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            backend::generated_inject_external_event(cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
            backend::generated_drain_external_events(registry);
        }
    }

    // Enough frames for every volume to finish filling and commit its materials.
    void fill() {
        frame(6);
    }

    int cell_count(entt::entity volume) {
        int count = 0;
        for (const auto [entity, cell] : registry.view<Cell>().each()) {
            if (cell.volume == volume) {
                ++count;
            }
        }
        return count;
    }

    entt::entity cell_at(entt::entity volume, int x, int y, int z) {
        for (const auto [entity, cell] : registry.view<Cell>().each()) {
            if (cell.volume == volume && cell.x == x && cell.y == y && cell.z == z) {
                return entity;
            }
        }
        return entt::null;
    }

    bool solid(entt::entity cell) {
        return registry.all_of<Solid>(cell);
    }

    RayProbe& eye(entt::entity probe) {
        return registry.get<RayProbe>(probe);
    }

    void aim(entt::entity probe, Vector3 origin, Vector3 direction, float max_distance = 6.0F) {
        auto& ray        = eye(probe);
        ray.origin       = origin;
        ray.direction    = direction;
        ray.max_distance = max_distance;
    }
};

Catch::Approx near(float value) {
    return Catch::Approx(value).margin(1e-4);
}

RayHit cast(Vector3 origin, Vector3 direction, float max_distance, Vector3 center, Vector3 size) {
    return cactus_gen::stdlib::collision::volume::ray_box(origin, direction, max_distance, center, size);
}

constexpr Vector3 kCenter{.x = 0.0F, .y = 0.0F, .z = 0.0F};
constexpr Vector3 kUnit{.x = 1.0F, .y = 1.0F, .z = 1.0F};

}  // namespace

// ── Pure ray/box geometry ───────────────────────────────────────────────────

TEST_CASE("ray_box hits the struck face with an outward normal", "[runtime][codegen-entt][stdlib-collision]") {
    const auto hit = cast(Vector3{.x = -3.0F, .y = 0.2F, .z = 0.1F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(hit.hit);
    CHECK(hit.distance == near(2.5F));
    CHECK(hit.point.x == near(-0.5F));
    CHECK(hit.point.y == near(0.2F));
    CHECK(hit.normal.x == -1.0F);
    CHECK(hit.normal.y == 0.0F);

    const auto top = cast(Vector3{.x = 0.1F, .y = 4.0F, .z = 0.0F}, Vector3{.x = 0.0F, .y = -2.0F, .z = 0.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(top.hit);
    CHECK(top.distance == near(3.5F));
    CHECK(top.normal.y == 1.0F);
}

TEST_CASE("ray_box normalizes scaled directions to world distance", "[runtime][codegen-entt][stdlib-collision]") {
    const auto slow = cast(Vector3{.x = 0.0F, .y = 0.0F, .z = 5.0F}, Vector3{.x = 0.0F, .y = 0.0F, .z = -0.01F}, 10.0F, kCenter, kUnit);
    const auto fast = cast(Vector3{.x = 0.0F, .y = 0.0F, .z = 5.0F}, Vector3{.x = 0.0F, .y = 0.0F, .z = -50.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(slow.hit);
    REQUIRE(fast.hit);
    CHECK(slow.distance == near(4.5F));
    CHECK(fast.distance == near(4.5F));
    CHECK(slow.normal.z == 1.0F);
}

TEST_CASE("ray_box misses a ray that passes near the centre but outside the box",
          "[runtime][codegen-entt][stdlib-collision]") {
    CHECK_FALSE(cast(Vector3{.x = -3.0F, .y = 0.51F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit).hit);
    CHECK_FALSE(cast(Vector3{.x = 3.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit).hit);
}

TEST_CASE("ray_box breaks edge and corner ties X, then Y, then Z", "[runtime][codegen-entt][stdlib-collision]") {
    const auto edge = cast(Vector3{.x = -1.5F, .y = 1.5F, .z = 0.0F}, Vector3{.x = 1.0F, .y = -1.0F, .z = 0.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(edge.hit);
    CHECK(edge.normal.x == -1.0F);
    CHECK(edge.normal.y == 0.0F);
    const auto corner = cast(Vector3{.x = 1.5F, .y = 1.5F, .z = 1.5F}, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(corner.hit);
    CHECK(corner.normal.x == 1.0F);
    const auto yz = cast(Vector3{.x = 0.0F, .y = 1.5F, .z = 1.5F}, Vector3{.x = 0.0F, .y = -1.0F, .z = -1.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(yz.hit);
    CHECK(yz.normal.y == 1.0F);
}

TEST_CASE("ray_box starting inside, on a face, or at the limit", "[runtime][codegen-entt][stdlib-collision]") {
    const auto inside = cast(Vector3{.x = 0.1F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(inside.hit);
    CHECK(inside.distance == 0.0F);
    CHECK(inside.normal.x == 0.0F);
    CHECK(inside.normal.y == 0.0F);
    CHECK(inside.normal.z == 0.0F);

    const auto entering = cast(Vector3{.x = -0.5F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(entering.hit);
    CHECK(entering.distance == 0.0F);
    CHECK(entering.normal.x == -1.0F);
    CHECK_FALSE(cast(Vector3{.x = -0.5F, .y = 0.0F, .z = 0.0F}, Vector3{.x = -1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit).hit);

    CHECK(cast(Vector3{.x = -2.5F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 2.0F, kCenter, kUnit).hit);
    CHECK_FALSE(cast(Vector3{.x = -2.5F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 1.999F, kCenter, kUnit).hit);
}

TEST_CASE("ray_box grazing a face counts as inside that slab", "[runtime][codegen-entt][stdlib-collision]") {
    const auto graze = cast(Vector3{.x = -3.0F, .y = 0.5F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit);
    REQUIRE(graze.hit);
    CHECK(graze.distance == near(2.5F));
    CHECK(graze.normal.x == -1.0F);
}

TEST_CASE("ray_box is total for invalid geometry", "[runtime][codegen-entt][stdlib-collision]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const Vector3 from{.x = -3.0F, .y = 0.0F, .z = 0.0F};
    const Vector3 east{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    CHECK_FALSE(cast(from, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit).hit);
    CHECK_FALSE(cast(from, east, 10.0F, kCenter, Vector3{.x = 1.0F, .y = 0.0F, .z = 1.0F}).hit);
    CHECK_FALSE(cast(from, east, 0.0F, kCenter, kUnit).hit);
    CHECK_FALSE(cast(from, east, -1.0F, kCenter, kUnit).hit);
    CHECK_FALSE(cast(from, east, nan, kCenter, kUnit).hit);
    CHECK_FALSE(cast(Vector3{.x = nan, .y = 0.0F, .z = 0.0F}, east, 10.0F, kCenter, kUnit).hit);
    CHECK_FALSE(cast(from, Vector3{.x = inf, .y = 0.0F, .z = 0.0F}, 10.0F, kCenter, kUnit).hit);
    const auto negative = cast(from, east, 10.0F, kCenter, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F});
    REQUIRE(negative.hit);
    CHECK(negative.distance == near(2.5F));
}

// ── Cells are ordinary entities ─────────────────────────────────────────────

TEST_CASE("a volume fills in batches with one cell per coordinate", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    const auto terrain = slots().voxel_volume.Terrain;
    world.frame();
    CHECK(world.registry.get<Volume>(terrain).cells_built == 16);
    world.fill();
    CHECK(world.registry.get<Volume>(terrain).cells_built == 48);
    // The volume's 48 cells, plus StrayCell.
    CHECK(world.cell_count(terrain) == 49);
    for (int x = 0; x <= 3; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = 0; z <= 3; ++z) {
                CHECK(world.registry.valid(world.cell_at(terrain, x, y, z)));
            }
        }
    }
    const auto cell = world.cell_at(terrain, 2, -1, 3);
    const auto& place = world.registry.get<cactus_gen::stdlib::transform::volume::WorldTransform>(cell);
    CHECK(place.position.x == near(12.5F));
    CHECK(place.position.y == near(-0.5F));
    CHECK(place.position.z == near(3.5F));
    CHECK(world.registry.get<cactus_gen::stdlib::physics::volume::BoxCollider>(cell).size.x == near(1.0F));
}

TEST_CASE("air cells exist without Solid and materials follow on added Cell",
          "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    const auto stone   = world.cell_at(terrain, 1, -1, 1);
    const auto wood    = world.cell_at(terrain, 1, 0, 1);
    const auto air     = world.cell_at(terrain, 3, 0, 1);
    CHECK(world.solid(stone));
    CHECK(world.registry.get<Cell>(stone).texture == Stone);
    CHECK(world.solid(wood));
    CHECK(world.registry.get<Cell>(wood).texture == Wood);
    CHECK_FALSE(world.solid(air));
}

TEST_CASE("an invalid volume creates no cells and reports why", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto broken = slots().voxel_volume.Broken;
    CHECK(world.cell_count(broken) == 0);
    CHECK(world.registry.get<Volume>(broken).issue == cactus_gen::stdlib::voxel::volume::VolumeIssue::BadCellSize);
    CHECK(world.registry.get<Volume>(slots().voxel_volume.Terrain).issue ==
          cactus_gen::stdlib::voxel::volume::VolumeIssue::Valid);
}

TEST_CASE("bounds too far apart for an int extent are too many cells", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    const Vector3 origin{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    CHECK(cactus_gen::stdlib::voxel::volume::volume_issue(origin, 1.0F, -2147483647 - 1, 0, 0, 2147483647, 0, 0) ==
          cactus_gen::stdlib::voxel::volume::VolumeIssue::TooManyCells);
    CHECK(cactus_gen::stdlib::voxel::volume::volume_issue(origin, 1.0F, 0, 0, 0, 3, 2, 3) ==
          cactus_gen::stdlib::voxel::volume::VolumeIssue::Valid);
}

TEST_CASE("cell probes find air and solid cells, or nothing", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    auto& probe        = world.registry.get<CellProbe>(slots().voxel_volume.Picker);
    probe.x = 3;
    probe.y = 1;
    probe.z = 2;
    world.frame();
    CHECK(probe.found);
    CHECK(probe.cell == world.cell_at(terrain, 3, 1, 2));

    probe.x = 9;
    world.frame();
    CHECK_FALSE(probe.found);
    CHECK_FALSE(world.registry.valid(probe.cell));

    probe.x       = 0;
    probe.enabled = false;
    world.frame();
    CHECK_FALSE(probe.found);
}

TEST_CASE("a duplicate cell is reported and the earliest one is chosen", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    auto& probe        = world.registry.get<CellProbe>(slots().voxel_volume.Picker);
    probe.x = 2;
    probe.y = 1;
    probe.z = 2;
    CHECK_FALSE(world.registry.get<Volume>(terrain).duplicate_found);
    world.frame();
    CHECK(probe.cell == slots().voxel_volume.StrayCell);
    CHECK(world.registry.get<Volume>(terrain).duplicate_found);
}

// ── Ray probes ──────────────────────────────────────────────────────────────

TEST_CASE("a ray probe names the struck cell, face and adjacent coordinate", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    const auto eye     = slots().voxel_volume.Eye;
    world.aim(eye, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = -1.0F, .z = 0.0F});
    world.frame();
    const auto& ray = world.eye(eye);
    REQUIRE(ray.hit);
    CHECK(ray.cell == world.cell_at(terrain, 1, 0, 1));
    CHECK(ray.x == 1);
    CHECK(ray.y == 0);
    CHECK(ray.z == 1);
    CHECK(ray.distance == near(2.0F));
    CHECK(ray.normal.y == 1.0F);
    CHECK(ray.has_adjacent);
    CHECK(ray.adjacent_x == 1);
    CHECK(ray.adjacent_y == 1);
    CHECK(ray.adjacent_z == 1);
}

TEST_CASE("an identity join folds only a live cell its handle names", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    auto& lookup       = world.registry.get<cactus_gen::voxel_volume::CellLookup>(slots().voxel_volume.Lookup);
    const auto air     = world.cell_at(terrain, 3, 0, 2);
    lookup.target      = air;
    world.frame();
    CHECK(lookup.x == 3);
    CHECK(lookup.z == 2);

    lookup.target = terrain;
    world.frame();
    CHECK(lookup.x == -1);
    CHECK(lookup.z == -1);

    lookup.target = air;
    world.frame();
    REQUIRE(lookup.x == 3);
    world.registry.destroy(air);
    world.frame();
    CHECK(lookup.x == -1);
    CHECK(lookup.z == -1);
}

TEST_CASE("a ray probe places against a positive-X face", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto eye = slots().voxel_volume.Eye;
    world.aim(eye, Vector3{.x = 13.9F, .y = 0.5F, .z = 2.5F}, Vector3{.x = -1.0F, .y = 0.0F, .z = 0.0F});
    world.frame();
    const auto& ray = world.eye(eye);
    REQUIRE(ray.hit);
    CHECK(ray.x == 1);
    CHECK(ray.normal.x == 1.0F);
    CHECK(ray.adjacent_x == 2);
    CHECK(ray.adjacent_y == 0);
}

TEST_CASE("a ray near a cell centre but outside it is not a hit", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto eye = slots().voxel_volume.Eye;
    // Over the air at x = 3 (the solid wood ends at x = 2), skimming 0.01
    // above the wood top, then too short to reach the stone.
    world.aim(eye, Vector3{.x = 9.0F, .y = 1.01F, .z = 1.5F}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 10.0F);
    world.frame();
    CHECK_FALSE(world.eye(eye).hit);
}

TEST_CASE("a miss replaces an earlier hit", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto eye = slots().voxel_volume.Eye;
    world.aim(eye, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = -1.0F, .z = 0.0F});
    world.frame();
    REQUIRE(world.eye(eye).hit);
    world.aim(eye, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
    world.frame();
    CHECK_FALSE(world.eye(eye).hit);
    CHECK_FALSE(world.registry.valid(world.eye(eye).cell));
    CHECK_FALSE(world.eye(eye).has_adjacent);

    world.aim(eye, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = -1.0F, .z = 0.0F}, 0.0F);
    world.frame();
    CHECK_FALSE(world.eye(eye).hit);
    world.aim(eye, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    world.frame();
    CHECK_FALSE(world.eye(eye).hit);
}

TEST_CASE("a ray from inside a solid cell hits it at zero with no adjacent cell",
          "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto eye = slots().voxel_volume.Eye;
    world.aim(eye, Vector3{.x = 10.5F, .y = -0.5F, .z = 0.5F}, Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
    world.frame();
    const auto& ray = world.eye(eye);
    REQUIRE(ray.hit);
    CHECK(ray.distance == 0.0F);
    CHECK_FALSE(ray.has_adjacent);
    CHECK(ray.x == 0);
    CHECK(ray.y == -1);
}

TEST_CASE("ray probes handle negative coordinates and scaled cells", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto eye = slots().voxel_volume.FineEye;
    // Fine's cell (-2, 0, -1) spans x [-6, -5.5], y [0, 0.5], z [19.5, 20].
    world.aim(eye, Vector3{.x = -5.75F, .y = 2.0F, .z = 19.75F}, Vector3{.x = 0.0F, .y = -3.0F, .z = 0.0F});
    world.frame();
    const auto& ray = world.eye(eye);
    REQUIRE(ray.hit);
    CHECK(ray.x == -2);
    CHECK(ray.y == 0);
    CHECK(ray.z == -1);
    CHECK(ray.distance == near(1.5F));
    CHECK(ray.adjacent_y == 1);
}

TEST_CASE("a consumer after the probes sees this pass's result, and the dig shows next pass",
          "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    const auto shovel  = slots().voxel_volume.Shovel;
    world.aim(shovel, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = -1.0F, .z = 0.0F});
    world.registry.get<cactus_gen::voxel_volume::Digger>(shovel).armed = true;
    world.frame();
    const auto& digger = world.registry.get<cactus_gen::voxel_volume::Digger>(shovel);
    CHECK(digger.saw_x == 1);
    CHECK(digger.saw_y == 0);
    CHECK(digger.saw_z == 1);
    CHECK_FALSE(world.solid(world.cell_at(terrain, 1, 0, 1)));
    // The probe result of that pass still names the dug cell.
    CHECK(world.eye(shovel).y == 0);
    world.frame();
    CHECK(world.eye(shovel).y == -1);
    CHECK(world.eye(shovel).normal.y == 1.0F);
}

TEST_CASE("destroyed cells drop out of later probe passes", "[runtime][codegen-entt][stdlib-voxel-volume]") {
    World world;
    world.fill();
    const auto terrain = slots().voxel_volume.Terrain;
    const auto eye     = slots().voxel_volume.Eye;
    world.registry.destroy(world.cell_at(terrain, 1, 0, 1));
    world.aim(eye, Vector3{.x = 11.5F, .y = 3.0F, .z = 1.5F}, Vector3{.x = 0.0F, .y = -1.0F, .z = 0.0F});
    world.frame();
    REQUIRE(world.eye(eye).hit);
    CHECK(world.eye(eye).y == -1);
}
// ── Persistence ─────────────────────────────────────────────────────────────

TEST_CASE("a restore brings back saved occupancy and textures without refilling",
          "[runtime][codegen-entt][stdlib-voxel-volume][persistence]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    cactus_test::ScopedPersistenceAdapter scoped(memory.as_adapter());
    World world;
    world.fill();
    auto& registry     = world.registry;
    const auto terrain = slots().voxel_volume.Terrain;
    registry.remove<Solid>(world.cell_at(terrain, 1, 0, 1));
    registry.get<Cell>(world.cell_at(terrain, 0, 0, 0)).texture = Stone;
    registry.get<cactus_gen::voxel_volume::StorageControl>(slots().voxel_volume.Storage).save = true;
    world.frame(2);
    REQUIRE(registry.get<cactus_gen::voxel_volume::StorageControl>(slots().voxel_volume.Storage).failure.empty());
    REQUIRE(registry.get<cactus_gen::voxel_volume::StorageControl>(slots().voxel_volume.Storage).saved);

    registry.emplace<Solid>(world.cell_at(terrain, 1, 0, 1));
    registry.emplace<Solid>(world.cell_at(terrain, 3, 1, 3));
    registry.get<Cell>(world.cell_at(terrain, 0, 0, 0)).texture = Wood;
    registry.get<cactus_gen::voxel_volume::StorageControl>(slots().voxel_volume.Storage).restore = true;
    const auto storage = slots().voxel_volume.Storage;
    world.frame(3);
    if (registry.valid(storage) && registry.all_of<cactus_gen::voxel_volume::StorageControl>(storage)) {
        INFO(registry.get<cactus_gen::voxel_volume::StorageControl>(storage).failure);
        CHECK(registry.get<cactus_gen::voxel_volume::StorageControl>(storage).failure.empty());
    }

    entt::entity restored = entt::null;
    for (const auto [entity, volume] : registry.view<Volume>().each()) {
        if (volume.min_y == -1) {
            restored = entity;
        }
    }
    REQUIRE(registry.valid(restored));
    CHECK(registry.get<Volume>(restored).cells_built == 48);
    CHECK(world.cell_count(restored) == 49);
    CHECK_FALSE(world.solid(world.cell_at(restored, 1, 0, 1)));
    CHECK_FALSE(world.solid(world.cell_at(restored, 3, 1, 3)));
    CHECK(world.solid(world.cell_at(restored, 0, 0, 0)));
    CHECK(registry.get<Cell>(world.cell_at(restored, 0, 0, 0)).texture == Stone);
    CHECK(world.solid(world.cell_at(restored, 1, -1, 1)));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
