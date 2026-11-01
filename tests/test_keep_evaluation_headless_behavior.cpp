// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP
#include "fake_raylib/headless_frame_driver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace game    = cactus_gen::keep_evaluation_runtime;
namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;
    std::map<int, entt::entity> tiles;

    World() {
        cactus_headless_test::init_and_load(registry);
        for (const auto [entity, tile] : registry.view<game::Tile>().each()) {
            tiles.emplace(tile.id, entity);
        }
        tick();
    }

    static const auto& slots() {
        return cactus_gen::detail::generated_named_slots().keep_evaluation_runtime;
    }

    void tick() {
        cactus_headless_test::drive_frame(registry);
    }

    game::Tile& tile(int id) {
        return registry.get<game::Tile>(tiles.at(id));
    }

    std::string& trace() {
        return registry.get<game::Log>(slots().Book).trace;
    }

    // The trace one tick writes.
    std::string traced_tick() {
        trace().clear();
        tick();
        return trace();
    }

    std::uint64_t commands_of_tick() {
        const auto before = backend::perf_counters().commands;
        tick();
        return backend::perf_counters().commands - before;
    }

    // Live tiles, oldest first.
    std::vector<entt::entity> oldest_tiles() {
        return backend::creation_ordered(registry, registry.view<game::Tile>());
    }

    // What the full product of tiles keeps as Neighbor, in tuple order.
    std::vector<std::pair<entt::entity, entt::entity>> expected_neighbors() {
        std::vector<std::pair<entt::entity, entt::entity>> rows;
        const auto order = oldest_tiles();
        for (const auto a : order) {
            for (const auto b : order) {
                const auto& from = registry.get<game::Tile>(a);
                const auto& to   = registry.get<game::Tile>(b);
                if (std::abs(from.x - to.x) + std::abs(from.z - to.z) == 1) {
                    rows.emplace_back(a, b);
                }
            }
        }
        return rows;
    }

    std::vector<std::pair<entt::entity, entt::entity>> neighbor_rows() {
        std::vector<std::pair<entt::entity, entt::entity>> rows;
        for (const auto& entry : backend::relation_store<game::Neighbor>(registry).rows()) {
            rows.emplace_back(entry.from, entry.to);
        }
        return rows;
    }

    void set_weights(int weight) {
        for (const auto& [id, entity] : tiles) {
            if (registry.all_of<game::Tile>(entity)) {
                registry.get<game::Tile>(entity).weight = weight;
            }
        }
    }

    void link(int from, int to) {
        backend::relation_store<game::Link>(registry).add(tiles.at(from), tiles.at(to), game::Link{});
    }

    // A group's count and best partner by `weight`, the earliest row on a tie,
    // over partners in creation order.
    struct Fold {
        int count       = 0;
        entt::entity top = entt::null;
        int best        = 0;

        void add(entt::entity partner, int weight) {
            if (count == 0 || weight > best) {
                top  = partner;
                best = weight;
            }
            ++count;
        }
    };

    template <typename Admits>
    Fold fold_tiles(const Admits& admits) {
        Fold fold;
        for (const auto partner : oldest_tiles()) {
            if (admits(partner)) {
                fold.add(partner, registry.get<game::Tile>(partner).weight);
            }
        }
        return fold;
    }

    // Tiles `heart` reaches through Territory, itself excluded unless on a cycle.
    std::vector<entt::entity> reached(entt::entity heart) {
        std::vector<entt::entity> found;
        std::vector<entt::entity> frontier{heart};
        const auto& territory = backend::relation_store<game::Territory>(registry);
        while (!frontier.empty()) {
            const auto from = frontier.back();
            frontier.pop_back();
            territory.for_each_from(from, [&](const auto& row) {
                if (std::ranges::find(found, row.to) == found.end()) {
                    found.push_back(row.to);
                    frontier.push_back(row.to);
                }
            });
        }
        return found;
    }

    bool reaches(entt::entity heart, entt::entity tile) {
        const auto found = reached(heart);
        return std::ranges::find(found, tile) != found.end();
    }

    bool sees(entt::entity probe, entt::entity tile) {
        const auto& p    = registry.get<game::Probe>(probe);
        const auto& cell = registry.get<game::Tile>(tile);
        const float dx   = p.x - static_cast<float>(cell.x) - 0.5F;
        const float dz   = p.z - static_cast<float>(cell.z) - 0.5F;
        return dx * dx + dz * dz <= 2.0F;
    }

    std::vector<entt::entity> hearts() {
        std::vector<entt::entity> found;
        for (const auto tile : oldest_tiles()) {
            if (registry.all_of<game::Heart>(tile)) {
                found.push_back(tile);
            }
        }
        return found;
    }

    entt::entity top_of(entt::entity entity, const auto& trait) {
        using Top = std::remove_cvref_t<decltype(trait)>;
        return registry.all_of<Top>(entity) ? registry.get<Top>(entity).top : entt::null;
    }

    void territory(int from, int to) {
        backend::relation_store<game::Territory>(registry).add(tiles.at(from), tiles.at(to), game::Territory{});
    }

    // Every planned fold against the full product, folded here in creation order.
    void check_folds() {
        const auto live = oldest_tiles();
        for (const auto a : live) {
            const auto& tile = registry.get<game::Tile>(a);
            INFO("tile " << tile.id);
            const auto column = fold_tiles([&](entt::entity b) { return registry.get<game::Tile>(b).x == tile.x; });
            CHECK(tile.column == column.count);
            CHECK(top_of(a, game::ColumnTop{}) == column.top);
            const auto heavy = fold_tiles([&](entt::entity b) {
                return registry.get<game::Tile>(b).x == tile.x && registry.get<game::Tile>(b).weight > 5;
            });
            CHECK(registry.all_of<game::Columned>(a) == (heavy.count > 0));
            const auto& links = backend::relation_store<game::Link>(registry);
            const auto linked = fold_tiles([&](entt::entity b) { return links.contains(a, b); });
            CHECK(tile.linked == linked.count);
            CHECK(top_of(a, game::LinkTop{}) == linked.top);
            const bool linked_to = std::ranges::any_of(live, [&](entt::entity from) { return links.contains(from, a); });
            CHECK(registry.all_of<game::Linked>(a) == linked_to);
            const bool connected =
                std::ranges::any_of(hearts(), [&](entt::entity heart) { return a == heart || reaches(heart, a); });
            CHECK(registry.all_of<game::Connected>(a) == connected);
            bool spotted = false;
            for (const auto probe : registry.view<game::Probe>()) {
                spotted = spotted || sees(probe, a);
            }
            CHECK(registry.all_of<game::Spotted>(a) == spotted);
        }
        for (const auto heart : hearts()) {
            const auto reach = fold_tiles([&](entt::entity b) { return b == heart || reaches(heart, b); });
            CHECK(registry.get<game::Tile>(heart).reach == reach.count);
            CHECK(top_of(heart, game::ReachTop{}) == reach.top);
            const auto any = fold_tiles([&](entt::entity b) {
                return registry.get<game::Tile>(b).weight > 100 || reaches(heart, b);
            });
            CHECK(registry.get<game::Tile>(heart).reach_any == any.count);
        }
        for (const auto probe : registry.view<game::Probe>()) {
            const auto seen = fold_tiles([&](entt::entity b) { return sees(probe, b); });
            CHECK(registry.get<game::Probe>(probe).seen == seen.count);
            CHECK(top_of(probe, game::SeenTop{}) == seen.top);
        }
    }
};

// Ids in creation order: tile i of the load loop has id (i * 7) % 15; Goal (15) comes first.
int created_before(int left, int right) {
    const auto position = [](int id) { return id == 15 ? -1 : (id * 13) % 15; };
    return position(left) < position(right) ? left : right;
}

}  // namespace

// Generated scheduler state is process-global, so everything runs in one
// test case on one registry, scenario after scenario.
TEST_CASE("keep evaluation: batched keeps, planned folds and cached membership", "[runtime][keep-evaluation]") {
    World world;

    {  // new rows are created in tuple order
        CHECK(world.neighbor_rows() == world.expected_neighbors());
        REQUIRE(world.neighbor_rows().size() == 48);
    }

    {  // a trait keep queues one add and one remove command, whatever it keeps
        world.set_weights(0);
        world.tick();
        world.tile(1).weight = 1;
        world.link(1, 2);
        world.tick();
        const auto few = world.commands_of_tick();
        world.set_weights(1);
        for (int id = 3; id < 12; ++id) {
            world.link(id, id + 1);
        }
        world.tick();
        const auto many = world.commands_of_tick();
        CHECK(many == few);
    }

    {  // lifecycle deliveries keep per-entity command order: adds by tuple order, then removals
        world.set_weights(0);
        world.tick();
        world.tile(3).weight = 1;
        world.tick();
        world.tile(3).weight = 0;
        world.tile(2).weight = 1;
        world.tile(9).weight = 1;
        const int first  = created_before(2, 9);
        const int second = first == 2 ? 9 : 2;
        CHECK(world.traced_tick() == "+L" + std::to_string(first) + ";+L" + std::to_string(second) + ";-L3;");
    }

    {  // the keep run commits between the removals queued before and after it
        world.set_weights(0);
        world.tick();
        world.tile(5).doom_early = true;
        world.tile(6).doom_late  = true;
        world.tile(7).weight     = 1;
        CHECK(world.traced_tick() == "-T5;+L7;-T6;");
        // A row whose end lost its Tile earlier in the commit is not kept.
        const auto rows = world.neighbor_rows();
        CHECK(std::ranges::none_of(rows, [&](const auto& row) {
            return row.first == world.tiles.at(5) || row.second == world.tiles.at(5) ||
                   row.first == world.tiles.at(6) || row.second == world.tiles.at(6);
        }));
        CHECK(rows == world.expected_neighbors());
    }
    {  // planned folds fold the same rows in the same order as the full product
        for (const auto& [id, entity] : world.tiles) {
            if (world.registry.all_of<game::Tile>(entity)) {
                world.registry.get<game::Tile>(entity).weight = (id * 5) % 9;
            }
        }
        world.link(4, 8);
        world.link(4, 12);
        world.link(8, 4);
        world.territory(0, 1);
        world.territory(1, 2);
        world.territory(2, 0);
        world.territory(10, 11);
        world.territory(11, 7);
        world.tick();
        world.check_folds();
        world.territory(7, 3);
        world.tile(9).weight = 9;
        world.tick();
        world.check_folds();
    }

    {  // a targeted event folds only the recipient's group
        world.tile(4).ping = true;
        world.tick();
        world.tile(4).ping = false;
        int expected = 0;
        for (const auto b : world.oldest_tiles()) {
            const auto& other = world.registry.get<game::Tile>(b);
            expected += static_cast<int>(other.x == world.tile(4).x && other.weight > 0);
        }
        CHECK(world.tile(4).pinged == expected);
        for (const auto& [id, entity] : world.tiles) {
            if (id != 4 && world.registry.all_of<game::Tile>(entity)) {
                CHECK(world.tile(id).pinged == -1);
            }
        }
    }
    {  // a derived relation test keys its search by the end that stays fixed
        world.territory(7, 15);
        const auto reach_before = backend::derived_relation<game::Reach>(world.registry).builds();
        world.tick();
        CHECK(backend::derived_relation<game::Reach>(world.registry).builds() - reach_before <= world.hearts().size());
        world.check_folds();

        for (const auto& [from, to] : {std::pair{12, 13}, std::pair{13, 14}, std::pair{14, 15}, std::pair{15, 11}}) {
            backend::relation_store<game::Walk>(world.registry).add(world.tiles.at(from), world.tiles.at(to), game::Walk{});
        }
        const auto route_before = backend::derived_relation<game::Route>(world.registry).builds();
        world.tick();
        // One search keyed by Goal for each direction.
        CHECK(backend::derived_relation<game::Route>(world.registry).builds() - route_before == 2);
        for (const auto& [id, entity] : world.tiles) {
            if (!world.registry.all_of<game::Tile>(entity)) {
                continue;
            }
            INFO("tile " << id);
            CHECK(world.registry.all_of<game::RoutedTo>(entity) == (id == 12 || id == 13 || id == 14));
            CHECK(world.registry.all_of<game::RoutedFrom>(entity) == (id == 11));
        }
    }
    {  // a pass between commit rounds sees the round's membership changes
        world.set_weights(0);
        world.tick();
        auto& lit_trace = world.registry.get<game::Log>(World::slots().Book).lit_trace;
        lit_trace.clear();
        world.tile(1).weight = 1;
        world.tile(3).weight = 1;
        world.tick();
        CHECK(lit_trace == "2;2;");
        lit_trace.clear();
        world.tick();
        CHECK(lit_trace.empty());
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
