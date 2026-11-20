#include "backends/cpp-entt/clamped_fields.hpp"

#include "common/codegen_error.hpp"
#include "frontend/symbol_identity.hpp"

#include "backends/cpp-entt/accumulation_plan.hpp"
#include "backends/cpp-entt/range_checks.hpp"
#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <ranges>
#include <sstream>
#include <vector>

namespace cactus {

namespace {

std::string bound_cpp(const std::optional<ResolvedClampBound>& bound,
                      const ResolvedField& field,
                      const std::string& object,
                      const DecoratedProgram& program,
                      const char* limit) {
    if (!bound.has_value()) {
        return std::string{"cactus::runtime::"} + limit + "<" + EnttCodegenUtils::value_type_to_cpp(field.type) +
               ">()";
    }
    if (!bound->let_field.empty()) {
        return object + "." + cpp_escaped(bound->let_field);
    }
    return EnttCodegenUtils::emit_expr(*bound->expr, program);
}

// The trait value a written field `lhs` belongs to: `lhs` without its trailing `.field`.
std::string owner_of(const std::string& lhs, const ResolvedField& field, const VarAssign& assign) {
    const auto suffix = "." + cpp_escaped(field.name);
    if (!lhs.ends_with(suffix)) {
        throw CodegenError(assign.location, "can't find the trait value of clamped field '" + field.name + "'");
    }
    return lhs.substr(0, lhs.size() - suffix.size());
}

bool reads_let_bound(const ResolvedFieldClamp& clamp) {
    return (clamp.min.has_value() && !clamp.min->let_field.empty()) ||
           (clamp.max.has_value() && !clamp.max->let_field.empty());
}

const char* bool_cpp(bool value) {
    return value ? "true" : "false";
}

// The `lo, hi, has_min` arguments of a clamp helper call, reading `let` bounds from `object`.
std::string clamp_arguments(const ResolvedField& field, const std::string& object, const DecoratedProgram& program) {
    if (!field.clamp.has_value()) {
        return {};
    }
    return bound_cpp(field.clamp->min, field, object, program, "clamp_lowest") + ", " +
           bound_cpp(field.clamp->max, field, object, program, "clamp_highest") + ", " +
           bool_cpp(field.clamp->min.has_value());
}

// The trait field `assign` writes, when it writes one.
std::optional<TraitFieldRef> written_field(const VarAssign& assign) {
    if (assign.accumulation.has_value()) {
        return TraitFieldRef{.trait = assign.accumulation->trait, .field = assign.accumulation->field};
    }
    if (assign.field_write.has_value() && assign.field_write->field.has_value()) {
        return assign.field_write;
    }
    return std::nullopt;
}

}  // namespace

const ResolvedTrait* find_written_trait(const DecoratedProgram& program, const SymbolId& trait) {
    const auto* found = EnttCodegenUtils::find_trait(program, make_canonical_id(trait));
    return found != nullptr ? found : EnttCodegenUtils::find_trait(program, trait.local_name);
}

const ResolvedField* find_clamped_field(const DecoratedProgram& program,
                                        const SymbolId& trait,
                                        const std::string& field) {
    const auto* declaration = find_written_trait(program, trait);
    if (declaration == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(declaration->fields, field, &ResolvedField::name);
    return found != declaration->fields.end() && found->clamp.has_value() ? &*found : nullptr;
}

const ResolvedField* clamped_write_target(const VarAssign& assign, const DecoratedProgram& program) {
    const auto written = written_field(assign);
    if (!written.has_value() || !written->field.has_value()) {
        return nullptr;
    }
    return find_clamped_field(program, written->trait, *written->field);
}

std::string emit_clamped_write(const VarAssign& assign,
                               const ResolvedField& field,
                               const ClampedWrite& write,
                               const DecoratedProgram& program) {
    const auto args  = clamp_arguments(field, owner_of(write.lhs, field, assign), program);
    const bool adds  = assign.op == "+=" || assign.op == "-=";
    const bool minus = assign.op == "-=";
    const auto* plan = AccumulationScope::active();
    const auto written = written_field(assign);
    const auto table   = plan == nullptr || !written.has_value() ? std::string{} : plan->clamped_table(*written);
    if (assign.accumulation.has_value() && assign.accumulation->buffered && !table.empty()) {
        const auto value = assign.operands == ArithmeticOperands::FloatInt ? "static_cast<float>(" + write.value + ")"
                                                                            : write.value;
        return write.indent + table + "()." + (minus ? "subtract(" : "add(") + write.entity + ", " + write.lhs + ", " +
               value + ", " + args + ");\n";
    }
    std::string out;
    if (adds && field.type.kind != TypeKind::Float) {
        out = write.indent + write.lhs + " = cactus::runtime::clamped_add(" + write.lhs + ", " + write.value + ", " +
              args + (minus ? ", true" : "") + ");\n";
    } else {
        out = write.indent + EnttCodegenUtils::emit_compound_assignment(assign, write.lhs, write.value) + ";\n" +
              write.indent + write.lhs + " = cactus::runtime::clamp_field(" + write.lhs + ", " + args + ");\n";
    }
    if (!table.empty()) {
        out += write.indent + table + "().forget(" + write.entity + ");\n";
    }
    return out;
}

bool has_clamped_field(const ResolvedTrait& trait) {
    return std::ranges::any_of(trait.fields, [](const ResolvedField& field) { return field.clamp.has_value(); });
}

std::string emit_clamp_checks(const ResolvedTrait& trait, const std::string& value, const DecoratedProgram& program) {
    std::ostringstream out;
    for (const auto& field : trait.fields) {
        if (!field.clamp.has_value()) {
            continue;
        }
        const auto& clamp = *field.clamp;
        out << "    if (auto fault = cactus::runtime::entt_backend::check_clamp<"
            << EnttCodegenUtils::value_type_to_cpp(field.type) << ">(\n";
        out << "            {.trait = \"" << trait.name << "\", .field = \"" << field.name
            << "\", .has_min = " << bool_cpp(clamp.min.has_value()) << ", .has_max = " << bool_cpp(clamp.max.has_value())
            << ", .let_bounds = " << bool_cpp(reads_let_bound(clamp)) << ", .check_value = check_clamped},\n";
        out << "            " << value << "." << cpp_escaped(field.name) << ",\n";
        out << "            " << bound_cpp(clamp.min, field, value, program, "clamp_lowest") << ",\n";
        out << "            " << bound_cpp(clamp.max, field, value, program, "clamp_highest") << ");\n";
        out << "        fault.has_value()) {\n";
        out << "        return fault;\n";
        out << "    }\n";
    }
    return out.str();
}

std::string emit_clamp_projections(const ResolvedTrait& trait,
                                   const std::string& object,
                                   const std::string& indent,
                                   const DecoratedProgram& program) {
    std::string out;
    for (const auto& field : trait.fields) {
        if (field.clamp.has_value()) {
            const auto stored = object + "." + cpp_escaped(field.name);
            out += indent;
            out += stored;
            out += " = cactus::runtime::clamp_field(";
            out += stored;
            out += ", ";
            out += clamp_arguments(field, object, program);
            out += ");\n";
        }
    }
    return out;
}

bool program_has_clamped_fields(const DecoratedProgram& program) {
    return std::ranges::any_of(program.traits | std::views::values, has_clamped_field);
}

std::string emit_clamp_listeners(const DecoratedProgram& program) {
    std::vector<const ResolvedTrait*> traits;
    for (const auto& trait : program.traits | std::views::values) {
        if (has_clamped_field(trait)) {
            traits.push_back(&trait);
        }
    }
    if (traits.empty()) {
        return {};
    }
    std::ranges::sort(traits, {}, [](const ResolvedTrait* trait) { return trait->canonical_id; });
    traits.erase(std::ranges::unique(traits, {}, [](const ResolvedTrait* trait) { return trait->canonical_id; }).begin(),
                 traits.end());
    std::ostringstream listeners;
    std::ostringstream connects;
    listeners << "template <class T>\n";
    listeners << "void project_clamped(entt::registry& registry, entt::entity entity);\n\n";
    for (const auto* trait : traits) {
        const auto cpp = resolved_trait_cpp(*trait, program);
        listeners << "template <>\n";
        listeners << "inline void project_clamped<" << cpp << ">(entt::registry& registry, entt::entity entity) {\n";
        listeners << "    auto& value = registry.get<" << cpp << ">(entity);\n";
        listeners << emit_clamp_projections(*trait, "value", "    ", program);
        listeners << "}\n\n";
        for (const char* signal : {"on_construct", "on_update"}) {
            connects << "    registry." << signal << "<" << cpp << ">().connect<&::cactus_gen::detail::project_clamped<"
                     << cpp << ">>();\n";
        }
    }
    std::ostringstream out;
    out << "// ── Clamped Fields ──────────────────────────────────────────────────\n\n";
    out << namespace_block("cactus_gen::detail", listeners.str()) << "\n";
    out << "inline void generated_connect_clamped_fields(entt::registry& registry) {\n";
    out << connects.str();
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
