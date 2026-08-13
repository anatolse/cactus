// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace {

using CreationOrdinal = cactus::runtime::entt_backend::CreationOrdinal;

entt::entity create_runtime_entity(entt::registry& registry) {
    const auto entity = registry.create();
    registry.emplace<CreationOrdinal>(
        entity, CreationOrdinal{.value = cactus::runtime::entt_backend::generated_next_creation_ordinal()});
    return entity;
}

entt::entity create_node(entt::registry& registry) {
    const auto entity = create_runtime_entity(registry);
    registry.emplace<hierarchy_query_runtime__Node>(entity);
    return entity;
}

void set_parent(entt::registry& registry, entt::entity child, entt::entity parent) {
    registry.emplace_or_replace<std_core__Parent>(child, std_core__Parent{.parent = parent});
}

}  // namespace

TEST_CASE("hierarchy queries headless: filtered forests are stable, finite, and detached",
          "[runtime][query][hierarchy]") {
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_load_project(registry);

    const auto observer_view = registry.view<hierarchy_query_runtime__Results>();
    REQUIRE(observer_view.begin() != observer_view.end());
    const auto observer = *observer_view.begin();

    const auto root_a = create_node(registry);
    registry.emplace<hierarchy_query_runtime__RequestedRoot>(root_a);

    const auto child_b = create_node(registry);
    set_parent(registry, child_b, root_a);
    const auto grandchild_d = create_node(registry);
    set_parent(registry, grandchild_d, child_b);
    const auto child_c = create_node(registry);
    set_parent(registry, child_c, root_a);

    const auto hidden_parent = create_node(registry);
    registry.emplace<hierarchy_query_runtime__Hidden>(hidden_parent);
    set_parent(registry, hidden_parent, root_a);
    const auto filtered_root = create_node(registry);
    set_parent(registry, filtered_root, hidden_parent);

    const auto root_e  = create_node(registry);
    const auto child_f = create_node(registry);
    set_parent(registry, child_f, root_e);

    const auto stale_parent_root       = create_node(registry);
    const auto nonmatching_parent      = create_runtime_entity(registry);
    const auto nonmatching_parent_root = create_node(registry);
    set_parent(registry, nonmatching_parent_root, nonmatching_parent);

    const auto cycle_x = create_node(registry);
    const auto cycle_y = create_node(registry);
    const auto cycle_z = create_node(registry);
    set_parent(registry, cycle_x, cycle_z);
    set_parent(registry, cycle_y, cycle_x);
    set_parent(registry, cycle_z, cycle_y);

    const auto stale_parent = create_runtime_entity(registry);
    registry.destroy(stale_parent);
    set_parent(registry, stale_parent_root, stale_parent);

    auto& results     = registry.get<hierarchy_query_runtime__Results>(observer);
    results.requested = root_a;
    cactus_headless_test::drive_frame(registry);

    const std::vector<entt::entity> expected_direct{child_b, child_c};
    const std::vector<entt::entity> expected_preorder{
        root_a,
        child_b,
        grandchild_d,
        child_c,
        filtered_root,
        root_e,
        child_f,
        stale_parent_root,
        nonmatching_parent_root,
        cycle_x,
        cycle_y,
        cycle_z,
    };
    const std::vector<entt::entity> expected_postorder{
        grandchild_d,
        child_b,
        child_c,
        root_a,
        filtered_root,
        child_f,
        root_e,
        stale_parent_root,
        nonmatching_parent_root,
        cycle_z,
        cycle_y,
        cycle_x,
    };

    CHECK(results.direct == expected_direct);
    CHECK(results.preorder == expected_preorder);
    CHECK(results.postorder == expected_postorder);
    CHECK(std::ranges::count(results.preorder, cycle_x) == 1);
    CHECK(std::ranges::count(results.preorder, cycle_y) == 1);
    CHECK(std::ranges::count(results.preorder, cycle_z) == 1);
    CHECK(std::ranges::count(results.postorder, cycle_x) == 1);
    CHECK(std::ranges::count(results.postorder, cycle_y) == 1);
    CHECK(std::ranges::count(results.postorder, cycle_z) == 1);

    const auto detached_direct    = results.direct;
    const auto detached_preorder  = results.preorder;
    const auto detached_postorder = results.postorder;
    registry.destroy(grandchild_d);
    set_parent(registry, child_c, root_e);
    CHECK(results.direct == detached_direct);
    CHECK(results.preorder == detached_preorder);
    CHECK(results.postorder == detached_postorder);

    results.requested = stale_parent;
    cactus_headless_test::drive_frame(registry);
    CHECK(results.direct.empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)