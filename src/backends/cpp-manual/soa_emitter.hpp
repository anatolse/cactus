#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

class SoaEmitter {
public:
    // Generate SoA storage class for a trait (legacy compatibility)
    static std::string emit_soa_storage(const ResolvedTrait& trait);

    // Generate POD struct for a Cactus struct
    static std::string emit_pod_struct(const ResolvedStruct& s);

    // Generate enum
    static std::string emit_enum(const ResolvedEnum& e);

    // Map Cactus type to C++ type string
    static std::string type_to_cpp(const TypeInfo& type);

    // ── Dynamic ECS model ─────────────────────────────────────────────────

    /// Emit TraitBits namespace assigning each trait a unique bit index (task 7.1).
    /// Traits are numbered in the order they appear in trait_names_ordered.
    static std::string emit_trait_bits(const std::vector<std::string>& trait_names_ordered);

    /// Emit global entity pool declarations: entity_count + g_trait_mask[] (task 7.2).
    static std::string emit_global_entity_pool();

    /// Emit flat global SoA field arrays: g_TraitName_fieldName[MAX_ENTITIES] (task 7.2).
    static std::string emit_global_field_arrays(const std::unordered_map<std::string, ResolvedTrait>& traits,
                                                const std::vector<std::string>& trait_names_ordered);

    /// Emit a C++ zero-value literal for the given type.
    static std::string default_cpp_value(const TypeInfo& type);
};

}  // namespace cactus
