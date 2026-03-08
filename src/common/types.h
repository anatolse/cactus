#pragma once

#include <memory>
#include <string>
#include <vector>

namespace cactus {

enum class TypeKind {
    Int,
    Float,
    Bool,
    String,
    Vec2,
    Vec3,
    Quat,
    Color,
    EntityId,
    Struct,
    Enum,
    List,
    Func,
    Void,
    Unknown
};

struct TypeInfo {
    TypeKind kind = TypeKind::Unknown;
    std::string name;                     // for Struct/Enum: the user-defined name
    std::shared_ptr<TypeInfo> element;    // for List: the element type
    std::vector<TypeInfo> params;         // for Func: parameter types
    std::shared_ptr<TypeInfo> ret;        // for Func: return type
    bool is_let = false;
    bool is_persist = false;
    bool is_sync = false;
    bool is_pub = false;

    [[nodiscard]] bool is_primitive() const {
        return kind == TypeKind::Int || kind == TypeKind::Float || kind == TypeKind::Bool ||
               kind == TypeKind::String || kind == TypeKind::Vec2 || kind == TypeKind::Vec3 ||
               kind == TypeKind::Quat || kind == TypeKind::Color || kind == TypeKind::EntityId;
    }
};

// Built-in type factory functions
inline TypeInfo make_int_type() { return {TypeKind::Int, "int"}; }
inline TypeInfo make_float_type() { return {TypeKind::Float, "float"}; }
inline TypeInfo make_bool_type() { return {TypeKind::Bool, "bool"}; }
inline TypeInfo make_string_type() { return {TypeKind::String, "string"}; }
inline TypeInfo make_vec2_type() { return {TypeKind::Vec2, "vec2"}; }
inline TypeInfo make_vec3_type() { return {TypeKind::Vec3, "vec3"}; }
inline TypeInfo make_quat_type() { return {TypeKind::Quat, "quat"}; }
inline TypeInfo make_color_type() { return {TypeKind::Color, "color"}; }
inline TypeInfo make_entity_id_type() { return {TypeKind::EntityId, "entity_id"}; }
inline TypeInfo make_void_type() { return {TypeKind::Void, "void"}; }
inline TypeInfo make_unknown_type() { return {TypeKind::Unknown, "unknown"}; }

inline TypeInfo make_list_type(TypeInfo element_type) {
    TypeInfo t;
    t.kind = TypeKind::List;
    t.name = "list";
    t.element = std::make_shared<TypeInfo>(std::move(element_type));
    return t;
}

}  // namespace cactus
