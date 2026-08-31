// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

void run_one_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    drive_frame(registry);
}

const pairs_limit_runtime__Recorder& recorder_of(entt::registry& registry) {
    const auto view = registry.view<pairs_limit_runtime__Recorder>();
    REQUIRE(view.begin() != view.end());
    return registry.get<pairs_limit_runtime__Recorder>(*view.begin());
}

const pairs_limit_runtime__Actor& actor_of(entt::registry& registry) {
    const auto view = registry.view<pairs_limit_runtime__Actor>();
    REQUIRE(view.begin() != view.end());
    return registry.get<pairs_limit_runtime__Actor>(*view.begin());
}

}  // namespace

TEST_CASE("unary limit takes the first N of the ordered domain", "[runtime][codegen-entt][rule-limit]") {
    // Scores 3/1/2 (tags 1/2/3) sort ascending as tag2, tag3, tag1. Taking 2
    // yields 23: an unlimited pass would produce 231, and truncating an
    // unsorted pass would produce 12.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(recorder_of(registry).unary_sequence == 23);
}

TEST_CASE("unary limit without order by uses the domain's own iteration order",
          "[runtime][codegen-entt][rule-limit]") {
    // VisitAllMarked walks the same filter unrestricted, so the first digit of
    // its two-digit fingerprint is by definition the domain's first entity.
    // `limit: 1` must select exactly that one — comparing against the domain
    // itself rather than against a hardcoded direction of EnTT's view.
    entt::registry registry;
    run_one_frame(registry);
    const auto& recorder = recorder_of(registry);
    REQUIRE(recorder.mark_all_sequence > 9);
    CHECK(recorder.mark_sequence == recorder.mark_all_sequence / 10);
}

TEST_CASE("pairs global limit bounds total tuple activations", "[runtime][codegen-entt][rule-limit][pair-relations]") {
    // 3 Ranked entities produce 9 tuples in left-binding-major creation order;
    // `limit: 3` keeps (Alpha,Alpha), (Alpha,Beta), (Alpha,Gamma) => 123.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(recorder_of(registry).pair_sequence == 123);
}

TEST_CASE("per-binding limit retains N tuples for each distinct binding value",
          "[runtime][codegen-entt][rule-limit][pair-relations]") {
    // Each `lead` partition is resolved independently and keeps its own first
    // two tuples: Alpha 1,2 — Beta 4,5 — Gamma 7,8. A global `limit: 6` would
    // instead have produced 123456, all of them Alpha's and Beta's.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(recorder_of(registry).per_sequence == 124578);
}

TEST_CASE("a where:-rejected candidate cannot consume a limited slot",
          "[runtime][codegen-entt][rule-limit][where-clause]") {
    // Ceiling (top 12.0) ranks first by `height - top` = -2.0 but fails
    // `where: top <= height`. Truncating before the predicate would give it
    // the actor's single slot and leave ground at 0; the correct winner is
    // Ledge (code 2), the closest surface at or below the actor.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(actor_of(registry).ground == 2);
}

TEST_CASE("a provably-one per-binding limit writes straight through its pair binding",
          "[runtime][codegen-entt][rule-limit][pair-relations]") {
    // The write lands on the durable Actor component itself — no event, no
    // sentinel accumulator, no second rule — and survives the frame.
    entt::registry registry;
    run_one_frame(registry);
    CHECK(actor_of(registry).ground != 0);
    drive_frame(registry);
    CHECK(actor_of(registry).ground == 2);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
