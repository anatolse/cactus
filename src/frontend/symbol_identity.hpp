#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cactus {

enum class SymbolKind : std::uint8_t {
    Trait,
    Struct,
    Enum,
    Event,
    Phase,
    Func,
    Rule,
    Template,
    Entity,
    Asset,
    Input,
    Const,
    Group,
    State,
};

struct ModuleId {
    std::string name;

    [[nodiscard]] bool empty() const {
        return name.empty();
    }

    friend bool operator==(const ModuleId&, const ModuleId&) = default;
};

struct SymbolId {
    SymbolKind kind;
    ModuleId module;
    std::string local_name;

    friend bool operator==(const SymbolId&, const SymbolId&) = default;
};

struct ModuleIdHash {
    [[nodiscard]] std::size_t operator()(const ModuleId& module) const noexcept {
        return std::hash<std::string>{}(module.name);
    }
};

namespace detail {

inline void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9E3779B9U + (seed << 6U) + (seed >> 2U);
}

inline void require_explicit_module_name(const std::string& module_name) {
    if (module_name.empty()) {
        throw std::invalid_argument("symbol identity requires an explicit non-empty module name");
    }
}

}  // namespace detail

struct SymbolIdHash {
    [[nodiscard]] std::size_t operator()(const SymbolId& symbol) const noexcept {
        std::size_t seed = std::hash<std::underlying_type_t<SymbolKind>>{}(
            static_cast<std::underlying_type_t<SymbolKind>>(symbol.kind));
        detail::hash_combine(seed, ModuleIdHash{}(symbol.module));
        detail::hash_combine(seed, std::hash<std::string>{}(symbol.local_name));
        return seed;
    }
};

[[nodiscard]] inline const char* symbol_kind_name(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Trait:
            return "trait";
        case SymbolKind::Struct:
            return "struct";
        case SymbolKind::Enum:
            return "enum";
        case SymbolKind::Event:
            return "event";
        case SymbolKind::Phase:
            return "phase";
        case SymbolKind::Func:
            return "func";
        case SymbolKind::Rule:
            return "rule";
        case SymbolKind::Template:
            return "template";
        case SymbolKind::Entity:
            return "entity";
        case SymbolKind::Asset:
            return "asset";
        case SymbolKind::Input:
            return "input";
        case SymbolKind::Const:
            return "const";
        case SymbolKind::Group:
            return "group";
        case SymbolKind::State:
            return "state";
    }
    return "unknown";
}

[[nodiscard]] inline SymbolId make_symbol_id(SymbolKind kind, const ModuleId& module, const std::string& local_name) {
    detail::require_explicit_module_name(module.name);
    return SymbolId{.kind = kind, .module = module, .local_name = local_name};
}

[[nodiscard]] inline SymbolId make_symbol_id(SymbolKind kind,
                                             const std::string& module_name,
                                             const std::string& local_name) {
    return make_symbol_id(kind, ModuleId{.name = module_name}, local_name);
}

// A state's slot trait shares the state's name; variant `V` is the trait `S.V`.
[[nodiscard]] inline SymbolId state_slot_trait(const SymbolId& state) {
    return SymbolId{.kind = SymbolKind::Trait, .module = state.module, .local_name = state.local_name};
}

[[nodiscard]] inline SymbolId state_variant_trait(const SymbolId& state, const std::string& variant) {
    return SymbolId{.kind = SymbolKind::Trait, .module = state.module, .local_name = state.local_name + "." + variant};
}

// Only variant traits have a dotted local name.
[[nodiscard]] inline std::optional<SymbolId> state_of_variant_trait(const SymbolId& trait) {
    const auto dot = trait.local_name.find('.');
    if (trait.kind != SymbolKind::Trait || dot == std::string::npos) {
        return std::nullopt;
    }
    return SymbolId{.kind = SymbolKind::State, .module = trait.module, .local_name = trait.local_name.substr(0, dot)};
}

// The `V` of a variant trait `S.V`.
[[nodiscard]] inline std::string variant_name_of_trait(const SymbolId& trait) {
    return trait.local_name.substr(trait.local_name.find('.') + 1);
}

// Canonical ID: "module.path.LocalName". Callers are expected to supply an explicit module identity.
[[nodiscard]] inline std::string canonical_string(const std::string& module_name, const std::string& local_name) {
    detail::require_explicit_module_name(module_name);
    return module_name + "." + local_name;
}

[[nodiscard]] inline std::string canonical_string(const ModuleId& module, const std::string& local_name) {
    return canonical_string(module.name, local_name);
}

[[nodiscard]] inline std::string canonical_string(const SymbolId& symbol) {
    return canonical_string(symbol.module, symbol.local_name);
}

// Backward-compatible name used throughout the existing frontend/backend code.
[[nodiscard]] inline std::string make_canonical_id(const std::string& module_name, const std::string& local_name) {
    return canonical_string(module_name, local_name);
}

[[nodiscard]] inline std::string make_canonical_id(const ModuleId& module, const std::string& local_name) {
    return canonical_string(module, local_name);
}

[[nodiscard]] inline std::string make_canonical_id(const SymbolId& symbol) {
    return canonical_string(symbol);
}

// Names generated C++ cannot declare as-is: C++23 keywords, raylib.h macros, and the
// names the backend spells unqualified inside module namespaces.
inline constexpr auto kCppReservedNames = std::to_array<std::string_view>({
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
    // raylib.h macros
    "RAYLIB_H",
    "RAYLIB_VERSION_MAJOR",
    "RAYLIB_VERSION_MINOR",
    "RAYLIB_VERSION_PATCH",
    "RAYLIB_VERSION",
    "RLAPI",
    "PI",
    "DEG2RAD",
    "RAD2DEG",
    "RL_MALLOC",
    "RL_CALLOC",
    "RL_REALLOC",
    "RL_FREE",
    "CLITERAL",
    "RL_COLOR_TYPE",
    "RL_RECTANGLE_TYPE",
    "RL_VECTOR2_TYPE",
    "RL_VECTOR3_TYPE",
    "RL_VECTOR4_TYPE",
    "RL_QUATERNION_TYPE",
    "RL_MATRIX_TYPE",
    "LIGHTGRAY",
    "GRAY",
    "DARKGRAY",
    "YELLOW",
    "GOLD",
    "ORANGE",
    "PINK",
    "RED",
    "MAROON",
    "GREEN",
    "LIME",
    "DARKGREEN",
    "SKYBLUE",
    "BLUE",
    "DARKBLUE",
    "PURPLE",
    "VIOLET",
    "DARKPURPLE",
    "BEIGE",
    "BROWN",
    "DARKBROWN",
    "WHITE",
    "BLACK",
    "BLANK",
    "MAGENTA",
    "RAYWHITE",
    "RL_BOOL_TYPE",
    "MOUSE_LEFT_BUTTON",
    "MOUSE_RIGHT_BUTTON",
    "MOUSE_MIDDLE_BUTTON",
    "MATERIAL_MAP_DIFFUSE",
    "MATERIAL_MAP_SPECULAR",
    "SHADER_LOC_MAP_DIFFUSE",
    "SHADER_LOC_MAP_SPECULAR",
    "GetMouseRay",
    "__declspec",
    // spelled unqualified by generated code
    "std",
    "entt",
    "cactus",
    "cactus_gen",
    "Vector2",
    "Vector3",
    "Quat",
    "Color",
});

[[nodiscard]] inline std::string cpp_escaped(std::string name) {
    if (std::ranges::find(kCppReservedNames, name) != kCppReservedNames.end()) {
        name += '_';
    }
    return name;
}

// "std.camera.volume" -> {"stdlib", "camera", "volume"}. A user top-level module never
// lands on `stdlib` or `detail`, which the backend reserves.
[[nodiscard]] inline std::vector<std::string> cpp_namespace_segments(const std::string& module_name) {
    std::vector<std::string> segments;
    for (std::size_t begin = 0; !module_name.empty() && begin <= module_name.size();) {
        const auto end = std::min(module_name.find('.', begin), module_name.size());
        auto segment   = module_name.substr(begin, end - begin);
        if (segments.empty() && segment == "std") {
            segment = "stdlib";
        } else if (segments.empty() && (segment == "stdlib" || segment == "detail")) {
            segment += '_';
        } else {
            segment = cpp_escaped(std::move(segment));
        }
        segments.push_back(std::move(segment));
        begin = end + 1;
    }
    return segments;
}

// "std.camera.volume" -> "cactus_gen::stdlib::camera::volume".
[[nodiscard]] inline std::string cpp_namespace_path(const std::string& module_name) {
    std::string result = "cactus_gen";
    for (const auto& segment : cpp_namespace_segments(module_name)) {
        result += "::" + segment;
    }
    return result;
}

[[nodiscard]] inline std::string cpp_namespace_path(const ModuleId& module) {
    return cpp_namespace_path(module.name);
}

namespace detail {

[[nodiscard]] inline std::string pascal_case(const std::string& name) {
    std::string result;
    bool upper = true;
    for (const char c : name) {
        if (c == '_') {
            upper = true;
            continue;
        }
        result += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    return result;
}

}  // namespace detail

// The name a module-scope declaration gets inside its module namespace. A state variant
// `S.V` is the nested type `S::V` of its slot.
[[nodiscard]] inline std::string cpp_decl_name(SymbolKind kind, const std::string& local_name) {
    switch (kind) {
        case SymbolKind::Event:
            return local_name + "Event";
        case SymbolKind::Phase:
            return detail::pascal_case(local_name) + "Phase";
        default:
            break;
    }
    const auto dot = local_name.find('.');
    if (dot == std::string::npos) {
        return cpp_escaped(local_name);
    }
    return cpp_escaped(local_name.substr(0, dot)) + "::" + cpp_escaped(local_name.substr(dot + 1));
}

[[nodiscard]] inline std::string cpp_decl_name(const SymbolId& symbol) {
    return cpp_decl_name(symbol.kind, symbol.local_name);
}

// Fully qualified, so it resolves the same wherever it is emitted.
[[nodiscard]] inline std::string cpp_type_ref(SymbolKind kind,
                                              const std::string& module_name,
                                              const std::string& local_name) {
    return "::" + cpp_namespace_path(module_name) + "::" + cpp_decl_name(kind, local_name);
}

[[nodiscard]] inline std::string cpp_type_ref(const SymbolId& symbol) {
    return cpp_type_ref(symbol.kind, symbol.module.name, symbol.local_name);
}

}  // namespace cactus

namespace std {

template <>
struct hash<cactus::ModuleId> {
    [[nodiscard]] std::size_t operator()(const cactus::ModuleId& module) const noexcept {
        return cactus::ModuleIdHash{}(module);
    }
};

template <>
struct hash<cactus::SymbolId> {
    [[nodiscard]] std::size_t operator()(const cactus::SymbolId& symbol) const noexcept {
        return cactus::SymbolIdHash{}(symbol);
    }
};

}  // namespace std
