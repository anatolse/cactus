// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

using cactus_gen::trait_parameters::Recorder;

const Recorder& run_one_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(
        cactus_gen::stdlib::core::frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
    const auto view = registry.view<Recorder>();
    REQUIRE(view.begin() != view.end());
    return registry.get<Recorder>(*view.begin());
}

}  // namespace

TEST_CASE("an alias passed in where: keeps the same entities as the inline read", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.low_passed == 2);
    CHECK(recorder.low_passed == recorder.low_inline);
}

TEST_CASE("a pair alias and a bracketed binding read the same tuples", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.pair_passed == 3);
    CHECK(recorder.pair_passed == recorder.pair_inline);
}

TEST_CASE("a named read passes the named entity's trait", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.named_passed == 1);
    CHECK(recorder.named_passed == recorder.named_inline);
}

TEST_CASE("a projected trait is passed from where it was projected this frame", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.glow_passed == 2);
    CHECK(recorder.glow_passed == recorder.glow_inline);
}

TEST_CASE("an event alias passes the triggering event", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.crit_passed == 1);
    CHECK(recorder.crit_passed == recorder.crit_inline);
}

TEST_CASE("the callee sees the handler's own earlier write", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.written_passed == 1);
    CHECK(recorder.written_passed == recorder.written_inline);
}

TEST_CASE("a parameter passed on reads the same fields", "[runtime][trait-parameters]") {
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.visible_passed == 1);
    CHECK(recorder.visible_passed == recorder.visible_inline);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
