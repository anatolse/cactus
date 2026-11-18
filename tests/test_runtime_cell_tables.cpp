// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/cactus_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

using cactus::runtime::CellColumn;
using cactus::runtime::CellShape;
using cactus::runtime::kNoCell;

TEST_CASE("cell shape: the first dimension is the most significant", "[runtime][trait-dimensions]") {
    const CellShape<2> shape({0, 10}, {4, 13});
    CHECK(shape.count() == 12);
    CHECK(shape.at(0, 10) == 0);
    CHECK(shape.at(0, 11) == 1);
    CHECK(shape.at(1, 10) == 3);
    CHECK(shape.at(3, 12) == 11);
    CHECK(shape.key(11, 0) == 3);
    CHECK(shape.key(11, 1) == 12);
}

TEST_CASE("cell shape: values outside a range name no cell", "[runtime][trait-dimensions]") {
    const CellShape<2> shape({0, 0}, {4, 3});
    CHECK(shape.at(-1, 0) == kNoCell);
    CHECK(shape.at(4, 0) == kNoCell);
    CHECK(shape.at(0, 3) == kNoCell);
    CHECK(shape.step(shape.at(0, 1), 0, -1) == kNoCell);
    CHECK(shape.step(shape.at(0, 1), 0, 1) == shape.at(1, 1));
    CHECK(shape.step(shape.at(2, 2), 1, 1) == kNoCell);
    CHECK(shape.step(shape.at(2, 2), 1, -1) == shape.at(2, 1));
}

TEST_CASE("cell shape: an empty range gives no cells", "[runtime][trait-dimensions]") {
    const CellShape<1> empty({5}, {5});
    CHECK(empty.count() == 0);
    CHECK(empty.at(5) == kNoCell);
}

TEST_CASE("cell column: a sparse column stores only values that differ from the default", "[runtime][trait-dimensions]") {
    CellColumn<float, 0.0F> gas;
    CHECK(gas.get(7) == 0.0F);
    gas.set(7, 1.5F);
    gas.set(900, 2.0F);
    CHECK(gas.get(7) == 1.5F);
    CHECK(gas.stored_count() == 2);
    gas.set(7, 0.0F);
    CHECK(gas.stored_count() == 1);
    gas.set(900, 0.0F);
    CHECK(gas.stored_count() == 0);
    CHECK(gas == CellColumn<float, 0.0F>{});
}

TEST_CASE("cell column: negative zero is not at rest", "[runtime][trait-dimensions]") {
    CellColumn<float, 0.0F> gas;
    gas.set(3, -0.0F);
    CHECK(gas.stored_count() == 1);
    CHECK(std::bit_cast<std::uint32_t>(gas.get(3)) == std::bit_cast<std::uint32_t>(-0.0F));
}

TEST_CASE("cell column: writes outside a range do nothing", "[runtime][trait-dimensions]") {
    CellColumn<int, 0> count;
    count.set(kNoCell, 5);
    CHECK(count.stored_count() == 0);
    CHECK(count.get(kNoCell) == 0);
}

TEST_CASE("cell column: a dense column holds every cell", "[runtime][trait-dimensions]") {
    CellColumn<int, 3, 4> count;
    CHECK(count.get(2) == 3);
    count.set(2, 5);
    std::vector<std::uint64_t> cells;
    count.append_stored(cells);
    CHECK(cells == std::vector<std::uint64_t>{2});
    count.set(2, 3);
    cells.clear();
    count.append_stored(cells);
    CHECK(cells.empty());
}

TEST_CASE("cell column: += merges exactly, in any order", "[runtime][trait-dimensions]") {
    CellColumn<float, 0.0F> first;
    CellColumn<float, 0.0F> second;
    first.set(1, 1.0F);
    second.set(1, 1.0F);
    for (const float delta : {1.0e8F, 1.0F, -1.0e8F}) {
        first.accumulate(1, delta);
    }
    for (const float delta : {-1.0e8F, 1.0e8F, 1.0F}) {
        second.accumulate(1, delta);
    }
    CHECK(first.get(1) == 2.0F);
    CHECK(second.get(1) == 2.0F);
}

TEST_CASE("cell column: an emptied block is released", "[runtime][trait-dimensions]") {
    CellColumn<int, 0> count;
    for (std::uint64_t cell = 0; cell < 200; ++cell) {
        count.set(cell, 1);
    }
    for (std::uint64_t cell = 0; cell < 200; ++cell) {
        count.set(cell, 0);
    }
    CHECK(count.stored_count() == 0);
    CHECK(count == CellColumn<int, 0>{});
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
