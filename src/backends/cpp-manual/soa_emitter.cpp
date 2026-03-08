#include "backends/cpp-manual/soa_emitter.h"

namespace cactus {

std::string SoaEmitter::type_to_cpp(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Int: return "int";
        case TypeKind::Float: return "float";
        case TypeKind::Bool: return "bool";
        case TypeKind::String: return "std::string";
        case TypeKind::Vec2: return "Vec2";
        case TypeKind::Vec3: return "Vec3";
        case TypeKind::Quat: return "Quat";
        case TypeKind::Color: return "Color";
        case TypeKind::EntityId: return "uint32_t";
        case TypeKind::Struct: return type.name;
        case TypeKind::Enum: return type.name;
        case TypeKind::List:
            if (type.element) return "std::vector<" + type_to_cpp(*type.element) + ">";
            return "std::vector<int>";
        case TypeKind::Void: return "void";
        default: return "/* unknown */";
    }
}

std::string SoaEmitter::emit_soa_storage(const ResolvedTrait& trait) {
    std::ostringstream out;
    out << "struct " << trait.name << "Storage {\n";
    for (auto& field : trait.fields) {
        out << "    std::vector<" << type_to_cpp(field.type) << "> " << field.name << ";\n";
    }
    out << "    size_t count = 0;\n";
    out << "\n";
    out << "    void resize(size_t n) {\n";
    out << "        count = n;\n";
    for (auto& field : trait.fields) {
        out << "        " << field.name << ".resize(n);\n";
    }
    out << "    }\n";
    out << "\n";
    out << "    void push_back() {\n";
    out << "        ++count;\n";
    for (auto& field : trait.fields) {
        out << "        " << field.name << ".push_back({});\n";
    }
    out << "    }\n";
    out << "};\n";
    return out.str();
}

std::string SoaEmitter::emit_pod_struct(const ResolvedStruct& s) {
    std::ostringstream out;
    out << "struct " << s.name << " {\n";
    for (auto& field : s.fields) {
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
        if (i + 1 < e.variants.size()) out << ",";
        out << "\n";
    }
    out << "};\n";
    return out.str();
}

}  // namespace cactus
