#pragma once

// The cells of a dimensioned trait: one table per carrier, one column per cell field. A column
// stores only the values that differ from the field's default, bit for bit, so a cell written
// back to its default is as if never written. Included by cactus_runtime.hpp after reduce::Sum.

#include "common/numeric.hpp"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace cactus::runtime {

// The cell index of a value outside a range: a read gives the default, a write does nothing.
inline constexpr std::uint64_t kNoCell = ~std::uint64_t{0};
inline constexpr std::int64_t kMaxDimensionValues = 1048576;

// The ranges of a table's dimensions; the first dimension is the most significant.
template <std::size_t N>
class CellShape {
public:
    constexpr CellShape(std::array<int, N> lo, std::array<int, N> hi) noexcept
        : lo_(lo) {
        std::uint64_t stride = 1;
        for (std::size_t d = N; d-- > 0;) {
            const auto extent = static_cast<std::int64_t>(hi[d]) - static_cast<std::int64_t>(lo[d]);
            extent_[d]        = extent > 0 ? static_cast<std::uint64_t>(extent) : 0U;
            stride_[d]        = stride;
            stride *= extent_[d];
        }
        count_ = stride;
    }

    [[nodiscard]] constexpr std::uint64_t count() const noexcept {
        return count_;
    }

    template <typename... Keys>
        requires(sizeof...(Keys) == N)
    [[nodiscard]] constexpr std::uint64_t at(Keys... keys) const noexcept {
        const std::array<std::int64_t, N> values{static_cast<std::int64_t>(keys)...};
        std::uint64_t cell = 0;
        for (std::size_t d = 0; d < N; ++d) {
            const auto offset = values[d] - lo_[d];
            if (offset < 0 || static_cast<std::uint64_t>(offset) >= extent_[d]) {
                return kNoCell;
            }
            cell += static_cast<std::uint64_t>(offset) * stride_[d];
        }
        return cell;
    }

    [[nodiscard]] constexpr int key(std::uint64_t cell, std::size_t dimension) const noexcept {
        const auto offset = (cell / stride_[dimension]) % extent_[dimension];
        return static_cast<int>(static_cast<std::int64_t>(lo_[dimension]) + static_cast<std::int64_t>(offset));
    }

    // The cell one step along `dimension`, or kNoCell past the range's end.
    [[nodiscard]] constexpr std::uint64_t step(std::uint64_t cell, std::size_t dimension, int delta) const noexcept {
        const auto offset = static_cast<std::int64_t>((cell / stride_[dimension]) % extent_[dimension]) + delta;
        if (offset < 0 || static_cast<std::uint64_t>(offset) >= extent_[dimension]) {
            return kNoCell;
        }
        return delta < 0 ? cell - stride_[dimension] : cell + stride_[dimension];
    }

private:
    std::array<int, N> lo_{};
    std::array<std::uint64_t, N> extent_{};
    std::array<std::uint64_t, N> stride_{};
    std::uint64_t count_ = 0;
};

namespace detail {

template <typename T>
inline constexpr bool kFloatCell = std::is_same_v<T, float> || std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3>;

template <typename T>
[[nodiscard]] bool same_bits(const T& left, const T& right) noexcept {
    if constexpr (kFloatCell<T>) {
        return std::memcmp(&left, &right, sizeof(T)) == 0;
    } else {
        return left == right;
    }
}

}  // namespace detail

// One cell field of one table. DenseSize > 0 keeps every cell in an array: a small, fixed table.
template <typename T, T Default, std::size_t DenseSize = 0>
class CellColumn {
public:
    using value_type               = T;
    static constexpr T kDefault    = Default;
    static constexpr bool kDense   = DenseSize > 0;
    static constexpr int kBlockLog = 6;

    [[nodiscard]] T get(std::uint64_t cell) const {
        settle();
        return value(cell);
    }

    void set(std::uint64_t cell, T next) {
        settle();
        store(cell, next);
    }

    // `+=` from writers that need no order between them. Float and vector deltas wait for the next
    // access, which applies round(old + exact sum), so the order of the writers does not matter.
    void accumulate(std::uint64_t cell, T delta) {
        if (cell == kNoCell) {
            return;
        }
        if constexpr (detail::kFloatCell<T>) {
            pending_.emplace_back(cell, delta);
        } else {
            set(cell, num::add(value(cell), delta));
        }
    }

    void subtract(std::uint64_t cell, T delta) {
        if constexpr (std::is_same_v<T, Vector2>) {
            accumulate(cell, Vector2{-delta.x, -delta.y});
        } else if constexpr (std::is_same_v<T, Vector3>) {
            accumulate(cell, Vector3{-delta.x, -delta.y, -delta.z});
        } else if constexpr (std::is_same_v<T, float>) {
            accumulate(cell, -delta);
        } else {
            set(cell, num::sub(get(cell), delta));
        }
    }

    // Appends the cells whose value differs from the default, in cell order.
    void append_stored(std::vector<std::uint64_t>& cells) const {
        settle();
        if constexpr (kDense) {
            for (std::size_t cell = 0; cell < DenseSize; ++cell) {
                if (!detail::same_bits(dense_[cell], Default)) {
                    cells.push_back(cell);
                }
            }
        } else {
            for (const auto& [block, entry] : blocks_) {
                for (auto mask = entry.mask; mask != 0; mask &= mask - 1U) {
                    cells.push_back((block << kBlockLog) + static_cast<std::uint64_t>(std::countr_zero(mask)));
                }
            }
        }
    }

    // How many cells differ from the default.
    [[nodiscard]] std::size_t stored_count() const {
        settle();
        std::size_t count = 0;
        if constexpr (kDense) {
            for (const auto& held : dense_) {
                count += detail::same_bits(held, Default) ? 0U : 1U;
            }
        } else {
            for (const auto& entry : blocks_ | std::views::values) {
                count += static_cast<std::size_t>(std::popcount(entry.mask));
            }
        }
        return count;
    }

    friend bool operator==(const CellColumn& left, const CellColumn& right) {
        std::vector<std::uint64_t> left_cells;
        std::vector<std::uint64_t> right_cells;
        left.append_stored(left_cells);
        right.append_stored(right_cells);
        return left_cells == right_cells && std::ranges::all_of(left_cells, [&](std::uint64_t cell) {
                   return detail::same_bits(left.value(cell), right.value(cell));
               });
    }

private:
    template <typename Range>
    static T exact_sum(T start, const Range& deltas) {
        reduce::Sum<T> sum;
        sum.add(start);
        for (const auto& delta : deltas) {
            sum.add(delta.second);
        }
        return sum.value();
    }

    struct Block {
        std::array<T, std::size_t{1} << kBlockLog> values;
        std::uint64_t mask = 0;  // the cells that differ from the default

        Block() {
            values.fill(Default);
        }
    };

    [[nodiscard]] T value(std::uint64_t cell) const {
        if (cell == kNoCell) {
            return Default;
        }
        if constexpr (kDense) {
            return cell < DenseSize ? dense_[cell] : Default;
        } else {
            const auto* block = find_block(cell >> kBlockLog);
            return block == nullptr ? Default : block->values[cell & kLowMask];
        }
    }

    void store(std::uint64_t cell, T next) const {
        if (cell == kNoCell) {
            return;
        }
        if constexpr (kDense) {
            if (cell < DenseSize) {
                dense_[cell] = next;
            }
        } else {
            const bool rest = detail::same_bits(next, Default);
            const auto key  = cell >> kBlockLog;
            const auto bit  = std::uint64_t{1} << (cell & kLowMask);
            auto found      = std::ranges::lower_bound(blocks_, key, {}, &std::pair<std::uint64_t, Block>::first);
            const bool held = found != blocks_.end() && found->first == key;
            if (rest) {
                if (!held) {
                    return;
                }
                found->second.values[cell & kLowMask] = Default;
                found->second.mask &= ~bit;
                if (found->second.mask == 0) {
                    blocks_.erase(found);
                }
                return;
            }
            if (!held) {
                found = blocks_.emplace(found, key, Block{});
            }
            found->second.values[cell & kLowMask] = next;
            found->second.mask |= bit;
        }
    }

    // The sum is exact, so sorting the deltas by cell needs no stable order.
    void settle() const {
        if constexpr (detail::kFloatCell<T>) {
            if (pending_.empty()) {
                return;
            }
            std::ranges::sort(pending_, {}, &std::pair<std::uint64_t, T>::first);
            for (auto begin = pending_.begin(); begin != pending_.end();) {
                const auto end = std::ranges::find_if_not(
                    begin, pending_.end(), [cell = begin->first](const auto& delta) { return delta.first == cell; });
                store(begin->first, CellColumn::exact_sum(value(begin->first), std::ranges::subrange(begin, end)));
                begin = end;
            }
            pending_.clear();
        }
    }

    static constexpr std::uint64_t kLowMask = (std::uint64_t{1} << kBlockLog) - 1U;

    [[nodiscard]] const Block* find_block(std::uint64_t key) const {
        const auto found = std::ranges::lower_bound(blocks_, key, {}, &std::pair<std::uint64_t, Block>::first);
        return found != blocks_.end() && found->first == key ? &found->second : nullptr;
    }

    static constexpr std::array<T, kDense ? DenseSize : 1> filled() {
        std::array<T, kDense ? DenseSize : 1> values{};
        values.fill(Default);
        return values;
    }

    // Mutable because a const read first applies the pending deltas, which no reader can observe.
    mutable std::array<T, kDense ? DenseSize : 1> dense_ = filled();
    // Sorted by block index, so stored cells come out in cell order.
    mutable std::vector<std::pair<std::uint64_t, Block>> blocks_;
    mutable std::vector<std::pair<std::uint64_t, T>> pending_;
};

// The cells a proven rule visits: those where any of `columns` differs from its default, in order.
template <typename... Columns>
[[nodiscard]] std::vector<std::uint64_t> stored_cells(const Columns&... columns) {
    std::vector<std::uint64_t> cells;
    (columns.append_stored(cells), ...);
    if constexpr (sizeof...(Columns) > 1) {
        std::ranges::sort(cells);
        const auto [first, last] = std::ranges::unique(cells);
        cells.erase(first, last);
    }
    return cells;
}

// The cells next to `cell` along the `int` dimensions in `steps`, a lower and an upper one for each;
// kNoCell past a range's end.
template <std::size_t N, std::size_t S>
[[nodiscard]] constexpr std::array<std::uint64_t, 2 * S> adjacent_cells(const CellShape<N>& shape,
                                                                        const std::array<std::size_t, S>& steps,
                                                                        std::uint64_t cell) noexcept {
    std::array<std::uint64_t, 2 * S> cells{};
    for (std::size_t index = 0; index < S; ++index) {
        cells[2 * index]      = shape.step(cell, steps[index], -1);
        cells[(2 * index) + 1] = shape.step(cell, steps[index], 1);
    }
    return cells;
}

// The distinct cells of `cells`, in order.
template <std::size_t K>
[[nodiscard]] std::span<const std::uint64_t> distinct_cells(std::array<std::uint64_t, K>& cells) {
    std::ranges::sort(cells);
    return {cells.begin(), std::ranges::unique(cells).begin()};
}

// The stored cells and their neighbours along the `int` dimensions in `steps`: every left cell of a
// pair of adjacent cells that is not at rest.
template <std::size_t N, std::size_t S, typename... Columns>
[[nodiscard]] std::vector<std::uint64_t> stored_cells_and_neighbours(const CellShape<N>& shape,
                                                                     const std::array<std::size_t, S>& steps,
                                                                     const Columns&... columns) {
    auto cells        = stored_cells(columns...);
    const auto stored = cells.size();
    for (std::size_t index = 0; index < stored; ++index) {
        for (const auto next : adjacent_cells(shape, steps, cells[index])) {
            if (next != kNoCell) {
                cells.push_back(next);
            }
        }
    }
    std::ranges::sort(cells);
    const auto [first, last] = std::ranges::unique(cells);
    cells.erase(first, last);
    return cells;
}

// A cell of a table reached by value: a missing table reads the default and takes no write.
template <typename Table, typename Column, typename... Keys>
[[nodiscard]] typename Column::value_type cell_read(const Table* table, Column Table::* column, Keys... keys) {
    if (table == nullptr) {
        return Column::kDefault;
    }
    return (table->*column).get(table->cactus_shape().at(keys...));
}

template <typename Table, typename Column, typename Update, typename... Keys>
void cell_update(Table* table, Column Table::* column, const Update& update, Keys... keys) {
    if (table == nullptr) {
        return;
    }
    const auto cell = table->cactus_shape().at(keys...);
    if (cell == kNoCell) {
        return;
    }
    auto& values = table->*column;
    values.set(cell, update(values.get(cell)));
}

template <typename Table, typename Column, typename... Keys>
void cell_accumulate(Table* table, Column Table::* column, bool subtract, typename Column::value_type delta, Keys... keys) {
    if (table == nullptr) {
        return;
    }
    const auto cell = table->cactus_shape().at(keys...);
    if (subtract) {
        (table->*column).subtract(cell, delta);
    } else {
        (table->*column).accumulate(cell, delta);
    }
}

}  // namespace cactus::runtime
