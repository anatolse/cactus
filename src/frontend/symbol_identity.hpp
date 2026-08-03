#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>

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

// Deterministic C++ identifier derived from a canonical module/local identity.
// "std.transform.flat.WorldTransform" -> "std_transform_flat__WorldTransform"
// Empty module_name: returns local_name unchanged (test/legacy unqualified scenario).
[[nodiscard]] inline std::string cpp_identifier(const std::string& module_name, const std::string& local_name) {
    if (module_name.empty()) {
        return local_name;
    }
    std::string result;
    result.reserve(module_name.size() + 2 + local_name.size());
    for (char c : module_name) {
        result += (c == '.') ? '_' : c;
    }
    result += "__";
    result += local_name;
    return result;
}

[[nodiscard]] inline std::string cpp_identifier(const ModuleId& module, const std::string& local_name) {
    return cpp_identifier(module.name, local_name);
}

[[nodiscard]] inline std::string cpp_identifier(const SymbolId& symbol) {
    return cpp_identifier(symbol.module, symbol.local_name);
}

// Backward-compatible name used throughout the existing cpp-entt backend.
[[nodiscard]] inline std::string canonical_to_cpp_name(const std::string& module_name, const std::string& local_name) {
    return cpp_identifier(module_name, local_name);
}

[[nodiscard]] inline std::string canonical_to_cpp_name(const ModuleId& module, const std::string& local_name) {
    return cpp_identifier(module, local_name);
}

[[nodiscard]] inline std::string canonical_to_cpp_name(const SymbolId& symbol) {
    return cpp_identifier(symbol);
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
