// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#include "heap_allocation_counter.hpp"

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

namespace world = cactus_gen::persistence_capture_scaling_runtime;

struct Capture {
    std::size_t allocations = 0;
    std::size_t entities    = 0;
    std::size_t bytes       = 0;
};

Capture capture_cells(int cells) {
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    for (int index = 0; index < cells; ++index) {
        const auto entity = cactus_gen::detail::persistence_capture_scaling_runtime::create_block_t(registry);
        auto& block       = registry.get<world::Block>(entity);
        block.material    = index % 3 == 0 ? world::Material::Grass : world::Material::Stone;
        block.lit         = index % 2 == 0;
        block.offset      = Vector3{.x = static_cast<float>(index), .y = 1.0F, .z = 2.0F};
    }

    std::size_t allocations = 0;
    const auto snapshot     = [&] {
        const cactus_test::AllocationCount counting;
        auto captured = cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
        allocations   = cactus_test::AllocationCount::count();
        return captured;
    }();

    const auto bytes = snapshot.arena().size() + snapshot.entity_rows().size_bytes() + snapshot.trait_rows().size_bytes();
    return Capture{.allocations = allocations, .entities = snapshot.entity_count(), .bytes = bytes};
}

}  // namespace

TEST_CASE("capture allocations do not grow with the number of recorded values",
          "[runtime][persistence][capture][memory]") {
    const auto small = capture_cells(100);
    const auto large = capture_cells(20000);
    REQUIRE(small.entities == 100);
    REQUIRE(large.entities == 20000);

    INFO("small world: " << small.allocations << " allocations, large world: " << large.allocations);
    CHECK(large.allocations <= small.allocations + 8);
    CHECK(large.allocations < 1000);
}

TEST_CASE("a captured cell takes a bounded number of bytes", "[runtime][persistence][capture][memory]") {
    const auto large = capture_cells(20000);
    CHECK(large.bytes / large.entities <= 256);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
