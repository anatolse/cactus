#include "common/persistence_validation.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <utility>

namespace cactus::persistence {

namespace {

// One step of a field path, kept on the stack and rendered only when a
// diagnostic needs it.
struct PathStep {
    const PathStep* parent = nullptr;
    std::string_view member;   // ".member", or the archetype at the root
    std::uint64_t index = 0;   // "[index]" when member is empty
};

void render(const PathStep& step, std::string& out) {
    if (step.parent != nullptr) {
        render(*step.parent, out);
    }
    if (!step.member.empty()) {
        if (step.parent != nullptr) {
            out += '.';
        }
        out += step.member;
        return;
    }
    out += '[';
    out += std::to_string(step.index);
    out += ']';
}

std::string render(const PathStep& step) {
    std::string out;
    render(step, out);
    return out;
}

// Which schema field a document field stands for. `field == nullptr` with
// `unknown == false` is a field recorded in the other bucket: it is reported
// as missing from the bucket it belongs to, not as unknown.
struct FieldPlan {
    const FieldDescriptor* field = nullptr;
    bool unknown                 = false;
};

struct TraitPlan {
    const TraitDescriptor* descriptor = nullptr;
    std::vector<FieldPlan> construction;
    std::vector<FieldPlan> persisted;
    std::vector<std::string_view> missing;
};

struct StructPlan {
    std::vector<const FieldDescriptor*> fields;  // per document field; nullptr when the schema has none
    std::vector<std::string_view> missing;
};

enum class Walk : std::uint8_t { Continue, Stop };

enum class Chain : std::uint8_t { Unvisited, OnPath, Acyclic, Cyclic };

class DocumentValidator {
public:
    DocumentValidator(const SchemaDescriptor& schema, const Snapshot& document, std::vector<ValidationError>& errors)
        : schema_(&schema),
          document_(&document),
          errors_(&errors) {}

    void run() {
        if (document_->entity_count() > kMaxPersistenceEntityCount) {
            errors_->push_back({.field_path = "$document", .message = "entity count exceeds the configured limit"});
        }
        index_identities();
        map_tables();
        for (std::size_t index = 0; index < document_->entity_count(); ++index) {
            validate_entity(index);
        }
        detect_hierarchy_cycles();
        if (schema_->parents_first) {
            require_parents_first();
        }
    }

private:
    void report(const PathStep& at, std::string message,
                ValidationErrorCategory category = ValidationErrorCategory::Structural) {
        errors_->push_back({.field_path = render(at), .message = std::move(message), .category = category});
    }

    [[nodiscard]] PathStep root(const EntityRow& row) const {
        return PathStep{.member = document_->name(row.archetype)};
    }

    void index_identities() {
        const auto rows = document_->entity_rows();
        ids_.reserve(rows.size());
        for (std::uint32_t index = 0; index < rows.size(); ++index) {
            ids_.emplace_back(rows[index].id, index);
        }
        std::ranges::sort(ids_);
        for (std::size_t index = 1; index < ids_.size(); ++index) {
            if (ids_[index].first == ids_[index - 1].first) {
                const auto& row  = rows[ids_[index].second];
                const auto base  = root(row);
                report(PathStep{.parent = &base, .index = row.id}, "duplicate document identity");
            }
        }
    }

    // The first row recorded with `id`, or kNoIndex.
    [[nodiscard]] std::uint32_t row_of(DocumentId id) const {
        const auto found = std::ranges::lower_bound(ids_, std::pair{id, std::uint32_t{0}});
        return found != ids_.end() && found->first == id ? found->second : kNoIndex;
    }

    [[nodiscard]] bool known(DocumentId id) const { return row_of(id) != kNoIndex; }

    void map_tables() {
        const auto enums = document_->enums();
        enums_.reserve(enums.size());
        variants_.reserve(enums.size());
        for (const auto& entry : enums) {
            const auto* descriptor = find_enum_descriptor(*schema_, document_->name(entry.name));
            enums_.push_back(descriptor);
            std::vector<bool> valid;
            valid.reserve(entry.variants.size());
            for (const auto variant : entry.variants) {
                valid.push_back(descriptor != nullptr &&
                                std::ranges::find(descriptor->variants, document_->name(variant)) !=
                                    descriptor->variants.end());
            }
            variants_.push_back(std::move(valid));
        }
        const auto shapes = document_->trait_shapes();
        traits_.reserve(shapes.size());
        for (const auto& shape : shapes) {
            traits_.push_back(plan_trait(shape));
        }
        archetypes_.assign(document_->names().size(), nullptr);
        archetypes_mapped_.assign(document_->names().size(), false);
    }

    [[nodiscard]] std::vector<FieldPlan> plan_bucket(const TraitDescriptor& descriptor,
                                                     const std::vector<DocumentField>& bucket,
                                                     bool persisted) const {
        std::vector<FieldPlan> plans;
        plans.reserve(bucket.size());
        for (const auto& field : bucket) {
            const auto* found = find_field_descriptor(descriptor, document_->name(field.name));
            if (found == nullptr) {
                plans.push_back({.unknown = true});
            } else {
                plans.push_back({.field = found->persist == persisted ? found : nullptr});
            }
        }
        return plans;
    }

    [[nodiscard]] TraitPlan plan_trait(const DocumentTrait& shape) const {
        TraitPlan plan{.descriptor = find_trait_descriptor(*schema_, document_->name(shape.name))};
        if (plan.descriptor == nullptr) {
            return plan;
        }
        plan.construction = plan_bucket(*plan.descriptor, shape.construction, false);
        plan.persisted    = plan_bucket(*plan.descriptor, shape.persisted, true);
        for (const auto& field : plan.descriptor->fields) {
            const auto& bucket = field.persist ? shape.persisted : shape.construction;
            const bool recorded = std::ranges::any_of(
                bucket, [&](const DocumentField& candidate) { return document_->name(candidate.name) == field.name; });
            if (!recorded) {
                plan.missing.push_back(field.name);
            }
        }
        return plan;
    }

    [[nodiscard]] const StructPlan& plan_struct(std::uint32_t shape, const StructDescriptor& descriptor) {
        const auto key = std::pair{shape, &descriptor};
        if (const auto found = structs_.find(key); found != structs_.end()) {
            return found->second;
        }
        StructPlan plan;
        const auto& fields = document_->struct_shapes()[shape];
        for (const auto& field : fields) {
            const auto name = document_->name(field.name);
            const auto found = std::ranges::find(descriptor.fields, name, &FieldDescriptor::name);
            plan.fields.push_back(found == descriptor.fields.end() ? nullptr : &*found);
        }
        for (const auto& field : descriptor.fields) {
            if (std::ranges::find(plan.fields, &field) == plan.fields.end()) {
                plan.missing.push_back(field.name);
            }
        }
        return structs_.emplace(key, std::move(plan)).first->second;
    }

    [[nodiscard]] const ArchetypeDescriptor* archetype_of(std::uint32_t name) {
        if (name >= archetypes_.size()) {
            return nullptr;
        }
        if (!archetypes_mapped_[name]) {
            archetypes_[name]        = find_archetype_descriptor(*schema_, document_->name(name));
            archetypes_mapped_[name] = true;
        }
        return archetypes_[name];
    }

    void validate_entity(std::size_t index) {
        const auto& row  = document_->entity_rows()[index];
        const auto base  = root(row);
        const auto path  = PathStep{.parent = &base, .index = row.id};
        const auto* archetype = archetype_of(row.archetype);
        if (archetype == nullptr) {
            report(path, "unknown archetype");
            return;
        }
        if (row.parent.present && !known(row.parent.id)) {
            report(PathStep{.parent = &path, .member = "parent"},
                   "reference names a document identity with no matching record");
        }
        const auto rows = document_->trait_rows();
        if (row.first_trait > rows.size() || rows.size() - row.first_trait < row.trait_count) {
            report(path, "trait rows lie outside the document");
            return;
        }
        seen_.clear();
        for (std::uint32_t offset = 0; offset < row.trait_count; ++offset) {
            validate_trait(rows[row.first_trait + offset], path);
        }
        validate_trait_set(*archetype, row, path);
    }

    void validate_trait(const TraitRow& row, const PathStep& entity) {
        if (row.shape >= traits_.size()) {
            report(entity, "trait row names no recorded trait layout");
            return;
        }
        const auto& shape = document_->trait_shapes()[row.shape];
        const auto name   = document_->name(shape.name);
        const auto path   = PathStep{.parent = &entity, .member = name};
        if (std::ranges::find(seen_, name) != seen_.end()) {
            report(path, "trait recorded more than once for this entity");
            return;
        }
        seen_.push_back(name);
        const auto& plan = traits_[row.shape];
        if (plan.descriptor == nullptr) {
            report(path, "unknown trait");
            return;
        }
        if (!row.present) {
            return;  // an absent baseline trait carries no payload to validate
        }
        validate_bucket(shape.construction, plan.construction, document_->payload(row, false), path);
        validate_bucket(shape.persisted, plan.persisted, document_->payload(row, true), path);
        for (const auto missing : plan.missing) {
            report(PathStep{.parent = &path, .member = missing}, "missing required field");
        }
    }

    void validate_bucket(const std::vector<DocumentField>& fields,
                         const std::vector<FieldPlan>& plans,
                         std::span<const std::uint8_t> bytes,
                         const PathStep& trait) {
        ValueReader in(*document_, bytes);
        auto walk = Walk::Continue;
        for (std::size_t index = 0; index < fields.size() && walk == Walk::Continue; ++index) {
            const auto field_path = PathStep{.parent = &trait, .member = document_->name(fields[index].name)};
            if (plans[index].unknown) {
                report(field_path, "unknown field");
            }
            walk = plans[index].field == nullptr
                       ? skip(fields[index].type, in)
                       : validate_value(schema_->value_types[plans[index].field->type], fields[index].type, in,
                                        field_path, 0);
        }
        if (in.failed()) {
            report(trait, "value data is truncated or malformed");
        } else if (walk == Walk::Continue && !in.at_end()) {
            report(trait, "value data has unexpected trailing bytes");
        }
    }

    Walk skip(std::uint32_t type, ValueReader& in) const {
        document_->skip_value(type, in);
        return in.failed() ? Walk::Stop : Walk::Continue;
    }

    Walk validate_value(const ValueTypeDescriptor& expected,
                        std::uint32_t type_index,
                        ValueReader& in,
                        const PathStep& path,
                        std::size_t depth) {
        const auto* type = document_->type(type_index);
        if (type == nullptr) {
            in.fail();
            return Walk::Stop;
        }
        if (depth >= kMaxPersistenceNestingDepth) {
            report(path, "exceeds configured nesting depth");
            return skip(type_index, in);
        }
        if (type->kind != expected.kind) {
            report(path, "wrong value kind", ValidationErrorCategory::UnsupportedValue);
            return skip(type_index, in);
        }
        switch (expected.kind) {
            case PersistenceValueKind::Vector:
                if (type->lanes != expected.lanes || type->lane_kind != expected.lane_kind) {
                    report(path, "vector lane count or lane kind does not match the declared type",
                           ValidationErrorCategory::UnsupportedValue);
                }
                return skip(type_index, in);
            case PersistenceValueKind::EntityRef: {
                const auto ref = in.get_entity();
                if (!in.failed() && ref.present && !known(ref.id)) {
                    report(path, "reference names a document identity with no matching record");
                }
                break;
            }
            case PersistenceValueKind::Enum:
                validate_enum(*type, in, path);
                break;
            case PersistenceValueKind::Struct:
                return validate_struct(expected, type_index, in, path, depth);
            case PersistenceValueKind::List:
                return validate_list(expected, *type, in, path, depth);
            case PersistenceValueKind::String:
            case PersistenceValueKind::AssetRef:
            case PersistenceValueKind::InputRef:
                (void)in.get_name();
                break;
            case PersistenceValueKind::Bool:
            case PersistenceValueKind::Int:
            case PersistenceValueKind::Float:
            case PersistenceValueKind::Unsupported:
                return skip(type_index, in);
        }
        return in.failed() ? Walk::Stop : Walk::Continue;
    }

    void validate_enum(const DocumentType& type, ValueReader& in, const PathStep& path) {
        const auto variant = in.get_index();
        if (in.failed() || type.ref >= enums_.size() || variant >= variants_[type.ref].size()) {
            in.fail();
            return;
        }
        const auto& entry = document_->enums()[type.ref];
        if (enums_[type.ref] == nullptr) {
            report(path, "unknown enum type '" + std::string(document_->name(entry.name)) + "'",
                   ValidationErrorCategory::UnsupportedValue);
        } else if (!variants_[type.ref][variant]) {
            report(path, "unknown enum variant '" + std::string(document_->name(entry.variants[variant])) + "'",
                   ValidationErrorCategory::UnsupportedValue);
        }
    }

    Walk validate_struct(const ValueTypeDescriptor& expected,
                         std::uint32_t type_index,
                         ValueReader& in,
                         const PathStep& path,
                         std::size_t depth) {
        const auto& type        = *document_->type(type_index);
        const auto* descriptor  = find_struct_descriptor(*schema_, expected.declared_type);
        if (descriptor == nullptr) {
            report(path, "unknown struct type '" + std::string(expected.declared_type) + "'",
                   ValidationErrorCategory::UnsupportedValue);
            return skip(type_index, in);
        }
        if (type.ref >= document_->struct_shapes().size()) {
            in.fail();
            return Walk::Stop;
        }
        const auto& plan   = plan_struct(type.ref, *descriptor);
        const auto& fields = document_->struct_shapes()[type.ref];
        for (std::size_t index = 0; index < fields.size(); ++index) {
            const auto field_path = PathStep{.parent = &path, .member = document_->name(fields[index].name)};
            const auto walk       = plan.fields[index] == nullptr
                                        ? skip(fields[index].type, in)
                                        : validate_value(schema_->value_types[plan.fields[index]->type],
                                                         fields[index].type, in, field_path, depth + 1);
            if (walk == Walk::Stop) {
                return Walk::Stop;
            }
        }
        for (const auto missing : plan.missing) {
            report(PathStep{.parent = &path, .member = missing}, "missing required field");
        }
        return Walk::Continue;
    }

    Walk validate_list(const ValueTypeDescriptor& expected,
                       const DocumentType& type,
                       ValueReader& in,
                       const PathStep& path,
                       std::size_t depth) {
        const auto count = in.get_index();
        if (in.failed()) {
            return Walk::Stop;
        }
        if (count > kMaxPersistenceCollectionSize) {
            report(path, "list exceeds the configured collection size limit");
            return Walk::Stop;  // the remaining bytes of this trait are not walked
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto item_type = type.ref == kNoIndex ? in.get_index() : type.ref;
            const auto walk      = expected.element == kNoValueType
                                       ? skip(item_type, in)
                                       : validate_value(schema_->value_types[expected.element], item_type, in,
                                                        PathStep{.parent = &path, .index = index}, depth + 1);
            if (walk == Walk::Stop) {
                return Walk::Stop;
            }
        }
        return Walk::Continue;
    }

    void validate_trait_set(const ArchetypeDescriptor& archetype, const EntityRow& row, const PathStep& path) {
        if (!schema_->checks_trait_sets) {
            return;
        }
        trait_set_.assign(archetype.baseline_traits.begin(), archetype.baseline_traits.end());
        const auto rows = document_->trait_rows();
        for (std::uint32_t offset = 0; offset < row.trait_count; ++offset) {
            const auto& trait = rows[row.first_trait + offset];
            if (trait.shape >= traits_.size()) {
                continue;
            }
            const auto name = document_->name(document_->trait_shapes()[trait.shape].name);
            if (trait.present) {
                trait_set_.push_back(name);
            } else {
                std::erase(trait_set_, name);
            }
        }
        std::erase_if(trait_set_,
                      [&](std::string_view trait) { return std::ranges::binary_search(schema_->unchecked_traits, trait); });
        std::ranges::sort(trait_set_);
        const auto duplicates = std::ranges::unique(trait_set_);
        trait_set_.erase(duplicates.begin(), duplicates.end());
        const bool reachable = std::ranges::any_of(archetype.reachable_trait_sets, [&](std::span<const std::string_view> set) {
            return std::ranges::equal(set, trait_set_);
        });
        if (!reachable) {
            report(path, "illegal trait combination: no entity of this archetype can carry these traits");
        }
    }

    // Walks each record's parent chain once. Every record whose chain reaches a
    // cycle is reported, and a chain already cleared is never walked again.
    void detect_hierarchy_cycles() {
        const auto rows = document_->entity_rows();
        std::vector<Chain> state(rows.size(), Chain::Unvisited);
        std::vector<std::uint32_t> walk;
        for (std::uint32_t start = 0; start < rows.size(); ++start) {
            if (!rows[start].parent.present || state[start] == Chain::Acyclic) {
                continue;
            }
            auto outcome = state[start] == Chain::Cyclic ? Chain::Cyclic : Chain::Unvisited;
            walk.clear();
            for (auto current = start; outcome == Chain::Unvisited;) {
                state[current] = Chain::OnPath;
                walk.push_back(current);
                const auto parent = rows[current].parent.present ? row_of(rows[current].parent.id) : kNoIndex;
                if (parent == kNoIndex || state[parent] == Chain::Acyclic) {
                    outcome = Chain::Acyclic;
                } else if (state[parent] == Chain::OnPath || state[parent] == Chain::Cyclic) {
                    outcome = Chain::Cyclic;
                } else {
                    current = parent;
                }
            }
            for (const auto visited : walk) {
                state[visited] = outcome;
            }
            if (outcome == Chain::Cyclic) {
                const auto base = root(rows[start]);
                report(PathStep{.parent = &base, .index = rows[start].id}, "parent chain contains a cycle");
            }
        }
    }

    // Without a reparent every parent was created first, and the creation sweep
    // relies on it.
    void require_parents_first() {
        const auto rows = document_->entity_rows();
        for (std::uint32_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            if (!row.parent.present) {
                continue;
            }
            const auto parent = row_of(row.parent.id);
            if (parent != kNoIndex && parent >= index) {
                const auto base = root(row);
                report(PathStep{.parent = &base, .index = row.id}, "child recorded before its parent");
            }
        }
    }

    const SchemaDescriptor* schema_;
    const Snapshot* document_;
    std::vector<ValidationError>* errors_;
    std::vector<std::pair<DocumentId, std::uint32_t>> ids_;
    std::vector<const EnumDescriptor*> enums_;
    std::vector<std::vector<bool>> variants_;
    std::vector<TraitPlan> traits_;
    std::map<std::pair<std::uint32_t, const StructDescriptor*>, StructPlan> structs_;
    std::vector<const ArchetypeDescriptor*> archetypes_;
    std::vector<bool> archetypes_mapped_;
    std::vector<std::string_view> seen_;
    std::vector<std::string_view> trait_set_;
};

}  // namespace

std::vector<ValidationError> validate_document(const SchemaDescriptor& schema, const Snapshot& snapshot) {
    std::vector<ValidationError> errors;
    if (!schema_accepts(schema, snapshot)) {
        errors.push_back({.field_path = "$schema", .message = "document schema descriptor is incompatible with this program"});
        return errors;
    }
    DocumentValidator(schema, snapshot, errors).run();
    return errors;
}

}  // namespace cactus::persistence
