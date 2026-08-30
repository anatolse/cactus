// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <vector>

namespace {

void drive_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
}

const pairs_order_by_runtime__Recorder& run_one_frame(entt::registry& registry) {
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);
    drive_frame(registry);

    const auto view = registry.view<pairs_order_by_runtime__Recorder>();
    REQUIRE(view.begin() != view.end());
    return registry.get<pairs_order_by_runtime__Recorder>(*view.begin());
}

}  // namespace

TEST_CASE("unary rule ordered by a computed expression iterates in computed-key order",
          "[runtime][codegen-entt][rule-order-by]") {
    // Scores 1/4/7 (tags 1/2/3) have squared distances from 5 of 16/1/4, so
    // ascending computed penalty visits tag2, then tag3, then tag1. Reading
    // the sort key as a bare `score` field instead of evaluating the call
    // would produce 123, and leaving the pass unsorted would produce creation
    // order 123 as well — so 231 only appears if the expression is genuinely
    // evaluated per entity and the result actually drives iteration.
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.unary_sequence == 231);
}

TEST_CASE("unary sort output does not depend on which filter trait is declared first",
          "[runtime][codegen-entt][rule-order-by]") {
    // RankByPenaltySwapped declares the same two filter traits in the opposite
    // order, so codegen picks a different physical sort anchor. The anchor is
    // an implementation detail of which component pool gets permuted; it must
    // not change the observable iteration order.
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.swapped_sequence == recorder.unary_sequence);
    CHECK(recorder.swapped_sequence == 231);
}

TEST_CASE("unary sort is safe when the anchor trait's pool has entities outside the full filter",
          "[runtime][codegen-entt][rule-order-by]") {
    // WeightOnly has Weight (the anchor RankByWeight's sort physically
    // permutes) but not Tint, so it sits in the anchor pool without
    // satisfying the rule's full filter. Reaching this frame at all (rather
    // than crashing on a registry.get<Tint> of a component WeightOnly
    // doesn't have) is the regression this test guards; the fingerprint
    // itself just confirms the three filter-satisfying entities still sort
    // correctly with that extra, filtered-out entity present in the pool.
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.weight_sequence == 231);
}

TEST_CASE("pairs rule ordered by a cross-binding computed expression executes tuples in that order",
          "[runtime][codegen-entt][rule-order-by][pair-relations]") {
    // The 6 non-self tuples, keyed by actor.score - target.score descending:
    //   (3,1) key  6 -> digit 6
    //   (2,1) key  3 -> digit 3
    //   (3,2) key  3 -> digit 7
    //   (1,2) key -3 -> digit 1
    //   (2,3) key -3 -> digit 5
    //   (1,3) key -6 -> digit 2
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);
    CHECK(recorder.pair_sequence == 637152);
}

TEST_CASE("pair tuples with equal sort keys keep left-binding-major creation order",
          "[runtime][codegen-entt][rule-order-by][pair-relations]") {
    // Two key values are tied above: key 3 is shared by tuples (2,1)/(3,2) and
    // key -3 by (1,2)/(2,3). A stable sort leaves each tied group in the pair
    // pass's own left-binding-major creation order, so digit 3 precedes 7 and
    // digit 1 precedes 5. An unstable sort is free to swap either pair, which
    // would yield 673152, 637512, or 673512 instead.
    entt::registry registry;
    const auto& recorder = run_one_frame(registry);

    const auto digits = [](int value) {
        std::vector<int> out;
        for (; value > 0; value /= 10) {
            out.insert(out.begin(), value % 10);
        }
        return out;
    };
    const auto sequence = digits(recorder.pair_sequence);
    REQUIRE(sequence.size() == 6);

    const auto position_of = [&sequence](int digit) {
        return std::distance(sequence.begin(), std::ranges::find(sequence, digit));
    };
    CHECK(position_of(3) < position_of(7));
    CHECK(position_of(1) < position_of(5));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
