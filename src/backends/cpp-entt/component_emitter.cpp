#include "backends/cpp-entt/component_emitter.hpp"

#include "frontend/symbol_identity.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace cactus {

namespace {

bool should_defer_to_raylib_enum(const std::string& name) {
    return name == "MouseButton" || name == "GamepadButton" || name == "GamepadAxis";
}

}  // namespace

std::string EnttComponentEmitter::emit_component(const ResolvedTrait& trait, const DecoratedProgram& program) {
    const std::string cpp_name = canonical_to_cpp_name(trait.module_name, trait.name);
    std::ostringstream out;
    if (trait.is_state_slot()) {
        return emit_state_slot(cpp_name, *trait.state, program);
    }
    if (trait.fields.empty()) {
        out << "struct " << cpp_name << " {};\n";
        return out.str();
    }

    out << "struct " << cpp_name << " {\n";
    for (const auto& field : trait.fields) {
        out << "    " << EnttCodegenUtils::value_type_to_cpp(field.type) << " " << field.name;
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

std::string EnttComponentEmitter::emit_state_slot(const std::string& cpp_name,
                                                  const SymbolId& state,
                                                  const DecoratedProgram& program) {
    const auto* declaration  = EnttCodegenUtils::find_state(program, state);
    const auto variant_count = declaration == nullptr ? std::size_t{0} : declaration->variants.size();
    std::vector<std::uint64_t> final_masks(std::max(std::size_t{1}, (variant_count + 63) / 64), 0);
    for (std::size_t index = 0; index < variant_count; ++index) {
        if (declaration->variants[index].is_final) {
            final_masks[index / 64] |= std::uint64_t{1} << (index % 64);
        }
    }
    std::ostringstream out;
    out << "struct " << cpp_name << " {\n";
    out << "    " << (variant_count <= 256 ? "std::uint8_t" : "std::size_t") << " index = 0;\n";
    if (final_masks.size() == 1) {
        out << "    static constexpr std::uint64_t final_mask = 0x" << std::hex << final_masks.front() << std::dec
            << ";\n";
    } else {
        out << "    static constexpr std::array<std::uint64_t, " << final_masks.size() << "> final_mask{";
        for (std::size_t index = 0; index < final_masks.size(); ++index) {
            out << (index == 0 ? "0x" : ", 0x") << std::hex << final_masks[index] << std::dec;
        }
        out << "};\n";
    }
    out << "};\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_state_helpers(const DecoratedProgram& program) {
    if (program.states.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "namespace cactus::runtime::entt_backend {\n";
    for (const auto& [_, state] : program.states) {
        if (!state.symbol_id.has_value()) {
            continue;
        }
        const auto slot = EnttCodegenUtils::state_slot_cpp_name(program, *state.symbol_id);
        out << "template <>\n";
        out << "inline void remove_state_variant<" << slot
            << ">(entt::registry& registry, entt::entity entity, std::size_t index) {\n";
        out << "    switch (index) {\n";
        for (std::size_t index = 0; index < state.variants.size(); ++index) {
            const auto variant = state_variant_trait(*state.symbol_id, state.variants[index].name);
            out << "        case " << index << ":\n";
            out << "            registry.remove<" << EnttCodegenUtils::trait_cpp_name(variant, "", program)
                << ">(entity);\n";
            out << "            break;\n";
        }
        out << "        default:\n";
        out << "            break;\n";
        out << "    }\n";
        out << "}\n";
    }
    out << "}  // namespace cactus::runtime::entt_backend\n\n";
    return out.str();
}

std::string EnttComponentEmitter::emit_pod_struct(const ResolvedStruct& s) {
    const std::string cpp_name = canonical_to_cpp_name(s.module_name, s.name);
    std::ostringstream out;
    out << "struct " << cpp_name << " {\n";
    for (const auto& field : s.fields) {
        out << "    " << EnttCodegenUtils::value_type_to_cpp(field.type) << " " << field.name << ";\n";
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
