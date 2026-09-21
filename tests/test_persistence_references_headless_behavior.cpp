// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iterator>
#include <vector>

namespace {

using cactus_headless_test::init_and_load;

using Rival  = persistence_references_runtime__Rival;
using Roster = persistence_references_runtime__Roster;

struct World {
    entt::registry registry;

    World() { init_and_load(registry); }
};

cactus::persistence::Snapshot capture(entt::registry& registry) {
    return cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
}

struct RivalPair {
    entt::entity entity;
    entt::entity target;
};

std::vector<RivalPair> rival_pairs(entt::registry& registry) {
    std::vector<RivalPair> pairs;
    for (const auto entity : registry.view<Rival>()) {
        pairs.push_back(RivalPair{.entity = entity, .target = registry.get<Rival>(entity).rival});
    }
    return pairs;
}

}  // namespace

TEST_CASE("a world snapshot records mutual references between two included entities",
          "[runtime][persistence][restore][references]") {
    World world;
    const auto snapshot = capture(world.registry);

    const auto records = snapshot.entities;
    REQUIRE(records.size() == 4);  // rival_a, rival_b, lonely, also_lonely (Prop is ineligible)

    int mutual_pairs = 0;
    int absent_count = 0;
    for (const auto& record : records) {
        const auto* rival = cactus::persistence::find_trait(record, "persistence_references_runtime.Rival");
        REQUIRE(rival != nullptr);
        const auto* field = cactus::persistence::find_field(rival->persisted, "rival");
        REQUIRE(field != nullptr);
        const auto ref = field->value.as_entity();
        if (ref.present) {
            ++mutual_pairs;
        } else {
            ++absent_count;
        }
    }
    CHECK(mutual_pairs == 2);
    CHECK(absent_count == 2);
}

TEST_CASE("restoring mutual references resolves each to the other's restored entity",
          "[runtime][persistence][restore][references]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);
    const auto outcome  = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto pairs = rival_pairs(registry);
    REQUIRE(pairs.size() == 4);

    std::vector<RivalPair> mutual;
    for (const auto& pair : pairs) {
        if (registry.valid(pair.target)) {
            mutual.push_back(pair);
        }
    }
    REQUIRE(mutual.size() == 2);
    CHECK(mutual[0].target == mutual[1].entity);
    CHECK(mutual[1].target == mutual[0].entity);
}

TEST_CASE("restoring a reference to an excluded entity restores stale and safe",
          "[runtime][persistence][restore][references]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);
    const auto outcome  = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto pairs = rival_pairs(registry);
    REQUIRE(pairs.size() == 4);

    std::vector<entt::entity> absent;
    for (const auto& pair : pairs) {
        if (!registry.valid(pair.target)) {
            absent.push_back(pair.target);
        }
    }
    REQUIRE(absent.size() == 2);
    // Repeated references to the same excluded target stay equal to each
    // other; registry.valid() being false is what the generated total
    // entity_id guards ("Cross-entity operations guarded by validity check")
    // branch on to make add/remove/destroy safe no-ops for this value.
    CHECK(absent[0] == absent[1]);
    CHECK_FALSE(registry.valid(absent[0]));
}

TEST_CASE("restoring an entity reference nested inside a list resolves to the restored entity",
          "[runtime][persistence][restore][references]") {
    World world;
    auto& registry = world.registry;

    const auto snapshot = capture(registry);
    const auto outcome  = cactus::runtime::entt_backend::generated_restore_world(registry, snapshot);
    REQUIRE(outcome.ok);

    const auto pairs = rival_pairs(registry);
    std::vector<RivalPair> mutual;
    for (const auto& pair : pairs) {
        if (registry.valid(pair.target)) {
            mutual.push_back(pair);
        }
    }
    REQUIRE(mutual.size() == 2);

    const auto view = registry.view<Roster>();
    REQUIRE(std::distance(view.begin(), view.end()) == 1);
    const auto& members = registry.get<Roster>(*view.begin()).members;
    REQUIRE(members.size() == 2);
    CHECK(registry.valid(members[0]));
    CHECK(registry.valid(members[1]));
    CHECK(members[0] != members[1]);
    // Both list elements are members of the restored mutual pair.
    CHECK(((members[0] == mutual[0].entity && members[1] == mutual[1].entity) ||
          (members[0] == mutual[1].entity && members[1] == mutual[0].entity)));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
