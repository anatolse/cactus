// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <tuple>
#include <vector>

using cactus::runtime::entt_backend::connect_relation_ends;
using cactus::runtime::entt_backend::CreationOrdinal;
using cactus::runtime::entt_backend::derived_relation;
using cactus::runtime::entt_backend::Derivation;
using cactus::runtime::entt_backend::RelationStore;
using cactus::runtime::entt_backend::uncompacted_relation_store;

namespace {

struct Tile {};

struct Step {
    static constexpr bool kFromOne = false;
    static constexpr bool kToOne   = false;
    using FromTrait                = Tile;
    using ToTrait                  = Tile;
    int cost                       = 1;

    friend bool operator==(const Step&, const Step&) = default;
};

struct Reach {
    static constexpr bool kFromOne         = false;
    static constexpr bool kToOne           = false;
    static constexpr Derivation kDerivation = Derivation::Closure;
    using FromTrait                        = Tile;
    using ToTrait                          = Tile;
    using Source                           = Step;
};

struct Route {
    static constexpr Derivation kDerivation = Derivation::Shortest;
    using FromTrait                        = Tile;
    using ToTrait                          = Tile;
    using Source                           = Step;
    static std::int64_t source_cost(const Step& row) noexcept {
        return row.cost;
    }
    entt::entity next = entt::null;
    int distance      = 0;
};

struct Group {
    static constexpr Derivation kDerivation = Derivation::Components;
    using FromTrait                        = Tile;
    using ToTrait                          = Tile;
    using Source                           = Step;
};

// Over another derived relation: the closure of a closure is the closure.
struct ReachAgain {
    static constexpr Derivation kDerivation = Derivation::Closure;
    using FromTrait                        = Tile;
    using ToTrait                          = Tile;
    using Source                           = Reach;
};

using Pair = std::pair<entt::entity, entt::entity>;

struct World {
    entt::registry registry;
    std::vector<entt::entity> tiles;

    // Creation ordinals follow `order`, not entity ids, so ties are decided by ordinal.
    explicit World(std::size_t count, std::mt19937* shuffle = nullptr) {
        connect_relation_ends<Step>(registry);
        tiles.resize(count);
        registry.create(tiles.begin(), tiles.end());
        std::vector<std::uint64_t> ordinals(count);
        std::iota(ordinals.begin(), ordinals.end(), std::uint64_t{1});
        if (shuffle != nullptr) {
            std::ranges::shuffle(ordinals, *shuffle);
        }
        for (std::size_t index = 0; index < count; ++index) {
            registry.emplace<Tile>(tiles[index]);
            registry.emplace<CreationOrdinal>(tiles[index], ordinals[index]);
        }
    }

    RelationStore<Step>& steps() {
        return uncompacted_relation_store<Step>(registry);
    }

    void link(std::size_t from, std::size_t to, int cost = 1) {
        steps().add(tiles[from], tiles[to], Step{.cost = cost});
    }

    [[nodiscard]] std::uint64_t ordinal(entt::entity entity) const {
        return registry.get<CreationOrdinal>(entity).value;
    }
};

template <typename Cache>
std::vector<Pair> from_rows(Cache& cache, entt::entity from) {
    std::vector<Pair> rows;
    cache.for_each_from(from, [&](const auto& entry) { rows.emplace_back(entry.from, entry.to); });
    return rows;
}

template <typename Cache>
std::vector<Pair> to_rows(Cache& cache, entt::entity to) {
    std::vector<Pair> rows;
    cache.for_each_to(to, [&](const auto& entry) { rows.emplace_back(entry.from, entry.to); });
    return rows;
}

// ── Brute-force references ──────────────────────────────────────────────────

constexpr std::int64_t kUnreached = std::numeric_limits<std::int64_t>::max();

struct Reference {
    std::vector<std::vector<bool>> reach;          // one or more rows
    std::vector<std::vector<std::int64_t>> cost;   // least cost of one or more rows
    std::vector<std::vector<std::int64_t>> walk;   // least cost of zero or more rows
};

Reference reference(World& world) {
    const auto n = world.tiles.size();
    Reference ref{.reach = std::vector(n, std::vector<bool>(n, false)),
                  .cost  = std::vector(n, std::vector<std::int64_t>(n, kUnreached)),
                  .walk  = std::vector(n, std::vector<std::int64_t>(n, kUnreached))};
    std::map<entt::entity, std::size_t> index;
    for (std::size_t i = 0; i < n; ++i) {
        index[world.tiles[i]] = i;
        ref.walk[i][i]        = 0;
    }
    world.steps().for_each_live([&](const auto& entry) {
        const auto a = index.at(entry.from);
        const auto b = index.at(entry.to);
        ref.reach[a][b] = true;
        ref.cost[a][b]  = std::min(ref.cost[a][b], std::int64_t{std::max(entry.row.cost, 0)});
        ref.walk[a][b]  = std::min(ref.walk[a][b], ref.cost[a][b]);
    });
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                ref.reach[i][j] = ref.reach[i][j] || (ref.reach[i][k] && ref.reach[k][j]);
                if (ref.walk[i][k] != kUnreached && ref.walk[k][j] != kUnreached) {
                    ref.walk[i][j] = std::min(ref.walk[i][j], ref.walk[i][k] + ref.walk[k][j]);
                }
            }
        }
    }
    // One or more rows: a first row, then any walk.
    for (std::size_t i = 0; i < n; ++i) {
        world.steps().for_each_from(world.tiles[i], [&](const auto& entry) {
            const auto w     = index.at(entry.to);
            const auto first = std::int64_t{std::max(entry.row.cost, 0)};
            for (std::size_t j = 0; j < n; ++j) {
                if (ref.walk[w][j] != kUnreached) {
                    ref.cost[i][j] = std::min(ref.cost[i][j], first + ref.walk[w][j]);
                }
            }
        });
    }
    return ref;
}

// The lowest-ordinal second entity of a least-cost chain from `a` to `b`.
entt::entity reference_next(World& world, const Reference& ref, std::size_t a, std::size_t b) {
    std::map<entt::entity, std::size_t> index;
    for (std::size_t i = 0; i < world.tiles.size(); ++i) {
        index[world.tiles[i]] = i;
    }
    entt::entity best = entt::null;
    world.steps().for_each_from(world.tiles[a], [&](const auto& entry) {
        const auto w = index.at(entry.to);
        if (ref.walk[w][b] == kUnreached || std::max(entry.row.cost, 0) + ref.walk[w][b] != ref.cost[a][b]) {
            return;
        }
        if (best == entt::null || world.ordinal(entry.to) < world.ordinal(best)) {
            best = entry.to;
        }
    });
    return best;
}

void random_links(World& world, std::mt19937& random, int edges) {
    std::uniform_int_distribution<std::size_t> pick(0, world.tiles.size() - 1);
    std::uniform_int_distribution<int> cost(-2, 4);
    for (int edge = 0; edge < edges; ++edge) {
        world.link(pick(random), pick(random), cost(random));
    }
    world.steps().compact();
}

}  // namespace

// ── Revisions ───────────────────────────────────────────────────────────────

TEST_CASE("RelationStore: the revision changes only with rows or fields", "[runtime][relations][derived]") {
    World world(3);
    auto& store      = world.steps();
    auto revision    = store.revision();
    const auto moved = [&] {
        const auto now     = store.revision();
        const bool changed = now != revision;
        revision           = now;
        return changed;
    };
    world.link(0, 1, 2);
    CHECK(moved());
    world.link(0, 1, 2);
    CHECK_FALSE(moved());
    world.link(0, 1, 3);
    CHECK(moved());
    store.compact();
    CHECK_FALSE(moved());
    store.begin_run();
    store.keep(world.tiles[0], world.tiles[1], Step{.cost = 3});
    store.drop_unstamped();
    CHECK_FALSE(moved());
    store.begin_run();
    store.drop_unstamped();
    CHECK(moved());
    store.compact();
    CHECK_FALSE(moved());
    world.link(1, 2);
    CHECK(moved());
    world.registry.destroy(world.tiles[2]);
    CHECK(moved());
}

TEST_CASE("RelationStore: two stores never share a revision", "[runtime][relations][derived]") {
    RelationStore<Step> first;
    RelationStore<Step> second;
    CHECK(first.revision() != second.revision());
}

// ── Closure ─────────────────────────────────────────────────────────────────

TEST_CASE("Derived closure: forward and reverse match the brute force", "[runtime][relations][derived]") {
    std::mt19937 random(7);
    for (int round = 0; round < 25; ++round) {
        World world(9, &random);
        random_links(world, random, 12);
        const auto ref = reference(world);
        auto& reach    = derived_relation<Reach>(world.registry);
        const auto n   = world.tiles.size();
        for (std::size_t a = 0; a < n; ++a) {
            std::vector<Pair> forward;
            std::vector<Pair> reverse;
            for (std::size_t b = 0; b < n; ++b) {
                CHECK(reach.contains(world.tiles[a], world.tiles[b]) == ref.reach[a][b]);
                if (ref.reach[a][b]) {
                    forward.emplace_back(world.tiles[a], world.tiles[b]);
                }
                if (ref.reach[b][a]) {
                    reverse.emplace_back(world.tiles[b], world.tiles[a]);
                }
            }
            const auto by_ordinal = [&](bool from_end) {
                return [&, from_end](const Pair& left, const Pair& right) {
                    return from_end ? world.ordinal(left.first) < world.ordinal(right.first)
                                    : world.ordinal(left.second) < world.ordinal(right.second);
                };
            };
            std::ranges::sort(forward, by_ordinal(false));
            std::ranges::sort(reverse, by_ordinal(true));
            CHECK(from_rows(reach, world.tiles[a]) == forward);
            CHECK(to_rows(reach, world.tiles[a]) == reverse);
            CHECK(reach.has_from(world.tiles[a]) == !forward.empty());
            CHECK(reach.has_to(world.tiles[a]) == !reverse.empty());
        }
    }
}

TEST_CASE("Derived closure: a row (a, a) needs a chain back to a", "[runtime][relations][derived]") {
    World world(3);
    world.link(0, 1);
    auto& reach = derived_relation<Reach>(world.registry);
    CHECK_FALSE(reach.contains(world.tiles[0], world.tiles[0]));
    CHECK_FALSE(reach.contains(world.tiles[2], world.tiles[2]));
    world.link(1, 0);
    CHECK(derived_relation<Reach>(world.registry).contains(world.tiles[0], world.tiles[0]));
}

TEST_CASE("Derived closure: an unchanged source rebuilds nothing", "[runtime][relations][derived]") {
    World world(4);
    world.link(0, 1);
    world.link(1, 2);
    auto& reach = derived_relation<Reach>(world.registry);
    CHECK(reach.contains(world.tiles[0], world.tiles[2]));
    const auto builds = reach.builds();
    CHECK(derived_relation<Reach>(world.registry).contains(world.tiles[1], world.tiles[2]));
    CHECK(derived_relation<Reach>(world.registry).contains(world.tiles[0], world.tiles[2]));
    CHECK(reach.builds() == builds);
    world.link(2, 3);
    CHECK(derived_relation<Reach>(world.registry).contains(world.tiles[0], world.tiles[3]));
    CHECK(reach.builds() == builds + 1);
}

TEST_CASE("Derived closure: links that disappear drop their rows", "[runtime][relations][derived]") {
    World world(3);
    world.link(0, 1);
    world.link(1, 2);
    CHECK(derived_relation<Reach>(world.registry).contains(world.tiles[0], world.tiles[2]));
    world.steps().erase(world.tiles[1], world.tiles[2]);
    world.steps().compact();
    CHECK_FALSE(derived_relation<Reach>(world.registry).contains(world.tiles[0], world.tiles[2]));
    world.registry.destroy(world.tiles[1]);
    CHECK_FALSE(derived_relation<Reach>(world.registry).has_from(world.tiles[0]));
}

TEST_CASE("Derived closure: over a derived source", "[runtime][relations][derived]") {
    std::mt19937 random(11);
    World world(8, &random);
    random_links(world, random, 10);
    auto& once  = derived_relation<Reach>(world.registry);
    auto& twice = derived_relation<ReachAgain>(world.registry);
    for (const auto a : world.tiles) {
        for (const auto b : world.tiles) {
            CHECK(twice.contains(a, b) == once.contains(a, b));
        }
    }
}

// ── Shortest ────────────────────────────────────────────────────────────────

TEST_CASE("Derived shortest: forward and reverse give the brute-force rows", "[runtime][relations][derived]") {
    std::mt19937 random(3);
    for (int round = 0; round < 25; ++round) {
        World world(9, &random);
        random_links(world, random, 16);
        const auto ref = reference(world);
        auto& route    = derived_relation<Route>(world.registry);
        const auto n   = world.tiles.size();
        using Row      = std::tuple<entt::entity, entt::entity, entt::entity, int>;
        std::set<Row> forward;
        std::set<Row> reverse;
        std::set<Row> expected;
        for (std::size_t a = 0; a < n; ++a) {
            route.for_each_from(world.tiles[a], [&](const auto& entry) {
                forward.emplace(entry.from, entry.to, entry.row.next, entry.row.distance);
            });
            route.for_each_to(world.tiles[a], [&](const auto& entry) {
                reverse.emplace(entry.from, entry.to, entry.row.next, entry.row.distance);
            });
            for (std::size_t b = 0; b < n; ++b) {
                if (a == b || ref.cost[a][b] == kUnreached) {
                    continue;
                }
                expected.emplace(world.tiles[a],
                                 world.tiles[b],
                                 reference_next(world, ref, a, b),
                                 static_cast<int>(ref.cost[a][b]));
            }
        }
        CHECK(forward == expected);
        CHECK(reverse == expected);
    }
}

TEST_CASE("Derived shortest: the cheapest step, and ties by creation order", "[runtime][relations][derived]") {
    // a=0, b=1, c=2, d=3; b is older than c.
    World world(4);
    world.link(0, 1, 1);
    world.link(1, 3, 1);
    world.link(0, 2, 1);
    world.link(2, 3, 5);
    // Each read goes through the accessor, which drops rows a source change made stale.
    const auto row = [&](std::size_t from, std::size_t to) {
        std::optional<std::pair<entt::entity, int>> found;
        derived_relation<Route>(world.registry).for_each_to(world.tiles[to], [&](const auto& entry) {
            if (entry.from == world.tiles[from]) {
                found.emplace(entry.row.next, entry.row.distance);
            }
        });
        return found;
    };
    CHECK(row(0, 3) == std::pair{world.tiles[1], 2});
    world.link(2, 3, 1);
    CHECK(row(0, 3) == std::pair{world.tiles[1], 2});
    world.link(1, 3, 2);
    CHECK(row(0, 3) == std::pair{world.tiles[2], 2});
    CHECK_FALSE(row(3, 0).has_value());
    CHECK_FALSE(derived_relation<Route>(world.registry).contains(world.tiles[0], world.tiles[0]));
}

TEST_CASE("Derived shortest: negative costs count as zero", "[runtime][relations][derived]") {
    World world(3);
    world.link(0, 1, -5);
    world.link(1, 2, 3);
    std::optional<int> distance;
    derived_relation<Route>(world.registry).for_each_from(world.tiles[0], [&](const auto& entry) {
        if (entry.to == world.tiles[2]) {
            distance = entry.row.distance;
        }
    });
    CHECK(distance == 3);
}

TEST_CASE("Derived shortest: a chain over the int range is dropped", "[runtime][relations][derived]") {
    constexpr int kMax = std::numeric_limits<int>::max();
    World world(3);
    world.link(0, 1, kMax);
    world.link(1, 2, 1);
    auto& route = derived_relation<Route>(world.registry);
    CHECK(route.contains(world.tiles[0], world.tiles[1]));
    CHECK(route.contains(world.tiles[1], world.tiles[2]));
    CHECK_FALSE(route.contains(world.tiles[0], world.tiles[2]));
    CHECK(from_rows(route, world.tiles[0]) == std::vector<Pair>{{world.tiles[0], world.tiles[1]}});
}

// ── Components ──────────────────────────────────────────────────────────────

TEST_CASE("Derived components: every member maps to its group's oldest", "[runtime][relations][derived]") {
    std::mt19937 random(5);
    for (int round = 0; round < 25; ++round) {
        World world(10, &random);
        random_links(world, random, 7);
        auto& group  = derived_relation<Group>(world.registry);
        const auto n = world.tiles.size();
        for (std::size_t m = 0; m < n; ++m) {
            auto root = world.tiles[m];
            std::vector<bool> seen(n, false);
            std::vector<std::size_t> frontier{m};
            seen[m] = true;
            while (!frontier.empty()) {
                const auto at = frontier.back();
                frontier.pop_back();
                for (std::size_t next = 0; next < n; ++next) {
                    const bool edge = world.steps().contains(world.tiles[at], world.tiles[next]) ||
                                      world.steps().contains(world.tiles[next], world.tiles[at]);
                    if (edge && !seen[next]) {
                        seen[next] = true;
                        frontier.push_back(next);
                    }
                }
            }
            for (std::size_t other = 0; other < n; ++other) {
                if (seen[other] && world.ordinal(world.tiles[other]) < world.ordinal(root)) {
                    root = world.tiles[other];
                }
            }
            CHECK(from_rows(group, world.tiles[m]) == std::vector<Pair>{{world.tiles[m], root}});
            CHECK(group.contains(world.tiles[m], root));
            CHECK(group.has_to(root));
        }
    }
}

TEST_CASE("Derived components: merge, split and lone members", "[runtime][relations][derived]") {
    World world(6);
    const auto& t = world.tiles;
    world.link(0, 1);
    world.link(3, 4);
    auto root_of = [&](std::size_t member) {
        std::vector<Pair> rows = from_rows(derived_relation<Group>(world.registry), t[member]);
        REQUIRE(rows.size() == 1);
        return rows.front().second;
    };
    CHECK(root_of(1) == t[0]);
    CHECK(root_of(4) == t[3]);
    CHECK(root_of(5) == t[5]);
    world.link(4, 1);
    CHECK(root_of(4) == t[0]);
    CHECK(root_of(3) == t[0]);
    CHECK(to_rows(derived_relation<Group>(world.registry), t[0]) ==
          std::vector<Pair>{{t[0], t[0]}, {t[1], t[0]}, {t[3], t[0]}, {t[4], t[0]}});
    world.registry.destroy(t[1]);
    CHECK(root_of(0) == t[0]);
    CHECK(root_of(4) == t[3]);
    const auto late = world.registry.create();
    world.registry.emplace<Tile>(late);
    world.registry.emplace<CreationOrdinal>(late, 100U);
    CHECK(from_rows(derived_relation<Group>(world.registry), late) == std::vector<Pair>{{late, late}});
    world.registry.remove<Tile>(late);
    CHECK(from_rows(derived_relation<Group>(world.registry), late).empty());
    auto& group = derived_relation<Group>(world.registry);
    std::vector<Pair> all;
    for (const auto& entry : group.rows()) {
        all.emplace_back(entry.from, entry.to);
    }
    CHECK(all == std::vector<Pair>{{t[0], t[0]}, {t[2], t[2]}, {t[3], t[3]}, {t[4], t[3]}, {t[5], t[5]}});
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
