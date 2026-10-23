#include "backends/cpp-entt/range_checks.hpp"

#include "common/ast_expressions.hpp"
#include "common/codegen_error.hpp"
#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <vector>

namespace cactus {

namespace {

constexpr const char* VALUE = "value";

bool has_ranged_fields(const ResolvedTrait& trait) {
    return std::ranges::any_of(trait.fields, [](const ResolvedField& field) { return field.range.has_value(); });
}

std::string trait_cpp(const ResolvedTrait& trait, const DecoratedProgram& program) {
    return EnttCodegenUtils::trait_cpp_name(
        std::nullopt, trait.canonical_id.empty() ? trait.name : trait.canonical_id, program);
}

std::string check_ref(const ResolvedTrait& trait) {
    return range_check_function_name(trait.module_name, trait.name).ref();
}

// A container read `handle[T].field` names one container lookup per distinct handle and trait.
struct ContainerLookup {
    std::string handle;
    SymbolId trait;
    std::string var;
};

const MemberExpr* container_read(const ExprNode& expr) {
    const auto* member = std::get_if<MemberExpr>(&expr.expr);
    if (member == nullptr || !std::holds_alternative<TraitAccessExpr>(member->object->expr)) {
        return nullptr;
    }
    return member;
}

const std::string& container_handle(const MemberExpr& read) {
    return std::get<IdentExpr>(std::get<TraitAccessExpr>(read.object->expr).object->expr).name;
}

const SymbolId& container_trait(const MemberExpr& read) {
    return *std::get<TraitAccessExpr>(read.object->expr).resolved_trait_id;
}

const ContainerLookup* find_lookup(const std::vector<ContainerLookup>& lookups, const MemberExpr& read) {
    const auto found = std::ranges::find_if(lookups, [&](const ContainerLookup& lookup) {
        return lookup.handle == container_handle(read) && lookup.trait == container_trait(read);
    });
    return found == lookups.end() ? nullptr : &*found;
}

void collect_containers(const ExprNode& bound, std::vector<ContainerLookup>& lookups) {
    visit_expression(bound, [&](const ExprNode& node) {
        const auto* member = container_read(node);
        if (member == nullptr || find_lookup(lookups, *member) != nullptr) {
            return;
        }
        lookups.push_back(ContainerLookup{.handle = container_handle(*member),
                                          .trait  = container_trait(*member),
                                          .var    = "cactus_gen_container_" + std::to_string(lookups.size())});
    });
}

// NOLINTNEXTLINE(misc-no-recursion) -- a bound is an expression tree
std::string emit_bound(const ExprNode& bound,
                       const std::vector<ContainerLookup>& lookups,
                       const DecoratedProgram& program) {
    if (const auto* member = container_read(bound)) {
        return find_lookup(lookups, *member)->var + "->" + cpp_escaped(member->member);
    }
    if (const auto* ident = std::get_if<IdentExpr>(&bound.expr); ident != nullptr && !ident->resolved_const_id) {
        return std::string{VALUE} + "." + cpp_escaped(ident->name);
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&bound.expr)) {
        return EnttCodegenUtils::emit_unary(*unary, emit_bound(*unary->operand, lookups, program));
    }
    if (const auto* binary = std::get_if<BinaryExpr>(&bound.expr)) {
        return EnttCodegenUtils::emit_binary(
            *binary, emit_bound(*binary->left, lookups, program), emit_bound(*binary->right, lookups, program));
    }
    if (std::holds_alternative<LiteralExpr>(bound.expr) || std::holds_alternative<IdentExpr>(bound.expr) ||
        std::holds_alternative<MemberExpr>(bound.expr)) {
        return EnttCodegenUtils::emit_expr(bound, program);
    }
    throw CodegenError(bound.location, "unsupported range bound");
}

std::string fault(const ResolvedTrait& trait,
                  const ResolvedField& field,
                  const std::string& lo,
                  const std::string& hi,
                  const std::string& reason) {
    return "cactus::runtime::entt_backend::RangeFault{.trait = \"" + trait.name + "\", .field = \"" + field.name +
           "\", .value = " + VALUE + "." + cpp_escaped(field.name) + ", .lo = " + lo + ", .hi = " + hi +
           ", .reason = cactus::runtime::entt_backend::RangeFaultReason::" + reason + "}";
}

void emit_field_check(std::ostringstream& out,
                      const ResolvedTrait& trait,
                      const ResolvedField& field,
                      const ResolvedFieldRange& range,
                      const DecoratedProgram& program) {
    std::vector<ContainerLookup> lookups;
    collect_containers(*range.lo, lookups);
    collect_containers(*range.hi, lookups);
    out << "    {\n";
    for (const auto& lookup : lookups) {
        const auto handle = std::string{VALUE} + "." + cpp_escaped(lookup.handle);
        out << "        if (!registry.valid(" << handle << ")) {\n";
        out << "            return " << fault(trait, field, "0", "0", "StaleContainer") << ";\n";
        out << "        }\n";
        out << "        const auto* " << lookup.var << " = registry.try_get<"
            << EnttCodegenUtils::trait_cpp_name(lookup.trait, lookup.trait.local_name, program) << ">(" << handle
            << ");\n";
        out << "        if (" << lookup.var << " == nullptr) {\n";
        out << "            return " << fault(trait, field, "0", "0", "MissingContainerTrait") << ";\n";
        out << "        }\n";
    }
    const auto field_value = std::string{VALUE} + "." + cpp_escaped(field.name);
    out << "        const int lo = " << emit_bound(*range.lo, lookups, program) << ";\n";
    out << "        const int hi = " << emit_bound(*range.hi, lookups, program) << ";\n";
    out << "        if (" << field_value << " < lo || " << field_value << " >= hi) {\n";
    out << "            return " << fault(trait, field, "lo", "hi", "OutOfRange") << ";\n";
    out << "        }\n";
    out << "    }\n";
}

}  // namespace

const ResolvedTrait* find_ranged_trait(const DecoratedProgram& program,
                                       const std::optional<SymbolId>& symbol,
                                       const std::string& source_name) {
    const bool canonical = symbol.has_value() && !symbol->module.name.empty();
    auto found           = canonical ? program.traits.find(make_canonical_id(*symbol)) : program.traits.end();
    if (found == program.traits.end()) {
        found = program.traits.find(symbol.has_value() ? symbol->local_name : source_name);
    }
    if (found == program.traits.end() || !has_ranged_fields(found->second)) {
        return nullptr;
    }
    return &found->second;
}

std::string emit_range_check_functions(const DecoratedProgram& program) {
    std::vector<const ResolvedTrait*> traits;
    for (const auto& trait : program.traits | std::views::values) {
        if (has_ranged_fields(trait)) {
            traits.push_back(&trait);
        }
    }
    std::ranges::sort(traits, {}, [](const ResolvedTrait* trait) { return trait->canonical_id; });
    std::string result;
    for (const auto* trait : traits) {
        const auto name = range_check_function_name(trait->module_name, trait->name);
        std::ostringstream out;
        out << "[[maybe_unused]] std::optional<cactus::runtime::entt_backend::RangeFault> " << name.name
            << "([[maybe_unused]] const entt::registry& registry, const " << trait_cpp(*trait, program) << "& " << VALUE
            << ") {\n";
        for (const auto& field : trait->fields) {
            if (field.range.has_value()) {
                emit_field_check(out, *trait, field, *field.range, program);
            }
        }
        out << "    return std::nullopt;\n";
        out << "}\n";
        result += namespace_block(name.ns, out.str()) + "\n";
    }
    if (result.empty()) {
        return result;
    }
    return "// ── Range Checks ────────────────────────────────────────────────────\n\n" + result;
}

std::string emit_range_check_statement(const ResolvedTrait& trait,
                                       const std::string& value,
                                       const std::string& on_fault,
                                       const std::string& indent) {
    return indent + "if (!" + emit_range_check_passes(trait, value) + ") {\n" + indent + "    " + on_fault + "\n" +
           indent + "}\n";
}

std::string emit_range_check_passes(const ResolvedTrait& trait, const std::string& value) {
    return "cactus::runtime::entt_backend::passes_range_check(" + check_ref(trait) + "(registry, " + value + "))";
}

}  // namespace cactus
