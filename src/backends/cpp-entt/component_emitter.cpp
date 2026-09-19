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
            EnttCodegenUtils::find_trait_field_default(program, trait.module_name, trait.name, field.name);
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
