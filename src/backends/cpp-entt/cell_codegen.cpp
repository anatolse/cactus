#include "backends/cpp-entt/cell_codegen.hpp"

#include "backends/cpp-entt/type_utils.hpp"
#include "common/ast_expressions.hpp"
#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace cactus::cell_codegen {

namespace {

const ResolvedEnum* enum_of(const TypeInfo& type, const DecoratedProgram& program) {
    if (!type.symbol_id.has_value()) {
        return EnttCodegenUtils::find_enum(program, type.name);
    }
    if (const auto found = program.enums.find(make_canonical_id(*type.symbol_id)); found != program.enums.end()) {
        return &found->second;
    }
    return EnttCodegenUtils::find_enum(program, type.symbol_id->local_name);
}

std::optional<long long> literal_int(const ExprNode& expr) {
    const auto* literal = std::get_if<LiteralExpr>(&expr.expr);
    if (literal == nullptr || literal->kind != LiteralExpr::Kind::Int) {
        return std::nullopt;
    }
    return std::stoll(literal->value);
}

std::size_t enum_extent(const ResolvedDimension& dimension, const DecoratedProgram& program) {
    const auto* declaration = enum_of(dimension.type, program);
    return declaration == nullptr ? 0 : declaration->variants.size();
}

// The number of values of a dimension, when the declaration fixes it.
std::optional<std::size_t> dimension_extent(const ResolvedDimension& dimension, const DecoratedProgram& program) {
    if (!dimension.range.has_value()) {
        return enum_extent(dimension, program);
    }
    const auto lo = literal_int(*dimension.range->lo);
    const auto hi = literal_int(*dimension.range->hi);
    if (!lo.has_value() || !hi.has_value()) {
        return std::nullopt;
    }
    return *hi > *lo ? static_cast<std::size_t>(*hi - *lo) : 0;
}

std::pair<std::string, std::string> dimension_bounds(const ResolvedDimension& dimension,
                                                     const DecoratedProgram& program) {
    if (dimension.range.has_value()) {
        return {EnttCodegenUtils::emit_expr(*dimension.range->lo, program),
                EnttCodegenUtils::emit_expr(*dimension.range->hi, program)};
    }
    return {"0", std::to_string(enum_extent(dimension, program))};
}

const CellScope*& active_scope() {
    static thread_local const CellScope* scope = nullptr;
    return scope;
}

}  // namespace

std::size_t dense_size(const ResolvedTrait& trait, const DecoratedProgram& program) {
    std::size_t cells = 1;
    for (const auto& dimension : trait.dimensions) {
        const auto extent = dimension_extent(dimension, program);
        if (!extent.has_value()) {
            return 0;
        }
        cells *= *extent;
        if (cells > kDenseLimit) {
            return 0;
        }
    }
    return cells;
}

std::string key_cpp(const ResolvedDimension& dimension, const std::string& key) {
    return dimension.range.has_value() ? key : "static_cast<int>(" + key + ")";
}

std::string dimension_value_cpp(const ResolvedDimension& dimension,
                                std::size_t index,
                                const std::string& shape,
                                const std::string& cell) {
    const auto value = shape + ".key(" + cell + ", " + std::to_string(index) + ")";
    return dimension.range.has_value()
               ? value
               : "static_cast<" + EnttCodegenUtils::value_type_to_cpp(dimension.type) + ">(" + value + ")";
}

bool shape_per_carrier(const ResolvedTrait& trait) {
    return std::ranges::any_of(trait.dimensions, [](const ResolvedDimension& dimension) {
        if (!dimension.range.has_value()) {
            return false;
        }
        bool reads = false;
        for (const auto* bound : {dimension.range->lo.get(), dimension.range->hi.get()}) {
            visit_expression(*bound, [&](const ExprNode& node) {
                const auto* ident = std::get_if<IdentExpr>(&node.expr);
                reads             = reads || (ident != nullptr && !ident->resolved_const_id.has_value());
            });
        }
        return reads;
    });
}

std::string shape_call(const ResolvedTrait& trait, const std::string& table) {
    return shape_per_carrier(trait) ? table + ".cactus_shape()"
                                    : "std::remove_cvref_t<decltype(" + table + ")>::cactus_shape()";
}

std::string emit_table_members(const ResolvedTrait& trait, const DecoratedProgram& program) {
    std::ostringstream out;
    const auto dense = dense_size(trait, program);
    for (const auto& field : trait.fields) {
        if (!trait.is_cell_field(field)) {
            continue;
        }
        const auto* default_expr =
            EnttCodegenUtils::find_trait_field_default(program, trait.module_name, trait.name, field.name);
        const auto value = default_expr == nullptr ? EnttCodegenUtils::value_type_to_cpp(field.type) + "{}"
                                                   : EnttCodegenUtils::emit_expr(*default_expr, program);
        out << "    cactus::runtime::CellColumn<" << EnttCodegenUtils::value_type_to_cpp(field.type) << ", "
            << value;
        if (dense != 0) {
            out << ", " << dense;
        }
        out << "> " << cpp_escaped(field.name) << ";\n";
    }
    const auto rank = std::to_string(trait.dimensions.size());
    std::vector<std::string> lo;
    std::vector<std::string> hi;
    for (const auto& dimension : trait.dimensions) {
        auto [low, high] = dimension_bounds(dimension, program);
        lo.push_back(std::move(low));
        hi.push_back(std::move(high));
    }
    const bool per_carrier = shape_per_carrier(trait);
    out << "    [[nodiscard]] " << (per_carrier ? "" : "static ") << "cactus::runtime::CellShape<" << rank
        << "> cactus_shape()" << (per_carrier ? " const" : "") << " noexcept {\n";
    out << "        return {std::array<int, " << rank << ">{" << join_strings(lo, ", ") << "}, std::array<int, " << rank
        << ">{" << join_strings(hi, ", ") << "}};\n";
    out << "    }\n";
    return out.str();
}

const CellAliasCodegen* CellScope::find(const std::string& alias) const {
    const auto found = std::ranges::find(aliases, alias, &CellAliasCodegen::alias);
    return found == aliases.end() ? nullptr : &*found;
}

ActiveCellScope::ActiveCellScope(const CellScope& scope)
    : previous_(active_scope()) {
    active_scope() = &scope;
}

ActiveCellScope::~ActiveCellScope() {
    active_scope() = previous_;
}

const CellScope* ActiveCellScope::active() {
    return active_scope();
}

}  // namespace cactus::cell_codegen
