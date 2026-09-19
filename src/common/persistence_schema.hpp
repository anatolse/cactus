#pragma once

#include <cstdint>
#include <string_view>

// Vocabulary shared by the compiler's decorated persistence metadata
// (common/persistence_metadata.hpp) and the runtime's snapshot document, so the
// two never drift into separate spellings of the same value taxonomy. Kept free
// of raylib, EnTT, and frontend includes so either side can reach it.

namespace cactus {

// Exact backend representation of a persistable value. Widths and lane counts
// are reported alongside (PersistenceValueType) rather than folded into the
// kind, so a document records what the backend actually stores.
enum class PersistenceValueKind : std::uint8_t {
    Bool,
    Int,
    Float,
    Vector,  // fixed-lane numeric aggregate: vec2, vec3, quat, color
    String,
    EntityRef,
    AssetRef,
    InputRef,
    Enum,
    Struct,
    List,
    Unsupported,
};

// One label/enumerator pair per kind, from one exhaustive switch: the compiler
// still flags a missing case when a kind is added, but the spelling lives in
// exactly one place instead of two parallel switches that could drift.
struct PersistenceValueKindSpelling {
    const char* label;
    const char* enumerator;
};

[[nodiscard]] constexpr PersistenceValueKindSpelling persistence_value_kind_spelling(
    PersistenceValueKind kind) noexcept {
    switch (kind) {
        case PersistenceValueKind::Bool:
            return {.label = "bool", .enumerator = "Bool"};
        case PersistenceValueKind::Int:
            return {.label = "int", .enumerator = "Int"};
        case PersistenceValueKind::Float:
            return {.label = "float", .enumerator = "Float"};
        case PersistenceValueKind::Vector:
            return {.label = "vector", .enumerator = "Vector"};
        case PersistenceValueKind::String:
            return {.label = "string", .enumerator = "String"};
        case PersistenceValueKind::EntityRef:
            return {.label = "entity_ref", .enumerator = "EntityRef"};
        case PersistenceValueKind::AssetRef:
            return {.label = "asset_ref", .enumerator = "AssetRef"};
        case PersistenceValueKind::InputRef:
            return {.label = "input_ref", .enumerator = "InputRef"};
        case PersistenceValueKind::Enum:
            return {.label = "enum", .enumerator = "Enum"};
        case PersistenceValueKind::Struct:
            return {.label = "struct", .enumerator = "Struct"};
        case PersistenceValueKind::List:
            return {.label = "list", .enumerator = "List"};
        case PersistenceValueKind::Unsupported:
            return {.label = "unsupported", .enumerator = "Unsupported"};
    }
    return {.label = "unsupported", .enumerator = "Unsupported"};
}

[[nodiscard]] constexpr const char* persistence_value_kind_name(PersistenceValueKind kind) noexcept {
    return persistence_value_kind_spelling(kind).label;
}

// The enumerator's own spelling, for codegen that has to write this enum back
// out as C++ rather than as a document label.
[[nodiscard]] constexpr const char* persistence_value_kind_enumerator(PersistenceValueKind kind) noexcept {
    return persistence_value_kind_spelling(kind).enumerator;
}

// Bumped whenever the descriptor's shape changes. Documents carry it so an
// incompatible build never silently reads an older encoding; the schema
// fingerprint covers the program's own declarations on top of this.
inline constexpr std::uint32_t kPersistenceSchemaRevision = 1;

// FNV-1a over a canonical rendering of the schema. The compiler computes it and
// the runtime compares it, so both sides derive it the same way here.
[[nodiscard]] constexpr std::uint64_t persistence_fingerprint(std::string_view text) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

}  // namespace cactus
