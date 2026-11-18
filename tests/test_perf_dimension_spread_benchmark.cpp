// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <cstdint>

namespace {

namespace game = cactus_gen::perf_dimension_spread;

constexpr std::uint64_t kCells   = 512 * 512;
constexpr int kPiles             = 1000;
constexpr int kWarmupTicks       = 40;
// Spread visits each stored cell and its four neighbours, Cool the stored cells; Ripple does both
// twice and Settle once more. Each bound adds the source cell a pass may store anew.
constexpr std::uint64_t kRowsPerStoredCell = 6;

struct World {
    entt::registry registry;

    World() {
        cactus_headless_test::init_and_load(registry);
        auto& field = registry.get<game::Field>(cactus_gen::detail::generated_named_slots().perf_dimension_spread.Land);
        for (std::uint64_t cell = 0; cell < kCells; ++cell) {
            field.stone.set(cell, 1);
        }
        for (int pile = 0; pile < kPiles; ++pile) {
            const auto entity = registry.create();
            registry.emplace<game::Pile>(entity);
            registry.emplace<cactus::runtime::entt_backend::CreationOrdinal>(
                entity,
                cactus::runtime::entt_backend::CreationOrdinal{
                    .value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
        }
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::uint64_t stored() {
        const auto land = cactus_gen::detail::generated_named_slots().perf_dimension_spread.Land;
        return registry.get<game::Field>(land).heat.stored_count() + registry.get<game::Wave>(land).heat.stored_count();
    }
};

}  // namespace

TEST_CASE("rules over cells visit the cells that hold heat, not the whole table", "[perf][trait-dimensions]") {
    World world;
    for (int tick = 0; tick < kWarmupTicks; ++tick) {
        world.tick();
    }
    const auto before = world.stored();
    REQUIRE(before > 100);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    const auto after = world.stored();
    INFO("stored before " << before << ", after " << after << ", rows " << work.cell_rows);
    CHECK(work.cell_rows > 0);
    // Neither the 262144 stone cells nor the 1000 untouched piles add rows.
    CHECK(work.cell_rows <= kRowsPerStoredCell * (std::max(before, after) + 2));
    CHECK(work.cell_rows < kCells / 8);
}

TEST_CASE("each pass of a repeating rule over cells follows that pass's stored cells", "[perf][trait-dimensions]") {
    World world;
    world.tick();
    const auto land  = cactus_gen::detail::generated_named_slots().perf_dimension_spread.Land;
    const auto& wave = world.registry.get<game::Wave>(land);
    const auto& field = world.registry.get<game::Field>(land);
    // One activation: Field spread once, Wave spread twice from one source cell.
    CHECK(field.heat.stored_count() == 5);
    CHECK(wave.heat.stored_count() == 13);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
