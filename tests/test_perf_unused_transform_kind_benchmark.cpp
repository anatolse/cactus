// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

#include <catch2/benchmark/catch_benchmark.hpp>

namespace {

namespace volume = cactus_gen::stdlib::transform::volume;

constexpr int kSize       = 20000;
constexpr int kDoubleSize = 40000;

// Limits at kSize. Work counts are exact; times are O3 measurements x 1.5.
// The volume propagation looks at each world once and each child once more.
constexpr std::uint64_t kNodesPerFrame = (2 * kSize) + 1;
constexpr cactus_perf::TimeLimits kTimes{.base_ms = 4.5, .growth = 3.0};

struct World {
    entt::registry registry;

    explicit World(int size) {
        cactus_headless_test::init_and_load(registry);
        const auto root = cactus_gen::detail::generated_named_slots().perf_unused_transform_kind.Root;
        for (int child = 0; child < size; ++child) {
            const auto entity = registry.create();
            registry.emplace<cactus::runtime::entt_backend::CreationOrdinal>(
                entity,
                cactus::runtime::entt_backend::CreationOrdinal{
                    .value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
            registry.emplace<volume::LocalTransform>(entity);
            registry.emplace<volume::WorldTransform>(entity);
            registry.emplace<cactus_gen::stdlib::core::Parent>(entity, cactus_gen::stdlib::core::Parent{.parent = root});
        }
        tick();
    }

    void tick() { cactus_headless_test::drive_frame(registry); }
};

}  // namespace

TEST_CASE("perf: a linked but unused transform kind visits no node", "[perf][transform]") {
    World world(kSize);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.propagated_nodes == kNodesPerFrame);
    const auto child = world.registry.view<cactus_gen::stdlib::core::Parent>().front();
    CHECK(world.registry.get<volume::WorldTransform>(child).position.x == 1.0F);
}

TEST_CASE("perf: a volume hierarchy next to an unused flat kind stays within its frame time",
          "[perf][transform][time]") {
    cactus_perf::skip_unless_timed();
    World base(kSize);
    World twice(kDoubleSize);
    cactus_perf::require_frame_times([&] { base.tick(); }, [&] { twice.tick(); }, kTimes);
    BENCHMARK("volume hierarchy frame, 20000 children") {
        base.tick();
    };
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
