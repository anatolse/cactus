#include "backends/cpp-manual/soa_emitter.h"

namespace cactus {

std::string SoaEmitter::type_to_cpp(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Bool: return "bool";
        case TypeKind::String: return "std::string";
        case TypeKind::Vec2: return "Vector2";
        case TypeKind::Vec3: return "Vector3";
        case TypeKind::Quat: return "Quat";
        case TypeKind::Color: return "Color";
        case TypeKind::EntityId: return "uint32_t";
        case TypeKind::Struct: return type.name;
        case TypeKind::Enum: return type.name;
        case TypeKind::List:
            if (type.element) {
                return "std::vector<" + type_to_cpp(*type.element) + ">";
            }
            return "std::vector<int>";
        case TypeKind::Void: return "void";
        default: return "/* unknown */";
    }
}

std::string SoaEmitter::emit_soa_storage(const ResolvedTrait& trait) {
    std::ostringstream out;
    out << "struct " << trait.name << "Storage {\n";
    for (const auto& field : trait.fields) {
        out << "    std::vector<" << type_to_cpp(field.type) << "> " << field.name << ";\n";
    }
    out << "    size_t count = 0;\n";
    out << "\n";
    out << "    void resize(size_t n) {\n";
    out << "        count = n;\n";
    for (const auto& field : trait.fields) {
        out << "        " << field.name << ".resize(n);\n";
    }
    out << "    }\n";
    out << "\n";
    out << "    void push_back() {\n";
    out << "        ++count;\n";
    for (const auto& field : trait.fields) {
        out << "        " << field.name << ".push_back({});\n";
    }
    out << "    }\n";
    out << "};\n";
    return out.str();
}

std::string SoaEmitter::emit_pod_struct(const ResolvedStruct& s) {
    std::ostringstream out;
    out << "struct " << s.name << " {\n";
    for (const auto& field : s.fields) {
        out << "    " << type_to_cpp(field.type) << " " << field.name << ";\n";
    }
    out << "};\n";
    return out.str();
}

std::string SoaEmitter::emit_enum(const ResolvedEnum& e) {
    std::ostringstream out;
    out << "enum class " << e.name << " {\n";
    for (size_t i = 0; i < e.variants.size(); ++i) {
        out << "    " << e.variants[i];
        if (i + 1 < e.variants.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "};\n";
    return out.str();
}

// ── Dynamic ECS model ──────────────────────────────────────────────────────

std::string SoaEmitter::emit_trait_bits(const std::vector<std::string>& trait_names_ordered) {
    std::ostringstream out;
    out << "// ── TraitBits ────────────────────────────────────────────────────────\n";
    out << "namespace TraitBits {\n";
    for (size_t i = 0; i < trait_names_ordered.size(); ++i) {
        out << "    constexpr uint64_t " << trait_names_ordered[i]
            << " = 1ULL << " << i << ";\n";
    }
    out << "}\n";
    return out.str();
}

std::string SoaEmitter::emit_global_entity_pool() {
    std::ostringstream out;
    out << "// ── Global Entity Pool ───────────────────────────────────────────────\n";
    out << "constexpr size_t MAX_ENTITIES = 4096;\n";
    out << "static size_t entity_count = 0;\n";
    out << "static uint64_t g_trait_mask[MAX_ENTITIES];\n";
    return out.str();
}

std::string SoaEmitter::emit_global_field_arrays(
    const std::unordered_map<std::string, ResolvedTrait>& traits,
    const std::vector<std::string>& trait_names_ordered) {
    std::ostringstream out;
    out << "// ── Field Arrays (SoA) ───────────────────────────────────────────────\n";
    for (const auto& name : trait_names_ordered) {
        auto it = traits.find(name);
        if (it == traits.end()) {
            continue;
        }
        for (const auto& field : it->second.fields) {
            out << "static " << type_to_cpp(field.type) << " g_" << name << "_"
                << field.name << "[MAX_ENTITIES];\n";
        }
    }
    return out.str();
}

std::string SoaEmitter::default_cpp_value(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Int: return "0";
        case TypeKind::Float: return "0.0f";
        case TypeKind::Bool: return "false";
        case TypeKind::String: return "\"\"";
        case TypeKind::Vec2: return "{0.0f, 0.0f}";
        case TypeKind::Vec3: return "{0.0f, 0.0f, 0.0f}";
        case TypeKind::Quat: return "{0.0f, 0.0f, 0.0f, 1.0f}";
        case TypeKind::Color: return "{0, 0, 0, 255}";
        case TypeKind::EntityId: return "0u";
        default: return "{}";
    }
}

}  // namespace cactus
