#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <cstddef>
#include <string>
#include <vector>

// C++ lowering of dimensioned traits: their tables, cell reads and writes, and passes over cells.
namespace cactus::cell_codegen {

// The cell columns and the `cactus_shape()` member of a dimensioned trait's component.
[[nodiscard]] std::string emit_table_members(const ResolvedTrait& trait, const DecoratedProgram& program);

// The table holds every cell in one array when its dimensions are enums or literal ranges this small.
inline constexpr std::size_t kDenseLimit = 4096;
[[nodiscard]] std::size_t dense_size(const ResolvedTrait& trait, const DecoratedProgram& program);

// Bounds that read no `let` field give every carrier one shape, from a static `cactus_shape()`.
[[nodiscard]] bool shape_per_carrier(const ResolvedTrait& trait);
[[nodiscard]] std::string shape_call(const ResolvedTrait& trait, const std::string& table);

// `static_cast<int>(key)` for an enum dimension; the key itself otherwise.
[[nodiscard]] std::string key_cpp(const ResolvedDimension& dimension, const std::string& key);

// The value of `dimension` of `cell` in a table of `shape`, typed as the dimension.
[[nodiscard]] std::string dimension_value_cpp(const ResolvedDimension& dimension,
                                              std::size_t index,
                                              const std::string& shape,
                                              const std::string& cell);

// A rule over cells being lowered binds each alias's table to the alias name, the carrier to
// `entity` and the table's shape to `cactus_shape`; each alias's cell index is in `cell`.
struct CellAliasCodegen {
    std::string alias;
    std::string cell;
};

struct CellScope {
    const ResolvedTrait* trait = nullptr;
    std::vector<CellAliasCodegen> aliases;

    [[nodiscard]] const CellAliasCodegen* find(const std::string& alias) const;
};

// Scoped, not a parameter: rewrite_expr's recursive calls pass only partial context.
class ActiveCellScope {
public:
    explicit ActiveCellScope(const CellScope& scope);
    explicit ActiveCellScope(CellScope&&) = delete;
    ActiveCellScope(const ActiveCellScope&)            = delete;
    ActiveCellScope& operator=(const ActiveCellScope&) = delete;
    ActiveCellScope(ActiveCellScope&&)                 = delete;
    ActiveCellScope& operator=(ActiveCellScope&&)      = delete;
    ~ActiveCellScope();

    [[nodiscard]] static const CellScope* active();

private:
    const CellScope* previous_;
};

}  // namespace cactus::cell_codegen
