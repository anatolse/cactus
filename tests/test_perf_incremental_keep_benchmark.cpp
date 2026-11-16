// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "perf_benchmark_support.hpp"

namespace {

namespace game = cactus_gen::perf_incremental_keep;

constexpr int kMovers = 4096;
constexpr int kMoving = 16;
// Per moving mover: its own row in the unary keep, and its lane's four posts in the pair keep.
constexpr std::uint64_t kCandidatesPerMover = 5;

struct World {
    entt::registry registry;

    World() {
        cactus_perf::load_sized(registry, [](entt::registry&) {});
        tick();
    }

    game::Board& board() {
        return registry.get<game::Board>(cactus_gen::detail::generated_named_slots().perf_incremental_keep.Field);
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    [[nodiscard]] std::size_t near_rows() {
        return cactus::runtime::entt_backend::relation_store<game::Near>(registry).size();
    }
};

}  // namespace

TEST_CASE("perf: keeps whose inputs did not change evaluate no rows", "[perf][incremental-keep]") {
    World world;
    REQUIRE(world.near_rows() == static_cast<std::size_t>(kMovers));
    REQUIRE(world.registry.view<game::Even>().size() == static_cast<std::size_t>(kMovers));
    REQUIRE(world.registry.view<game::Posted>().size() == 1);
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(work.keep_runs == 0);
    CHECK(work.candidates == 0);
    CHECK(work.commands == 0);
}

TEST_CASE("perf: keeps evaluate rows in proportion to the movers", "[perf][incremental-keep]") {
    World world;
    world.board().moving = kMoving;
    const auto work = cactus_perf::work_of([&] { world.tick(); });
    CHECK(world.near_rows() == static_cast<std::size_t>(kMovers));
    CHECK(world.registry.view<game::Even>().size() == static_cast<std::size_t>(kMovers - kMoving));
    CHECK(world.registry.view<game::Posted>().size() == 1);
    // The derived-relation keep reads nothing that moved, so only the unary and pair keeps run.
    CHECK(work.keep_runs == 2);
    CHECK(work.candidates <= kCandidatesPerMover * kMoving);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
