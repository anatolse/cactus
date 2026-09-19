#include "common/persistence_metadata.hpp"

#include "frontend/semantic_analyzer.hpp"

#include <algorithm>
#include <unordered_set>

namespace cactus {

namespace {

PersistenceValueType scalar(PersistenceValueKind kind, std::uint8_t bit_width) {
    return PersistenceValueType{.kind = kind, .bit_width = bit_width};
}

PersistenceValueType vector_of(PersistenceValueKind lane_kind, std::uint8_t lane_width, std::uint8_t lanes) {
    return PersistenceValueType{.kind      = PersistenceValueKind::Vector,
                                .bit_width = lane_width,
                                .lanes     = lanes,
                                .lane_kind = lane_kind};
}

}  // namespace

std::string canonical_node_string(const ArchetypeNodeId& node) {
    std::string result = make_canonical_id(node.archetype);
    for (const auto& role : node.role_path) {
        result += "/";
        result += role;
    }
    return result;
}

SymbolId resolved_or_local_symbol(SymbolKind kind,
                                  const std::optional<SymbolId>& resolved,
                                  const std::string& module_name,
                                  const std::string& local_name) {
    // Not value_or: its argument is evaluated even when the optional is
    // engaged, and make_symbol_id rejects an empty module name.
    if (resolved.has_value()) {
        return *resolved;
    }
    return make_symbol_id(kind, module_name, local_name);
}

void sort_archetype_descriptors(std::vector<PersistenceArchetypeDescriptor>& archetypes) {
    std::ranges::sort(archetypes,
                      [](const PersistenceArchetypeDescriptor& left, const PersistenceArchetypeDescriptor& right) {
                          return canonical_node_string(left.node) < canonical_node_string(right.node);
                      });
}

// Widths mirror the C++ types the backend actually emits, so a document reports
// what is stored rather than what the language nominally promises.
PersistenceValueType describe_persistence_value(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Bool:
            return scalar(PersistenceValueKind::Bool, 8);
        case TypeKind::Int:
            return scalar(PersistenceValueKind::Int, 32);
        case TypeKind::Float:
            // dsl-type-system describes `float` as 64-bit; the cpp-entt backend
            // has always stored a 32-bit C++ float here. 32 is not a typo: see
            // runtime.hpp's ABI static_asserts and design.md decision 3.
            return scalar(PersistenceValueKind::Float, 32);
        case TypeKind::String:
            return scalar(PersistenceValueKind::String, 0);
        case TypeKind::Vec2:
            return vector_of(PersistenceValueKind::Float, 32, 2);
        case TypeKind::Vec3:
            return vector_of(PersistenceValueKind::Float, 32, 3);
        case TypeKind::Quat:
            return vector_of(PersistenceValueKind::Float, 32, 4);
        case TypeKind::Color:
            return vector_of(PersistenceValueKind::Int, 8, 4);
        case TypeKind::EntityId:
            return scalar(PersistenceValueKind::EntityRef, 32);
        case TypeKind::MeshId:
        case TypeKind::ModelId:
        case TypeKind::TextureId:
        case TypeKind::SoundId:
        case TypeKind::MusicId:
        case TypeKind::FontId:
        case TypeKind::MaterialId:
            return scalar(PersistenceValueKind::AssetRef, 32);
        case TypeKind::InputButton:
        case TypeKind::InputAxis:
            return scalar(PersistenceValueKind::InputRef, 8);
        case TypeKind::Enum: {
            auto described          = scalar(PersistenceValueKind::Enum, 8);
            described.declared_type = type.symbol_id;
            return described;
        }
        case TypeKind::Struct: {
            auto described          = scalar(PersistenceValueKind::Struct, 0);
            described.declared_type = type.symbol_id;
            return described;
        }
        case TypeKind::List: {
            auto described = scalar(PersistenceValueKind::List, 0);
            if (type.element != nullptr) {
                described.element.push_back(describe_persistence_value(*type.element));
            }
            return described;
        }
        case TypeKind::Func:
        case TypeKind::Void:
        case TypeKind::Unknown:
            break;
    }
    return PersistenceValueType{};
}

namespace {

const ResolvedStruct* find_struct(const DecoratedProgram& program, const SymbolId& symbol) {
    if (const auto found = program.structs.find(make_canonical_id(symbol)); found != program.structs.end()) {
        return &found->second;
    }
    if (const auto found = program.structs.find(symbol.local_name); found != program.structs.end()) {
        return &found->second;
    }
    return nullptr;
}

void collect_unsupported_in_type(const DecoratedProgram& program,
                                 const TypeInfo& type,
                                 const std::string& path,
                                 std::unordered_set<std::string>& visited_structs,
                                 std::vector<PersistenceUnsupportedField>& out) {
    const auto described = describe_persistence_value(type);
    if (described.kind == PersistenceValueKind::Unsupported) {
        out.push_back(PersistenceUnsupportedField{.field_path = path, .type_spelling = type.name});
        return;
    }
    if (described.kind == PersistenceValueKind::List) {
        if (type.element == nullptr) {
            out.push_back(PersistenceUnsupportedField{.field_path = path + "[]", .type_spelling = "unknown"});
            return;
        }
        collect_unsupported_in_type(program, *type.element, path + "[]", visited_structs, out);
        return;
    }
    if (described.kind != PersistenceValueKind::Struct || !described.declared_type.has_value()) {
        return;
    }
    const auto* declaration = find_struct(program, *described.declared_type);
    if (declaration == nullptr) {
        out.push_back(PersistenceUnsupportedField{.field_path = path, .type_spelling = type.name});
        return;
    }
    // A struct reachable from itself is already accounted for by the outer
    // visit; recursing again would not find a new unsupported leaf.
    if (!visited_structs.insert(make_canonical_id(*described.declared_type)).second) {
        return;
    }
    for (const auto& field : declaration->fields) {
        collect_unsupported_in_type(program, field.type, path + "." + field.name, visited_structs, out);
    }
    visited_structs.erase(make_canonical_id(*described.declared_type));
}

}  // namespace

std::vector<PersistenceUnsupportedField> collect_unsupported_persistence_fields(const DecoratedProgram& program) {
    std::vector<PersistenceUnsupportedField> unsupported;
    std::unordered_set<std::string> seen_traits;
    for (const auto& [_, trait] : program.traits) {
        const auto& trait_id = declaration_identity(trait);
        if (!seen_traits.insert(trait_id).second) {
            continue;
        }
        std::unordered_set<std::string> visited_structs;
        for (const auto& field : trait.fields) {
            if (!field.is_persist) {
                continue;
            }
            collect_unsupported_in_type(program, field.type, trait_id + "." + field.name, visited_structs, unsupported);
        }
    }
    std::ranges::sort(unsupported,
                      [](const PersistenceUnsupportedField& left, const PersistenceUnsupportedField& right) {
                          return left.field_path < right.field_path;
                      });
    return unsupported;
}

void report_unsupported_persistence_fields(const DecoratedProgram& program, ErrorReporter& errors) {
    const SourceLocation location = program.ast != nullptr ? program.ast->location : SourceLocation{};
    for (const auto& field : collect_unsupported_persistence_fields(program)) {
        errors.error(location,
                     "persist field '" + field.field_path + "' has type '" + field.type_spelling +
                         "', which a world snapshot cannot represent");
    }
}

bool trait_declares_persist_field(const ResolvedTrait& trait) {
    return std::ranges::any_of(trait.fields, [](const ResolvedField& field) { return field.is_persist; });
}


bool archetype_retains_construction_data(const PersistenceArchetypeDescriptor& archetype,
                                         bool program_attaches_persistent_trait) {
    return archetype.declares_persistent_trait || program_attaches_persistent_trait;
}

const PersistenceArchetypeDescriptor* find_archetype_descriptor(const ModulePersistenceMetadata& metadata,
                                                                const ArchetypeNodeId& node) {
    const auto found = std::ranges::find_if(metadata.archetypes,
                                            [&](const PersistenceArchetypeDescriptor& candidate) {
                                                return candidate.node == node;
                                            });
    return found == metadata.archetypes.end() ? nullptr : &*found;
}

}  // namespace cactus
