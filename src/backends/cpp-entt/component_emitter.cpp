#include "backends/cpp-entt/component_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include <sstream>

namespace cactus {

namespace {

std::string entt_type_to_cpp(const TypeInfo& type) {
    if (type.kind == TypeKind::EntityId) {
        return "entt::entity";
    }
    if (type.kind == TypeKind::List && type.element != nullptr) {
        return "std::vector<" + entt_type_to_cpp(*type.element) + ">";
    }
    return EnttCodegenUtils::type_to_cpp(type);
}

bool should_defer_to_raylib_enum(const std::string& name) {
    return name == "MouseButton" || name == "GamepadButton" || name == "GamepadAxis";
}

// Finds a trait field's declared `= expression` default directly from the
// parsed AST, so the generated struct's default member initializer always
// matches the DSL source of truth (the trait declaration) instead of a
// backend-side copy that can silently drift from it. Trait names can collide
// across modules (e.g. `WorldTransform` is declared once in
// `std.transform.flat` and once in `std.transform.volume`, with different
// field types), so this tracks the enclosing module while scanning rather
// than matching on trait name alone.
const ExprNode* find_field_default_expr(const ProgramNode* ast,
                                        const std::string& module_name,
                                        const std::string& trait_name,
                                        const std::string& field_name) {
    if (ast == nullptr) {
        return nullptr;
    }
    std::string current_module;
    for (const auto& decl : ast->declarations) {
        if (const auto* mod = std::get_if<ModuleNode>(&decl)) {
            current_module = mod->name;
            continue;
        }
        const auto* trait_node = std::get_if<TraitNode>(&decl);
        if (trait_node == nullptr || trait_node->name != trait_name || current_module != module_name) {
            continue;
        }
        for (const auto& field : trait_node->fields) {
            if (field.name == field_name) {
                return field.default_value.has_value() ? field.default_value->get() : nullptr;
            }
        }
    }
    return nullptr;
}

}  // namespace

std::string EnttComponentEmitter::emit_component(const ResolvedTrait& trait, const DecoratedProgram& program) {
    const std::string cpp_name = canonical_to_cpp_name(trait.module_name, trait.name);
    std::ostringstream out;
    if (trait.fields.empty()) {
        out << "struct " << cpp_name << " {};\n";
        return out.str();
    }

    out << "struct " << cpp_name << " {\n";
    for (const auto& field : trait.fields) {
        out << "    " << entt_type_to_cpp(field.type) << " " << field.name;
        const auto* default_expr =
            find_field_default_expr(program.ast, trait.module_name, trait.name, field.name);
        if (default_expr == nullptr) {
            out << "{};\n";
            continue;
        }
        // A default expression may call a module-aliased stdlib extern func
        // (e.g. `rand.seeded(0)`), which only the DecoratedProgram overload of
        // emit_expr resolves to its runtime namespace.
        const auto rendered = EnttCodegenUtils::emit_expr(*default_expr, program);
        // A declared `= ""` default renders identically to std::string's own
        // default construction; spelling it out trips clang-tidy's
        // readability-redundant-string-init on generated output.
        const bool is_redundant_empty_string = field.type.kind == TypeKind::String && rendered == "\"\"";
        if (is_redundant_empty_string) {
            out << "{};\n";
        } else {
            out << " = " << rendered << ";\n";
        }
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_pod_struct(const ResolvedStruct& s) {
    const std::string cpp_name = canonical_to_cpp_name(s.module_name, s.name);
    std::ostringstream out;
    out << "struct " << cpp_name << " {\n";
    for (const auto& field : s.fields) {
        out << "    " << entt_type_to_cpp(field.type) << " " << field.name << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_enum(const ResolvedEnum& e) {
    if (should_defer_to_raylib_enum(e.name)) {
        return "";
    }
    return EnttCodegenUtils::emit_enum(e);
}

}  // namespace cactus
