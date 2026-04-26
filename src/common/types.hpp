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
    // Asset opaque ID types (§5.1)
    MeshId,
    TextureId,
    SoundId,
    MusicId,
    FontId,
    MaterialId,
    // Input handle types (§5.1)
    InputButton,
    InputAxis,
    Struct,
    Enum,
    List,
    Func,
    Void,
    Unknown
};

struct TypeInfo {
    TypeKind kind = TypeKind::Unknown;
    std::string name;                   // for Struct/Enum: the user-defined name
    std::shared_ptr<TypeInfo> element;  // for List: the element type
    std::vector<TypeInfo> params;       // for Func: parameter types
    std::shared_ptr<TypeInfo> ret;      // for Func: return type
    bool is_let     = false;
    bool is_persist = false;
    bool is_sync    = false;
    bool is_pub     = false;
    [[nodiscard]] bool is_primitive() const {
        return kind == TypeKind::Int || kind == TypeKind::Float || kind == TypeKind::Bool || kind == TypeKind::String ||
               kind == TypeKind::Vec2 || kind == TypeKind::Vec3 || kind == TypeKind::Quat || kind == TypeKind::Color ||
               kind == TypeKind::EntityId;
    }
};

inline TypeInfo make_type_info(TypeKind kind, std::string name) {
    return {.kind = kind, .name = std::move(name)};
}

// Built-in type factory functions
inline TypeInfo make_int_type() {
    return make_type_info(TypeKind::Int, "int");
}
inline TypeInfo make_float_type() {
    return make_type_info(TypeKind::Float, "float");
}
inline TypeInfo make_bool_type() {
    return make_type_info(TypeKind::Bool, "bool");
}
inline TypeInfo make_string_type() {
    return make_type_info(TypeKind::String, "string");
}
inline TypeInfo make_vec2_type() {
    return make_type_info(TypeKind::Vec2, "vec2");
}
inline TypeInfo make_vec3_type() {
    return make_type_info(TypeKind::Vec3, "vec3");
}
inline TypeInfo make_quat_type() {
    return make_type_info(TypeKind::Quat, "quat");
}
inline TypeInfo make_color_type() {
    return make_type_info(TypeKind::Color, "color");
}
inline TypeInfo make_entity_id_type() {
    return make_type_info(TypeKind::EntityId, "entity_id");
}
inline TypeInfo make_mesh_id_type() {
    return make_type_info(TypeKind::MeshId, "mesh_id");
}
inline TypeInfo make_texture_id_type() {
    return make_type_info(TypeKind::TextureId, "texture_id");
}
inline TypeInfo make_sound_id_type() {
    return make_type_info(TypeKind::SoundId, "sound_id");
}
inline TypeInfo make_music_id_type() {
    return make_type_info(TypeKind::MusicId, "music_id");
}
inline TypeInfo make_font_id_type() {
    return make_type_info(TypeKind::FontId, "font_id");
}
inline TypeInfo make_material_id_type() {
    return make_type_info(TypeKind::MaterialId, "material_id");
}
inline TypeInfo make_input_button_type() {
    return make_type_info(TypeKind::InputButton, "InputButton");
}
inline TypeInfo make_input_axis_type() {
    return make_type_info(TypeKind::InputAxis, "InputAxis");
}
inline TypeInfo make_void_type() {
    return make_type_info(TypeKind::Void, "void");
}
inline TypeInfo make_unknown_type() {
    return make_type_info(TypeKind::Unknown, "unknown");
}

inline TypeInfo make_list_type(TypeInfo element_type) {
    TypeInfo t;
    t.kind    = TypeKind::List;
    t.name    = "list";
    t.element = std::make_shared<TypeInfo>(std::move(element_type));
    return t;
}

}  // namespace cactus
