// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cactus::runtime::entt_backend;

TEST_CASE("DocumentIdMap: two included entities keep stable, present, distinct identities",
          "[runtime][persistence][document-id-map]") {
    entt::registry registry;
    const auto first  = registry.create();
    const auto second = registry.create();

    DocumentIdMap ids;
    const auto first_id  = ids.include(first);
    const auto second_id = ids.include(second);
    CHECK(first_id != second_id);

    const auto first_ref  = ids.reference(first);
    const auto second_ref = ids.reference(second);
    CHECK(first_ref.id == first_id);
    CHECK(first_ref.present);
    CHECK(second_ref.id == second_id);
    CHECK(second_ref.present);
}

TEST_CASE("DocumentIdMap: mutual references between included entities form a representable cycle",
          "[runtime][persistence][document-id-map]") {
    entt::registry registry;
    const auto first  = registry.create();
    const auto second = registry.create();

    DocumentIdMap ids;
    ids.include(first);
    ids.include(second);

    // Each entity's reference resolves to the other's own identity, and both
    // resolve as present, with no allocation-order requirement between them.
    const auto first_points_at_second = ids.reference(second);
    const auto second_points_at_first = ids.reference(first);
    CHECK(first_points_at_second.id == ids.reference(second).id);
    CHECK(second_points_at_first.id == ids.reference(first).id);
    CHECK(first_points_at_second.present);
    CHECK(second_points_at_first.present);
}

TEST_CASE("DocumentIdMap: a forward reference made before inclusion keeps its identity after inclusion",
          "[runtime][persistence][document-id-map]") {
    entt::registry registry;
    const auto later_entity = registry.create();

    DocumentIdMap ids;
    // Referenced before anything ever calls include() on it — as happens when
    // a field is captured for an entity whose eventual parent has not been
    // visited yet.
    const auto forward_ref = ids.reference(later_entity);
    CHECK_FALSE(forward_ref.present);

    const auto included_id = ids.include(later_entity);
    CHECK(included_id == forward_ref.id);

    const auto ref_after_inclusion = ids.reference(later_entity);
    CHECK(ref_after_inclusion.id == forward_ref.id);
    CHECK(ref_after_inclusion.present);
}

TEST_CASE("DocumentIdMap: repeated references to the same excluded target share one absent identity",
          "[runtime][persistence][document-id-map]") {
    entt::registry registry;
    const auto excluded = registry.create();

    DocumentIdMap ids;
    const auto first_look  = ids.reference(excluded);
    const auto second_look = ids.reference(excluded);
    CHECK_FALSE(first_look.present);
    CHECK_FALSE(second_look.present);
    CHECK(first_look.id == second_look.id);
}

TEST_CASE("DocumentIdMap: two different excluded targets get distinct absent identities",
          "[runtime][persistence][document-id-map]") {
    entt::registry registry;
    const auto excluded_a = registry.create();
    const auto excluded_b = registry.create();

    DocumentIdMap ids;
    const auto ref_a = ids.reference(excluded_a);
    const auto ref_b = ids.reference(excluded_b);
    CHECK_FALSE(ref_a.present);
    CHECK_FALSE(ref_b.present);
    CHECK(ref_a.id != ref_b.id);
}

TEST_CASE("DocumentIdMap: a null entity reference is always absent and never allocates an identity",
          "[runtime][persistence][document-id-map]") {
    DocumentIdMap ids;
    const auto null_ref = ids.reference(entt::entity{entt::null});
    CHECK_FALSE(null_ref.present);
    CHECK(null_ref.id == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
