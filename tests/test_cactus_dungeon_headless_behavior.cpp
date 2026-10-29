#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <queue>
#include <tuple>
#include <vector>

namespace {

namespace game      = cactus_gen::world;
namespace territory = cactus_gen::territory;
namespace tasks     = cactus_gen::tasks;
namespace backend   = cactus::runtime::entt_backend;
using Cell          = cactus_gen::stdlib::voxel::volume::Cell;
using Volume        = cactus_gen::stdlib::voxel::volume::Volume;
using Solid         = cactus_gen::stdlib::physics::volume::Solid;
using Transform     = cactus_gen::stdlib::transform::volume::WorldTransform;

constexpr int kSize = 24;

struct World {
    entt::registry registry;

    explicit World(int seed = 7) {
        cactus_raylib_fake::reset();
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        for (auto [entity, config] : registry.view<game::WorldConfig>().each()) {
            config.seed = seed;
        }
        cactus_headless_test::dispatch_load_event(registry);
        frame(4);
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            cactus_headless_test::drive_frame(registry);
        }
    }

    entt::entity tile(int x, int z) {
        for (auto [entity, cell, tile] : registry.view<Cell, game::Tile>().each()) {
            if (cell.x == x && cell.z == z) {
                return entity;
            }
        }
        return entt::null;
    }

    std::vector<game::Kind> kinds() {
        std::vector<game::Kind> result;
        for (int x = 0; x < kSize; ++x) {
            for (int z = 0; z < kSize; ++z) {
                const auto entity = tile(x, z);
                REQUIRE((entity != entt::null));
                result.push_back(registry.get<game::Tile>(entity).kind);
            }
        }
        return result;
    }

    void floor(int x, int z) {
        const auto entity = tile(x, z);
        REQUIRE((entity != entt::null));
        registry.get<game::Tile>(entity).kind = game::Kind::Floor;
        registry.remove<Solid>(entity);
    }

    entt::entity human() {
        return cactus_gen::detail::generated_named_slots().world.Human;
    }

    void mark(int x, int z, game::Kind kind = game::Kind::Earth) {
        const auto target = tile(x, z);
        registry.get<game::Tile>(target).kind = kind;
        registry.emplace_or_replace<Solid>(target);
        backend::relation_store<tasks::DigMark>(registry).add(human(), target, tasks::DigMark{});
    }

    entt::entity worker(int x, int z) {
        const auto robot = cactus_gen::detail::small_map::create_worker(
            registry, Vector3{static_cast<float>(x) + 0.5F, 1.25F, static_cast<float>(z) + 0.5F});
        backend::relation_store<tasks::Located>(registry).add(robot, tile(x, z), tasks::Located{});
        backend::relation_store<tasks::Commands>(registry).add(robot, human(), tasks::Commands{});
        return robot;
    }

    void remove_workers() {
        const auto workers = registry.view<game::Robot>();
        const std::vector<entt::entity> entities(workers.begin(), workers.end());
        for (const auto entity : entities) {
            registry.destroy(entity);
        }
    }
};

TEST_CASE("Dungeon seed repeats tiles, bases and robots", "[cactus-dungeon][headless]") {
    using Snapshot =
        std::tuple<std::vector<game::Kind>, std::vector<std::tuple<int, int>>, std::vector<std::tuple<float, float>>>;
    const auto capture = [](int seed) {
        World world(seed);
        REQUIRE(world.registry.view<game::Tile>().size() == kSize * kSize);
        std::vector<std::tuple<int, int>> hearts;
        for (auto [entity, cell, heart] : world.registry.view<Cell, game::Heart>().each()) {
            hearts.emplace_back(cell.x, cell.z);
        }
        std::vector<std::tuple<float, float>> robots;
        for (auto [entity, transform] : world.registry.view<Transform, game::Robot>().each()) {
            robots.emplace_back(transform.position.x, transform.position.z);
        }
        REQUIRE(hearts.size() == 2);
        REQUIRE(robots.size() == 8);
        return Snapshot{world.kinds(), hearts, robots};
    };
    const auto first = capture(7);
    REQUIRE(capture(7) == first);
    REQUIRE(std::get<0>(capture(71)) != std::get<0>(first));
}

TEST_CASE("Dungeon volume has a rock border, a solid floor and smooth walls", "[cactus-dungeon][headless]") {
    World world;
    REQUIRE(world.registry.view<game::Tile>().size() == kSize * kSize);
    REQUIRE(world.registry.view<Cell>().size() == 2 * kSize * kSize);
    for (auto [entity, cell] : world.registry.view<Cell>().each()) {
        if (cell.y == 0) {
            REQUIRE(world.registry.all_of<Solid>(entity));
            continue;
        }
        if (cell.x == 0 || cell.z == 0 || cell.x == kSize - 1 || cell.z == kSize - 1) {
            REQUIRE(world.registry.get<game::Tile>(entity).kind == game::Kind::Rock);
            REQUIRE(world.registry.all_of<Solid>(entity));
        }
    }
    using Renderer = cactus_gen::stdlib::voxel::volume::VolumeRenderer;
    using Style    = cactus_gen::stdlib::voxel::volume::SurfaceStyle;
    for (auto [entity, renderer] : world.registry.view<Renderer>().each()) {
        REQUIRE(renderer.style == Style::Smooth);
    }
}

TEST_CASE("Dungeon bases have a diggable route between them", "[cactus-dungeon][headless]") {
    World world;
    std::vector<std::pair<int, int>> hearts;
    for (auto [entity, cell, heart] : world.registry.view<Cell, game::Heart>().each()) {
        REQUIRE(world.registry.get<game::Tile>(entity).kind == game::Kind::Floor);
        hearts.emplace_back(cell.x, cell.z);
    }
    REQUIRE(hearts.size() == 2);
    std::array<std::array<bool, kSize>, kSize> seen{};
    std::queue<std::pair<int, int>> pending;
    pending.push(hearts.front());
    while (!pending.empty()) {
        const auto [x, z] = pending.front();
        pending.pop();
        if (x < 0 || z < 0 || x >= kSize || z >= kSize || seen[x][z]) {
            continue;
        }
        const auto entity = world.tile(x, z);
        REQUIRE((entity != entt::null));
        const auto kind = world.registry.get<game::Tile>(entity).kind;
        if (kind != game::Kind::Earth && kind != game::Kind::Gold && kind != game::Kind::Floor) {
            continue;
        }
        seen[x][z] = true;
        pending.emplace(x + 1, z);
        pending.emplace(x - 1, z);
        pending.emplace(x, z + 1);
        pending.emplace(x, z - 1);
    }
    REQUIRE(seen[hearts.back().first][hearts.back().second]);
}

TEST_CASE("Dungeon walk links follow a dug floor", "[cactus-dungeon][headless]") {
    World world;
    world.frame(4);
    auto& walk       = backend::relation_store<territory::Walk>(world.registry);
    const auto edge  = world.tile(6, 4);
    const auto earth = world.tile(7, 4);
    REQUIRE_FALSE(walk.contains(edge, earth));
    world.floor(7, 4);
    world.frame(3);
    REQUIRE(walk.contains(edge, earth));
    REQUIRE(walk.contains(earth, edge));
    REQUIRE_FALSE(walk.contains(edge, world.tile(7, 5)));
}

TEST_CASE("Dungeon claimed bases connect to their hearts", "[cactus-dungeon][headless]") {
    World world;
    world.frame(5);
    const auto& slots = cactus_gen::detail::generated_named_slots().world;
    auto& owned       = backend::relation_store<game::OwnedBy>(world.registry);
    for (auto [entity, cell, heart] : world.registry.view<Cell, game::Heart>().each()) {
        REQUIRE(world.registry.all_of<territory::Connected>(entity));
        const auto player = cell.x == 4 ? slots.Human : slots.Enemy;
        REQUIRE(owned.contains(entity, player));
        const auto corner = world.tile(cell.x + 2, cell.z + 2);
        REQUIRE(owned.contains(corner, player));
        REQUIRE(world.registry.all_of<territory::Connected>(corner));
    }
    REQUIRE_FALSE(world.registry.all_of<territory::Connected>(world.tile(7, 4)));
}

TEST_CASE("Dungeon bridge removal disconnects and rebuilding reconnects", "[cactus-dungeon][headless]") {
    World world;
    const auto player                           = cactus_gen::detail::generated_named_slots().world.Human;
    const auto bridge                           = world.tile(7, 4);
    const auto island                           = world.tile(8, 4);
    world.registry.get<game::Tile>(bridge).kind = game::Kind::Water;
    world.registry.remove<Solid>(bridge);
    world.floor(8, 4);
    auto& owned = backend::relation_store<game::OwnedBy>(world.registry);
    owned.add(island, player, game::OwnedBy{});
    backend::relation_store<territory::BridgePlan>(world.registry).add(player, bridge, territory::BridgePlan{});
    world.frame(5);
    REQUIRE(world.registry.all_of<territory::Connected>(island));
    backend::relation_store<territory::BridgePlan>(world.registry)
        .add(player, bridge, territory::BridgePlan{.sell = true});
    world.frame(4);
    REQUIRE_FALSE(world.registry.all_of<territory::Connected>(island));
    REQUIRE(owned.contains(island, player));
    backend::relation_store<territory::BridgePlan>(world.registry).add(player, bridge, territory::BridgePlan{});
    world.frame(5);
    REQUIRE(world.registry.all_of<territory::Connected>(island));
}

TEST_CASE("Dungeon marks create tasks only for diggable tiles", "[cactus-dungeon][headless]") {
    World world;
    world.mark(7, 4);
    world.mark(7, 5, game::Kind::Rock);
    world.frame(5);
    REQUIRE(world.registry.all_of<tasks::Task>(world.tile(7, 4)));
    REQUIRE_FALSE(world.registry.all_of<tasks::Task>(world.tile(7, 5)));
}

TEST_CASE("Dungeon robots claim the nearest reachable free tasks once", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    const auto first = world.worker(6, 4);
    const auto second = world.worker(4, 4);
    const auto third = world.worker(3, 4);
    world.mark(7, 4);
    world.mark(7, 6);
    world.mark(12, 12);
    world.frame(8);
    auto& claims = backend::relation_store<tasks::Claim>(world.registry);
    REQUIRE(claims.size() == 2);
    REQUIRE(claims.contains(first, world.tile(7, 4)));
    REQUIRE(claims.contains(second, world.tile(7, 6)));
    REQUIRE_FALSE(claims.contains(third, world.tile(7, 4)));
    REQUIRE_FALSE(claims.contains(third, world.tile(7, 6)));
    REQUIRE_FALSE(claims.contains(first, world.tile(12, 12)));
    REQUIRE_FALSE(claims.contains(second, world.tile(12, 12)));
}

TEST_CASE("Dungeon robots dig and mine through route steps", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(4, 4);
    const auto initial_gold = world.registry.get<game::Player>(world.human()).gold;
    world.mark(7, 4);
    world.mark(8, 4, game::Kind::Gold);
    world.frame(240);
    REQUIRE(world.registry.get<game::Tile>(world.tile(7, 4)).kind == game::Kind::Floor);
    REQUIRE(world.registry.get<game::Tile>(world.tile(8, 4)).kind == game::Kind::Floor);
    REQUIRE_FALSE(world.registry.all_of<Solid>(world.tile(8, 4)));
    REQUIRE(world.registry.get<game::Player>(world.human()).gold > initial_gold);
    REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(robot, world.tile(7, 4)));
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, world.tile(8, 4)));
}

TEST_CASE("Dungeon infinite gold pays repeatedly without disappearing", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    world.worker(6, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    const auto initial_gold = world.registry.get<game::Player>(world.human()).gold;
    world.frame(180);
    const auto first_gold = world.registry.get<game::Player>(world.human()).gold;
    REQUIRE(first_gold > initial_gold);
    world.frame(180);
    REQUIRE(world.registry.get<game::Player>(world.human()).gold > first_gold);
    REQUIRE(world.registry.get<game::Tile>(world.tile(7, 4)).kind == game::Kind::InfiniteGold);
    REQUIRE(world.registry.all_of<Solid>(world.tile(7, 4)));
}

TEST_CASE("Dungeon floor marks cannot trap a worker in an invalid digging claim", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(4, 4);
    const auto floor = world.tile(5, 4);
    backend::relation_store<tasks::DigMark>(world.registry).add(world.human(), floor, tasks::DigMark{});
    world.frame(8);
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, floor));
    REQUIRE_FALSE(world.registry.all_of<tasks::Task>(floor));
}

TEST_CASE("Dungeon canceled marks release claims and accumulated work", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(6, 4);
    const auto target = world.tile(7, 4);
    world.mark(7, 4);
    world.frame(10);
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
    REQUIRE(world.registry.get<tasks::Work>(target).progress > 0.0F);
    backend::relation_store<tasks::DigMark>(world.registry).erase(world.human(), target);
    world.frame(5);
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
    REQUIRE_FALSE(world.registry.all_of<tasks::Work>(target));
    REQUIRE(world.registry.get<game::Tile>(target).kind == game::Kind::Earth);
}

TEST_CASE("Dungeon robots claim neutral floor next to their territory", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    world.worker(6, 4);
    world.floor(7, 4);
    world.floor(12, 12);
    world.frame(180);
    auto& owned = backend::relation_store<game::OwnedBy>(world.registry);
    REQUIRE(owned.contains(world.tile(7, 4), world.human()));
    REQUIRE_FALSE(owned.has_from(world.tile(12, 12)));
    REQUIRE(world.registry.all_of<territory::Connected>(world.tile(7, 4)));
}

TEST_CASE("Dungeon floor claiming keeps multiple supports and cancels the final one", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    world.floor(12, 12);
    world.floor(11, 12);
    world.floor(12, 11);
    const auto robot = world.worker(11, 12);
    const auto target = world.tile(12, 12);
    auto& owned = backend::relation_store<game::OwnedBy>(world.registry);
    owned.add(world.tile(11, 12), world.human(), {});
    owned.add(world.tile(12, 11), world.human(), {});
    world.frame(8);
    REQUIRE(world.registry.all_of<tasks::Task>(target));
    owned.erase_from(world.tile(11, 12));
    owned.compact();
    world.frame(8);
    REQUIRE(world.registry.all_of<tasks::Task>(target));
    SECTION("remaining support permits claiming") {
        world.frame(180);
        REQUIRE(owned.contains(target, world.human()));
    }
    SECTION("removing the final support cancels work") {
        owned.erase_from(world.tile(12, 11));
        owned.compact();
        world.frame(180);
        REQUIRE_FALSE(owned.has_from(target));
        REQUIRE_FALSE(world.registry.all_of<tasks::Task>(target));
        REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
    }
}

TEST_CASE("Dungeon floor claiming uses the worker faction", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    world.floor(12, 12);
    world.floor(11, 12);
    const auto robot = world.worker(11, 12);
    const auto blue = cactus_gen::detail::generated_named_slots().world.Enemy;
    auto& commands = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(robot);
    commands.compact();
    commands.add(robot, blue, {});
    auto& owned = backend::relation_store<game::OwnedBy>(world.registry);
    owned.add(world.tile(11, 12), blue, {});
    world.frame(180);
    REQUIRE(owned.contains(world.tile(12, 12), blue));
    REQUIRE_FALSE(owned.contains(world.tile(12, 12), world.human()));
}

TEST_CASE("Dungeon workers release a task marked only by the other faction", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(6, 4);
    const auto target = world.tile(7, 4);
    world.mark(7, 4);
    world.frame(10);
    auto& marks = backend::relation_store<tasks::DigMark>(world.registry);
    marks.erase(world.human(), target);
    marks.add(cactus_gen::detail::generated_named_slots().world.Enemy, target, tasks::DigMark{});
    world.frame(5);
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
}

}  // namespace
