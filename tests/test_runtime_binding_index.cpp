// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/runtime.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

struct Member {
    Vector3 center{};
};
struct Mover {};
struct Solid {};

std::vector<entt::entity> entities_of(std::vector<OrderedEntity> found) {
    std::ranges::sort(found);
    std::vector<entt::entity> result;
    result.reserve(found.size());
    for (const auto& [ordinal, entity] : found) {
        result.push_back(entity);
    }
    return result;
}

entt::entity make_ordered(entt::registry& registry) {
    const auto entity = create_entity(registry);
    registry.emplace<CreationOrdinal>(entity, CreationOrdinal{.value = generated_next_creation_ordinal()});
    return entity;
}

entt::entity make(entt::registry& registry, Vector3 center) {
    const auto entity = make_ordered(registry);
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

void attach(entt::registry& registry, SpatialBindingIndex3D& index, StaticStore3D& store) {
    index.reset(registry, store, 1.0F);
    watch_binding_traits<Member, Mover>(registry, index.tracker());
    for (const auto entity : registry.view<Member>()) {
        index.tracker().mark(entity);
    }
}

void catch_up(entt::registry& registry, SpatialBindingIndex3D& index) {
    index.catch_up(registry, [&](entt::entity entity) { return classify(registry, entity); });
}

std::vector<entt::entity> near(const entt::registry& registry, SpatialBindingIndex3D& index, Vector3 min, Vector3 max) {
    std::vector<OrderedEntity> found;
    index.query_static(registry, min, max, found);
    CHECK(std::ranges::is_sorted(found));
    CHECK(std::ranges::adjacent_find(found) == found.end());
    return entities_of(found);
}

bool finite_box(Vector3 min, Vector3 max) {
    return std::isfinite(min.x) && std::isfinite(min.y) && std::isfinite(min.z) && std::isfinite(max.x) &&
           std::isfinite(max.y) && std::isfinite(max.z);
}

// What a grid query must return: every member for a non-finite query, else the
// members whose bounds overlap it or are not finite, by handle.
struct GridOracle {
    struct Box {
        Vector3 min;
        Vector3 max;
    };
    std::map<entt::entity, Box> members;

    [[nodiscard]] std::vector<entt::entity> query(Vector3 min, Vector3 max) const {
        std::vector<entt::entity> found;
        for (const auto& [entity, box] : members) {
            if (!finite_box(min, max) || !finite_box(box.min, box.max) ||
                proxy_bounds_overlap(min, max, box.min, box.max)) {
                found.push_back(entity);
            }
        }
        return found;
    }
};

std::vector<entt::entity> grid_query(StaticGrid3D& grid,
                                     Vector3 min,
                                     Vector3 max,
                                     std::uint32_t owners = StaticGrid3D::kAllOwners) {
    std::vector<entt::entity> found;
    grid.query(min, max, found, owners);
    std::ranges::sort(found);
    const auto [first, last] = std::ranges::unique(found);
    found.erase(first, last);
    return found;
}

// ── Equality fixtures ────────────────────────────────────────────────────────

struct Grid {
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
};

struct Cell {
    entt::entity grid{entt::null};
    int x    = 0;
    int y    = 0;
    int kind = 0;  // an enum-like part with 3 values
};

using CellIndex = DenseKeyIndex<3>;

std::optional<CellIndex::Extents> grid_extents(const entt::registry& registry, entt::entity grid) {
    const auto* found = registry.valid(grid) ? registry.try_get<Grid>(grid) : nullptr;
    if (found == nullptr) {
        return std::nullopt;
    }
    return CellIndex::Extents{std::pair<std::int64_t, std::int64_t>{found->min_x, found->max_x + 1},
                              std::pair<std::int64_t, std::int64_t>{found->min_y, found->max_y + 1},
                              std::pair<std::int64_t, std::int64_t>{0, 3}};
}

std::optional<CellIndex::Key> cell_key(const entt::registry& registry, entt::entity entity) {
    const auto* cell = registry.try_get<Cell>(entity);
    if (cell == nullptr) {
        return std::nullopt;
    }
    return CellIndex::Key{.block = cell->grid, .values = {dense_key_part(cell->x), dense_key_part(cell->y),
                                                          dense_key_part(cell->kind)}};
}

void refresh(entt::registry& registry, CellIndex& index) {
    if (index.tracker().stale(registry)) {
        index.reset(registry);
        watch_binding_traits<Cell>(registry, index.tracker());
        for (const auto entity : registry.view<Cell>()) {
            index.tracker().mark(entity);
        }
    }
    index.catch_up(
        registry, [&](entt::entity entity) { return cell_key(registry, entity); },
        [&](entt::entity grid) { return grid_extents(registry, grid); });
}

using FlatCellIndex = FlatKeyIndex<4>;

std::optional<FlatCellIndex::Key> flat_cell_key(const entt::registry& registry, entt::entity entity) {
    const auto* cell = registry.try_get<Cell>(entity);
    if (cell == nullptr) {
        return std::nullopt;
    }
    return FlatCellIndex::Key{equality_key_part(cell->grid), equality_key_part(cell->x), equality_key_part(cell->y),
                              equality_key_part(cell->kind)};
}

void refresh(entt::registry& registry, FlatCellIndex& index) {
    if (index.tracker().stale(registry)) {
        index.reset(registry);
        watch_binding_traits<Cell>(registry, index.tracker());
        for (const auto entity : registry.view<Cell>()) {
            index.tracker().mark(entity);
        }
    }
    index.catch_up(registry, [&](entt::entity entity) { return flat_cell_key(registry, entity); });
}

// Every live cell matching the query, oldest first: what a full scan folds.
std::vector<entt::entity> scan_cells(const entt::registry& registry,
                                     std::optional<entt::entity> grid,
                                     std::int64_t x,
                                     std::int64_t y,
                                     std::int64_t kind) {
    std::vector<entt::entity> found;
    for (const auto entity : registry.view<Cell>()) {
        const auto& cell = registry.get<Cell>(entity);
        if ((!grid.has_value() || cell.grid == *grid) && cell.x == x && cell.y == y && cell.kind == kind) {
            found.push_back(entity);
        }
    }
    return creation_ordered(registry, found);
}

entt::entity make_cell(entt::registry& registry, entt::entity grid, int x, int y, int kind = 0) {
    const auto entity = make_ordered(registry);
    registry.emplace<Cell>(entity, Cell{.grid = grid, .x = x, .y = y, .kind = kind});
    return entity;
}

entt::entity make_grid(entt::registry& registry, int max_x, int max_y) {
    const auto entity = make_ordered(registry);
    registry.emplace<Grid>(entity, Grid{.min_x = 0, .max_x = max_x, .min_y = -2, .max_y = max_y});
    return entity;
}

std::vector<entt::entity> dense_find(const entt::registry& registry,
                                     const CellIndex& index,
                                     entt::entity grid,
                                     std::int64_t x,
                                     std::int64_t y,
                                     std::int64_t kind = 0) {
    std::vector<entt::entity> found{entt::null};
    if (grid == entt::null) {
        index.find_any_block(registry, {x, y, kind}, found);
    } else {
        index.find(registry, CellIndex::Key{.block = grid, .values = {x, y, kind}}, found);
    }
    return found;
}

}  // namespace

// ── Sparse slots ─────────────────────────────────────────────────────────────

TEST_CASE("sparse slots map entity indexes to values on pages made on demand", "[runtime][binding-index]") {
    SparseSlots slots;
    CHECK(slots.get(entt::entity{3}) == SparseSlots::kNone);
    CHECK(slots.page_count() == 0);
    slots.set(entt::entity{3}, 7);
    CHECK(slots.get(entt::entity{3}) == 7);
    CHECK(slots.page_count() == 1);
    CHECK(slots.get(entt::entity{4}) == SparseSlots::kNone);
    CHECK(slots.get(entt::entity{900000}) == SparseSlots::kNone);
    CHECK(slots.page_count() == 1);

    slots.set(entt::entity{900000}, 1);
    CHECK(slots.page_count() == 2);
    CHECK(slots.get(entt::entity{900000}) == 1);

    slots.erase(entt::entity{3});
    CHECK(slots.get(entt::entity{3}) == SparseSlots::kNone);
    slots.erase(entt::entity{123456});
    slots.set(entt::entity{3}, 9);
    CHECK(slots.get(entt::entity{3}) == 9);

    entt::registry registry;
    const auto first = registry.create();
    slots.set(first, 5);
    registry.destroy(first);
    const auto reused = registry.create();
    REQUIRE(entt::to_entity(reused) == entt::to_entity(first));
    CHECK(slots.get(reused) == 5);

    slots.clear();
    CHECK(slots.page_count() == 0);
    CHECK(slots.get(entt::entity{900000}) == SparseSlots::kNone);
}

// ── Equality indexes ─────────────────────────────────────────────────────────

TEST_CASE("key parts map values to addressable integers", "[runtime][binding-index]") {
    enum class Shade : std::uint8_t { Dark, Light };
    CHECK(dense_key_part(-3) == -3);
    CHECK(dense_key_part(true) == 1);
    CHECK(dense_key_part(false) == 0);
    CHECK(dense_key_part(Shade::Light) == 1);
    CHECK(dense_key_part(4.0F) == 4);
    CHECK(dense_key_part(-2.0) == -2);
    for (const double value : {3.5, 2147483648.0, std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        CHECK(dense_key_part(value) == kNoDenseKey);
    }
    CHECK(dense_key_part(-2147483647 - 1) != kNoDenseKey);
}

TEST_CASE("a dense index finds each key's members oldest first within its block", "[runtime][binding-index]") {
    entt::registry registry;
    const auto grid  = make_grid(registry, 3, 3);
    const auto a     = make_cell(registry, grid, 1, 2);
    const auto other = make_cell(registry, grid, 2, 1);
    const auto b     = make_cell(registry, grid, 1, 2);
    const auto lit   = make_cell(registry, grid, 1, 2, 2);
    CellIndex index;
    refresh(registry, index);
    CHECK(index.size() == 4);
    CHECK(dense_find(registry, index, grid, 1, 2) == std::vector{a, b});
    CHECK(dense_find(registry, index, grid, 2, 1) == std::vector{other});
    CHECK(dense_find(registry, index, grid, 1, 2, 2) == std::vector{lit});
    CHECK(dense_find(registry, index, grid, 3, 3).empty());

    // Out of range, a non-integral float, and the wrong block find nothing.
    CHECK(dense_find(registry, index, grid, 4, 0).empty());
    CHECK(dense_find(registry, index, grid, -1, 0).empty());
    CHECK(dense_find(registry, index, grid, kNoDenseKey, 2).empty());
    CHECK(dense_find(registry, index, entt::entity{999}, 1, 2).empty());
    CHECK(dense_find(registry, index, a, 1, 2).empty());
    std::vector<entt::entity> unblocked{entt::null};
    index.find(registry, CellIndex::Key{.block = entt::null, .values = {1, 2, 0}}, unblocked);
    CHECK(unblocked.empty());

    registry.destroy(a);
    refresh(registry, index);
    CHECK(dense_find(registry, index, grid, 1, 2) == std::vector{b});
    CHECK(index.size() == 3);
    registry.remove<Cell>(b);
    refresh(registry, index);
    CHECK(dense_find(registry, index, grid, 1, 2).empty());
}

TEST_CASE("shifted queries past a grid edge find nothing", "[runtime][binding-index]") {
    entt::registry registry;
    const auto grid   = make_grid(registry, 3, 3);
    const auto corner = make_cell(registry, grid, 0, -2);
    const auto edge   = make_cell(registry, grid, 3, 3);
    CellIndex dense;
    refresh(registry, dense);
    FlatCellIndex flat;
    refresh(registry, flat);
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{-1, -2}, {0, -3}, {4, 3}, {3, 4}}) {
        CHECK(dense_find(registry, dense, grid, x, y).empty());
        CHECK(dense_find(registry, dense, entt::null, x, y).empty());
        std::vector<entt::entity> found{entt::null};
        flat.find(registry,
                  FlatCellIndex::Key{equality_key_part(grid), equality_key_part(x), equality_key_part(y),
                                     equality_key_part(0)},
                  found);
        CHECK(found.empty());
    }
    CHECK(dense_find(registry, dense, grid, 0, -2) == std::vector{corner});
    CHECK(dense_find(registry, dense, entt::null, 3, 3) == std::vector{edge});
}

TEST_CASE("merged equality candidates come oldest first, each once", "[runtime][binding-index]") {
    entt::registry registry;
    const auto a = make_ordered(registry);
    const auto b = make_ordered(registry);
    const auto c = make_ordered(registry);
    std::vector<entt::entity> candidates{c, a, b, a, c, c};
    merge_equality_candidates(registry, candidates);
    CHECK(candidates == std::vector{a, b, c});

    candidates.clear();
    merge_equality_candidates(registry, candidates);
    CHECK(candidates.empty());

    // A buffer that has grown is reused without allocating.
    candidates.reserve(16);
    const auto* storage  = candidates.data();
    const auto capacity  = candidates.capacity();
    candidates.assign({b, b, a});
    merge_equality_candidates(registry, candidates);
    CHECK(candidates == std::vector{a, b});
    CHECK(candidates.data() == storage);
    CHECK(candidates.capacity() == capacity);
}

TEST_CASE("a dense index keeps creation order when an older member joins later", "[runtime][binding-index]") {
    entt::registry registry;
    const auto grid  = make_grid(registry, 3, 3);
    const auto older = make_ordered(registry);
    const auto newer = make_cell(registry, grid, 0, 0);
    CellIndex index;
    refresh(registry, index);
    registry.emplace<Cell>(older, Cell{.grid = grid, .x = 0, .y = 0});
    const auto newest = make_cell(registry, grid, 0, 0);
    refresh(registry, index);
    CHECK(dense_find(registry, index, grid, 0, 0) == std::vector{older, newer, newest});
    registry.destroy(newer);
    refresh(registry, index);
    CHECK(dense_find(registry, index, grid, 0, 0) == std::vector{older, newest});
}

TEST_CASE("a dense index searches every block for a query without one", "[runtime][binding-index]") {
    entt::registry registry;
    const auto first  = make_grid(registry, 3, 3);
    const auto second = make_grid(registry, 5, 5);
    const auto a      = make_cell(registry, second, 1, 1);
    const auto b      = make_cell(registry, first, 1, 1);
    const auto c      = make_cell(registry, second, 1, 1);
    const auto d      = make_cell(registry, second, 5, 5);
    CellIndex index;
    refresh(registry, index);
    CHECK(dense_find(registry, index, entt::null, 1, 1) == std::vector{a, b, c});
    CHECK(dense_find(registry, index, entt::null, 5, 5) == std::vector{d});
    CHECK(dense_find(registry, index, entt::null, 6, 5).empty());
}

TEST_CASE("a dense index still finds members whose block it cannot address", "[runtime][binding-index]") {
    entt::registry registry;
    const auto grid  = make_grid(registry, 3, 3);
    const auto stray = make_cell(registry, grid, 9, 0);
    const auto gone  = make_grid(registry, 3, 3);
    const auto orphan = make_cell(registry, gone, 1, 1);
    registry.destroy(gone);
    const auto reused = registry.create();
    registry.emplace<CreationOrdinal>(reused, CreationOrdinal{.value = generated_next_creation_ordinal()});
    registry.emplace<Grid>(reused, Grid{.min_x = 0, .max_x = 1, .min_y = 0, .max_y = 1});
    REQUIRE(entt::to_entity(reused) == entt::to_entity(gone));
    const auto fresh = make_cell(registry, reused, 1, 1);
    CellIndex index;
    refresh(registry, index);
    CHECK(dense_find(registry, index, grid, 9, 0) == std::vector{stray});
    CHECK(dense_find(registry, index, gone, 1, 1) == std::vector{orphan});
    CHECK(dense_find(registry, index, reused, 1, 1) == std::vector{fresh});
    CHECK(dense_find(registry, index, entt::null, 1, 1) == std::vector{orphan, fresh});
    registry.destroy(orphan);
    refresh(registry, index);
    CHECK(dense_find(registry, index, gone, 1, 1).empty());
    CHECK(dense_find(registry, index, entt::null, 1, 1) == std::vector{fresh});
}

TEST_CASE("a dense index without a block entity uses one block", "[runtime][binding-index]") {
    entt::registry registry;
    DenseKeyIndex<1> index;
    index.reset(registry);
    watch_binding_traits<Cell>(registry, index.tracker());
    const auto key_of = [&](entt::entity entity) -> std::optional<DenseKeyIndex<1>::Key> {
        const auto* cell = registry.try_get<Cell>(entity);
        if (cell == nullptr) {
            return std::nullopt;
        }
        return DenseKeyIndex<1>::Key{.block = entt::null, .values = {dense_key_part(cell->kind)}};
    };
    const auto extents_of = [](entt::entity) {
        return std::optional{DenseKeyIndex<1>::Extents{std::pair<std::int64_t, std::int64_t>{0, 3}}};
    };
    const auto a = make_cell(registry, entt::null, 0, 0, 2);
    const auto b = make_cell(registry, entt::null, 0, 0, 1);
    const auto c = make_cell(registry, entt::null, 0, 0, 2);
    index.catch_up(registry, key_of, extents_of);
    std::vector<entt::entity> found;
    index.find(registry, DenseKeyIndex<1>::Key{.block = entt::null, .values = {2}}, found);
    CHECK(found == std::vector{a, c});
    index.find(registry, DenseKeyIndex<1>::Key{.block = entt::null, .values = {1}}, found);
    CHECK(found == std::vector{b});
}

TEST_CASE("a new epoch rebuilds a dense index", "[runtime][binding-index]") {
    entt::registry registry;
    const auto grid = make_grid(registry, 3, 3);
    const auto a    = make_cell(registry, grid, 1, 1);
    CellIndex index;
    refresh(registry, index);
    REQUIRE(dense_find(registry, index, grid, 1, 1) == std::vector{a});
    advance_binding_index_epoch();
    CHECK(index.tracker().stale(registry));
    refresh(registry, index);
    CHECK(dense_find(registry, index, grid, 1, 1) == std::vector{a});
    CHECK(index.size() == 1);
}

TEST_CASE("a flat index keeps each key's members oldest first", "[runtime][binding-index]") {
    entt::registry registry;
    const auto grid   = make_grid(registry, 3, 3);
    const auto first  = make_cell(registry, grid, 100000, 2);
    const auto other  = make_cell(registry, grid, 2, 1);
    const auto second = make_cell(registry, grid, 100000, 2);
    FlatCellIndex index;
    refresh(registry, index);
    const auto key = FlatCellIndex::Key{equality_key_part(grid), equality_key_part(100000), equality_key_part(2),
                                        equality_key_part(0)};
    std::vector<entt::entity> found{entt::null};
    index.find(registry, key, found);
    CHECK(found == std::vector{first, second});
    index.find(registry,
               FlatCellIndex::Key{equality_key_part(grid), equality_key_part(2), equality_key_part(1),
                                  equality_key_part(0)},
               found);
    CHECK(found == std::vector{other});
    index.find(registry, FlatCellIndex::Key{9, 9, 9, 9}, found);
    CHECK(found.empty());

    registry.destroy(first);
    refresh(registry, index);
    index.find(registry, key, found);
    CHECK(found == std::vector{second});
    CHECK(index.size() == 2);
    advance_binding_index_epoch();
    refresh(registry, index);
    CHECK(index.size() == 2);
}

TEST_CASE("dense and flat indexes find what a full scan finds", "[runtime][binding-index]") {
    std::mt19937 random(20261023U);
    entt::registry registry;
    std::vector<entt::entity> grids{make_grid(registry, 4, 3), make_grid(registry, 2, 6)};
    CellIndex dense;
    FlatCellIndex flat;
    std::vector<entt::entity> cells;
    const auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(random); };
    for (int round = 0; round < 60; ++round) {
        for (int step = 0; step < 25; ++step) {
            const int action = pick(0, 9);
            if (action < 6 || cells.empty()) {
                const auto grid = grids[static_cast<std::size_t>(pick(0, 1))];
                cells.push_back(make_cell(registry, grid, pick(-1, 6), pick(-3, 7), pick(0, 2)));
            } else if (action < 8) {
                const auto at = static_cast<std::size_t>(pick(0, static_cast<int>(cells.size()) - 1));
                registry.destroy(cells[at]);
                cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(at));
            } else {
                const auto at = static_cast<std::size_t>(pick(0, static_cast<int>(cells.size()) - 1));
                if (registry.all_of<Cell>(cells[at])) {
                    registry.remove<Cell>(cells[at]);
                } else {
                    registry.emplace<Cell>(cells[at], Cell{.grid = grids[0], .x = pick(0, 4), .y = pick(0, 3)});
                }
            }
        }
        if (round == 30) {
            advance_binding_index_epoch();
        }
        refresh(registry, dense);
        refresh(registry, flat);
        for (int query = 0; query < 30; ++query) {
            const auto grid = grids[static_cast<std::size_t>(pick(0, 1))];
            const int x     = pick(-2, 7);
            const int y     = pick(-4, 8);
            const int kind  = pick(0, 2);
            const auto expected = scan_cells(registry, grid, x, y, kind);
            CHECK(dense_find(registry, dense, grid, x, y, kind) == expected);
            CHECK(dense_find(registry, dense, entt::null, x, y, kind) == scan_cells(registry, std::nullopt, x, y, kind));
            std::vector<entt::entity> found;
            flat.find(registry,
                      FlatCellIndex::Key{equality_key_part(grid), equality_key_part(x), equality_key_part(y),
                                         equality_key_part(kind)},
                      found);
            CHECK(found == expected);
        }
    }
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

// ── Static grid ──────────────────────────────────────────────────────────────

TEST_CASE("the static grid returns overlapping members once each", "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(1.0F);
    const auto a = entt::entity{1};
    const auto b = entt::entity{2};
    const auto c = entt::entity{3};
    grid.insert(a, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F});
    grid.insert(b, Vector3{.x = 0.9F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 2.9F, .y = 1.0F, .z = 1.0F});
    grid.insert(c, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, Vector3{.x = 6.0F, .y = 6.0F, .z = 6.0F});
    CHECK(grid_query(grid, Vector3{.x = 0.5F, .y = 0.5F, .z = 0.5F}, Vector3{.x = 1.5F, .y = 0.6F, .z = 0.6F}) ==
          std::vector{a, b});
    CHECK(grid_query(grid, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{a, b});

    grid.remove(a);
    CHECK(grid_query(grid, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.95F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{b});
    CHECK(grid.size() == 2);
    CHECK(grid.contains(b));
    CHECK_FALSE(grid.contains(a));
}

TEST_CASE("the static grid keeps huge or non-finite members aside and scans for huge queries",
          "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(1.0F);
    const float inf = std::numeric_limits<float>::infinity();
    const auto wide = entt::entity{1};
    const auto odd  = entt::entity{2};
    const auto unit = entt::entity{3};
    grid.insert(wide, Vector3{.x = -100.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 100.0F, .y = 1.0F, .z = 1.0F});
    grid.insert(odd, Vector3{.x = -inf, .y = 0.0F, .z = 0.0F}, Vector3{.x = 0.0F, .y = 1.0F, .z = 1.0F});
    grid.insert(unit, Vector3{.x = 50.0F, .y = 50.0F, .z = 50.0F}, Vector3{.x = 51.0F, .y = 51.0F, .z = 51.0F});
    CHECK(grid_query(grid, Vector3{.x = 99.0F, .y = 0.5F, .z = 0.5F}, Vector3{.x = 99.5F, .y = 0.6F, .z = 0.6F}) ==
          std::vector{wide, odd});
    CHECK(grid_query(grid, Vector3{.x = -1000.0F, .y = -1000.0F, .z = -1000.0F},
                     Vector3{.x = 1000.0F, .y = 1000.0F, .z = 1000.0F}) == std::vector{wide, odd, unit});
    CHECK(grid_query(grid, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = inf, .y = 1.0F, .z = 1.0F}) ==
          std::vector{wide, odd, unit});
}

TEST_CASE("the static grid keeps members spanning a vast volume aside and offers everything to a NaN query",
          "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(1.0F);
    const auto vast = entt::entity{1};
    const auto unit = entt::entity{2};
    grid.insert(vast, Vector3{.x = -1.0e12F, .y = -1.0e12F, .z = -1.0e12F},
                Vector3{.x = 1.0e12F, .y = 1.0e12F, .z = 1.0e12F});
    grid.insert(unit, Vector3{.x = 5.0F, .y = 5.0F, .z = 5.0F}, Vector3{.x = 6.0F, .y = 6.0F, .z = 6.0F});
    CHECK(grid_query(grid, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{vast});
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(grid_query(grid, Vector3{.x = nan, .y = 0.0F, .z = 0.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{vast, unit});
}

TEST_CASE("the static grid files members larger than a cell in every cell they cover", "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(1.0F);
    const auto wall = entt::entity{1};
    const auto unit = entt::entity{2};
    grid.insert(wall, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 6.0F, .y = 0.5F, .z = 0.5F});
    grid.insert(unit, Vector3{.x = 5.5F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 6.5F, .y = 0.5F, .z = 0.5F});
    CHECK(grid_query(grid, Vector3{.x = 5.9F, .y = 0.1F, .z = 0.1F}, Vector3{.x = 6.0F, .y = 0.2F, .z = 0.2F}) ==
          std::vector{wall, unit});
    CHECK(grid_query(grid, Vector3{.x = 6.2F, .y = 0.1F, .z = 0.1F}, Vector3{.x = 7.0F, .y = 0.2F, .z = 0.2F}) ==
          std::vector{unit});
    CHECK(grid_query(grid, Vector3{.x = 0.1F, .y = 0.1F, .z = 0.1F}, Vector3{.x = 0.2F, .y = 0.2F, .z = 0.2F}) ==
          std::vector{wall});
}

TEST_CASE("the static grid sees inserts and removals made after a query", "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(1.0F);
    const Vector3 lo{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const Vector3 hi{.x = 1.0F, .y = 1.0F, .z = 1.0F};
    for (std::uint32_t i = 0; i < 200; ++i) {
        const auto x = static_cast<float>(i) * 2.0F;
        grid.insert(entt::entity{i}, Vector3{.x = x, .y = 0.0F, .z = 0.0F}, Vector3{.x = x + 1.0F, .y = 1.0F, .z = 1.0F});
    }
    CHECK(grid_query(grid, lo, hi) == std::vector{entt::entity{0}});
    grid.insert(entt::entity{500}, lo, hi);
    CHECK(grid_query(grid, lo, hi) == std::vector{entt::entity{0}, entt::entity{500}});
    grid.remove(entt::entity{0});
    CHECK(grid_query(grid, lo, hi) == std::vector{entt::entity{500}});
    for (std::uint32_t i = 1; i < 200; ++i) {
        grid.remove(entt::entity{i});
    }
    CHECK(grid.size() == 1);
    CHECK(grid_query(grid, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 500.0F, .y = 2.0F, .z = 2.0F}) ==
          std::vector{entt::entity{500}});
}

TEST_CASE("the static grid keeps a member while any owner holds it and refiles changed bounds",
          "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(1.0F);
    const auto a = entt::entity{4};
    const Vector3 lo{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const Vector3 hi{.x = 1.0F, .y = 1.0F, .z = 1.0F};
    CHECK(grid.insert(a, lo, hi, 0));
    CHECK_FALSE(grid.insert(a, lo, hi, 0));
    CHECK(grid.insert(a, lo, hi, 1));
    CHECK(grid.size() == 1);
    CHECK(grid_query(grid, lo, hi, StaticGrid3D::owner_mask(1)) == std::vector{a});
    CHECK(grid.remove(a, 0));
    CHECK_FALSE(grid.remove(a, 0));
    CHECK(grid.contains(a));
    CHECK(grid_query(grid, lo, hi, StaticGrid3D::owner_mask(0)).empty());
    CHECK(grid_query(grid, lo, hi, StaticGrid3D::owner_mask(1)) == std::vector{a});
    CHECK_FALSE(grid.insert(a, Vector3{.x = 10.0F, .y = 0.0F, .z = 0.0F}, Vector3{.x = 11.0F, .y = 1.0F, .z = 1.0F}, 1));
    CHECK(grid_query(grid, lo, hi).empty());
    CHECK(grid_query(grid, Vector3{.x = 10.5F, .y = 0.5F, .z = 0.5F}, Vector3{.x = 10.5F, .y = 0.5F, .z = 0.5F}) ==
          std::vector{a});
    CHECK(grid.remove(a, 1));
    CHECK_FALSE(grid.contains(a));
    CHECK(grid.size() == 0);
}

TEST_CASE("the static grid holds two versions of one entity index apart", "[runtime][binding-index]") {
    entt::registry registry;
    const auto old_handle = registry.create();
    registry.destroy(old_handle);
    const auto new_handle = registry.create();
    REQUIRE(entt::to_entity(new_handle) == entt::to_entity(old_handle));
    StaticGrid3D grid;
    grid.clear(1.0F);
    const Vector3 lo{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    const Vector3 hi{.x = 1.0F, .y = 1.0F, .z = 1.0F};
    grid.insert(old_handle, lo, hi, 0);
    grid.insert(new_handle, lo, hi, 1);
    CHECK(grid.size() == 2);
    CHECK(grid_query(grid, lo, hi, StaticGrid3D::owner_mask(0)) == std::vector{old_handle});
    CHECK(grid_query(grid, lo, hi, StaticGrid3D::owner_mask(1)) == std::vector{new_handle});
    CHECK_FALSE(grid.remove(new_handle, 0));
    CHECK(grid.remove(old_handle, 0));
    CHECK(grid.contains(new_handle));
    CHECK_FALSE(grid.contains(old_handle));
    CHECK(grid.remove(new_handle, 1));
    CHECK(grid.size() == 0);
}

TEST_CASE("an unset cell size becomes the members' median extent", "[runtime][binding-index]") {
    StaticGrid3D grid;
    grid.clear(0.0F);
    const std::array extents{0.5F, 3.0F, 2.0F};
    for (std::uint32_t i = 0; i < 3; ++i) {
        grid.insert(entt::entity{i}, Vector3{}, Vector3{.x = 0.1F, .y = extents.at(i), .z = 0.1F});
    }
    CHECK(grid_query(grid, Vector3{}, Vector3{}).size() == 3);
    CHECK(grid.cell_size() == Catch::Approx(2.0F * StaticGrid3D::kCellSlack));
    grid.insert(entt::entity{9}, Vector3{}, Vector3{.x = 40.0F, .y = 40.0F, .z = 40.0F});
    CHECK(grid_query(grid, Vector3{}, Vector3{}).size() == 4);
    CHECK(grid.cell_size() == Catch::Approx(2.0F * StaticGrid3D::kCellSlack));

    StaticGrid3D empty;
    empty.clear(-1.0F);
    CHECK(grid_query(empty, Vector3{}, Vector3{}).empty());
    empty.insert(entt::entity{1}, Vector3{}, Vector3{});
    CHECK(grid_query(empty, Vector3{}, Vector3{}).size() == 1);
    CHECK(empty.cell_size() == Catch::Approx(StaticGrid3D::kCellSlack));
}

TEST_CASE("the static grid finds what a full scan finds for mixed sizes", "[runtime][binding-index]") {
    std::mt19937 random(424242U);
    const auto uniform = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(random); };
    for (const float cell_size : {0.25F, 1.0F, 2.0F, 7.0F, 0.0F}) {
        StaticGrid3D grid;
        grid.clear(cell_size);
        GridOracle oracle;
        for (int round = 0; round < 40; ++round) {
            for (int step = 0; step < 30; ++step) {
                const auto entity = entt::entity{static_cast<std::uint32_t>(uniform(0.0F, 400.0F))};
                if (oracle.members.contains(entity) && uniform(0.0F, 1.0F) < 0.5F) {
                    grid.remove(entity);
                    oracle.members.erase(entity);
                    continue;
                }
                if (oracle.members.contains(entity)) {
                    continue;
                }
                const float size = uniform(0.0F, 1.0F) < 0.8F ? uniform(0.1F, 1.5F) : uniform(0.5F, 29.5F);
                const Vector3 min{.x = uniform(-30.0F, 30.0F), .y = uniform(-5.0F, 5.0F), .z = uniform(-30.0F, 30.0F)};
                const Vector3 max{.x = min.x + size, .y = min.y + uniform(0.1F, 1.5F), .z = min.z + uniform(0.1F, 1.5F)};
                grid.insert(entity, min, max);
                oracle.members.emplace(entity, GridOracle::Box{.min = min, .max = max});
            }
            for (int query = 0; query < 20; ++query) {
                const Vector3 min{.x = uniform(-35.0F, 35.0F), .y = uniform(-6.0F, 6.0F), .z = uniform(-35.0F, 35.0F)};
                const float reach = uniform(0.0F, 1.0F) < 0.9F ? uniform(0.0F, 3.0F) : uniform(3.0F, 80.0F);
                const Vector3 max{.x = min.x + reach, .y = min.y + reach, .z = min.z + reach};
                CHECK(grid_query(grid, min, max) == oracle.query(min, max));
            }
            CHECK(grid.size() == oracle.members.size());
        }
    }
}

// ── Spatial binding indexes ──────────────────────────────────────────────────

TEST_CASE("a spatial binding index follows membership through signals", "[runtime][binding-index]") {
    entt::registry registry;
    const auto a = make(registry, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    const auto b = make(registry, Vector3{.x = 3.0F, .y = 0.0F, .z = 0.0F});
    SpatialBindingIndex3D index;
    StaticStore3D store;
    REQUIRE(index.tracker().stale(registry));
    attach(registry, index, store);
    CHECK_FALSE(index.tracker().stale(registry));
    catch_up(registry, index);
    CHECK(index.static_size() == 2);
    CHECK(near(registry, index, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
          std::vector{a});

    const auto c = make(registry, Vector3{.x = 0.5F, .y = 0.0F, .z = 0.0F});
    registry.remove<Member>(a);
    catch_up(registry, index);
    CHECK(near(registry, index, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}) ==
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

TEST_CASE("spatial indexes sharing a store each see only their own members", "[runtime][binding-index]") {
    entt::registry registry;
    const auto plain = make(registry, Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F});
    const auto solid = make(registry, Vector3{.x = 0.2F, .y = 0.0F, .z = 0.0F});
    registry.emplace<Solid>(solid);
    StaticStore3D store;
    SpatialBindingIndex3D all;
    SpatialBindingIndex3D solids;
    const auto classify_solid = [&](entt::entity entity) {
        auto found = classify(registry, entity);
        found.member = found.member && registry.all_of<Solid>(entity);
        return found;
    };
    const auto refresh_solids = [&] {
        if (solids.tracker().stale(registry)) {
            solids.reset(registry, store, 1.0F);
            watch_binding_traits<Member, Mover, Solid>(registry, solids.tracker());
            for (const auto entity : registry.view<Member, Solid>()) {
                solids.tracker().mark(entity);
            }
        }
        solids.catch_up(registry, classify_solid);
    };
    attach(registry, all, store);
    catch_up(registry, all);
    refresh_solids();
    const Vector3 lo{.x = -1.0F, .y = -1.0F, .z = -1.0F};
    const Vector3 hi{.x = 1.0F, .y = 1.0F, .z = 1.0F};
    CHECK(store.grid().size() == 2);
    CHECK(near(registry, all, lo, hi) == std::vector{plain, solid});
    CHECK(near(registry, solids, lo, hi) == std::vector{solid});
    CHECK(solids.static_size() == 1);
    CHECK(all.static_size() == 2);

    registry.remove<Solid>(solid);
    refresh_solids();
    catch_up(registry, all);
    CHECK(store.grid().size() == 2);
    CHECK(near(registry, solids, lo, hi).empty());
    CHECK(near(registry, all, lo, hi) == std::vector{plain, solid});

    registry.destroy(solid);
    catch_up(registry, all);
    refresh_solids();
    CHECK(store.grid().size() == 1);

    // A new epoch resets the store once; every sharer then refiles its members.
    advance_binding_index_epoch();
    registry.emplace<Solid>(plain);
    attach(registry, all, store);
    catch_up(registry, all);
    refresh_solids();
    CHECK(store.grid().size() == 1);
    CHECK(near(registry, solids, lo, hi) == std::vector{plain});
    CHECK(near(registry, all, lo, hi) == std::vector{plain});
}

TEST_CASE("dynamic members stay in creation order", "[runtime][binding-index]") {
    entt::registry registry;
    std::vector<entt::entity> movers;
    movers.reserve(4);
    for (int i = 0; i < 4; ++i) {
        movers.push_back(make(registry, Vector3{.x = static_cast<float>(i), .y = 0.0F, .z = 0.0F}));
    }
    SpatialBindingIndex3D index;
    StaticStore3D store;
    attach(registry, index, store);
    for (auto it = movers.rbegin(); it != movers.rend(); ++it) {
        registry.emplace<Mover>(*it);
    }
    catch_up(registry, index);
    REQUIRE(index.dynamic_members().size() == 4);
    for (std::size_t i = 0; i < movers.size(); ++i) {
        CHECK(index.dynamic_members()[i].second == movers[i]);
    }
}

TEST_CASE("a new epoch drops what every tracker marked in the old world", "[runtime][binding-index]") {
    entt::registry registry;
    BindingIndexTracker kept;
    kept.reset(registry);
    kept.mark(entt::entity{1});
    BindingIndexTracker copy{kept};
    BindingIndexTracker moved{std::move(copy)};
    {
        BindingIndexTracker gone;
        gone.reset(registry);
    }
    moved.mark(entt::entity{2});
    advance_binding_index_epoch();
    CHECK(kept.take_pending().empty());
    CHECK(moved.take_pending().empty());

    // A stale tracker ignores signals; after a reset it records them again.
    kept.on_signal(registry, entt::entity{3});
    CHECK(kept.take_pending().empty());
    kept.reset(registry);
    kept.on_signal(registry, entt::entity{4});
    CHECK(kept.take_pending() == std::vector{entt::entity{4}});
    kept = moved;
    kept = BindingIndexTracker{};
    CHECK(kept.stale(registry));
}

TEST_CASE("a new epoch makes an index stale", "[runtime][binding-index]") {
    entt::registry registry;
    SpatialBindingIndex3D index;
    StaticStore3D store;
    attach(registry, index, store);
    REQUIRE_FALSE(index.tracker().stale(registry));
    advance_binding_index_epoch();
    CHECK(index.tracker().stale(registry));
    entt::registry other;
    attach(registry, index, store);
    CHECK(index.tracker().stale(other));
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
    StaticStore3D store;
    attach(registry, index, store);
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
    index.candidates(registry, Vector3{.x = -1.0F, .y = -1.0F, .z = -1.0F}, Vector3{.x = 1.0F, .y = 1.0F, .z = 1.0F}, dynamic,
                     found);
    std::vector<entt::entity> order;
    order.reserve(found.size());
    for (const auto& [ordinal, entity] : found) {
        order.push_back(entity);
    }
    CHECK(order == std::vector{all[0], all[1], all[2], all[4]});

    const float inf = std::numeric_limits<float>::infinity();
    index.candidates(registry, Vector3{.x = -inf, .y = -1.0F, .z = -1.0F}, Vector3{.x = inf, .y = 1.0F, .z = 1.0F}, dynamic,
                     found);
    CHECK(std::ranges::is_sorted(found));
    CHECK(found.size() == 5);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
