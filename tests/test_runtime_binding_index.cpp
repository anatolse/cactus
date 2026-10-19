// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

struct Member {
    Vector3 center{};
};
struct Mover {};

std::vector<entt::entity> entities_of(std::vector<OrderedEntity> found) {
    std::ranges::sort(found);
    std::vector<entt::entity> result;
    result.reserve(found.size());
    for (const auto& [ordinal, entity] : found) {
        result.push_back(entity);
    }
    return result;
}

entt::entity make(entt::registry& registry, Vector3 center) {
    const auto entity = create_entity(registry);
    registry.emplace<CreationOrdinal>(entity, CreationOrdinal{.value = generated_next_creation_ordinal()});
    registry.emplace<Member>(entity, Member{.center = center});
    return entity;
}

SpatialBindingIndex3D::Classified classify(const entt::registry& registry, entt::entity entity) {
    const auto* member = registry.try_get<Member>(entity);
    if (member == nullptr) {
        return {};
    }
    const Vector3 half{.x = 0.5F, .y = 0.5F, .z = 0.5F};
    return SpatialBindingIndex3D::Classified{
        .member    = true,
        .is_static = !registry.all_of<Mover>(entity),
        .bounds    = std::pair{Vector3Subtract(member->center, half), Vector3Add(member->center, half)}};
}

void attach(entt::registry& registry, SpatialBindingIndex3D& index) {
    index.reset(registry, 1.0F);
    watch_binding_traits<Member, Mover>(registry, index.tracker());
    for (const auto entity : registry.view<Member>()) {
        index.tracker().mark(entity);
    }
}

void catch_up(entt::registry& registry, SpatialBindingIndex3D& index) {
    index.catch_up(registry, [&](entt::entity entity) { return classify(registry, entity); });
}

std::vector<entt::entity> near(const SpatialBindingIndex3D& index, Vector3 min, Vector3 max) {
    std::vector<OrderedEntity> found;
    index.query_static(min, max, found);
    return entities_of(found);
}

}  // namespace

TEST_CASE("the static grid returns overlapping members once each", "[runtime][binding-index]") {
    StaticProxyGrid3D grid;
    grid.clear(1.0F);
    const auto a = entt::entity{1};
    const auto b = entt::entity{2};
    const auto c = entt::entity{3};
    grid.insert(a, 10, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    grid.insert(b, 11, Vector3{.x = 0.9F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 2.9F, .y = 1.0F, .z = 1.0F});
    grid.insert(c, 12, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, Vector3{.x = 6.0F, .y = 6.0F, .z = 6.0F});
    std::vector<OrderedEntity> found;
    grid.query(Vector3{.x = 0.5F, .y = 0.5F, .z = 0.5F}, Vector3{.x = 1.5F, .y = 0.6F, .z = 0.6F}, found);
    CHECK(entities_of(found) == std::vector{a, b});

    found.clear();
    grid.query(Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, found);
    CHECK(entities_of(found) == std::vector{a, b});

    grid.remove(a);
    found.clear();
    grid.query(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.95F, .y = 1.0F, .z = 1.0F}, found);
    CHECK(entities_of(found) == std::vector{b});
    CHECK(grid.size() == 2);
}

TEST_CASE("the static grid keeps huge or non-finite members aside and scans for huge queries",
          "[runtime][binding-index]") {
    StaticProxyGrid3D grid;
    grid.clear(1.0F);
    const float inf = std::numeric_limits<float>::infinity();
    const auto wide = entt::entity{1};
    const auto odd  = entt::entity{2};
    const auto unit = entt::entity{3};
    grid.insert(wide, 1, Vector3{.x = -100.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 100.0F, .y = 1.0F, .z = 1.0F});
    grid.insert(odd, 2, Vector3{.x = -inf, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.0F, .y = 1.0F, .z = 1.0F});
    grid.insert(unit, 3, Vector3{.x = 50.0F, .y = 50.0F, .z = 50.0F}, Vector3{.x = 51.0F, .y = 51.0F, .z = 51.0F});
    std::vector<OrderedEntity> found;
    grid.query(Vector3{.x = 99.0F, .y = 0.5F, .z = 0.5F}, Vector3{.x = 99.5F, .y = 0.6F, .z = 0.6F}, found);
    CHECK(entities_of(found) == std::vector{wide, odd});

    found.clear();
    grid.query(Vector3{.x = -1000.0F, .y = -1000.0F, .z = -1000.0F}, Vector3{.x = 1000.0F, .y = 1000.0F, .z = 1000.0F},
               found);
    CHECK(entities_of(found) == std::vector{wide, odd, unit});

    found.clear();
    grid.query(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = inf, .y = 1.0F, .z = 1.0F}, found);
    CHECK(entities_of(found) == std::vector{wide, odd, unit});
}

TEST_CASE("the static grid keeps members spanning a vast volume aside and offers everything to a NaN query",
          "[runtime][binding-index]") {
    StaticProxyGrid3D grid;
    grid.clear(1.0F);
    const auto vast = entt::entity{1};
    const auto unit = entt::entity{2};
    grid.insert(vast, 1, Vector3{.x = -1.0e12F, .y = -1.0e12F, .z = -1.0e12F},
                Vector3{.x = 1.0e12F, .y = 1.0e12F, .z = 1.0e12F});
    grid.insert(unit, 2, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, Vector3{.x = 6.0F, .y = 6.0F, .z = 6.0F});
    std::vector<OrderedEntity> found;
    grid.query(Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, found);
    CHECK(entities_of(found) == std::vector{vast});

    const float nan = std::numeric_limits<float>::quiet_NaN();
    found.clear();
    grid.query(Vector3{.x = nan, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, found);
    CHECK(entities_of(found) == std::vector{vast, unit});
}

TEST_CASE("a spatial binding index follows membership through signals", "[runtime][binding-index]") {
    entt::registry registry;
    const auto a = make(registry, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    const auto b = make(registry, Vector3{.x = 3.0F, .y = 0.0F, .z = 0.0F});
    SpatialBindingIndex3D index;
    REQUIRE(index.tracker().stale(registry));
    attach(registry, index);
    CHECK_FALSE(index.tracker().stale(registry));
    catch_up(registry, index);
    CHECK(index.static_size() == 2);
    CHECK(near(index, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{a});

    const auto c = make(registry, Vector3{.x = 0.5F, .y = 0.0F, .z = 0.0F});
    registry.remove<Member>(a);
    catch_up(registry, index);
    CHECK(near(index, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{c});

    registry.emplace<Mover>(b);
    catch_up(registry, index);
    CHECK(index.static_size() == 1);
    REQUIRE(index.dynamic_members().size() == 1);
    CHECK(index.dynamic_members().front().second == b);

    registry.destroy(b);
    catch_up(registry, index);
    CHECK(index.dynamic_members().empty());
}

TEST_CASE("dynamic members stay in creation order", "[runtime][binding-index]") {
    entt::registry registry;
    std::vector<entt::entity> movers;
    movers.reserve(4);
    for (int i = 0; i < 4; ++i) {
        movers.push_back(make(registry, Vector3{.x = static_cast<float>(i), .y = 0.0F, .z = 0.0F}));
    }
    SpatialBindingIndex3D index;
    attach(registry, index);
    for (auto it = movers.rbegin(); it != movers.rend(); ++it) {
        registry.emplace<Mover>(*it);
    }
    catch_up(registry, index);
    REQUIRE(index.dynamic_members().size() == 4);
    for (std::size_t i = 0; i < movers.size(); ++i) {
        CHECK(index.dynamic_members()[i].second == movers[i]);
    }
}

TEST_CASE("a new epoch makes an index stale", "[runtime][binding-index]") {
    entt::registry registry;
    SpatialBindingIndex3D index;
    attach(registry, index);
    REQUIRE_FALSE(index.tracker().stale(registry));
    advance_binding_index_epoch();
    CHECK(index.tracker().stale(registry));
    entt::registry other;
    attach(registry, index);
    CHECK(index.tracker().stale(other));
}

TEST_CASE("an equality index keeps each key's members oldest first", "[runtime][binding-index]") {
    entt::registry registry;
    EqualityBindingIndex<2> index;
    index.reset(registry);
    watch_binding_traits<Member>(registry, index.tracker());
    const auto key_of = [&](entt::entity entity) -> std::optional<EqualityBindingIndex<2>::Key> {
        const auto* member = registry.try_get<Member>(entity);
        if (member == nullptr) {
            return std::nullopt;
        }
        return EqualityBindingIndex<2>::Key{equality_key_part(static_cast<int>(member->center.x)),
                                            equality_key_part(static_cast<int>(member->center.y))};
    };
    const auto first  = make(registry, Vector3{.x = 1.0F, .y = 2.0F, .z = 0.0F});
    const auto other  = make(registry, Vector3{.x = 2.0F, .y = 1.0F, .z = 0.0F});
    const auto second = make(registry, Vector3{.x = 1.0F, .y = 2.0F, .z = 0.0F});
    index.catch_up(registry, key_of);
    const auto key = EqualityBindingIndex<2>::Key{equality_key_part(1), equality_key_part(2)};
    REQUIRE(index.find(key).size() == 2);
    CHECK(index.find(key)[0].second == first);
    CHECK(index.find(key)[1].second == second);
    CHECK(index.find(EqualityBindingIndex<2>::Key{equality_key_part(2), equality_key_part(1)}).front().second == other);
    CHECK(index.find(EqualityBindingIndex<2>::Key{equality_key_part(9), equality_key_part(9)}).empty());

    registry.destroy(first);
    index.catch_up(registry, key_of);
    REQUIRE(index.find(key).size() == 1);
    CHECK(index.find(key).front().second == second);
    CHECK(index.size() == 2);
}

TEST_CASE("a float query key finds exactly the int members it equals", "[runtime][binding-index]") {
    CHECK(equality_key_part(3.0F) == equality_key_part(3));
    CHECK(equality_key_part(-2.0) == equality_key_part(-2));
    CHECK(equality_key_part(-2147483648.0F) == equality_key_part(-2147483647 - 1));
    for (const double value : {3.5, 2147483648.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        const auto key = equality_key_part(value);
        CHECK(key != equality_key_part(static_cast<int>(std::trunc(std::isfinite(value) ? value / 2.0 : 0.0))));
        CHECK(key != equality_key_part(-1));
        CHECK(key != equality_key_part(true));
    }
}

TEST_CASE("segment and aligned box proxies bound what ray_box can hit", "[runtime][binding-index]") {
    const auto segment = segment_proxy(entt::null, 0, SapSide::Left, Vector3{.x = 1.0F, .y = 2.0F, .z = 3.0F},
                                       Vector3{.x = 0.0F, .y = -2.0F, .z = 0.0F}, 4.0F);
    CHECK(segment.min.y == 2.0F - 4.0F);
    CHECK(segment.max.y == 2.0F);
    CHECK(segment.min.x == 1.0F);
    CHECK(segment.max.z == 3.0F);

    const auto none = segment_proxy(entt::null, 0, SapSide::Left, Vector3{.x = 1.0F, .y = 2.0F, .z = 3.0F},
                                    Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, 4.0F);
    CHECK(none.min.x > none.max.x);
    CHECK(segment_proxy(entt::null, 0, SapSide::Left, Vector3{}, Vector3{.x = 1.0F, .y = 0.0F, .z = 0.0F}, 0.0F).min.x >
          0.0F);

    const auto box = aligned_box_proxy(entt::null, 0, SapSide::Right, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F},
                                       Vector3{.x = -2.0F, .y = 1.0F, .z = 4.0F});
    // bounds_around keeps a tiny rounding slack.
    CHECK(box.min.x == Catch::Approx(0.0F).margin(1e-3));
    CHECK(box.max.x == Catch::Approx(2.0F).margin(1e-3));
    CHECK(box.min.z == Catch::Approx(-1.0F).margin(1e-3));
}

TEST_CASE("candidates merge static and dynamic members oldest first", "[runtime][binding-index]") {
    entt::registry registry;
    std::vector<entt::entity> all;
    all.reserve(5);
    for (int i = 0; i < 5; ++i) {
        all.push_back(make(registry, Vector3{.x = static_cast<float>(i) * 0.1F, .y = 0.0F, .z = 0.0F}));
    }
    const auto far = make(registry, Vector3{.x = 50.0F, .y = 0.0F, .z = 0.0F});
    registry.emplace<Mover>(all[1]);
    registry.emplace<Mover>(all[3]);
    registry.emplace<Mover>(far);
    SpatialBindingIndex3D index;
    attach(registry, index);
    catch_up(registry, index);

    std::vector<SpatialBindingIndex3D::BoundedMember> dynamic;
    index.bound_dynamic(
        [&](entt::entity entity) -> std::optional<SpatialBindingIndex3D::Bounds> {
            if (entity == all[3]) {
                return std::nullopt;
            }
            return classify(registry, entity).bounds;
        },
        dynamic);
    REQUIRE(dynamic.size() == 2);
    CHECK(dynamic[0].first.second == all[1]);
    CHECK(dynamic[1].first.second == far);

    std::vector<OrderedEntity> found{OrderedEntity{0, entt::null}};
    index.candidates(Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, dynamic,
                     found);
    std::vector<entt::entity> order;
    order.reserve(found.size());
    for (const auto& [ordinal, entity] : found) {
        order.push_back(entity);
    }
    CHECK(order == std::vector{all[0], all[1], all[2], all[4]});

    const float inf = std::numeric_limits<float>::infinity();
    index.candidates(Vector3{.x = -inf, .y = -1.0F, .z = -1.0F}, Vector3{.x = inf, .y = 1.0F, .z = 1.0F}, dynamic,
                     found);
    CHECK(std::ranges::is_sorted(found));
    CHECK(found.size() == 5);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
