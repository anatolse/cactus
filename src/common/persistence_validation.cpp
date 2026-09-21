#include "common/persistence_validation.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cactus::persistence {

namespace {

std::string node_path(const EntityRecord& record) {
    return record.archetype + "[" + std::to_string(record.id) + "]";
}

void validate_value(const SchemaDescriptor& schema,
                    const ValueTypeDescriptor& expected,
                    const Value& value,
                    const std::string& path,
                    std::size_t depth,
                    const std::unordered_set<DocumentId>& known_ids,
                    std::vector<ValidationError>& errors) {
    if (depth >= kMaxPersistenceNestingDepth) {
        errors.push_back({.field_path = path, .message = "exceeds configured nesting depth"});
        return;
    }
    if (value.kind() != expected.kind) {
        errors.push_back({.field_path = path,
                          .message    = "wrong value kind",
                          .category   = ValidationErrorCategory::UnsupportedValue});
        return;
    }
    switch (expected.kind) {
        case PersistenceValueKind::Vector: {
            const auto& vector = value.as_vector();
            if (vector.lanes != expected.lanes || vector.lane_kind != expected.lane_kind) {
                errors.push_back({.field_path = path,
                                  .message    = "vector lane count or lane kind does not match the declared type",
                                  .category   = ValidationErrorCategory::UnsupportedValue});
            }
            break;
        }
        case PersistenceValueKind::EntityRef: {
            const auto& ref = value.as_entity();
            if (ref.present && !known_ids.contains(ref.id)) {
                errors.push_back({.field_path = path, .message = "reference names a document identity with no matching record"});
            }
            break;
        }
        case PersistenceValueKind::Enum: {
            const auto& variant     = value.as_enum();
            const auto* enum_desc   = find_enum_descriptor(schema, variant.type);
            if (enum_desc == nullptr) {
                errors.push_back({.field_path = path,
                                  .message    = "unknown enum type '" + variant.type + "'",
                                  .category   = ValidationErrorCategory::UnsupportedValue});
            } else if (std::ranges::find(enum_desc->variants, std::string_view(variant.variant)) ==
                       enum_desc->variants.end()) {
                errors.push_back({.field_path = path,
                                  .message    = "unknown enum variant '" + variant.variant + "'",
                                  .category   = ValidationErrorCategory::UnsupportedValue});
            }
            break;
        }
        case PersistenceValueKind::Struct: {
            const auto* struct_desc = find_struct_descriptor(schema, expected.declared_type);
            if (struct_desc == nullptr) {
                errors.push_back({.field_path = path,
                                  .message    = "unknown struct type '" + std::string(expected.declared_type) + "'",
                                  .category   = ValidationErrorCategory::UnsupportedValue});
                break;
            }
            const auto& fields = value.as_struct().fields;
            for (const auto& field_desc : struct_desc->fields) {
                const auto* field_value = find_field(fields, field_desc.name);
                const auto field_path   = path + "." + std::string(field_desc.name);
                if (field_value == nullptr) {
                    errors.push_back({.field_path = field_path, .message = "missing required field"});
                    continue;
                }
                validate_value(schema, schema.value_types[field_desc.type], field_value->value, field_path,
                               depth + 1, known_ids, errors);
            }
            break;
        }
        case PersistenceValueKind::List: {
            const auto& items = value.as_list().items;
            if (items.size() > kMaxPersistenceCollectionSize) {
                errors.push_back({.field_path = path, .message = "list exceeds the configured collection size limit"});
                break;
            }
            if (expected.element == kNoValueType) {
                break;
            }
            const auto& element_type = schema.value_types[expected.element];
            for (std::size_t index = 0; index < items.size(); ++index) {
                validate_value(schema, element_type, items[index], path + "[" + std::to_string(index) + "]",
                               depth + 1, known_ids, errors);
            }
            break;
        }
        case PersistenceValueKind::Bool:
        case PersistenceValueKind::Int:
        case PersistenceValueKind::Float:
        case PersistenceValueKind::String:
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
        case PersistenceValueKind::Unsupported:
            break;  // kind already matched expected.kind above; nothing further to check here
    }
}

void validate_field_bucket(const std::vector<FieldValue>& bucket,
                           const TraitDescriptor& trait_desc,
                           const std::string& trait_path,
                           std::vector<ValidationError>& errors) {
    for (const auto& field_value : bucket) {
        if (find_field_descriptor(trait_desc, field_value.name) == nullptr) {
            errors.push_back({.field_path = trait_path + "." + field_value.name, .message = "unknown field"});
        }
    }
}

void validate_trait_fields(const SchemaDescriptor& schema,
                           const TraitDescriptor& trait_desc,
                           const TraitRecord& trait_record,
                           const std::string& trait_path,
                           const std::unordered_set<DocumentId>& known_ids,
                           std::vector<ValidationError>& errors) {
    for (const auto& field_desc : trait_desc.fields) {
        const auto& bucket      = field_desc.persist ? trait_record.persisted : trait_record.construction;
        const auto* field_value = find_field(bucket, field_desc.name);
        const auto field_path   = trait_path + "." + std::string(field_desc.name);
        if (field_value == nullptr) {
            errors.push_back({.field_path = field_path, .message = "missing required field"});
            continue;
        }
        validate_value(schema, schema.value_types[field_desc.type], field_value->value, field_path, 0, known_ids,
                       errors);
    }
    validate_field_bucket(trait_record.construction, trait_desc, trait_path, errors);
    validate_field_bucket(trait_record.persisted, trait_desc, trait_path, errors);
}

// Path-local cycle detection over parent links among included records:
// walks each record's parent chain, remembering every id visited along any
// one walk so a revisit within that same walk is the cycle, while ids
// already cleared as acyclic by an earlier walk are never re-walked.
void detect_hierarchy_cycles(const Snapshot& snapshot, std::vector<ValidationError>& errors) {
    std::unordered_map<DocumentId, DocumentId> parent_of;
    for (const auto& record : snapshot.entities) {
        if (record.parent.present) {
            parent_of[record.id] = record.parent.id;
        }
    }
    std::unordered_set<DocumentId> acyclic;
    for (const auto& record : snapshot.entities) {
        if (!record.parent.present || acyclic.contains(record.id)) {
            continue;
        }
        std::unordered_set<DocumentId> path{record.id};
        auto current  = record.id;
        bool cyclic   = false;
        while (true) {
            const auto found = parent_of.find(current);
            if (found == parent_of.end()) {
                break;
            }
            current = found->second;
            if (!path.insert(current).second) {
                cyclic = true;
                break;
            }
        }
        if (cyclic) {
            errors.push_back({.field_path = node_path(record), .message = "parent chain contains a cycle"});
        } else {
            acyclic.insert(path.begin(), path.end());
        }
    }
}

}  // namespace

std::vector<ValidationError> validate_document(const SchemaDescriptor& schema, const Snapshot& snapshot) {
    std::vector<ValidationError> errors;
    if (!schema_accepts(schema, snapshot)) {
        errors.push_back({.field_path = "$schema", .message = "document schema descriptor is incompatible with this program"});
        return errors;
    }
    if (snapshot.entities.size() > kMaxPersistenceCollectionSize) {
        errors.push_back({.field_path = "$document", .message = "entity count exceeds the configured limit"});
    }

    std::unordered_set<DocumentId> known_ids;
    for (const auto& record : snapshot.entities) {
        if (!known_ids.insert(record.id).second) {
            errors.push_back({.field_path = node_path(record), .message = "duplicate document identity"});
        }
    }

    for (const auto& record : snapshot.entities) {
        const auto base             = node_path(record);
        const auto* archetype_desc = find_archetype_descriptor(schema, record.archetype);
        if (archetype_desc == nullptr) {
            errors.push_back({.field_path = base, .message = "unknown archetype"});
            continue;
        }
        if (record.parent.present && !known_ids.contains(record.parent.id)) {
            errors.push_back({.field_path = base + ".parent", .message = "reference names a document identity with no matching record"});
        }
        std::unordered_set<std::string> seen_traits;
        for (const auto& trait_record : record.traits) {
            const auto trait_path = base + "." + trait_record.trait;
            if (!seen_traits.insert(trait_record.trait).second) {
                errors.push_back({.field_path = trait_path, .message = "trait recorded more than once for this entity"});
                continue;
            }
            const auto* trait_desc = find_trait_descriptor(schema, trait_record.trait);
            if (trait_desc == nullptr) {
                errors.push_back({.field_path = trait_path, .message = "unknown trait"});
                continue;
            }
            if (!trait_record.present) {
                continue;  // an absent baseline trait carries no payload to validate
            }
            validate_trait_fields(schema, *trait_desc, trait_record, trait_path, known_ids, errors);
        }
    }

    detect_hierarchy_cycles(snapshot, errors);
    return errors;
}

}  // namespace cactus::persistence
