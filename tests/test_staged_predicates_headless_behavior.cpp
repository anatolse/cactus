// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
// Built twice: with clause expressions at their recorded stage, and with
// CACTUS_FORCE_ROW_STAGE, where every predicate runs per row. Both builds must
// meet the same expectations.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

void start(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
}

template <typename Trait>
Trait& single(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    REQUIRE(view.begin() != view.end());
    return registry.get<Trait>(*view.begin());
}

const cactus_gen::staged_predicates::Recorder& run_one_frame(entt::registry& registry) {
    start(registry);
    drive_frame(registry);
    return single<cactus_gen::staged_predicates::Recorder>(registry);
}

// Lava tag -> the field `read` picks.
template <typename Read>
std::map<int, int> per_lava(entt::registry& registry, Read read) {
    std::map<int, int> values;
    for (const auto entity : registry.view<cactus_gen::staged_predicates::Lava>()) {
        const auto& lava = registry.get<cactus_gen::staged_predicates::Lava>(entity);
        values[lava.tag] = read(registry, entity, lava);
    }
    return values;
}

}  // namespace

TEST_CASE("one-sided predicates and a true gate keep the tuples and their order", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    // Bodies with x > 0 (tags 1, 3) times active lava (tags 1, 3), left-major.
    CHECK(recorder.one_sided == 11133133);
}

TEST_CASE("a join predicate runs only over tuples that pass the one-sided filters", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.join == 112123);
}

TEST_CASE("a false pass gate runs no tuple", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.gated == 0);
    CHECK(recorder.unary_gated == 0);
}

TEST_CASE("binding-stage sort keys keep order and limit slots", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    // x desc, then heat asc, over active lava; the limit keeps the first three.
    CHECK(recorder.limited == 333113);
    CHECK(recorder.ordered == 112321);
}

TEST_CASE("a global reduction behind a false gate runs once with identities", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.global_closed == 1);
    CHECK(recorder.global_open == 408);
}

TEST_CASE("grouped reductions keep their groups and aggregates", "[runtime][staging]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto hits = per_lava(
        registry,
        [](entt::registry&, entt::entity, const cactus_gen::staged_predicates::Lava& lava) { return lava.hits; });
    CHECK(hits == std::map<int, int>{{1, 101}, {2, -1}, {3, 0}});
    const auto gated = per_lava(
        registry,
        [](entt::registry&, entt::entity, const cactus_gen::staged_predicates::Lava& lava) { return lava.gated; });
    CHECK(gated == std::map<int, int>{{1, 10}, {2, -1}, {3, 10}});
}

TEST_CASE("a keep rule whose gate turns false removes every kept trait", "[runtime][staging]") {
    entt::registry registry;
    run_one_frame(registry);
    const auto kept = [](entt::registry& reg, entt::entity entity, const cactus_gen::staged_predicates::Lava&) {
        const auto* hot = reg.try_get<cactus_gen::staged_predicates::Hot>(entity);
        return hot == nullptr ? -1 : hot->count;
    };
    CHECK(per_lava(registry, kept) == std::map<int, int>{{1, 2}, {2, 2}, {3, 2}});

    single<cactus_gen::staged_predicates::Match>(registry).keeping = false;
    drive_frame(registry);
    CHECK(per_lava(registry, kept) == std::map<int, int>{{1, -1}, {2, -1}, {3, -1}});
    CHECK(single<cactus_gen::staged_predicates::Tally>(registry).cooled == 3);
}

TEST_CASE("a targeted pass keeps only tuples incident to the recipient", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.targeted == 1113);
}

TEST_CASE("self-tuples survive binding filters", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.self_tuples == 11123132);
}

TEST_CASE("a predicate on a field the pass writes sees live values", "[runtime][staging]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    // Budget 2: the first two bodies in creation order spend it, the third is rejected.
    CHECK(recorder.budget == 12);
    CHECK(single<cactus_gen::staged_predicates::Budget>(registry).left == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
