#include "common/persistence_document.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace cactus::persistence {

Value::Value(PersistenceValueKind kind, Storage storage)
    : kind_(kind),
      storage_(std::move(storage)) {}

Value Value::of_bool(bool value) {
    return {PersistenceValueKind::Bool, value};
}

Value Value::of_int(std::int32_t value) {
    return {PersistenceValueKind::Int, value};
}

Value Value::of_float(float value) {
    return {PersistenceValueKind::Float, value};
}

Value Value::of_string(std::string value) {
    return {PersistenceValueKind::String, std::move(value)};
}

Value Value::of_vector(PersistenceValueKind lane_kind, std::uint8_t lanes, std::array<float, 4> components) {
    return {PersistenceValueKind::Vector,
            VectorValue{.lane_kind = lane_kind, .lanes = lanes, .components = components}};
}

Value Value::of_entity(EntityRef reference) {
    return {PersistenceValueKind::EntityRef, reference};
}

Value Value::of_enum(std::string type, std::string variant) {
    return {PersistenceValueKind::Enum, EnumRef{.type = std::move(type), .variant = std::move(variant)}};
}

Value Value::of_asset(std::string declaration) {
    return {PersistenceValueKind::AssetRef, DeclarationRef{.declaration = std::move(declaration)}};
}

Value Value::of_input(std::string declaration) {
    return {PersistenceValueKind::InputRef, DeclarationRef{.declaration = std::move(declaration)}};
}

Value Value::of_struct(StructValue value) {
    return {PersistenceValueKind::Struct, std::move(value)};
}

Value Value::of_list(ListValue value) {
    return {PersistenceValueKind::List, std::move(value)};
}

bool Value::as_bool() const {
    return std::get<bool>(storage_);
}

std::int32_t Value::as_int() const {
    return std::get<std::int32_t>(storage_);
}

float Value::as_float() const {
    return std::get<float>(storage_);
}

const std::string& Value::as_string() const {
    return std::get<std::string>(storage_);
}

const VectorValue& Value::as_vector() const {
    return std::get<VectorValue>(storage_);
}

const EntityRef& Value::as_entity() const {
    return std::get<EntityRef>(storage_);
}

const EnumRef& Value::as_enum() const {
    return std::get<EnumRef>(storage_);
}

const std::string& Value::as_declaration() const {
    return std::get<DeclarationRef>(storage_).declaration;
}

const StructValue& Value::as_struct() const {
    return std::get<StructValue>(storage_);
}

const ListValue& Value::as_list() const {
    return std::get<ListValue>(storage_);
}

const TraitRecord* find_trait(const EntityRecord& record, std::string_view trait) {
    const auto found = std::ranges::find(record.traits, trait, &TraitRecord::trait);
    return found == record.traits.end() ? nullptr : &*found;
}

const FieldValue* find_field(const std::vector<FieldValue>& fields, std::string_view name) {
    const auto found = std::ranges::find(fields, name, &FieldValue::name);
    return found == fields.end() ? nullptr : &*found;
}

const EntityRecord* find_record(const Snapshot& snapshot, DocumentId id) {
    const auto found = std::ranges::find(snapshot.entities, id, &EntityRecord::id);
    return found == snapshot.entities.end() ? nullptr : &*found;
}

const TraitDescriptor* find_trait_descriptor(const SchemaDescriptor& schema, std::string_view trait) {
    const auto found = std::ranges::find(schema.traits, trait, &TraitDescriptor::trait);
    return found == schema.traits.end() ? nullptr : &*found;
}

const FieldDescriptor* find_field_descriptor(const TraitDescriptor& trait, std::string_view field) {
    const auto found = std::ranges::find(trait.fields, field, &FieldDescriptor::name);
    return found == trait.fields.end() ? nullptr : &*found;
}

bool schema_accepts(const SchemaDescriptor& schema, const Snapshot& snapshot) noexcept {
    return schema.revision == snapshot.schema_revision && schema.fingerprint == snapshot.schema_fingerprint;
}

}  // namespace cactus::persistence
