// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"
#include "common/cactus_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <optional>
#include <vector>

using namespace cactus::runtime;
using entt_backend::HierarchyIndex;
using entt_backend::HierarchyStrategy;

namespace {

struct TreeParent {
    entt::entity parent{entt::null};
};
struct Node {};
struct Layer {
    int z_index = 0;
};
struct Watched {};
struct FlatLocal {
    Vector2 position{};
    float rotation = 0.0F;
    Vector2 scale{.x = 1.0F, .y = 1.0F};
};
struct FlatWorld {
    Vector2 position{};
    float rotation = 0.0F;
    Vector2 scale{.x = 1.0F, .y = 1.0F};
};
struct VolumeLocal {
    Vector3 position{};
    Quat rotation{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    Vector3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
};
struct VolumeWorld {
    Vector3 position{};
    Quat rotation{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    Vector3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
};

struct World {
    entt::registry registry;

    HierarchyIndex& index() {
        return entt_backend::hierarchy_index<TreeParent>(registry);
    }

    entt::entity make() {
        const auto entity = registry.create();
        registry.emplace<entt_backend::CreationOrdinal>(
            entity, entt_backend::CreationOrdinal{.value = entt_backend::generated_next_creation_ordinal()});
        return entity;
    }

    entt::entity child_of(entt::entity parent) {
        const auto entity = make();
        registry.emplace<TreeParent>(entity, TreeParent{.parent = parent});
        return entity;
    }

    void reparent(entt::entity entity, entt::entity parent) {
        registry.emplace_or_replace<TreeParent>(entity, TreeParent{.parent = parent});
    }

    std::vector<entt::entity> children(entt::entity parent) {
        std::vector<entt::entity> result;
        for (auto child = index().first_child(parent); child != entt::null; child = index().next_sibling(child)) {
            result.push_back(child);
        }
        return result;
    }

    [[nodiscard]] entt_backend::HierarchyPropagation flat() {
        return {
            .has_local_world = [this](entt::entity e) { return registry.all_of<FlatLocal, FlatWorld>(e); },
            .has_world       = [this](entt::entity e) { return registry.all_of<FlatWorld>(e); },
            .copy_local =
                [this](entt::entity e) {
                    const auto& local = registry.get<FlatLocal>(e);
                    auto& world       = registry.get<FlatWorld>(e);
                    world.position    = local.position;
                    world.rotation    = local.rotation;
                    world.scale       = local.scale;
                },
            .accumulate_from_parent =
                [this](entt::entity parent_entity, entt::entity e) {
                    const auto& local        = registry.get<FlatLocal>(e);
                    auto& world              = registry.get<FlatWorld>(e);
                    const auto& parent_world = registry.get<FlatWorld>(parent_entity);
                    world.position           = Vector2{.x = parent_world.position.x + local.position.x,
                                                       .y = parent_world.position.y + local.position.y};
                    world.rotation           = parent_world.rotation + local.rotation;
                    world.scale              = Vector2{.x = parent_world.scale.x * local.scale.x,
                                                       .y = parent_world.scale.y * local.scale.y};
                },
        };
    }

    [[nodiscard]] entt_backend::HierarchyPropagation volume() {
        return {
            .has_local_world = [this](entt::entity e) { return registry.all_of<VolumeLocal, VolumeWorld>(e); },
            .has_world       = [this](entt::entity e) { return registry.all_of<VolumeWorld>(e); },
            .copy_local =
                [this](entt::entity e) {
                    const auto& local = registry.get<VolumeLocal>(e);
                    auto& world       = registry.get<VolumeWorld>(e);
                    world.position    = local.position;
                    world.rotation    = local.rotation;
                    world.scale       = local.scale;
                },
            .accumulate_from_parent =
                [this](entt::entity parent_entity, entt::entity e) {
                    const auto& local        = registry.get<VolumeLocal>(e);
                    auto& world              = registry.get<VolumeWorld>(e);
                    const auto& parent_world = registry.get<VolumeWorld>(parent_entity);
                    world.position = Vector3{.x = parent_world.position.x + local.position.x,
                                             .y = parent_world.position.y + local.position.y,
                                             .z = parent_world.position.z + local.position.z};
                    world.rotation = stdlib::math::quat::compose(parent_world.rotation, local.rotation);
                    world.scale    = Vector3{.x = parent_world.scale.x * local.scale.x,
                                             .y = parent_world.scale.y * local.scale.y,
                                             .z = parent_world.scale.z * local.scale.z};
                },
        };
    }
};

// Restores the default strategy even when a check fails.
struct StrategyOverride {
    explicit StrategyOverride(HierarchyStrategy strategy) {
        entt_backend::set_hierarchy_strategy_override(strategy);
    }
    StrategyOverride(const StrategyOverride&)            = delete;
    StrategyOverride& operator=(const StrategyOverride&) = delete;
    StrategyOverride(StrategyOverride&&)                 = delete;
    StrategyOverride& operator=(StrategyOverride&&)      = delete;
    ~StrategyOverride() {
        entt_backend::set_hierarchy_strategy_override(std::nullopt);
    }
};

}  // namespace

// ── Index ───────────────────────────────────────────────────────────────────

TEST_CASE("Hierarchy index: siblings stay in creation order", "[runtime][hierarchy]") {
    World world;
    world.index();
    const auto root  = world.make();
    const auto older = world.make();  // a root until it is attached below
    const auto a     = world.child_of(root);
    const auto b     = world.child_of(root);
    CHECK(world.children(root) == std::vector{a, b});

    SECTION("attaching an older root inserts it by creation order") {
        world.reparent(older, root);
        CHECK(world.children(root) == std::vector{older, a, b});
        CHECK(world.index().parent_of(older) == root);
    }
    SECTION("a reparent moves the child") {
        const auto other = world.make();
        world.reparent(a, other);
        CHECK(world.children(root) == std::vector{b});
        CHECK(world.children(other) == std::vector{a});
        world.reparent(a, root);
        CHECK(world.children(root) == std::vector{a, b});
    }
    SECTION("remove and destroy unlink the child") {
        world.registry.remove<TreeParent>(a);
        CHECK(world.children(root) == std::vector{b});
        CHECK(world.index().parent_of(a) == entt::entity{entt::null});
        world.registry.destroy(b);
        CHECK(world.children(root).empty());
    }
    SECTION("an index built over an existing world matches") {
        entt::registry restored;
        const auto r = restored.create();
        restored.emplace<entt_backend::CreationOrdinal>(r, entt_backend::CreationOrdinal{.value = 1});
        const auto late = restored.create();
        restored.emplace<entt_backend::CreationOrdinal>(late, entt_backend::CreationOrdinal{.value = 3});
        restored.emplace<TreeParent>(late, TreeParent{.parent = r});
        const auto early = restored.create();
        restored.emplace<entt_backend::CreationOrdinal>(early, entt_backend::CreationOrdinal{.value = 2});
        restored.emplace<TreeParent>(early, TreeParent{.parent = r});
        const auto& index = entt_backend::hierarchy_index<TreeParent>(restored);
        CHECK(index.first_child(r) == early);
        CHECK(index.next_sibling(early) == late);
        CHECK(index.next_sibling(late) == entt::entity{entt::null});
    }
}

TEST_CASE("Hierarchy index: a cascade visits only the destroyed subtree", "[runtime][hierarchy]") {
    World world;
    auto& index        = world.index();
    const auto other   = world.make();
    for (int i = 0; i < 10000; ++i) {
        (void)world.child_of(other);
    }
    const auto root = world.make();
    const std::vector kids{world.child_of(root), world.child_of(root), world.child_of(root)};

    const auto before = index.child_visits();
    entt_backend::destroy_entity_recursive(world.registry, index, root);
    CHECK(index.child_visits() - before == 3);
    CHECK_FALSE(world.registry.valid(root));
    for (const auto kid : kids) {
        CHECK_FALSE(world.registry.valid(kid));
    }
    CHECK(world.registry.view<TreeParent>().size() == 10000);
}

TEST_CASE("Hierarchy index: a reparent under its own descendant would form a cycle", "[runtime][hierarchy]") {
    World world;
    const auto a     = world.make();
    const auto b     = world.child_of(a);
    const auto c     = world.child_of(b);
    const auto other = world.make();
    const auto& index = world.index();
    CHECK(entt_backend::reparent_would_cycle(index, a, c));
    CHECK(entt_backend::reparent_would_cycle(index, a, a));
    CHECK_FALSE(entt_backend::reparent_would_cycle(index, c, other));
    CHECK_FALSE(entt_backend::reparent_would_cycle(index, b, a));
    CHECK_FALSE(entt_backend::reparent_would_cycle(index, a, entt::null));
}

// ── Destroy ─────────────────────────────────────────────────────────────────

TEST_CASE("Hierarchy destroy: children go first, siblings in creation order", "[runtime][hierarchy]") {
    World world;
    auto& index = world.index();
    std::vector<entt::entity> destroyed;
    world.registry.on_destroy<Watched>().connect<[](std::vector<entt::entity>& order, entt::registry&, entt::entity e) {
        order.push_back(e);
    }>(destroyed);
    const auto root   = world.make();
    const auto first  = world.child_of(root);
    const auto second = world.child_of(root);
    const auto nested = world.child_of(first);
    const auto older  = world.make();
    world.reparent(older, second);
    for (const auto entity : {root, first, second, nested, older}) {
        world.registry.emplace<Watched>(entity);
    }

    entt_backend::destroy_entity_recursive(world.registry, index, root);
    CHECK(destroyed == std::vector{nested, first, older, second, root});
}

TEST_CASE("Hierarchy destroy: a deep chain is destroyed without exhausting the stack", "[runtime][hierarchy]") {
    World world;
    auto& index     = world.index();
    const auto root = world.make();
    auto last       = root;
    for (int i = 0; i < 100000; ++i) {
        last = world.child_of(last);
    }
    entt_backend::destroy_entity_recursive(world.registry, index, root);
    CHECK_FALSE(world.registry.valid(last));
    CHECK(world.registry.view<TreeParent>().empty());
}

// ── Propagation ─────────────────────────────────────────────────────────────

TEST_CASE("Hierarchy propagation: a deep chain propagates under every strategy", "[runtime][hierarchy]") {
    for (const auto strategy : {HierarchyStrategy::CreationSweep, HierarchyStrategy::DepthOrder}) {
        World world;
        auto& index = world.index();
        auto last   = world.make();
        world.registry.emplace<FlatLocal>(last, FlatLocal{.position = Vector2{.x = 1.0F}});
        world.registry.emplace<FlatWorld>(last);
        for (int i = 1; i < 100000; ++i) {
            last = world.child_of(last);
            world.registry.emplace<FlatLocal>(last, FlatLocal{.position = Vector2{.x = 1.0F}});
            world.registry.emplace<FlatWorld>(last);
        }
        entt_backend::propagate_hierarchy(world.registry, index, world.flat(), strategy);
        CHECK(world.registry.get<FlatWorld>(last).position.x == 100000.0F);
    }
}

namespace {

// Trees, a pose root, a stale parent and a child without WorldTransform, built
// without a reparent: the only worlds the creation sweep is chosen for.
template <typename Local, typename World_, typename MakeLocal>
void build_mixed_world(World& world, MakeLocal make_local) {
    auto& registry   = world.registry;
    const auto local = [&](entt::entity e, int seed) {
        registry.emplace<Local>(e, make_local(seed));
        registry.emplace<World_>(e);
    };
    std::vector<entt::entity> made;
    const auto root = world.make();
    local(root, 1);
    made.push_back(root);
    for (int i = 0; i < 40; ++i) {
        const auto parent = made[static_cast<std::size_t>((i * 7) % static_cast<int>(made.size()))];
        const auto child  = world.child_of(parent);
        local(child, i + 2);
        made.push_back(child);
    }
    const auto pose = world.make();
    registry.emplace<World_>(pose, World_{});
    const auto posed = world.child_of(pose);
    local(posed, 50);
    const auto gone  = world.make();
    const auto stale = world.child_of(gone);
    local(stale, 51);
    registry.destroy(gone);
    const auto bare     = world.child_of(root);
    const auto under_ba = world.child_of(bare);
    local(under_ba, 52);
    const auto late_root = world.make();
    local(late_root, 53);
}

template <typename World_>
std::vector<std::uint32_t> world_bits(entt::registry& registry) {
    std::vector<std::pair<std::uint64_t, entt::entity>> ordered;
    for (const auto entity : registry.view<World_>()) {
        ordered.emplace_back(registry.get<entt_backend::CreationOrdinal>(entity).value, entity);
    }
    std::ranges::sort(ordered);
    std::vector<std::uint32_t> bits;
    for (const auto& [ordinal, entity] : ordered) {
        const auto& value = registry.get<World_>(entity);
        const auto* words = reinterpret_cast<const std::uint32_t*>(&value);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        bits.insert(bits.end(), words, words + (sizeof(World_) / sizeof(std::uint32_t)));  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    return bits;
}

template <typename World_>
std::vector<std::uint32_t> propagate_under(HierarchyStrategy strategy,
                                           const auto& build,
                                           auto propagation_of) {
    World world;
    world.index();
    build(world);
    const StrategyOverride forced(strategy);
    entt_backend::propagate_hierarchy(
        world.registry, world.index(), propagation_of(world), HierarchyStrategy::CreationSweep);
    return world_bits<World_>(world.registry);
}

}  // namespace

TEST_CASE("Hierarchy propagation: every strategy gives the same bits", "[runtime][hierarchy]") {
    SECTION("flat") {
        const auto build = [](World& world) {
            build_mixed_world<FlatLocal, FlatWorld>(world, [](int seed) {
                const auto f = static_cast<float>(seed);
                return FlatLocal{.position = Vector2{.x = 0.37F * f, .y = -1.3F / f},
                                 .rotation = 0.11F * f,
                                 .scale    = Vector2{.x = 1.0F + (0.01F * f), .y = 0.9F}};
            });
        };
        const auto flat  = [](World& world) { return world.flat(); };
        const auto sweep = propagate_under<FlatWorld>(HierarchyStrategy::CreationSweep, build, flat);
        const auto depth = propagate_under<FlatWorld>(HierarchyStrategy::DepthOrder, build, flat);
        CHECK(sweep == depth);
        CHECK_FALSE(sweep.empty());
    }
    SECTION("volume") {
        const auto build = [](World& world) {
            build_mixed_world<VolumeLocal, VolumeWorld>(world, [](int seed) {
                const auto f = static_cast<float>(seed);
                return VolumeLocal{
                    .position = Vector3{.x = 0.37F * f, .y = -1.3F / f, .z = 0.5F},
                    .rotation = stdlib::math::quat::from_axis_angle(Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F}, 0.2F * f),
                    .scale    = Vector3{.x = 1.0F, .y = 1.0F + (0.01F * f), .z = 1.0F}};
            });
        };
        const auto volume = [](World& world) { return world.volume(); };
        const auto sweep  = propagate_under<VolumeWorld>(HierarchyStrategy::CreationSweep, build, volume);
        const auto depth  = propagate_under<VolumeWorld>(HierarchyStrategy::DepthOrder, build, volume);
        CHECK(sweep == depth);
    }
}

TEST_CASE("Hierarchy propagation: a stale parent and a parent without a world make roots", "[runtime][hierarchy]") {
    for (const auto strategy : {HierarchyStrategy::CreationSweep, HierarchyStrategy::DepthOrder}) {
        World world;
        auto& registry  = world.registry;
        const auto gone = world.make();
        const auto orphan = world.child_of(gone);
        registry.emplace<FlatLocal>(orphan, FlatLocal{.position = Vector2{.x = 3.0F}});
        registry.emplace<FlatWorld>(orphan);
        registry.destroy(gone);
        const auto bare  = world.make();
        const auto under = world.child_of(bare);
        registry.emplace<FlatLocal>(under, FlatLocal{.position = Vector2{.x = 4.0F}});
        registry.emplace<FlatWorld>(under);

        entt_backend::propagate_hierarchy(registry, world.index(), world.flat(), strategy);
        CHECK(registry.get<FlatWorld>(orphan).position.x == 3.0F);
        CHECK(registry.get<FlatWorld>(under).position.x == 4.0F);
    }
}

// ── Queries ─────────────────────────────────────────────────────────────────

TEST_CASE("Hierarchy queries: a deep chain snapshot is root first", "[runtime][hierarchy][query]") {
    World world;
    const auto root = world.make();
    world.registry.emplace<Node>(root);
    auto last = root;
    for (int i = 1; i < 100000; ++i) {
        last = world.child_of(last);
        world.registry.emplace<Node>(last);
    }
    const auto view      = world.registry.view<Node>();
    const auto preorder  = entt_backend::hierarchy_order(world.registry, world.index(), view, false);
    const auto postorder = entt_backend::hierarchy_order(world.registry, world.index(), view, true);
    REQUIRE(preorder.size() == 100000);
    REQUIRE(postorder.size() == 100000);
    CHECK(preorder.front() == root);
    CHECK(preorder.back() == last);
    CHECK(postorder.front() == last);
    CHECK(postorder.back() == root);
}

TEST_CASE("Hierarchy queries: forest order, filters and stale parents", "[runtime][hierarchy][query]") {
    World world;
    auto& registry   = world.registry;
    const auto node  = [&](entt::entity e) {
        registry.emplace<Node>(e);
        return e;
    };
    const auto root  = node(world.make());
    const auto a     = node(world.child_of(root));
    const auto a1    = node(world.child_of(a));
    const auto b     = node(world.child_of(root));
    const auto hole  = world.child_of(root);  // doesn't match: its child is a root
    const auto under = node(world.child_of(hole));
    const auto gone  = world.make();
    const auto stale = node(world.child_of(gone));
    registry.destroy(gone);
    const auto older = node(world.make());
    world.reparent(older, a);

    const auto view = registry.view<Node>();
    CHECK(entt_backend::hierarchy_order(registry, world.index(), view, false) ==
          std::vector{root, a, a1, older, b, under, stale});
    CHECK(entt_backend::hierarchy_order(registry, world.index(), view, true) ==
          std::vector{a1, older, a, b, root, under, stale});
    CHECK(entt_backend::hierarchy_children(registry, world.index(), root, view) == std::vector{a, b});
    CHECK(entt_backend::hierarchy_children(registry, world.index(), gone, view).empty());
}

TEST_CASE("Hierarchy queries: stacking order sorts siblings by z then creation", "[runtime][hierarchy][query]") {
    World world;
    auto& registry  = world.registry;
    const auto node = [&](entt::entity e, int z) {
        registry.emplace<Layer>(e, Layer{.z_index = z});
        return e;
    };
    const auto root   = node(world.make(), 0);
    const auto high   = node(world.child_of(root), 2);
    const auto low    = node(world.child_of(root), 1);
    const auto tie    = node(world.child_of(root), 1);
    const auto nested = node(world.child_of(high), 0);
    const auto other  = node(world.make(), 9);
    const auto plain  = world.make();

    const auto view = registry.view<Layer>();
    const auto z_of = [&](entt::entity e) { return registry.get<Layer>(e).z_index; };
    const auto at   = [&](entt::entity e) {
        return entt_backend::hierarchy_stacking_order(registry, world.index(), view, e, z_of);
    };
    CHECK(at(root) == 0);
    CHECK(at(low) == 1);
    CHECK(at(tie) == 2);
    CHECK(at(high) == 3);
    CHECK(at(nested) == 4);
    CHECK(at(other) == 5);
    CHECK(at(plain) == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
