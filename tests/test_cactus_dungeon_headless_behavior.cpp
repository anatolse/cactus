#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/fake_raylib_assertions.hpp"
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <queue>
#include <tuple>
#include <vector>

namespace {

namespace game      = cactus_gen::world;
namespace territory = cactus_gen::territory;
namespace tasks     = cactus_gen::tasks;
namespace units     = cactus_gen::units;
namespace backend   = cactus::runtime::entt_backend;
using Cell          = cactus_gen::stdlib::voxel::volume::Cell;
using Volume        = cactus_gen::stdlib::voxel::volume::Volume;
using Solid         = cactus_gen::stdlib::physics::volume::Solid;
using Transform     = cactus_gen::stdlib::transform::volume::WorldTransform;

constexpr int kSize = 24;

struct World {
    entt::registry registry;

    explicit World(int seed = 7, int robots = 4, int size = kSize, bool autostart = true) {
        backend::reset_quit_request();
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_screen_size(1280, 720);
        cactus_raylib_fake::set_mouse_position(Vector2{640.0F, 360.0F});
        cactus_raylib_fake::set_window_ready(true);
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        for (auto [entity, config] : registry.view<game::WorldConfig>().each()) {
            config.autostart       = autostart;
            config.seed            = seed;
            config.starting_robots = robots;
            config.size            = size;
            config.batch           = 4 * size * size;
        }
        cactus_headless_test::dispatch_load_event(registry);
        frame(4);
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            cactus_raylib_fake::clear_call_log();
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

using game::walled;

TEST_CASE("Dungeon volume has a rock border, a solid floor and tall rough walls", "[cactus-dungeon][headless]") {
    World world;
    REQUIRE(world.registry.view<game::Tile>().size() == kSize * kSize);
    REQUIRE(world.registry.view<Cell>().size() == 4 * kSize * kSize);
    std::array<std::array<int, kSize>, kSize> top{};
    for (auto [entity, cell] : world.registry.view<Cell>().each()) {
        if (cell.y == 0) {
            REQUIRE(world.registry.all_of<Solid>(entity));
            continue;
        }
        const auto below = world.tile(cell.x, cell.z);
        REQUIRE((below != entt::null));
        const auto kind = world.registry.get<game::Tile>(below).kind;
        if (cell.x == 0 || cell.z == 0 || cell.x == kSize - 1 || cell.z == kSize - 1) {
            REQUIRE(kind == game::Kind::Rock);
        }
        INFO("cell " << cell.x << "," << cell.y << "," << cell.z);
        if (!world.registry.all_of<Solid>(entity)) {
            continue;
        }
        REQUIRE(walled(kind));
        REQUIRE(cell.texture == world.registry.get<Cell>(below).texture);
        top[cell.x][cell.z] = std::max(top[cell.x][cell.z], cell.y);
    }
    std::vector<int> heights;
    for (int x = 1; x + 1 < kSize; ++x) {
        for (int z = 1; z + 1 < kSize; ++z) {
            const auto kind = world.registry.get<game::Tile>(world.tile(x, z)).kind;
            INFO("column " << x << "," << z);
            REQUIRE((top[x][z] > 1) == walled(kind));
            if (walled(kind)) {
                heights.push_back(top[x][z]);
            }
        }
    }
    REQUIRE(std::ranges::minmax(heights).min != std::ranges::minmax(heights).max);
    for (auto [entity, cell] : world.registry.view<Cell>().each()) {
        INFO("gap " << cell.x << "," << cell.y << "," << cell.z);
        REQUIRE((cell.y == 0 || cell.y > top[cell.x][cell.z] || world.registry.all_of<Solid>(entity)));
    }
    using Renderer = cactus_gen::stdlib::voxel::volume::VolumeRenderer;
    using Style    = cactus_gen::stdlib::voxel::volume::SurfaceStyle;
    for (auto [entity, renderer] : world.registry.view<Renderer>().each()) {
        REQUIRE(renderer.style == Style::Smooth);
        REQUIRE(renderer.roughness >= 0.35F);
    }
}

TEST_CASE("Dungeon digging opens the whole wall column", "[cactus-dungeon][headless]") {
    World world;
    const auto target = world.tile(1, 4);
    REQUIRE(walled(world.registry.get<game::Tile>(target).kind));
    const auto solid_at = [&](int x) {
        int count = 0;
        for (auto [entity, cell] : world.registry.view<Cell, Solid>().each()) {
            count += cell.y > 0 && cell.z == 4 && cell.x == x ? 1 : 0;
        }
        return count;
    };
    REQUIRE(solid_at(1) > 1);
    const auto border = solid_at(0);
    world.remove_workers();
    world.worker(2, 4);
    world.mark(1, 4);
    for (int i = 0; i < 600 && world.registry.get<game::Tile>(target).kind != game::Kind::Floor; ++i) {
        world.frame();
    }
    world.frame(2);
    REQUIRE(world.registry.get<game::Tile>(target).kind == game::Kind::Floor);
    REQUIRE(solid_at(1) == 0);
    REQUIRE(solid_at(0) == border);
}

TEST_CASE("Dungeon lava never touches water", "[cactus-dungeon][headless]") {
    constexpr int size = 85;
    Volume volume{};
    volume.max_x = size - 1;
    volume.max_y = 4;
    volume.max_z = size - 1;
    int lava  = 0;
    int water = 0;
    for (int seed = 1; seed <= 20; ++seed) {
        const game::Generation recipe{.seed = seed, .starting_robots = 4};
        std::vector<game::Kind> kinds(static_cast<std::size_t>(size * size));
        for (int x = 0; x < size; ++x) {
            for (int z = 0; z < size; ++z) {
                kinds[static_cast<std::size_t>(x * size + z)] =
                    game::kind_at(Cell{.volume = entt::null, .x = x, .y = 1, .z = z, .texture = 0}, volume, recipe);
            }
        }
        const auto at = [&](int x, int z) { return kinds[static_cast<std::size_t>(x * size + z)]; };
        for (int x = 1; x + 1 < size; ++x) {
            for (int z = 1; z + 1 < size; ++z) {
                lava += at(x, z) == game::Kind::Lava ? 1 : 0;
                water += at(x, z) == game::Kind::Water ? 1 : 0;
                if (at(x, z) != game::Kind::Lava) {
                    continue;
                }
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        INFO("seed " << seed << " lava at " << x << "," << z << " water at " << x + dx << "," << z + dz);
                        REQUIRE(at(x + dx, z + dz) != game::Kind::Water);
                    }
                }
            }
        }
    }
    REQUIRE(lava > 0);
    REQUIRE(water > 0);
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
    REQUIRE(world.registry.get<units::Unit>(robot).xp > 0);
    REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(robot, world.tile(7, 4)));
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, world.tile(8, 4)));
}

TEST_CASE("Dungeon infinite gold pays repeatedly without disappearing", "[cactus-dungeon][headless]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(6, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    const auto initial_gold = world.registry.get<game::Player>(world.human()).gold;
    world.frame(180);
    const auto first_gold = world.registry.get<game::Player>(world.human()).gold;
    const auto first_xp   = world.registry.get<units::Unit>(robot).xp;
    REQUIRE(first_gold > initial_gold);
    REQUIRE(first_xp > 0);
    world.frame(180);
    REQUIRE(world.registry.get<game::Player>(world.human()).gold > first_gold);
    REQUIRE(world.registry.get<units::Unit>(robot).xp > first_xp);
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
    const auto robot = world.worker(6, 4);
    world.floor(7, 4);
    world.floor(12, 12);
    world.frame(180);
    auto& owned = backend::relation_store<game::OwnedBy>(world.registry);
    REQUIRE(owned.contains(world.tile(7, 4), world.human()));
    REQUIRE_FALSE(owned.has_from(world.tile(12, 12)));
    REQUIRE(world.registry.all_of<territory::Connected>(world.tile(7, 4)));
    REQUIRE(world.registry.get<units::Unit>(robot).xp > 0);
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

TEST_CASE("Dungeon XP thresholds raise unit stats through level ten", "[cactus-dungeon][headless][levels]") {
    constexpr std::array thresholds{0, 100, 300, 600, 1000, 1500, 2100, 2800, 3600, 4500};
    for (std::size_t i = 1; i < thresholds.size(); ++i) {
        const auto previous = units::stats_for(units::Unit{.xp = thresholds[i] - 1});
        const auto current  = units::stats_for(units::Unit{.xp = thresholds[i]});
        REQUIRE(previous.level == static_cast<int>(i));
        REQUIRE(current.level == static_cast<int>(i + 1));
        REQUIRE(current.max_health > previous.max_health);
        REQUIRE(current.attack > previous.attack);
        REQUIRE(current.defense > previous.defense);
        REQUIRE(current.work_speed > previous.work_speed);
        const auto knight = units::stats_for(units::Unit{.kind = units::UnitKind::Knight, .xp = thresholds[i]});
        REQUIRE(knight.max_health > current.max_health);
        REQUIRE(knight.attack > current.attack);
        REQUIRE(knight.defense > current.defense);
    }
    REQUIRE(units::stats_for(units::Unit{.xp = -10}).level == 1);
    REQUIRE(units::stats_for(units::Unit{.xp = 100000}).level == 10);
}

TEST_CASE("Dungeon robots gain XP for every worker activity", "[cactus-dungeon][headless][levels]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(4, 4);
    auto& unit       = world.registry.get<units::Unit>(robot);
    constexpr std::array activities{units::Activity::Dig,
                                    units::Activity::Mine,
                                    units::Activity::Build,
                                    units::Activity::Claim,
                                    units::Activity::Combat};
    for (const auto activity : activities) {
        const auto before = unit.xp;
        cactus_headless_test::dispatch_boundary_event(world.registry, units::GainXPEvent{.activity = activity});
        REQUIRE(unit.xp > before);
    }
    const auto before = unit.xp;
    cactus_headless_test::dispatch_boundary_event(world.registry,
                                                  units::GainXPEvent{.activity = units::Activity::Train});
    REQUIRE(unit.xp == before);
}

TEST_CASE("Dungeon level ups grow health and keep XP bounded", "[cactus-dungeon][headless][levels]") {
    World world;
    world.remove_workers();
    const auto robot  = world.worker(4, 4);
    auto& unit        = world.registry.get<units::Unit>(robot);
    unit.xp           = 99;
    const auto before = unit.health;
    cactus_headless_test::dispatch_boundary_event(world.registry, units::GainXPEvent{.activity = units::Activity::Dig});
    REQUIRE(units::stats_for(unit).level == 2);
    REQUIRE(unit.health > before);
    unit.xp = 4499;
    for (int i = 0; i < 3; ++i) {
        cactus_headless_test::dispatch_boundary_event(world.registry,
                                                      units::GainXPEvent{.activity = units::Activity::Combat});
    }
    REQUIRE(unit.xp == 4500);
    REQUIRE(units::stats_for(unit).level == 10);
}

TEST_CASE("Dungeon knights gain XP from combat and training", "[cactus-dungeon][headless][levels]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(4, 4);
    auto& unit       = world.registry.get<units::Unit>(robot);
    unit.kind        = units::UnitKind::Knight;
    unit.health      = 300.0F;
    cactus_headless_test::dispatch_boundary_event(world.registry,
                                                  units::GainXPEvent{.activity = units::Activity::Combat});
    const auto combat = unit.xp;
    REQUIRE(combat > 0);
    cactus_headless_test::dispatch_boundary_event(world.registry,
                                                  units::GainXPEvent{.activity = units::Activity::Train});
    REQUIRE(unit.xp > combat);
    const auto trained = unit.xp;
    cactus_headless_test::dispatch_boundary_event(world.registry, units::GainXPEvent{.activity = units::Activity::Dig});
    REQUIRE(unit.xp == trained);
}

TEST_CASE("Dungeon working grants XP and higher levels work faster", "[cactus-dungeon][headless][levels]") {
    const auto progress = [](int xp) {
        World world;
        world.remove_workers();
        const auto robot                          = world.worker(6, 4);
        world.registry.get<units::Unit>(robot).xp = xp;
        world.mark(7, 4);
        world.frame(20);
        return world.registry.get<tasks::Work>(world.tile(7, 4)).progress;
    };
    const auto novice = progress(0);
    REQUIRE(novice > 0.0F);
    REQUIRE(progress(4500) > novice);

    World world;
    world.remove_workers();
    const auto robot = world.worker(6, 4);
    world.mark(7, 4);
    world.frame(90);
    REQUIRE(world.registry.get<units::Unit>(robot).xp > 0);
}

TEST_CASE("Dungeon manual movement interrupts work and then resumes", "[cactus-dungeon][headless][orders]") {
    World world;
    world.remove_workers();
    const auto robot  = world.worker(6, 4);
    const auto target = world.tile(7, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    world.frame(12);
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
    backend::generated_process_root_event(world.registry, units::MoveRequestedEvent{world.tile(2, 2)}, robot);
    const auto progress = world.registry.get<tasks::Work>(target).progress;
    world.frame();
    REQUIRE(world.registry.all_of<units::Order::Move>(robot));
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).has_from(robot));
    REQUIRE(world.registry.get<tasks::Work>(target).progress == progress);
    bool arrived = false;
    for (int i = 0; i < 300; ++i) {
        world.frame();
        if (backend::relation_store<tasks::Located>(world.registry).contains(robot, world.tile(2, 2))) {
            arrived = true;
        }
    }
    REQUIRE(arrived);
    REQUIRE(world.registry.all_of<units::Order::Auto>(robot));
    world.frame(360);
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
    REQUIRE(world.registry.get<tasks::Work>(target).progress > progress);
}

TEST_CASE("Dungeon units walk between tile centers and face their next tile", "[cactus-dungeon][headless][walking]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(2, 4);
    backend::generated_process_root_event(world.registry, units::MoveRequestedEvent{world.tile(5, 4)}, robot);
    bool between      = false;
    float previous_x  = world.registry.get<Transform>(robot).position.x;
    float longest     = 0.0F;
    bool faced_east   = false;
    bool arrived      = false;
    for (int i = 0; i < 240 && !arrived; ++i) {
        world.frame();
        const auto& pose = world.registry.get<Transform>(robot);
        const float x    = pose.position.x;
        longest          = std::max(longest, std::abs(x - previous_x));
        REQUIRE(x >= previous_x - 0.0001F);
        previous_x = x;
        between    = between || (x > 2.6F && x < 3.4F) || (x > 3.6F && x < 4.4F);
        if (x > 2.6F && x < 5.4F) {
            faced_east = faced_east || (std::abs(pose.rotation.y - 0.7071F) < 0.01F && std::abs(pose.rotation.w - 0.7071F) < 0.01F);
        }
        arrived = backend::relation_store<tasks::Located>(world.registry).contains(robot, world.tile(5, 4));
    }
    REQUIRE(arrived);
    REQUIRE(between);
    REQUIRE(faced_east);
    REQUIRE(longest < 0.25F);
    REQUIRE(world.registry.get<Transform>(robot).position.x == 5.5F);
}

TEST_CASE("Dungeon units walk faster with level and kind", "[cactus-dungeon][headless][walking]") {
    units::Unit novice{.kind = units::UnitKind::Robot, .xp = 0, .health = 100.0F};
    units::Unit veteran{.kind = units::UnitKind::Robot, .xp = 4500, .health = 100.0F};
    units::Unit knight{.kind = units::UnitKind::Knight, .xp = 0, .health = 300.0F};
    REQUIRE(units::stats_for(veteran).move_speed > units::stats_for(novice).move_speed);
    REQUIRE(units::stats_for(knight).move_speed != units::stats_for(novice).move_speed);
}

TEST_CASE("Dungeon unit clips follow their state and restart only on change", "[cactus-dungeon][headless][animation]") {
    namespace animation = cactus_gen::animation;
    using Animator      = cactus_gen::stdlib::render::models::ModelAnimator;
    using Playback      = cactus_gen::stdlib::render::models::ModelPlayback;
    World world;
    world.remove_workers();
    const auto robot = world.worker(5, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    world.frame();
    REQUIRE(world.registry.get<Animator>(robot).clip == animation::ROBOT_CLIPS.idle);
    bool walked  = false;
    int switches = 0;
    int previous = world.registry.get<Animator>(robot).clip;
    int revision = world.registry.get<Playback>(robot).revision;
    for (int i = 0; i < 180; ++i) {
        world.frame();
        const auto& animator = world.registry.get<Animator>(robot);
        const int current    = world.registry.get<Playback>(robot).revision;
        const bool changed   = animator.clip != previous;
        walked               = walked || animator.clip == animation::ROBOT_CLIPS.walk;
        REQUIRE(current - revision == (changed ? 1 : 0));
        switches += changed ? 1 : 0;
        previous = animator.clip;
        revision = current;
    }
    REQUIRE(walked);
    REQUIRE(previous == animation::ROBOT_CLIPS.work);
    REQUIRE(switches == 2);
}

TEST_CASE("Dungeon units use the CC0 robot and knight models", "[cactus-dungeon][headless][animation]") {
    namespace animation = cactus_gen::animation;
    using Animator      = cactus_gen::stdlib::render::models::ModelAnimator;
    World world;
    world.frame(2);
    for (auto [entity, unit, animator] : world.registry.view<units::Unit, Animator>().each()) {
        REQUIRE(animator.clip == animation::clip_for(unit.kind, animation::Pose::Idle));
    }
    REQUIRE(animation::clip_for(units::UnitKind::Knight, animation::Pose::Walk) == animation::KNIGHT_CLIPS.walk);
    REQUIRE(animation::KNIGHT_CLIPS.walk != animation::ROBOT_CLIPS.walk);
    const auto art = std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "cactus-dungeon" / "art";
    REQUIRE(std::filesystem::exists(art / "generate_textures.py"));
    REQUIRE_FALSE(std::filesystem::exists(art / "robot.glb"));
    REQUIRE_FALSE(std::filesystem::exists(art / "knight.glb"));
}

TEST_CASE("Dungeon idle robots wander inside their territory", "[cactus-dungeon][headless][walking]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(4, 4);
    auto& located    = backend::relation_store<tasks::Located>(world.registry);
    auto& owned      = backend::relation_store<game::OwnedBy>(world.registry);
    int moves        = 0;
    entt::entity at  = world.tile(4, 4);
    for (int i = 0; i < 600; ++i) {
        world.frame();
        for (int x = 1; x < 9; ++x) {
            for (int z = 1; z < 9; ++z) {
                const auto tile = world.tile(x, z);
                if (tile != at && located.contains(robot, tile)) {
                    REQUIRE(owned.contains(tile, world.human()));
                    at = tile;
                    ++moves;
                }
            }
        }
    }
    REQUIRE(moves >= 2);
}

TEST_CASE("Dungeon already reached and unreachable orders return to auto", "[cactus-dungeon][headless][orders]") {
    World world;
    world.remove_workers();
    const auto robot         = world.worker(4, 4);
    entt::entity destination = entt::null;
    SECTION("already at the destination") {
        destination = world.tile(4, 4);
    }
    SECTION("unreachable destination") {
        destination = world.tile(0, 0);
    }
    backend::generated_process_root_event(world.registry, units::MoveRequestedEvent{destination}, robot);
    world.frame(3);
    REQUIRE(world.registry.all_of<units::Order::Auto>(robot));
    REQUIRE(world.registry.all_of<units::OrderRequest::None>(robot));
    REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(robot, world.tile(4, 4)));
}

TEST_CASE("Dungeon canceling a manual order restores automatic work", "[cactus-dungeon][headless][orders]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(6, 4);
    backend::generated_process_root_event(world.registry, units::DefendRequestedEvent{world.tile(6, 4)}, robot);
    world.frame(2);
    REQUIRE(world.registry.all_of<units::Order::Defend>(robot));
    backend::generated_process_root_event(world.registry, units::CancelRequestedEvent{}, robot);
    world.frame(2);
    REQUIRE(world.registry.all_of<units::Order::Auto>(robot));
}

TEST_CASE("Dungeon attack requests keep their target and release work", "[cactus-dungeon][headless][orders]") {
    World world;
    world.remove_workers();
    const auto robot = world.worker(6, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    world.frame(8);
    const auto destination = world.worker(20, 20);
    auto& commands         = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(destination);
    commands.add(destination, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    backend::generated_process_root_event(world.registry, units::AttackRequestedEvent{destination}, robot);
    world.frame(2);
    REQUIRE(world.registry.all_of<units::Order::Attack>(robot));
    REQUIRE(world.registry.get<units::Order::Attack>(robot).target == destination);
    REQUIRE_FALSE(backend::relation_store<tasks::Claim>(world.registry).has_from(robot));
    backend::generated_process_root_event(world.registry, units::CancelRequestedEvent{}, robot);
    world.frame(3);
    REQUIRE(world.registry.all_of<units::Order::Auto>(robot));
}

TEST_CASE("Dungeon larger starting crews stay on owned walkable tiles", "[cactus-dungeon][headless][crews]") {
    World world(7, 10);
    world.frame(4);
    REQUIRE(world.registry.view<game::Robot>().size() == 20);
    auto& locations = backend::relation_store<tasks::Located>(world.registry);
    auto& commands  = backend::relation_store<tasks::Commands>(world.registry);
    auto& owned     = backend::relation_store<game::OwnedBy>(world.registry);
    for (const auto robot : world.registry.view<game::Robot>()) {
        REQUIRE(commands.has_from(robot));
        int places = 0;
        locations.for_each_from(robot, [&](const auto& place) {
            ++places;
            REQUIRE(world.registry.get<game::Tile>(place.to).kind == game::Kind::Floor);
            REQUIRE(owned.has_from(place.to));
        });
        REQUIRE(places == 1);
    }
}

TEST_CASE("Dungeon room plans charge once and reject invalid ground", "[cactus-dungeon][rooms][headless]") {
    World world(7, 0);
    world.frame(8);
    namespace rooms  = cactus_gen::rooms;
    auto& plans      = backend::relation_store<rooms::RoomPlan>(world.registry);
    const auto valid = world.tile(3, 3);
    REQUIRE(world.registry.all_of<territory::Connected>(valid));
    const auto wall         = world.tile(7, 3);
    const auto neutral      = world.tile(8, 3);
    const auto disconnected = world.tile(9, 3);
    world.floor(8, 3);
    world.floor(9, 3);
    backend::relation_store<game::OwnedBy>(world.registry).add(disconnected, world.human(), {});
    const auto gold = world.registry.get<game::Player>(world.human()).gold;
    for (const auto tile : {valid, wall, neutral, disconnected}) {
        plans.add(world.human(), tile, rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    }
    world.frame(5);
    REQUIRE(world.registry.all_of<rooms::RoomTile>(valid));
    for (const auto tile : {wall, neutral, disconnected}) {
        REQUIRE_FALSE(world.registry.all_of<rooms::RoomTile>(tile));
    }
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == gold - rooms::price(rooms::RoomKind::Lair));
    plans.add(world.human(), valid, rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    world.frame(3);
    REQUIRE(world.registry.get<rooms::RoomTile>(valid).kind == rooms::RoomKind::Lair);
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == gold - rooms::price(rooms::RoomKind::Lair));
    world.registry.get<game::Player>(world.human()).gold = 0;
    plans.add(world.human(), world.tile(3, 4), rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    world.frame(3);
    REQUIRE_FALSE(world.registry.all_of<rooms::RoomTile>(world.tile(3, 4)));
}

TEST_CASE("Dungeon rooms merge by oldest tile and selling shrinks capacity", "[cactus-dungeon][rooms][headless]") {
    World world(7, 0);
    world.frame(8);
    namespace rooms  = cactus_gen::rooms;
    auto& plans      = backend::relation_store<rooms::RoomPlan>(world.registry);
    const auto left  = world.tile(3, 3);
    const auto gap   = world.tile(4, 3);
    const auto right = world.tile(5, 3);
    for (const auto tile : {left, right}) {
        plans.add(world.human(), tile, rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    }
    world.frame(5);
    REQUIRE(world.registry.view<rooms::RoomStats>().size() == 2);
    REQUIRE(world.registry.get<rooms::RoomStats>(left).capacity == 1);
    REQUIRE(world.registry.get<rooms::RoomStats>(right).capacity == 1);
    auto& membership = backend::derived_relation<rooms::Room>(world.registry);
    REQUIRE(membership.contains(left, left));
    REQUIRE(membership.contains(right, right));
    plans.add(world.human(), gap, rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    world.frame(5);
    REQUIRE(world.registry.view<rooms::RoomStats>().size() == 1);
    REQUIRE(membership.contains(right, left));
    REQUIRE(world.registry.get<rooms::RoomStats>(left).capacity == 3);
    const auto gold = world.registry.get<game::Player>(world.human()).gold;
    plans.add(world.human(), gap, rooms::RoomPlan{.sell = true});
    world.frame(5);
    REQUIRE_FALSE(world.registry.all_of<rooms::RoomTile>(gap));
    REQUIRE(world.registry.get<game::Tile>(gap).kind == game::Kind::Floor);
    REQUIRE(backend::relation_store<game::OwnedBy>(world.registry).contains(gap, world.human()));
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == gold + rooms::price(rooms::RoomKind::Lair) / 2);
    REQUIRE(world.registry.view<rooms::RoomStats>().size() == 2);
    REQUIRE(world.registry.get<rooms::RoomStats>(left).capacity == 1);
    REQUIRE(world.registry.get<rooms::RoomStats>(right).capacity == 1);
}

TEST_CASE("Dungeon disconnected rooms stay built but inactive", "[cactus-dungeon][rooms][headless]") {
    World world(7, 0);
    world.frame(8);
    namespace rooms                             = cactus_gen::rooms;
    const auto bridge                           = world.tile(7, 4);
    const auto island                           = world.tile(8, 4);
    world.registry.get<game::Tile>(bridge).kind = game::Kind::Water;
    world.registry.remove<Solid>(bridge);
    world.floor(8, 4);
    backend::relation_store<game::OwnedBy>(world.registry).add(island, world.human(), {});
    auto& bridges = backend::relation_store<territory::BridgePlan>(world.registry);
    bridges.add(world.human(), bridge, {});
    world.frame(8);
    backend::relation_store<rooms::RoomPlan>(world.registry)
        .add(world.human(), island, rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    world.frame(5);
    REQUIRE(world.registry.all_of<rooms::ActiveRoom>(island));
    bridges.add(world.human(), bridge, territory::BridgePlan{.sell = true});
    world.frame(5);
    REQUIRE(world.registry.all_of<rooms::RoomTile, rooms::RoomStats>(island));
    REQUIRE_FALSE(world.registry.all_of<rooms::ActiveRoom>(island));
    bridges.add(world.human(), bridge, {});
    world.frame(8);
    REQUIRE(world.registry.all_of<rooms::ActiveRoom>(island));
}

TEST_CASE("Dungeon painted bridges cost gold and refund half on sale", "[cactus-dungeon][rooms][headless]") {
    World world(7, 0);
    world.frame(8);
    namespace rooms    = cactus_gen::rooms;
    const auto bridge  = world.tile(7, 4);
    const auto distant = world.tile(10, 4);
    for (const auto tile : {bridge, distant}) {
        world.registry.get<game::Tile>(tile).kind = game::Kind::Water;
        world.registry.remove<Solid>(tile);
    }
    world.frame(2);
    auto& plans     = backend::relation_store<rooms::RoomPlan>(world.registry);
    const auto gold = world.registry.get<game::Player>(world.human()).gold;
    for (const auto tile : {bridge, distant, world.tile(3, 3)}) {
        plans.add(world.human(), tile, rooms::RoomPlan{.kind = rooms::RoomKind::Bridge});
    }
    world.frame(8);
    REQUIRE(world.registry.all_of<territory::Bridge>(bridge));
    REQUIRE_FALSE(world.registry.all_of<territory::Bridge>(distant));
    REQUIRE_FALSE(world.registry.all_of<territory::Bridge>(world.tile(3, 3)));
    REQUIRE_FALSE(world.registry.all_of<rooms::RoomTile>(bridge));
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == gold - rooms::price(rooms::RoomKind::Bridge));
    plans.add(world.human(), bridge, rooms::RoomPlan{.kind = rooms::RoomKind::Bridge, .sell = true});
    world.frame(8);
    REQUIRE_FALSE(world.registry.all_of<territory::Bridge>(bridge));
    REQUIRE(world.registry.get<game::Tile>(bridge).kind == game::Kind::Water);
    REQUIRE_FALSE(backend::relation_store<game::OwnedBy>(world.registry).has_from(bridge));
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == gold - rooms::price(rooms::RoomKind::Bridge) / 2);
}

TEST_CASE("Dungeon separate six and four tile lairs retain their own capacity", "[cactus-dungeon][rooms][headless]") {
    World world(7, 0);
    world.frame(8);
    namespace rooms = cactus_gen::rooms;
    auto& plans     = backend::relation_store<rooms::RoomPlan>(world.registry);
    const std::array<std::pair<int, int>, 10> tiles{
        {{2, 2}, {2, 3}, {2, 4}, {3, 2}, {3, 3}, {3, 4}, {5, 2}, {5, 3}, {6, 2}, {6, 3}}};
    for (const auto [x, z] : tiles) {
        plans.add(world.human(), world.tile(x, z), rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    }
    world.frame(8);
    REQUIRE(world.registry.view<rooms::RoomStats>().size() == 2);
    REQUIRE(world.registry.get<rooms::RoomStats>(world.tile(2, 2)).capacity == 6);
    REQUIRE(world.registry.get<rooms::RoomStats>(world.tile(5, 2)).capacity == 4);
    plans.add(world.human(), world.tile(4, 3), rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    world.frame(8);
    REQUIRE(world.registry.view<rooms::RoomStats>().size() == 1);
    REQUIRE(world.registry.get<rooms::RoomStats>(world.tile(2, 2)).capacity == 11);
    REQUIRE(backend::derived_relation<rooms::Room>(world.registry).contains(world.tile(6, 3), world.tile(2, 2)));
}

TEST_CASE("Dungeon recruitment requires a free bed and food", "[cactus-dungeon][knights][headless]") {
    namespace rooms   = cactus_gen::rooms;
    namespace knights = cactus_gen::knights;
    World world(7, 0);
    world.frame(8);
    auto& plans = backend::relation_store<rooms::RoomPlan>(world.registry);
    backend::generated_process_root_event(world.registry, knights::RecruitRequestedEvent{}, world.human());
    world.frame(8);
    REQUIRE(world.registry.view<knights::Knight>().size() == 0);
    plans.add(world.human(), world.tile(3, 3), rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    world.frame(8);
    REQUIRE(world.registry.view<knights::Knight>().size() == 0);
    plans.add(world.human(), world.tile(5, 3), rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    world.frame(8);
    world.registry.get<knights::FarmStock>(world.tile(5, 3)).food = 2;
    world.frame(12);
    REQUIRE(world.registry.view<knights::Knight>().size() == 1);
    for (const auto knight : world.registry.view<knights::Knight>()) {
        REQUIRE(backend::relation_store<tasks::Commands>(world.registry).contains(knight, world.human()));
        REQUIRE(backend::relation_store<knights::Sleeps>(world.registry).contains(knight, world.tile(3, 3)));
        REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(knight, world.tile(4, 4)));
        REQUIRE(world.registry.get<units::Unit>(knight).kind == units::UnitKind::Knight);
    }
    REQUIRE(world.registry.get<knights::FarmStock>(world.tile(5, 3)).food == 1);
    REQUIRE(world.registry.get<knights::BedFill>(world.tile(3, 3)).count == 1);
    backend::generated_process_root_event(world.registry, knights::RecruitRequestedEvent{}, world.human());
    world.frame(12);
    REQUIRE(world.registry.view<knights::Knight>().size() == 1);
}

TEST_CASE("Dungeon knights travel to food and their beds", "[cactus-dungeon][knights][headless]") {
    namespace rooms   = cactus_gen::rooms;
    namespace knights = cactus_gen::knights;
    World world(7, 0);
    world.frame(8);
    auto& plans = backend::relation_store<rooms::RoomPlan>(world.registry);
    plans.add(world.human(), world.tile(2, 2), rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    plans.add(world.human(), world.tile(6, 6), rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    world.frame(8);
    auto& food  = world.registry.get<knights::FarmStock>(world.tile(6, 6));
    food.food   = 3;
    food.growth = -1000.0F;
    backend::generated_process_root_event(world.registry, knights::RecruitRequestedEvent{}, world.human());
    world.frame(12);
    REQUIRE(world.registry.view<knights::Knight>().size() == 1);
    const auto knight = *world.registry.view<knights::Knight>().begin();
    auto& needs       = world.registry.get<knights::Knight>(knight);
    needs.hunger      = 6.0F;
    REQUIRE(food.food == 2);
    float hungry = needs.hunger;
    for (int i = 0; i < 360 && food.food == 2; ++i) {
        hungry = needs.hunger;
        world.frame();
    }
    REQUIRE(food.food == 1);
    REQUIRE(needs.hunger < hungry - 4.9F);
    REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(knight, world.tile(6, 6)));
    needs.fatigue                                  = 11.0F;
    world.registry.get<units::Unit>(knight).health = 200.0F;
    backend::generated_process_root_event(world.registry, units::DefendRequestedEvent{world.tile(6, 6)}, knight);
    world.frame(12);
    REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(knight, world.tile(6, 6)));
    REQUIRE(world.registry.get<units::Unit>(knight).health == 200.0F);
    backend::generated_process_root_event(world.registry, units::CancelRequestedEvent{}, knight);
    for (int i = 0; i < 900 && needs.fatigue >= 10.0F; ++i) {
        needs.hunger = 0.0F;
        world.frame();
    }
    REQUIRE(needs.fatigue < 10.0F);
    REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(knight, world.tile(2, 2)));
    REQUIRE(world.registry.get<units::Unit>(knight).health > 200.0F);
    world.registry.get<units::Unit>(knight).health = 0.0F;
    world.frame(100);
    REQUIRE_FALSE(world.registry.valid(knight));
}

TEST_CASE("Dungeon training pays gold for XP and disconnected farms stop growing",
          "[cactus-dungeon][knights][headless]") {
    namespace rooms   = cactus_gen::rooms;
    namespace knights = cactus_gen::knights;
    World world(7, 0);
    world.frame(8);
    auto& plans = backend::relation_store<rooms::RoomPlan>(world.registry);
    plans.add(world.human(), world.tile(3, 3), rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    plans.add(world.human(), world.tile(5, 3), rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    plans.add(world.human(), world.tile(5, 5), rooms::RoomPlan{.kind = rooms::RoomKind::Training});
    world.frame(8);
    auto& stock = world.registry.get<knights::FarmStock>(world.tile(5, 3));
    stock.food  = 2;
    backend::generated_process_root_event(world.registry, knights::RecruitRequestedEvent{}, world.human());
    world.frame(12);
    const auto knight = *world.registry.view<knights::Knight>().begin();
    const auto gold   = world.registry.get<game::Player>(world.human()).gold;
    world.frame(240);
    REQUIRE(world.registry.get<units::Unit>(knight).xp >= 5);
    REQUIRE(world.registry.get<game::Player>(world.human()).gold < gold);
    REQUIRE(stock.food >= 2);
    world.registry.get<game::Player>(world.human()).gold = 0;
    const auto xp                                        = world.registry.get<units::Unit>(knight).xp;
    world.frame(90);
    REQUIRE(world.registry.get<units::Unit>(knight).xp == xp);
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == 0);
    stock.food = 0;
    backend::relation_store<game::OwnedBy>(world.registry).erase_from(world.tile(5, 3));
    world.frame(90);
    REQUIRE(stock.food == 0);
}

TEST_CASE("Dungeon knights defend their own territory", "[cactus-dungeon][knights][headless]") {
    namespace knights = cactus_gen::knights;
    World world(7, 0);
    world.frame(8);
    const auto defender =
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{4.5F, 1.25F, 4.5F}, world.tile(3, 3));
    const auto enemy = world.worker(5, 4);
    auto& commands   = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(enemy);
    commands.add(enemy, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    world.frame(12);
    using Attack = units::Order::Attack;
    REQUIRE(world.registry.all_of<Attack>(defender));
    REQUIRE(world.registry.get<Attack>(defender).target == enemy);
}

TEST_CASE("Dungeon opposing units fight while friendly units do not", "[cactus-dungeon][combat][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto first  = world.worker(4, 4);
    const auto second = world.worker(5, 4);
    SECTION("friendly units") {
        world.frame(90);
        REQUIRE(world.registry.get<units::Unit>(first).health == 100.0F);
        REQUIRE(world.registry.get<units::Unit>(second).health == 100.0F);
        REQUIRE(world.registry.get<game::Heart>(world.tile(4, 4)).health == 2000.0F);
    }
    SECTION("opposing units") {
        auto& commands = backend::relation_store<tasks::Commands>(world.registry);
        commands.erase_from(second);
        commands.add(second, cactus_gen::detail::generated_named_slots().world.Enemy, {});
        world.frame(90);
        REQUIRE(world.registry.get<units::Unit>(first).health < 100.0F);
        REQUIRE(world.registry.get<units::Unit>(second).health < 100.0F);
        REQUIRE(world.registry.get<units::Unit>(first).health > 0.0F);
    }
}

TEST_CASE("Dungeon equal level knights beat robots and death loses XP", "[cactus-dungeon][combat][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto knight =
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{4.5F, 1.25F, 4.5F}, world.tile(3, 3));
    const auto robot                          = world.worker(5, 4);
    world.registry.get<units::Unit>(robot).xp = 80;
    auto& commands                            = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(robot);
    commands.add(robot, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    world.frame(400);
    REQUIRE(world.registry.valid(knight));
    REQUIRE_FALSE(world.registry.valid(robot));
    REQUIRE(world.registry.get<units::Unit>(knight).health > 0.0F);
    REQUIRE(world.registry.get<units::Unit>(knight).xp >= 20);
    const auto replacement = world.worker(6, 6);
    REQUIRE(world.registry.get<units::Unit>(replacement).xp == 0);
    world.registry.get<units::Unit>(replacement).health = 0.0F;
    world.frame(100);
    REQUIRE_FALSE(world.registry.valid(replacement));
}

TEST_CASE("Dungeon hearts take damage without attacking", "[cactus-dungeon][combat][headless]") {
    namespace combat = cactus_gen::combat;
    World world(7, 0);
    world.frame(8);
    const auto robot = world.worker(4, 4);
    auto& commands   = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(robot);
    commands.add(robot, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    const auto heart = world.tile(4, 4);
    world.frame(60);
    REQUIRE(world.registry.get<game::Heart>(heart).health < 2000.0F);
    REQUIRE(world.registry.get<units::Unit>(robot).health == 100.0F);
    REQUIRE_FALSE(world.registry.all_of<units::Unit>(heart));
    REQUIRE(world.registry.get<combat::Outcome>(world.human()).status == combat::BattleResult::Active);
}

TEST_CASE("Dungeon simultaneous kills cannot restore dead units through a level gain",
          "[cactus-dungeon][combat][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto first  = world.worker(4, 4);
    const auto second = world.worker(5, 4);
    auto& commands    = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(second);
    commands.add(second, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    for (const auto unit : {first, second}) {
        auto& stats  = world.registry.get<units::Unit>(unit);
        stats.xp     = 80;
        stats.health = 0.01F;
    }
    world.frame(100);
    REQUIRE_FALSE(world.registry.valid(first));
    REQUIRE_FALSE(world.registry.valid(second));
}

TEST_CASE("Dungeon a fallen heart records victory and defeat", "[cactus-dungeon][combat][headless]") {
    namespace combat = cactus_gen::combat;
    World world(7, 0);
    world.frame(8);
    const auto enemy = cactus_gen::detail::generated_named_slots().world.Enemy;
    SECTION("human heart falls") {
        world.registry.get<game::Heart>(world.tile(4, 4)).health = 0.0F;
        world.frame(2);
        REQUIRE(world.registry.get<combat::Outcome>(world.human()).status == combat::BattleResult::Lost);
        REQUIRE(world.registry.get<combat::Outcome>(enemy).status == combat::BattleResult::Won);
        REQUIRE_FALSE(world.registry.all_of<game::Heart>(world.tile(4, 4)));
        REQUIRE_FALSE(world.registry.all_of<cactus_gen::stdlib::render::models::ModelRenderer>(world.tile(4, 4)));
        REQUIRE(world.registry.all_of<game::Tile>(world.tile(4, 4)));
    }
    SECTION("enemy heart falls") {
        world.registry.get<game::Heart>(world.tile(kSize - 5, kSize - 5)).health = 0.0F;
        world.frame(2);
        REQUIRE(world.registry.get<combat::Outcome>(world.human()).status == combat::BattleResult::Won);
        REQUIRE(world.registry.get<combat::Outcome>(enemy).status == combat::BattleResult::Lost);
    }
}

namespace controls = cactus_gen::controls;
namespace camera   = cactus_gen::stdlib::camera::volume;

entt::entity overview() {
    return cactus_gen::detail::generated_named_slots().controls.Overview;
}

Vector2 screen_at(World& world, entt::entity entity) {
    auto position = world.registry.get<Transform>(entity).position;
    if (world.registry.all_of<Cell>(entity) && !world.registry.all_of<game::Heart>(entity)) {
        position.y = world.registry.all_of<Solid>(entity) ? 2.0F : 1.0F;
    }
    return camera::to_screen(world.registry.get<camera::CameraView>(overview()), position).position;
}

void select_at(World& world, Vector2 start, Vector2 end, bool additive = false, bool box = false) {
    auto& selector = world.registry.get<controls::Selector>(overview());
    selector.start = start;
    selector.point = end;
    selector.shift = additive;
    selector.box   = box;
    backend::generated_process_root_event(world.registry, controls::SelectionCommitEvent{}, overview());
    world.frame(2);
}

bool selected(World& world, entt::entity unit) {
    return world.registry.get<controls::SelectionStatus>(unit).active;
}

void context_at(World& world, Vector2 point, bool defend = false) {
    cactus_raylib_fake::set_mouse_position(point);
    world.frame(2);
    auto& selector  = world.registry.get<controls::Selector>(overview());
    selector.point  = point;
    selector.defend = defend;
    selector.taken  = false;
    backend::generated_process_root_event(world.registry, controls::ContextCommitEvent{}, overview());
}

TEST_CASE("Dungeon click and shift click select only friendly units", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto robot = world.worker(3, 3);
    const auto knight =
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{6.5F, 1.25F, 3.5F}, world.tile(2, 2));
    const auto enemy = world.worker(20, 20);
    auto& commands   = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(enemy);
    commands.add(enemy, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    auto& rig = world.registry.get<controls::Rig>(overview());
    rig.focus = Vector3{11.5F, 0.0F, 11.5F};
    rig.zoom  = 40.0F;
    world.frame(8);
    const auto robot_pixel  = screen_at(world, robot);
    const auto knight_pixel = screen_at(world, knight);
    cactus_raylib_fake::set_mouse_position(robot_pixel);
    cactus_raylib_fake::set_mouse_button_down(MOUSE_BUTTON_LEFT, true);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
    world.frame();
    cactus_raylib_fake::set_mouse_button_down(MOUSE_BUTTON_LEFT, false);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, false);
    cactus_raylib_fake::set_mouse_button_released_this_frame(MOUSE_BUTTON_LEFT, true);
    world.frame(2);
    cactus_raylib_fake::set_mouse_button_released_this_frame(MOUSE_BUTTON_LEFT, false);
    REQUIRE(selected(world, robot));
    REQUIRE_FALSE(selected(world, knight));
    select_at(world, knight_pixel, knight_pixel, true);
    REQUIRE(selected(world, robot));
    REQUIRE(selected(world, knight));
    select_at(world, robot_pixel, robot_pixel, true);
    REQUIRE_FALSE(selected(world, robot));
    REQUIRE(selected(world, knight));
    const auto enemy_pixel = screen_at(world, enemy);
    select_at(world, enemy_pixel, enemy_pixel, true);
    REQUIRE_FALSE(selected(world, enemy));
    REQUIRE(selected(world, knight));
    REQUIRE(world.registry.all_of<controls::Highlight>(knight));
    REQUIRE_FALSE(world.registry.all_of<controls::Highlight>(robot));
    using ModelRenderer      = cactus_gen::stdlib::render::models::ModelRenderer;
    const auto& robot_model  = world.registry.get<ModelRenderer>(robot);
    const auto& knight_model = world.registry.get<ModelRenderer>(knight);
    REQUIRE(robot_model.model != knight_model.model);
    REQUIRE(robot_model.color.r == 112);
    REQUIRE(knight_model.color.r == 255);
    REQUIRE(knight_model.color.g == 223);
}

TEST_CASE("Dungeon drag box selects and highlights three of five mixed units", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(8);
    const std::array<entt::entity, 3> inside{
        world.worker(3, 3),
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{4.5F, 1.25F, 3.5F}, world.tile(2, 2)),
        world.worker(5, 3)};
    const std::array<entt::entity, 2> outside{
        world.worker(2, 6),
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{6.5F, 1.25F, 6.5F}, world.tile(2, 3))};
    world.frame(8);
    Vector2 lo = screen_at(world, inside.front());
    Vector2 hi = lo;
    for (const auto unit : inside) {
        const auto p = screen_at(world, unit);
        lo.x         = std::min(lo.x, p.x);
        lo.y         = std::min(lo.y, p.y);
        hi.x         = std::max(hi.x, p.x);
        hi.y         = std::max(hi.y, p.y);
    }
    select_at(world, Vector2{lo.x - 3.0F, lo.y - 3.0F}, Vector2{hi.x + 3.0F, hi.y + 3.0F}, false, true);
    for (const auto unit : inside) {
        REQUIRE(selected(world, unit));
        REQUIRE(world.registry.all_of<controls::Highlight>(unit));
    }
    for (const auto unit : outside) {
        REQUIRE_FALSE(selected(world, unit));
        REQUIRE_FALSE(world.registry.all_of<controls::Highlight>(unit));
    }
}

TEST_CASE("Dungeon context orders move, defend and attack for mixed selections",
          "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto robot = world.worker(4, 4);
    const auto knight =
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{5.5F, 1.25F, 4.5F}, world.tile(2, 2));
    world.frame(8);
    for (const auto unit : {robot, knight}) {
        world.registry.get<controls::SelectionStatus>(unit).active = true;
    }
    const auto destination = world.tile(2, 2);
    context_at(world, screen_at(world, destination));
    for (const auto unit : {robot, knight}) {
        REQUIRE(world.registry.all_of<units::OrderRequest::Move>(unit));
        REQUIRE(world.registry.get<units::OrderRequest::Move>(unit).target == destination);
    }
    world.frame(2);
    context_at(world, screen_at(world, world.tile(3, 3)), true);
    for (const auto unit : {robot, knight}) {
        REQUIRE(world.registry.all_of<units::OrderRequest::Defend>(unit));
        REQUIRE(world.registry.get<units::OrderRequest::Defend>(unit).target == world.tile(3, 3));
    }
    world.frame(2);
    const auto enemy = world.worker(6, 6);
    auto& commands   = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(enemy);
    commands.add(enemy, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    world.frame(3);
    context_at(world, screen_at(world, enemy));
    for (const auto unit : {robot, knight}) {
        REQUIRE(world.registry.all_of<units::OrderRequest::Attack>(unit));
        REQUIRE(world.registry.get<units::OrderRequest::Attack>(unit).target == enemy);
    }
    auto& rig = world.registry.get<controls::Rig>(overview());
    rig.focus = Vector3{11.5F, 0.0F, 11.5F};
    rig.zoom  = 40.0F;
    world.frame(3);
    const auto heart = world.tile(kSize - 5, kSize - 5);
    context_at(world, screen_at(world, heart));
    for (const auto unit : {robot, knight}) {
        REQUIRE(world.registry.all_of<units::OrderRequest::Attack>(unit));
        REQUIRE(world.registry.get<units::OrderRequest::Attack>(unit).target == heart);
    }
}

void press_key(World& world, int key) {
    cactus_raylib_fake::set_key_down(key, true);
    cactus_raylib_fake::set_key_pressed_this_frame(key, true);
    world.frame();
    cactus_raylib_fake::set_key_pressed_this_frame(key, false);
    cactus_raylib_fake::set_key_down(key, false);
}

TEST_CASE("Dungeon camera pans with WASD, arrows and screen edges", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(8);
    auto& rig = world.registry.get<controls::Rig>(overview());
    cactus_raylib_fake::set_mouse_position(Vector2{640.0F, 360.0F});
    const auto initial = rig.focus;
    cactus_raylib_fake::set_key_down(KEY_W, true);
    world.frame(5);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    REQUIRE(rig.focus.z < initial.z);
    const auto before_arrow = rig.focus;
    cactus_raylib_fake::set_key_down(KEY_RIGHT, true);
    world.frame(5);
    cactus_raylib_fake::set_key_down(KEY_RIGHT, false);
    REQUIRE(rig.focus.x > before_arrow.x);
    const auto before_edge = rig.focus;
    cactus_raylib_fake::set_mouse_position(Vector2{1279.0F, 360.0F});
    world.frame(5);
    REQUIRE(rig.focus.x > before_edge.x);
    REQUIRE(rig.focus.z == before_edge.z);
    cactus_raylib_fake::set_mouse_position(Vector2{640.0F, 360.0F});
    world.frame();
    const auto settled = rig.focus;
    world.frame(5);
    REQUIRE(rig.focus.x == settled.x);
    cactus_raylib_fake::set_mouse_position(Vector2{1279.0F, 360.0F});
    world.frame(600);
    REQUIRE(rig.focus.x == static_cast<float>(kSize - 2));
}

TEST_CASE("Dungeon camera turns in eased quarter steps and zooms within bounds", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(8);
    cactus_raylib_fake::set_mouse_position(Vector2{640.0F, 360.0F});
    auto& rig = world.registry.get<controls::Rig>(overview());
    press_key(world, KEY_E);
    REQUIRE(rig.yaw > 0.0F);
    REQUIRE(rig.yaw < 1.5F);
    world.frame(90);
    REQUIRE(rig.yaw == controls::QUARTER);
    cactus_raylib_fake::set_key_down(KEY_Q, true);
    world.frame(30);
    cactus_raylib_fake::set_key_down(KEY_Q, false);
    world.frame(90);
    REQUIRE(rig.yaw == controls::QUARTER);
    cactus_raylib_fake::set_mouse_wheel_move(100.0F);
    world.frame();
    REQUIRE(rig.zoom == controls::ZOOM_MIN);
    cactus_raylib_fake::set_mouse_wheel_move(-100.0F);
    world.frame();
    cactus_raylib_fake::set_mouse_wheel_move(0.0F);
    REQUIRE(rig.zoom == controls::ZOOM_MAX);
    const auto& pose = world.registry.get<Transform>(overview());
    const float dx   = pose.position.x - rig.focus.x;
    const float dz   = pose.position.z - rig.focus.z;
    REQUIRE(pose.position.y - rig.focus.y > 1.5F * std::sqrt(dx * dx + dz * dz));
}

TEST_CASE("Dungeon mouse orders stop automatic work in their input frame", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto robot = world.worker(6, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    world.frame(12);
    world.registry.get<controls::SelectionStatus>(robot).active = true;
    const auto target                                           = world.tile(7, 4);
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).contains(robot, target));
    const auto progress = world.registry.get<tasks::Work>(target).progress;
    cactus_raylib_fake::set_mouse_position(screen_at(world, world.tile(2, 2)));
    cactus_raylib_fake::set_mouse_button_down(MOUSE_BUTTON_RIGHT, true);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_RIGHT, true);
    world.frame();
    cactus_raylib_fake::set_mouse_button_down(MOUSE_BUTTON_RIGHT, false);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_RIGHT, false);
    REQUIRE(world.registry.get<tasks::Work>(target).progress == progress);
    world.frame();
    REQUIRE(world.registry.all_of<units::Order::Move>(robot));
}

TEST_CASE("Dungeon visible floor surfaces follow room and bridge appearance", "[cactus-dungeon][controls][headless]") {
    namespace rooms = cactus_gen::rooms;
    World world(7, 0);
    world.frame(8);
    const auto room   = world.tile(3, 3);
    const auto bridge = world.tile(7, 4);
    const auto water  = world.tile(8, 4);
    for (const auto tile : {bridge, water}) {
        world.registry.get<game::Tile>(tile).kind = game::Kind::Water;
        world.registry.remove<Solid>(tile);
    }
    world.frame(3);
    auto& plans = backend::relation_store<rooms::RoomPlan>(world.registry);
    plans.add(world.human(), room, rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    plans.add(world.human(), bridge, rooms::RoomPlan{.kind = rooms::RoomKind::Bridge});
    world.frame(8);
    const auto room_texture   = world.registry.get<Cell>(room).texture;
    const auto bridge_texture = world.registry.get<Cell>(bridge).texture;
    REQUIRE(room_texture != world.registry.get<Cell>(world.tile(3, 4)).texture);
    REQUIRE(bridge_texture != world.registry.get<Cell>(water).texture);
    int surfaces = 0;
    for (const auto [entity, cell] : world.registry.view<Cell>().each()) {
        if (cell.y == 0 && cell.x == 3 && cell.z == 3) {
            REQUIRE(cell.texture == room_texture);
            ++surfaces;
        }
        if (cell.y == 0 && cell.x == 7 && cell.z == 4) {
            REQUIRE(cell.texture == bridge_texture);
            ++surfaces;
        }
    }
    REQUIRE(surfaces == 2);
    world.registry.get<Cell>(room).texture = {};
    world.frame(2);
    REQUIRE(world.registry.get<Cell>(room).texture == room_texture);
}

namespace hud        = cactus_gen::hud;
namespace statistics = cactus_gen::statistics;
namespace pointer    = cactus_gen::stdlib::pointer;
namespace ui         = cactus_gen::stdlib::ui;
using Parent         = cactus_gen::stdlib::core::Parent;

entt::entity hud_caption(World& world, hud::CaptionKind kind) {
    for (const auto [entity, caption] : world.registry.view<hud::HudCaption>().each()) {
        if (caption.kind == kind) {
            return entity;
        }
    }
    return entt::null;
}

entt::entity hud_button(World& world, hud::Action action) {
    for (const auto [entity, button] : world.registry.view<hud::ActionButton>().each()) {
        if (button.action == action) {
            return entity;
        }
    }
    return entt::null;
}

entt::entity minimap(World& world) {
    for (const auto [entity, layer] : world.registry.view<cactus_gen::layers::ViewLayer>().each()) {
        if (layer.minimap) {
            return entity;
        }
    }
    return entt::null;
}

void press(World& world, entt::entity button) {
    backend::generated_process_root_event(world.registry, pointer::ClickEvent{Vector2{}}, button);
}

TEST_CASE("Dungeon top bar reports the human economy and unit counts", "[cactus-dungeon][hud][headless]") {
    World world;
    world.frame(8);
    auto label = world.registry.get<ui::Text>(hud_caption(world, hud::CaptionKind::Top)).value;
    REQUIRE(label.find("Gold: 1000") != std::string::npos);
    REQUIRE(label.find("Robots: 4") != std::string::npos);
    REQUIRE(label.find("Knights: 0") != std::string::npos);
    world.registry.get<game::Player>(world.human()).gold = 777;
    cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{4.5F, 1.25F, 4.5F}, world.tile(3, 3));
    world.frame(8);
    label = world.registry.get<ui::Text>(hud_caption(world, hud::CaptionKind::Top)).value;
    REQUIRE(label.find("Gold: 777") != std::string::npos);
    REQUIRE(label.find("Knights: 1") != std::string::npos);
    REQUIRE(world.registry.get<statistics::UnitCounts>(world.human()).robots == 4);
}

TEST_CASE("Dungeon toolbar sends dig, construction, sale and recruitment designations",
          "[cactus-dungeon][hud][headless]") {
    namespace rooms = cactus_gen::rooms;
    World world(7, 0);
    world.frame(8);
    const auto designate = [&](entt::entity tile) {
        auto& selector      = world.registry.get<controls::Selector>(overview());
        selector.start      = screen_at(world, tile);
        selector.point      = selector.start;
        const auto& cell    = world.registry.get<Cell>(tile);
        selector.cell_start = Vector2{static_cast<float>(cell.x), static_cast<float>(cell.z)};
        selector.cell_end   = selector.cell_start;
        selector.grounded   = true;
        selector.digging    = world.registry.all_of<controls::Tool::Dig>(overview());
        backend::generated_process_root_event(world.registry, controls::DesignationCommitEvent{}, overview());
    };
    press(world, hud_button(world, hud::Action::Lair));
    REQUIRE(world.registry.all_of<controls::Tool::Build>(overview()));
    REQUIRE(world.registry.get<controls::Tool::Build>(overview()).kind == rooms::RoomKind::Lair);
    designate(world.tile(3, 3));
    world.frame(8);
    REQUIRE(world.registry.get<rooms::RoomTile>(world.tile(3, 3)).kind == rooms::RoomKind::Lair);
    press(world, hud_button(world, hud::Action::Dig));
    REQUIRE(world.registry.all_of<controls::Tool::Dig>(overview()));
    world.registry.get<game::Tile>(world.tile(7, 4)).kind = game::Kind::Earth;
    designate(world.tile(7, 4));
    world.frame();
    REQUIRE(backend::relation_store<tasks::DigMark>(world.registry).contains(world.human(), world.tile(7, 4)));
    press(world, hud_button(world, hud::Action::Sell));
    designate(world.tile(3, 3));
    world.frame(4);
    REQUIRE_FALSE(world.registry.all_of<rooms::RoomTile>(world.tile(3, 3)));
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == 975);
    press(world, hud_button(world, hud::Action::Recruit));
    REQUIRE(world.registry.get<cactus_gen::knights::Recruitment>(world.human()).requested);
}

TEST_CASE("Dungeon room panel steps through roots and shows capacity and occupancy",
          "[cactus-dungeon][hud][headless]") {
    namespace rooms = cactus_gen::rooms;
    World world(7, 0);
    world.frame(8);
    auto& plans       = backend::relation_store<rooms::RoomPlan>(world.registry);
    const auto first  = world.tile(3, 3);
    const auto second = world.tile(5, 3);
    for (const auto tile : {first, second}) {
        plans.add(world.human(), tile, rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    }
    world.frame(8);
    REQUIRE(world.registry.get<hud::RoomSelection::Selected>(cactus_gen::detail::generated_named_slots().hud.HudRoot)
                .target == first);
    auto info = world.registry.get<ui::Text>(hud_caption(world, hud::CaptionKind::Room)).value;
    REQUIRE(info.find("Capacity: 1") != std::string::npos);
    REQUIRE(info.find("Fill: 0") != std::string::npos);
    cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{4.5F, 1.25F, 4.5F}, first);
    world.frame(8);
    REQUIRE(world.registry.get<ui::Text>(hud_caption(world, hud::CaptionKind::Room)).value.find("Fill: 1") !=
            std::string::npos);
    press(world, hud_button(world, hud::Action::Next));
    world.frame(4);
    REQUIRE(world.registry.get<hud::RoomSelection::Selected>(cactus_gen::detail::generated_named_slots().hud.HudRoot)
                .target == second);
    REQUIRE(world.registry.get<ui::Text>(hud_caption(world, hud::CaptionKind::Room)).value.find("Fill: 0") !=
            std::string::npos);
    press(world, hud_button(world, hud::Action::Previous));
    world.frame(4);
    REQUIRE(world.registry.get<hud::RoomSelection::Selected>(cactus_gen::detail::generated_named_slots().hud.HudRoot)
                .target == first);
}

TEST_CASE("Dungeon unit health, XP and level displays follow current stats", "[cactus-dungeon][hud][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto robot = world.worker(4, 4);
    auto& unit       = world.registry.get<units::Unit>(robot);
    unit.xp          = 150;
    unit.health      = 60.0F;
    world.frame(8);
    using Draw        = cactus_gen::stdlib::render::models::ModelRenderer;
    using Local       = cactus_gen::stdlib::transform::volume::LocalTransform;
    using ScreenLabel = cactus_gen::stdlib::render::text::ScreenLabel;
    REQUIRE(world.registry.get<ScreenLabel>(robot).text.find("L2") != std::string::npos);
    bool health = false;
    bool xp     = false;
    for (const auto [entity, bar, parent, draw, local] : world.registry.view<hud::Bar, Parent, Draw, Local>().each()) {
        if (parent.parent != robot) {
            continue;
        }
        if (bar.kind == hud::BarKind::Health) {
            REQUIRE(local.scale.x == 0.4F);
            health = true;
        }
        if (bar.kind == hud::BarKind::XP) {
            REQUIRE(local.scale.x == 0.2F);
            xp = true;
        }
    }
    REQUIRE(health);
    REQUIRE(xp);
    unit.xp = 4500;
    world.frame(2);
    REQUIRE(world.registry.get<ScreenLabel>(robot).text.find("L10") != std::string::npos);
}

TEST_CASE("Dungeon minimap reveals explored cells and hides world models", "[cactus-dungeon][hud][headless]") {
    World world(7, 0);
    world.frame(8);
    auto& explored = backend::relation_store<hud::Explored>(world.registry);
    REQUIRE(explored.size() == 0);
    const auto robot = world.worker(4, 4);
    const auto enemy = world.worker(20, 20);
    auto& commands   = backend::relation_store<tasks::Commands>(world.registry);
    commands.erase_from(enemy);
    commands.add(enemy, cactus_gen::detail::generated_named_slots().world.Enemy, {});
    world.frame(12);
    REQUIRE(explored.contains(world.human(), world.tile(4, 4)));
    REQUIRE_FALSE(explored.contains(world.human(), world.tile(20, 20)));
    using Model    = cactus_gen::stdlib::render::models::ModelRenderer;
    using HiddenIn = cactus_gen::stdlib::render::visibility::HiddenIn;
    const auto map = minimap(world);
    auto& hidden   = backend::relation_store<HiddenIn>(world.registry);
    REQUIRE(hidden.contains(robot, map));
    bool known_marker = false;
    for (const auto [marker, parent] : world.registry.view<hud::TileMarker, Parent>().each()) {
        REQUIRE(explored.contains(world.human(), parent.parent));
        REQUIRE(hidden.contains(marker, overview()));
        known_marker = known_marker || parent.parent == world.tile(4, 4);
    }
    REQUIRE(known_marker);
    bool friend_marker = false;
    bool enemy_marker  = false;
    for (const auto [marker, parent, draw] : world.registry.view<hud::UnitMarker, Parent, Model>().each()) {
        if (parent.parent == robot) {
            REQUIRE(draw.visible);
            friend_marker = true;
        }
        if (parent.parent == enemy) {
            REQUIRE_FALSE(draw.visible);
            enemy_marker = true;
        }
    }
    REQUIRE(friend_marker);
    REQUIRE(enemy_marker);
}

TEST_CASE("Dungeon minimap click moves the main camera target", "[cactus-dungeon][hud][headless]") {
    World world(7, 0);
    world.frame(8);
    const auto& cv   = world.registry.get<camera::CameraView>(minimap(world));
    const auto pixel = camera::to_screen(cv, Vector3{12.5F, 1.5F, 12.5F}).position;
    backend::generated_process_root_event(
        world.registry, pointer::ClickEvent{pixel}, world.registry.view<hud::MapClick>().front());
    world.frame(2);
    const auto focus = world.registry.get<controls::Rig>(overview()).focus;
    REQUIRE(focus.x > 12.49F);
    REQUIRE(focus.x < 12.51F);
    REQUIRE(focus.z > 12.49F);
    REQUIRE(focus.z < 12.51F);
}

TEST_CASE("Dungeon full map with twenty working robots", "[.dungeon-performance]") {
    World world(7, 0, 85);
    const auto kind = GENERATE(game::Kind::Earth, game::Kind::InfiniteGold);
    for (int coordinate = 2; coordinate <= 6; ++coordinate) {
        world.worker(2, coordinate);
        world.mark(1, coordinate, kind);
        world.worker(6, coordinate);
        world.mark(7, coordinate, kind);
        world.worker(coordinate, 2);
        world.mark(coordinate, 1, kind);
        world.worker(coordinate, 6);
        world.mark(coordinate, 7, kind);
    }
    world.frame(8);
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).size() == 20);
    const int frames = kind == game::Kind::Earth ? 30 : 120;
    std::vector<double> milliseconds;
    for (int frame = 0; frame < frames; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        world.frame();
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        cactus_raylib_fake::reset();
    }
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).size() == 20);
    const auto mean = std::accumulate(milliseconds.begin(), milliseconds.end(), 0.0) / static_cast<double>(frames);
    std::ranges::sort(milliseconds);
    const auto p95 = milliseconds[static_cast<std::size_t>(frames * 95 / 100)];
    std::cout << "map=85 robots=20 material=" << static_cast<int>(kind) << " samples=" << frames << " mean_ms=" << mean
              << " p95_ms=" << p95 << '\n';
}

namespace rooms = cactus_gen::rooms;
namespace ai    = cactus_gen::ai;

TEST_CASE("Dungeon AI decisions use explored frontier and shared plans", "[cactus-dungeon][ai][headless]") {
    World world;
    world.frame(12);
    const auto enemy = cactus_gen::detail::generated_named_slots().world.Enemy;
    world.registry.emplace<ai::Brain>(enemy);
    auto& explored = backend::relation_store<hud::Explored>(world.registry);
    explored.erase_from(enemy);
    backend::generated_process_root_event(world.registry, ai::DecisionEvent{enemy});
    REQUIRE(backend::relation_store<tasks::DigMark>(world.registry).size() == 0);
    REQUIRE(backend::relation_store<rooms::RoomPlan>(world.registry).size() == 0);
    explored.add(enemy, world.tile(19, 16), {});
    explored.add(enemy, world.tile(18, 18), {});
    backend::generated_process_root_event(world.registry, ai::DecisionEvent{enemy});
    const auto& marks = backend::relation_store<tasks::DigMark>(world.registry);
    REQUIRE(marks.contains(enemy, world.tile(19, 16)));
    REQUIRE(backend::relation_store<rooms::RoomPlan>(world.registry).contains(enemy, world.tile(18, 18)));
    REQUIRE_FALSE(world.registry.all_of<rooms::RoomTile>(world.tile(18, 18)));
    world.frame(8);
    REQUIRE(world.registry.all_of<rooms::RoomTile>(world.tile(18, 18)));
}

TEST_CASE("Dungeon AI attacks a discovered hostile heart above its army threshold", "[cactus-dungeon][ai][headless]") {
    World world(7, 0);
    world.frame(12);
    const auto enemy = cactus_gen::detail::generated_named_slots().world.Enemy;
    world.registry.emplace<ai::Brain>(enemy);
    std::vector<entt::entity> army;
    for (int i = 0; i < 5; ++i) {
        const auto knight = cactus_gen::detail::small_map::create_recruit(
            world.registry, Vector3{19.5F, 1.25F, 19.5F}, world.tile(18, 18));
        auto& commands = backend::relation_store<tasks::Commands>(world.registry);
        commands.add(knight, enemy, {});
        army.push_back(knight);
    }
    world.frame(8);
    const auto heart = world.tile(4, 4);
    backend::generated_process_root_event(world.registry, ai::DecisionEvent{enemy});
    for (const auto knight : army) {
        REQUIRE_FALSE(world.registry.all_of<units::OrderRequest::Attack>(knight));
    }
    backend::relation_store<hud::Explored>(world.registry).add(enemy, heart, {});
    backend::generated_process_root_event(world.registry, ai::DecisionEvent{enemy});
    for (const auto knight : army) {
        REQUIRE(world.registry.get<units::OrderRequest::Attack>(knight).target == heart);
    }
}

TEST_CASE("Dungeon AI grows over ten simulated minutes", "[.dungeon-long][cactus-dungeon][ai]") {
    World world;
    world.frame(12);
    const auto enemy = cactus_gen::detail::generated_named_slots().world.Enemy;
    world.registry.emplace<ai::Brain>(enemy);
    const auto initial = world.kinds();
    int peak_knights   = 0;
    bool lair          = false;
    bool farm          = false;
    for (int second = 0; second < 4500; ++second) {
        cactus_raylib_fake::clear_call_log();
        cactus_headless_test::drive_frame(world.registry, 8.0F / 60.0F);
        const auto* counts = world.registry.try_get<statistics::UnitCounts>(enemy);
        peak_knights       = std::max(peak_knights, counts ? counts->knights : 0);
        const auto* rooms  = world.registry.try_get<statistics::RoomCounts>(enemy);
        lair               = lair || (rooms && rooms->lairs > 0);
        farm               = farm || (rooms && rooms->farms > 0);
    }
    REQUIRE(world.kinds() != initial);
    REQUIRE(lair);
    REQUIRE(farm);
    REQUIRE(peak_knights > 0);
}

namespace flow    = cactus_gen::flow;
namespace session = cactus_gen::session;
namespace layers  = cactus_gen::layers;

TEST_CASE("Dungeon menu starts a fresh game and accepts game-over return", "[cactus-dungeon][flow][headless]") {
    World world;
    auto& state = world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game);
    backend::generated_process_root_event(world.registry, flow::MenuRequestedEvent{});
    REQUIRE(state.mode == session::SessionMode::Menu);
    backend::generated_process_root_event(world.registry, flow::NewGameRequestedEvent{});
    world.frame(16);
    REQUIRE(state.mode == session::SessionMode::Playing);
    REQUIRE(world.registry.view<game::Tile>().size() == kSize * kSize);
    REQUIRE(world.registry.view<game::Heart>().size() == 2);
    world.registry.get<game::Heart>(world.tile(4, 4)).health = 0.0F;
    world.frame(4);
    REQUIRE(state.mode == session::SessionMode::GameOver);
    backend::generated_process_root_event(world.registry, flow::MenuRequestedEvent{});
    REQUIRE(state.mode == session::SessionMode::Menu);
}

TEST_CASE("Dungeon pause freezes game time, workers and AI", "[cactus-dungeon][flow][headless]") {
    World world(7, 0);
    world.frame(12);
    const auto worker = world.worker(6, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    const auto enemy = cactus_gen::detail::generated_named_slots().world.Enemy;
    world.registry.emplace<ai::Brain>(enemy);
    world.frame(12);
    backend::generated_process_root_event(world.registry, hud::PauseRequestedEvent{});
    const auto position   = world.registry.get<Transform>(worker).position;
    const auto progress   = world.registry.get<tasks::Work>(world.tile(7, 4)).progress;
    const auto experience = world.registry.get<units::Unit>(worker).xp;
    const auto thought    = world.registry.get<ai::Brain>(enemy).elapsed;
    auto& state        = world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game);
    const auto seconds = state.seconds;
    world.frame(120);
    REQUIRE(state.mode == session::SessionMode::Paused);
    REQUIRE(state.seconds == seconds);
    REQUIRE(world.registry.get<Transform>(worker).position.x == position.x);
    REQUIRE(world.registry.get<Transform>(worker).position.z == position.z);
    REQUIRE(world.registry.get<tasks::Work>(world.tile(7, 4)).progress == progress);
    REQUIRE(world.registry.get<units::Unit>(worker).xp == experience);
    REQUIRE(world.registry.get<ai::Brain>(enemy).elapsed == thought);
    backend::generated_process_root_event(world.registry, hud::PauseRequestedEvent{});
    world.frame(8);
    REQUIRE(state.mode == session::SessionMode::Playing);
    REQUIRE(state.seconds > seconds);
}

TEST_CASE("Dungeon save restores gameplay facts and rebuilds its views",
          "[cactus-dungeon][flow][persistence][headless]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    auto adapter     = memory.as_adapter();
    const auto write = adapter.write;
    int writes       = 0;
    adapter.write    = [&](const std::string& slot,
                           const cactus::persistence::SchemaDescriptor& schema,
                           const cactus::persistence::Snapshot& snapshot) {
        ++writes;
        return write(slot, schema, snapshot);
    };
    cactus_test::ScopedPersistenceAdapter scoped(std::move(adapter));
    World world(7, 0);
    world.frame(12);
    const auto worker = world.worker(6, 4);
    world.mark(7, 4, game::Kind::InfiniteGold);
    backend::relation_store<rooms::RoomPlan>(world.registry)
        .add(world.human(), world.tile(3, 3), rooms::RoomPlan{.kind = rooms::RoomKind::Lair});
    backend::relation_store<rooms::RoomPlan>(world.registry)
        .add(world.human(), world.tile(3, 4), rooms::RoomPlan{.kind = rooms::RoomKind::Farm});
    world.frame(12);
    const auto knight =
        cactus_gen::detail::small_map::create_recruit(world.registry, Vector3{4.5F, 1.25F, 4.5F}, world.tile(3, 3));
    world.frame(8);
    backend::generated_process_root_event(world.registry, hud::PauseRequestedEvent{});
    world.registry.get<units::Unit>(worker).xp               = 321;
    world.registry.get<units::Unit>(worker).health           = 77.0F;
    world.registry.get<game::Player>(world.human()).gold     = 4321;
    world.registry.get<game::Heart>(world.tile(4, 4)).health = 1777.0F;
    const auto enemy                                         = cactus_gen::detail::generated_named_slots().world.Enemy;
    world.registry.emplace<ai::Brain>(enemy).elapsed         = 0.3F;
    auto& explored                                           = backend::relation_store<hud::Explored>(world.registry);
    explored.add(world.human(), world.tile(20, 20), {});
    auto& needs                                    = world.registry.get<cactus_gen::knights::Knight>(knight);
    needs.hunger                                   = 2.0F;
    needs.fatigue                                  = 3.0F;
    needs.training                                 = 0.4F;
    needs.resting                                  = true;
    world.registry.get<units::Unit>(knight).xp     = 654;
    world.registry.get<units::Unit>(knight).health = 171.0F;
    auto& farm  = world.registry.get<cactus_gen::knights::FarmStock>(world.tile(3, 4));
    farm.food   = 3;
    farm.growth = 0.2F;
    const auto seconds =
        world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game).seconds;
    const auto explored_count = explored.size();
    const auto kinds          = world.kinds();
    const auto progress       = world.registry.get<tasks::Work>(world.tile(7, 4)).progress;
    const auto food           = world.registry.get<cactus_gen::knights::FarmStock>(world.tile(3, 4));
    const auto ownership      = backend::relation_store<game::OwnedBy>(world.registry).size();
    const auto claims         = backend::relation_store<tasks::Claim>(world.registry).size();
    cactus_headless_test::dispatch_boundary_event(world.registry, hud::SaveRequestedEvent{});
    world.frame(2);
    REQUIRE(writes == 1);
    world.registry.get<game::Player>(world.human()).gold  = 1;
    world.registry.get<units::Unit>(worker).xp            = 0;
    world.registry.get<game::Tile>(world.tile(7, 4)).kind = game::Kind::Earth;
    world.registry.remove<rooms::RoomTile>(world.tile(3, 3));
    explored.erase_from(world.human());
    cactus_headless_test::dispatch_boundary_event(world.registry, hud::LoadRequestedEvent{});
    backend::generated_drain_external_events(world.registry);
    backend::generated_process_root_event(world.registry, hud::PauseRequestedEvent{});
    world.frame(8);
    REQUIRE(world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game).mode ==
            session::SessionMode::Paused);
    REQUIRE(world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game).seconds ==
            seconds);
    REQUIRE(world.kinds() == kinds);
    REQUIRE(world.registry.get<game::Player>(world.human()).gold == 4321);
    REQUIRE(world.registry.get<game::Heart>(world.tile(4, 4)).health == 1777.0F);
    REQUIRE(world.registry.get<rooms::RoomTile>(world.tile(3, 3)).kind == rooms::RoomKind::Lair);
    REQUIRE(world.registry.get<cactus_gen::knights::FarmStock>(world.tile(3, 4)).food == food.food);
    REQUIRE(backend::relation_store<game::OwnedBy>(world.registry).size() == ownership);
    REQUIRE(backend::relation_store<tasks::Claim>(world.registry).size() == claims);
    REQUIRE(backend::relation_store<hud::Explored>(world.registry).size() == explored_count);
    REQUIRE(backend::relation_store<hud::Explored>(world.registry).contains(world.human(), world.tile(20, 20)));
    REQUIRE(backend::relation_store<cactus_gen::knights::Sleeps>(world.registry).size() == 1);
    REQUIRE(world.registry.get<tasks::Work>(world.tile(7, 4)).progress == progress);
    bool restored_worker = false;
    for (const auto [entity, unit] : world.registry.view<game::Robot, units::Unit>().each()) {
        if (unit.xp >= 321 && unit.health == 77.0F) {
            restored_worker = true;
            REQUIRE(backend::relation_store<tasks::Located>(world.registry).contains(entity, world.tile(6, 4)));
            REQUIRE(backend::relation_store<tasks::Claim>(world.registry).contains(entity, world.tile(7, 4)));
        }
    }
    REQUIRE(restored_worker);
    bool restored_knight = false;
    for (const auto [entity, needs, unit] : world.registry.view<cactus_gen::knights::Knight, units::Unit>().each()) {
        REQUIRE(needs.hunger == 2.0F);
        REQUIRE(needs.fatigue == 3.0F);
        REQUIRE(needs.training == 0.4F);
        REQUIRE(needs.resting);
        REQUIRE(unit.xp == 654);
        REQUIRE(unit.health == 171.0F);
        REQUIRE(
            backend::relation_store<cactus_gen::knights::Sleeps>(world.registry).contains(entity, world.tile(3, 3)));
        restored_knight = true;
    }
    REQUIRE(restored_knight);
    REQUIRE(world.registry.get<cactus_gen::knights::FarmStock>(world.tile(3, 4)).growth == food.growth);
    REQUIRE(world.registry.get<ai::Brain>(cactus_gen::detail::generated_named_slots().world.Enemy).elapsed == 0.3F);
    REQUIRE(world.registry.all_of<ai::Brain>(cactus_gen::detail::generated_named_slots().world.Enemy));
    REQUIRE(world.registry.view<ui::Button>().size() >= 19);
    REQUIRE(world.registry.view<camera::CameraView>().size() == 2);
    REQUIRE(world.registry.view<game::WorldConfig>().size() == 1);
}

entt::entity menu_button(World& world, flow::MenuAction action) {
    for (const auto [entity, button] : world.registry.view<flow::MenuButton>().each()) {
        if (button.action == action) {
            return entity;
        }
    }
    return entt::null;
}

TEST_CASE("Dungeon startup menu routes New, Options, Back and Exit", "[cactus-dungeon][flow][headless]") {
    World world(7, 4, kSize, false);
    auto& state = world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game);
    REQUIRE(state.mode == session::SessionMode::Menu);
    REQUIRE(world.registry.view<game::Tile>().size() == 0);
    for (const auto [entity, port] : world.registry.view<cactus_gen::stdlib::camera::viewport::Viewport>().each()) {
        REQUIRE_FALSE(port.active);
    }
    REQUIRE(world.registry.view<flow::MenuPanel>().size() == 1);
    const auto panel = world.registry.view<flow::MenuPanel>().front();
    REQUIRE(world.registry.get<ui::Node>(panel).visible);
    press(world, menu_button(world, flow::MenuAction::Options));
    REQUIRE(state.mode == session::SessionMode::Options);
    press(world, menu_button(world, flow::MenuAction::Options));
    REQUIRE(state.mode == session::SessionMode::Menu);
    press(world, menu_button(world, flow::MenuAction::New));
    world.frame(12);
    REQUIRE(state.mode == session::SessionMode::Playing);
    REQUIRE(world.registry.view<game::Tile>().size() == kSize * kSize);
    REQUIRE_FALSE(world.registry.get<ui::Node>(panel).visible);
    press(world, menu_button(world, flow::MenuAction::Exit));
    REQUIRE(backend::quit_requested());
}

TEST_CASE("Dungeon menu paints its UI while every viewport is inactive", "[cactus-dungeon][flow][headless]") {
    World world(7, 4, kSize, false);
    for (const auto [entity, port] : world.registry.view<cactus_gen::stdlib::camera::viewport::Viewport>().each()) {
        REQUIRE_FALSE(port.active);
    }
    backend::reset_render_debug_state();
    world.frame();
    CHECK(backend::render_debug_state().submitted_ui_primitives > 0);
}

TEST_CASE("Dungeon menu covers the window and its options button turns into Back", "[cactus-dungeon][flow][headless]") {
    World world(7, 4, kSize, false);
    const auto panel   = world.registry.view<flow::MenuPanel>().front();
    const auto& anchor = world.registry.get<ui::Anchors>(panel);
    CHECK(anchor.min.x == 0.0F);
    CHECK(anchor.min.y == 0.0F);
    CHECK(anchor.max.x == 1.0F);
    CHECK(anchor.max.y == 1.0F);
    CHECK(world.registry.get<ui::Panel>(panel).background.a == 255);
    CHECK(world.registry.view<flow::MenuButton>().size() == 4);
    const auto options = menu_button(world, flow::MenuAction::Options);
    CHECK(world.registry.get<ui::Button>(options).label == "Options");
    press(world, options);
    world.frame();
    CHECK(world.registry.get<ui::Button>(options).label == "Back");
    press(world, options);
    world.frame();
    CHECK(world.registry.get<ui::Button>(options).label == "Options");
    for (const auto [entity, port, scope] :
         world.registry.view<cactus_gen::stdlib::camera::viewport::Viewport, layers::ViewLayer>().each()) {
        if (!scope.minimap) {
            CHECK(port.y == 0.0F);
            CHECK(port.height == 1.0F);
        }
    }
}

TEST_CASE("Dungeon missing save keeps the menu and its views", "[cactus-dungeon][flow][persistence][headless]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    cactus_test::ScopedPersistenceAdapter scoped(memory.as_adapter());
    World world(7, 0, kSize, false);
    press(world, menu_button(world, flow::MenuAction::Load));
    world.frame(4);
    const auto& state = world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game);
    REQUIRE(state.mode == session::SessionMode::Menu);
    REQUIRE_FALSE(state.status.empty());
    REQUIRE(world.registry.view<game::Tile>().size() == 0);
    REQUIRE(world.registry.view<flow::MenuPanel>().size() == 1);
}

TEST_CASE("Dungeon failed save preserves paused gameplay", "[cactus-dungeon][flow][persistence][headless]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    auto adapter = memory.as_adapter();
    adapter.write =
        [](const std::string&, const cactus::persistence::SchemaDescriptor&, const cactus::persistence::Snapshot&) {
            return backend::PersistenceWriteResult{.ok = false, .code = "disk_full", .message = "No space"};
        };
    cactus_test::ScopedPersistenceAdapter scoped(std::move(adapter));
    World world;
    world.frame(8);
    backend::generated_process_root_event(world.registry, hud::PauseRequestedEvent{});
    cactus_headless_test::dispatch_boundary_event(world.registry, hud::SaveRequestedEvent{});
    world.frame(3);
    const auto& state = world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game);
    REQUIRE(state.mode == session::SessionMode::Paused);
    REQUIRE(state.status == "No space");
    REQUIRE(world.registry.view<game::Tile>().size() == kSize * kSize);
}

Vector3 roof(World& world, int x, int z) {
    int top = 0;
    for (auto [entity, cell] : world.registry.view<Cell, Solid>().each()) {
        if (cell.x == x && cell.z == z) {
            top = std::max(top, cell.y);
        }
    }
    return Vector3{static_cast<float>(x) + 0.5F, static_cast<float>(top + 1), static_cast<float>(z) + 0.45F};
}

void click_world(World& world, Vector2 pixel, int button = MOUSE_BUTTON_LEFT) {
    cactus_raylib_fake::set_mouse_position(pixel);
    cactus_raylib_fake::set_mouse_button_down(button, true);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(button, true);
    world.frame();
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(button, false);
    cactus_raylib_fake::set_mouse_button_down(button, false);
    cactus_raylib_fake::set_mouse_button_released_this_frame(button, true);
    world.frame();
    cactus_raylib_fake::set_mouse_button_released_this_frame(button, false);
}

TEST_CASE("Dungeon native construction picks the visible floor surface", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(12);
    press(world, hud_button(world, hud::Action::Lair));
    const auto& camera = world.registry.get<camera::CameraView>(overview());
    click_world(world, camera::to_screen(camera, Vector3{3.5F, 1.0F, 3.55F}).position);
    world.frame(8);
    REQUIRE(world.registry.all_of<rooms::RoomTile>(world.tile(3, 3)));
}

TEST_CASE("Dungeon native digging picks the visible wall roof", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(12);
    press(world, hud_button(world, hud::Action::Dig));
    const auto& camera = world.registry.get<camera::CameraView>(overview());
    click_world(world, camera::to_screen(camera, roof(world, 7, 4)).position);
    REQUIRE(backend::relation_store<tasks::DigMark>(world.registry).contains(world.human(), world.tile(7, 4)));
}

TEST_CASE("Dungeon dragging over walls marks digging and right click drops the tool",
          "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(12);
    auto& marks        = backend::relation_store<tasks::DigMark>(world.registry);
    const auto& camera = world.registry.get<camera::CameraView>(overview());
    click_world(world, camera::to_screen(camera, roof(world, 7, 4)).position);
    REQUIRE(marks.contains(world.human(), world.tile(7, 4)));
    press(world, hud_button(world, hud::Action::Lair));
    REQUIRE(world.registry.all_of<controls::Tool::Build>(overview()));
    click_world(world, Vector2{640.0F, 360.0F}, MOUSE_BUTTON_RIGHT);
    REQUIRE(world.registry.all_of<controls::Tool::Select>(overview()));
}

TEST_CASE("Dungeon HUD blocks world context orders under the minimap", "[cactus-dungeon][controls][headless]") {
    World world(7, 0);
    world.frame(12);
    const auto worker = world.worker(11, 11);
    auto& rig         = world.registry.get<controls::Rig>(overview());
    rig.focus         = Vector3{11.5F, 0.0F, 11.5F};
    rig.zoom          = 8.0F;
    world.frame(4);
    world.registry.get<controls::SelectionStatus>(worker).active = true;
    cactus_raylib_fake::set_mouse_position(Vector2{1000.0F, 200.0F});
    cactus_raylib_fake::set_mouse_button_down(MOUSE_BUTTON_RIGHT, true);
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_RIGHT, true);
    world.frame();
    REQUIRE(world.registry.all_of<units::OrderRequest::None>(worker));
    REQUIRE(world.registry.all_of<units::Order::Auto>(worker));
}

TEST_CASE("Dungeon rendering uses cameras declared by imported modules", "[cactus-dungeon][controls][headless]") {
    World world(7, 1);
    world.frame(8);
    auto& rig = world.registry.get<controls::Rig>(overview());
    rig.focus = Vector3{11.5F, 0.0F, 11.5F};
    rig.zoom  = 30.0F;
    rig.yaw   = 0.3F;
    world.frame(3);
    const auto main_position = world.registry.get<Transform>(overview()).position;
    const auto map_position  = world.registry.get<Transform>(minimap(world)).position;
    bool main_camera         = false;
    bool map_camera          = false;
    for (const auto& call : cactus_raylib_fake::call_log()) {
        if (const auto* begin = std::get_if<cactus_raylib_fake::RecordedBeginMode3D>(&call)) {
            main_camera = main_camera || cactus_raylib_fake::approx_equal(begin->camera.position, main_position);
            map_camera  = map_camera || cactus_raylib_fake::approx_equal(begin->camera.position, map_position);
        }
    }
    REQUIRE(main_camera);
    REQUIRE(map_camera);
}

TEST_CASE("Dungeon full-map AI versus AI for ten simulated minutes", "[.dungeon-ai-performance]") {
    cactus_test::ScopedPersistenceAdapter scoped(backend::make_example_file_adapter("build/dungeon-benchmark-saves"));
    World world(7, 4, 85);
    world.frame(12);
    const auto human = world.human();
    const auto enemy = cactus_gen::detail::generated_named_slots().world.Enemy;
    world.registry.emplace_or_replace<ai::Brain>(human);
    world.registry.emplace_or_replace<ai::Brain>(enemy);
    std::vector<double> milliseconds;
    milliseconds.reserve(36000);
    int matches = 1;
    while (milliseconds.size() < 36000) {
        const auto mode =
            world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game).mode;
        if (mode == session::SessionMode::GameOver) {
            backend::generated_process_root_event(world.registry, flow::NewGameRequestedEvent{});
            ++matches;
        }
        const auto start = std::chrono::steady_clock::now();
        world.frame();
        if (mode != session::SessionMode::Playing) {
            continue;
        }
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (milliseconds.size() % 3600 == 0) {
            const auto count = milliseconds.size();
            const auto sum   = std::accumulate(milliseconds.end() - 3600, milliseconds.end(), 0.0);
            std::cout << "active_seconds=" << count / 60 << " recent_mean_ms=" << sum / 3600.0 << " matches=" << matches
                      << " units=" << world.registry.view<units::Unit>().size()
                      << " explored=" << backend::relation_store<hud::Explored>(world.registry).size() << std::endl;
        }
    }
    if (world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game).mode !=
        session::SessionMode::Playing) {
        backend::generated_process_root_event(world.registry, flow::NewGameRequestedEvent{});
        world.frame(16);
    }
    cactus_headless_test::dispatch_boundary_event(world.registry, hud::SaveRequestedEvent{});
    world.frame(2);
    const auto& state = world.registry.get<session::Session>(cactus_gen::detail::generated_named_slots().session.Game);
    REQUIRE(state.status == "Game saved");
    REQUIRE(world.registry.view<game::Tile>().size() == 85 * 85);
    REQUIRE(milliseconds.size() == 36000);
    const auto mean =
        std::accumulate(milliseconds.begin(), milliseconds.end(), 0.0) / static_cast<double>(milliseconds.size());
    std::ranges::sort(milliseconds);
    const auto p95        = milliseconds[milliseconds.size() * 95 / 100];
    const auto save_bytes = std::filesystem::file_size("build/dungeon-benchmark-saves/cactus-dungeon.cactussave");
    std::cout << "map=85 ai_players=2 samples=" << milliseconds.size() << " matches=" << matches << " mean_ms=" << mean
              << " p95_ms=" << p95 << " save_bytes=" << save_bytes << std::endl;
}

}  // namespace
