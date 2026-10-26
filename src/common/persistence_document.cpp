#include "common/persistence_document.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
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

namespace {

const StructField* find_named(const std::vector<StructField>& fields, std::string_view name) {
    const auto found = std::ranges::find(fields, name, &StructField::name);
    return found == fields.end() ? nullptr : &*found;
}

// A list longer than the remaining bytes can only be real when its items take no
// bytes; past the collection bound such a count is treated as malformed.
bool implausible_count(std::uint32_t count, const ValueReader& in) {
    return count > kMaxPersistenceCollectionSize && count > in.remaining();
}

}  // namespace

const Value* StructValue::find(std::string_view name) const {
    const auto* found = find_named(fields, name);
    return found == nullptr ? nullptr : &found->value;
}

// ── Snapshot ────────────────────────────────────────────────────────────────

std::optional<EntityView> Snapshot::find_entity(DocumentId id) const {
    const auto found = std::ranges::find(entities_, id, &EntityRow::id);
    if (found == entities_.end()) {
        return std::nullopt;
    }
    return EntityView{*this, static_cast<std::size_t>(found - entities_.begin())};
}

std::span<const std::uint8_t> Snapshot::payload(const TraitRow& row, bool persisted) const noexcept {
    const std::uint64_t begin = row.begin + (persisted ? row.construction_bytes : 0);
    const std::uint64_t size  = persisted ? row.persisted_bytes : row.construction_bytes;
    if (begin > arena_.size() || arena_.size() - begin < size) {
        return {};
    }
    return std::span<const std::uint8_t>(arena_).subspan(static_cast<std::size_t>(begin),
                                                         static_cast<std::size_t>(size));
}

std::vector<StructField> Snapshot::relation_fields(const RelationRow& row) const {
    std::vector<StructField> fields;
    if (row.values.shape >= traits_.size()) {
        return fields;
    }
    const auto& layout = traits_[row.values.shape].construction;
    fields.reserve(layout.size());
    ValueReader in(*this, payload(row.values, false));
    for (const auto& field : layout) {
        fields.push_back(StructField{.name = std::string(name(field.name)), .value = read_value(field.type, in)});
    }
    return fields;
}

Value Snapshot::read_value(std::uint32_t type_index, ValueReader& in, std::size_t depth) const {
    const auto* type = this->type(type_index);
    if (type == nullptr || depth > kMaxPersistenceNestingDepth) {
        in.fail();
        return {};
    }
    Value value;
    switch (type->kind) {
        case PersistenceValueKind::Bool:
            value = Value::of_bool(in.get_bool());
            break;
        case PersistenceValueKind::Int:
            value = Value::of_int(in.get_int());
            break;
        case PersistenceValueKind::Float:
            value = Value::of_float(in.get_float());
            break;
        case PersistenceValueKind::Vector: {
            std::array<float, 4> components{};
            if (type->lanes > components.size()) {
                in.fail();
                break;
            }
            in.get_lanes(std::span<float>(components).first(type->lanes));
            value = Value::of_vector(type->lane_kind, type->lanes, components);
            break;
        }
        case PersistenceValueKind::String:
            value = Value::of_string(std::string(in.get_name()));
            break;
        case PersistenceValueKind::EntityRef:
            value = Value::of_entity(in.get_entity());
            break;
        case PersistenceValueKind::AssetRef:
            value = Value::of_asset(std::string(in.get_name()));
            break;
        case PersistenceValueKind::InputRef:
            value = Value::of_input(std::string(in.get_name()));
            break;
        case PersistenceValueKind::Enum: {
            const auto variant = in.get_index();
            if (type->ref >= enums_.size() || variant >= enums_[type->ref].variants.size()) {
                in.fail();
                break;
            }
            const auto& entry = enums_[type->ref];
            value = Value::of_enum(std::string(name(entry.name)), std::string(name(entry.variants[variant])));
            break;
        }
        case PersistenceValueKind::Struct: {
            if (type->ref >= structs_.size()) {
                in.fail();
                break;
            }
            StructValue fields;
            fields.fields.reserve(structs_[type->ref].size());
            for (const auto& field : structs_[type->ref]) {
                fields.fields.push_back(
                    StructField{.name = std::string(name(field.name)), .value = read_value(field.type, in, depth + 1)});
            }
            value = Value::of_struct(std::move(fields));
            break;
        }
        case PersistenceValueKind::List: {
            const auto count = in.get_index();
            if (implausible_count(count, in)) {
                in.fail();
                break;
            }
            ListValue items;
            for (std::uint32_t index = 0; index < count && !in.failed(); ++index) {
                const auto item_type = type->ref == kNoIndex ? in.get_index() : type->ref;
                items.items.push_back(read_value(item_type, in, depth + 1));
            }
            value = Value::of_list(std::move(items));
            break;
        }
        case PersistenceValueKind::Unsupported:
            break;
    }
    return in.failed() ? Value{} : value;
}

void Snapshot::skip_value(std::uint32_t type_index, ValueReader& in, std::size_t depth) const {
    const auto* type = this->type(type_index);
    if (type == nullptr || depth > kMaxPersistenceNestingDepth) {
        in.fail();
        return;
    }
    switch (type->kind) {
        case PersistenceValueKind::Bool:
            (void)in.get_bool();
            return;
        case PersistenceValueKind::Int:
        case PersistenceValueKind::Float:
        case PersistenceValueKind::String:
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
        case PersistenceValueKind::Enum:
            (void)in.get_index();
            return;
        case PersistenceValueKind::Vector:
            for (std::uint8_t lane = 0; lane < type->lanes; ++lane) {
                (void)in.get_float();
            }
            return;
        case PersistenceValueKind::EntityRef:
            (void)in.get_entity();
            return;
        case PersistenceValueKind::Struct:
            if (type->ref >= structs_.size()) {
                in.fail();
                return;
            }
            for (const auto& field : structs_[type->ref]) {
                skip_value(field.type, in, depth + 1);
            }
            return;
        case PersistenceValueKind::List: {
            const auto count = in.get_index();
            if (implausible_count(count, in)) {
                in.fail();
                return;
            }
            for (std::uint32_t index = 0; index < count && !in.failed(); ++index) {
                skip_value(type->ref == kNoIndex ? in.get_index() : type->ref, in, depth + 1);
            }
            return;
        }
        case PersistenceValueKind::Unsupported:
            return;
    }
}

// ── Views ───────────────────────────────────────────────────────────────────

namespace {

const DocumentTrait* shape_of(const Snapshot& document, const TraitRow& row) {
    return row.shape < document.trait_shapes().size() ? &document.trait_shapes()[row.shape] : nullptr;
}

std::optional<Value> find_in(const Snapshot& document,
                             const TraitRow& row,
                             const std::vector<DocumentField>& layout,
                             bool persisted,
                             std::string_view name) {
    const auto found = std::ranges::find_if(
        layout, [&](const DocumentField& field) { return document.name(field.name) == name; });
    if (found == layout.end()) {
        return std::nullopt;
    }
    ValueReader in(document, document.payload(row, persisted));
    for (auto field = layout.begin(); field != found; ++field) {
        document.skip_value(field->type, in);
    }
    return document.read_value(found->type, in);
}

std::vector<StructField> read_all(const Snapshot& document,
                                  const TraitRow& row,
                                  const std::vector<DocumentField>& layout,
                                  bool persisted) {
    std::vector<StructField> fields;
    fields.reserve(layout.size());
    ValueReader in(document, document.payload(row, persisted));
    for (const auto& field : layout) {
        fields.push_back(StructField{.name = std::string(document.name(field.name)),
                                     .value = document.read_value(field.type, in)});
    }
    return fields;
}

}  // namespace

const TraitRow& TraitView::row() const {
    return document_->trait_rows()[row_];
}

std::string_view TraitView::name() const {
    const auto* shape = shape_of(*document_, row());
    return shape == nullptr ? std::string_view{} : document_->name(shape->name);
}

bool TraitView::present() const {
    return row().present;
}

std::uint32_t TraitView::shape() const {
    return row().shape;
}

std::optional<Value> TraitView::construction_field(std::string_view name) const {
    const auto* shape = shape_of(*document_, row());
    return shape == nullptr ? std::nullopt : find_in(*document_, row(), shape->construction, false, name);
}

std::optional<Value> TraitView::persisted_field(std::string_view name) const {
    const auto* shape = shape_of(*document_, row());
    return shape == nullptr ? std::nullopt : find_in(*document_, row(), shape->persisted, true, name);
}

std::optional<Value> TraitView::field(std::string_view name) const {
    auto found = construction_field(name);
    return found.has_value() ? found : persisted_field(name);
}

std::vector<StructField> TraitView::construction() const {
    const auto* shape = shape_of(*document_, row());
    return shape == nullptr ? std::vector<StructField>{} : read_all(*document_, row(), shape->construction, false);
}

std::vector<StructField> TraitView::persisted() const {
    const auto* shape = shape_of(*document_, row());
    return shape == nullptr ? std::vector<StructField>{} : read_all(*document_, row(), shape->persisted, true);
}

ValueReader TraitView::reader() const {
    const auto& trait = row();
    const auto first  = document_->payload(trait, false);
    const auto second = document_->payload(trait, true);
    if (first.empty() && second.empty()) {
        return {*document_, {}};
    }
    // Both ranges sit next to each other in the arena.
    const auto* begin = first.empty() ? second.data() : first.data();
    return {*document_, std::span<const std::uint8_t>(begin, first.size() + second.size())};
}

ValueReader TraitView::construction_reader() const {
    return {*document_, document_->payload(row(), false)};
}

ValueReader TraitView::persisted_reader() const {
    return {*document_, document_->payload(row(), true)};
}

const EntityRow& EntityView::row() const {
    return document_->entity_rows()[index_];
}

DocumentId EntityView::id() const {
    return row().id;
}

std::string_view EntityView::archetype() const {
    return document_->name(row().archetype);
}

EntityRef EntityView::parent() const {
    return row().parent;
}

std::size_t EntityView::trait_count() const {
    return row().trait_count;
}

TraitView EntityView::trait_at(std::size_t index) const {
    return {*document_, static_cast<std::uint32_t>(row().first_trait + index)};
}

std::optional<TraitView> EntityView::trait(std::string_view name) const {
    for (std::size_t index = 0; index < trait_count(); ++index) {
        const auto view = trait_at(index);
        if (view.name() == name) {
            return view;
        }
    }
    return std::nullopt;
}

// ── Layout comparison ───────────────────────────────────────────────────────

namespace {

bool same_type_at(const Snapshot& left,
                  std::uint32_t left_type,
                  const Snapshot& right,
                  std::uint32_t right_type,
                  std::size_t depth);

bool same_fields(const Snapshot& left,
                 const std::vector<DocumentField>& left_fields,
                 const Snapshot& right,
                 const std::vector<DocumentField>& right_fields,
                 std::size_t depth) {
    return std::ranges::equal(left_fields, right_fields, [&](const DocumentField& a, const DocumentField& b) {
        return left.name(a.name) == right.name(b.name) && same_type_at(left, a.type, right, b.type, depth);
    });
}

bool same_type_at(const Snapshot& left,
                  std::uint32_t left_type,
                  const Snapshot& right,
                  std::uint32_t right_type,
                  std::size_t depth) {
    // A struct reaching itself through a list compares equal past the nesting bound.
    if (depth > kMaxPersistenceNestingDepth) {
        return true;
    }
    if (left_type == kNoIndex || right_type == kNoIndex) {
        return left_type == right_type;
    }
    const auto* a = left.type(left_type);
    const auto* b = right.type(right_type);
    if (a == nullptr || b == nullptr || a->kind != b->kind || a->lanes != b->lanes || a->lane_kind != b->lane_kind) {
        return false;
    }
    switch (a->kind) {
        case PersistenceValueKind::Enum: {
            if (a->ref >= left.enums().size() || b->ref >= right.enums().size()) {
                return false;
            }
            const auto& x = left.enums()[a->ref];
            const auto& y = right.enums()[b->ref];
            return left.name(x.name) == right.name(y.name) &&
                   std::ranges::equal(x.variants, y.variants, [&](std::uint32_t p, std::uint32_t q) {
                       return left.name(p) == right.name(q);
                   });
        }
        case PersistenceValueKind::Struct:
            return a->ref < left.struct_shapes().size() && b->ref < right.struct_shapes().size() &&
                   same_fields(left, left.struct_shapes()[a->ref], right, right.struct_shapes()[b->ref], depth + 1);
        case PersistenceValueKind::List:
            return same_type_at(left, a->ref, right, b->ref, depth + 1);
        default:
            return true;
    }
}

}  // namespace

bool same_type(const Snapshot& left, std::uint32_t left_type, const Snapshot& right, std::uint32_t right_type) {
    return same_type_at(left, left_type, right, right_type, 0);
}

bool same_trait_layout(const Snapshot& left, std::uint32_t left_shape, const Snapshot& right, std::uint32_t right_shape) {
    if (left_shape >= left.trait_shapes().size() || right_shape >= right.trait_shapes().size()) {
        return false;
    }
    const auto& a = left.trait_shapes()[left_shape];
    const auto& b = right.trait_shapes()[right_shape];
    return left.name(a.name) == right.name(b.name) && same_fields(left, a.construction, right, b.construction, 0) &&
           same_fields(left, a.persisted, right, b.persisted, 0);
}

// ── Builder ─────────────────────────────────────────────────────────────────

SnapshotBuilder SnapshotBuilder::with_tables_of(const Snapshot& document) {
    auto builder = with_tables(DocumentTables{.names   = document.names_,
                                              .types   = document.types_,
                                              .structs = document.structs_,
                                              .enums   = document.enums_,
                                              .traits  = document.traits_});
    builder.set_schema(document.schema_revision(), document.schema_fingerprint());
    builder.set_module(document.module());
    return builder;
}

SnapshotBuilder SnapshotBuilder::with_tables(DocumentTables tables) {
    SnapshotBuilder builder;
    auto& copy    = builder.document_;
    copy.names_   = std::move(tables.names);
    copy.types_   = std::move(tables.types);
    copy.structs_ = std::move(tables.structs);
    copy.enums_   = std::move(tables.enums);
    copy.traits_  = std::move(tables.traits);
    for (std::uint32_t index = 0; index < copy.names_.size(); ++index) {
        builder.names_.try_emplace(copy.names_[index], index);
    }
    return builder;
}

std::uint32_t SnapshotBuilder::name(std::string_view text) {
    if (const auto found = names_.find(text); found != names_.end()) {
        return found->second;
    }
    const auto index = static_cast<std::uint32_t>(document_.names_.size());
    document_.names_.emplace_back(text);
    names_.emplace(document_.names_.back(), index);
    return index;
}

std::uint32_t SnapshotBuilder::type(const DocumentType& type) {
    auto& types = document_.types_;
    if (const auto found = std::ranges::find(types, type); found != types.end()) {
        return static_cast<std::uint32_t>(found - types.begin());
    }
    types.push_back(type);
    return static_cast<std::uint32_t>(types.size() - 1);
}

std::uint32_t SnapshotBuilder::struct_shape(std::vector<DocumentField> fields) {
    auto& structs = document_.structs_;
    if (const auto found = std::ranges::find(structs, fields); found != structs.end()) {
        return static_cast<std::uint32_t>(found - structs.begin());
    }
    structs.push_back(std::move(fields));
    return static_cast<std::uint32_t>(structs.size() - 1);
}

std::uint32_t SnapshotBuilder::declare_struct() {
    document_.structs_.emplace_back();
    return static_cast<std::uint32_t>(document_.structs_.size() - 1);
}

void SnapshotBuilder::define_struct(std::uint32_t shape, std::vector<DocumentField> fields) {
    document_.structs_.at(shape) = std::move(fields);
}

std::uint32_t SnapshotBuilder::enumeration(std::string_view enum_name, std::span<const std::string_view> variants) {
    const auto name_index = name(enum_name);
    auto& enums           = document_.enums_;
    auto found            = std::ranges::find(enums, name_index, &DocumentEnum::name);
    if (found == enums.end()) {
        enums.push_back(DocumentEnum{.name = name_index});
        found = enums.end() - 1;
    }
    const auto index = static_cast<std::uint32_t>(found - enums.begin());
    for (const auto variant : variants) {
        const auto variant_index = name(variant);
        auto& known              = enums[index].variants;
        if (std::ranges::find(known, variant_index) == known.end()) {
            known.push_back(variant_index);
        }
    }
    return index;
}

std::uint32_t SnapshotBuilder::trait_shape(std::string_view trait,
                                           std::vector<DocumentField> construction,
                                           std::vector<DocumentField> persisted) {
    DocumentTrait shape{.name = name(trait), .construction = std::move(construction), .persisted = std::move(persisted)};
    auto& traits = document_.traits_;
    if (const auto found = std::ranges::find(traits, shape); found != traits.end()) {
        return static_cast<std::uint32_t>(found - traits.begin());
    }
    traits.push_back(std::move(shape));
    return static_cast<std::uint32_t>(traits.size() - 1);
}

void SnapshotBuilder::reserve(std::size_t entities, std::size_t traits, std::size_t bytes) {
    document_.entities_.reserve(entities);
    document_.trait_rows_.reserve(traits);
    document_.arena_.reserve(bytes);
}

void SnapshotBuilder::grow_toward_expected() {
    const auto written = document_.entities_.size();
    if (expected_entities_ <= written || written < kReserveSample) {
        return;
    }
    const auto remaining = expected_entities_ - written;
    const auto ahead     = [&](auto& storage, std::size_t slack) {
        if (storage.capacity() - storage.size() >= slack) {
            return;
        }
        const auto per_entity = static_cast<double>(storage.size()) / static_cast<double>(written);
        storage.reserve(storage.size() + static_cast<std::size_t>(per_entity * static_cast<double>(remaining) * 1.0625) +
                        slack);
    };
    ahead(document_.trait_rows_, 256);
    ahead(document_.arena_, 16384);
}

void SnapshotBuilder::begin_entity(DocumentId id, std::uint32_t archetype, EntityRef parent) {
    grow_toward_expected();
    document_.entities_.push_back(EntityRow{.id          = id,
                                            .archetype   = archetype,
                                            .parent      = parent,
                                            .first_trait = static_cast<std::uint32_t>(document_.trait_rows_.size())});
}

void SnapshotBuilder::begin_trait(std::uint32_t shape, bool present) {
    assert(!document_.entities_.empty());
    if (document_.arena_.size() > kMaxSnapshotArenaBytes) {
        throw std::length_error("persistence snapshot: values exceed the 4 GiB arena limit");
    }
    document_.trait_rows_.push_back(TraitRow{.begin   = static_cast<std::uint32_t>(document_.arena_.size()),
                                             .shape   = shape,
                                             .present = present ? 1U : 0U});
    ++document_.entities_.back().trait_count;
    split_ = false;
}

void SnapshotBuilder::begin_relation_row(std::uint32_t shape, DocumentId from, DocumentId to) {
    if (document_.arena_.size() > kMaxSnapshotArenaBytes) {
        throw std::length_error("persistence snapshot: values exceed the 4 GiB arena limit");
    }
    document_.relation_rows_.push_back(RelationRow{
        .from = from, .to = to, .values = TraitRow{.begin = static_cast<std::uint32_t>(document_.arena_.size()), .shape = shape}});
}

void SnapshotBuilder::end_relation_row() {
    auto& row              = document_.relation_rows_.back().values;
    row.construction_bytes = static_cast<std::uint32_t>(document_.arena_.size() - row.begin);
}

void SnapshotBuilder::begin_persisted() {
    auto& row              = document_.trait_rows_.back();
    row.construction_bytes = static_cast<std::uint32_t>(document_.arena_.size() - row.begin);
    split_                 = true;
}

void SnapshotBuilder::end_trait() {
    auto& row         = document_.trait_rows_.back();
    const auto total  = static_cast<std::uint32_t>(document_.arena_.size() - row.begin);
    if (!split_) {
        row.construction_bytes = total;
    }
    row.persisted_bytes = total - row.construction_bytes;
}

void SnapshotBuilder::add_entity(DocumentId id, std::string_view archetype, EntityRef parent) {
    begin_entity(id, name(archetype), parent);
}

bool SnapshotBuilder::fits(const Value& value, std::uint32_t type_index) const {
    const auto* type = document_.type(type_index);
    if (type == nullptr || type->kind != value.kind()) {
        return false;
    }
    switch (type->kind) {
        case PersistenceValueKind::Vector:
            return type->lanes == value.as_vector().lanes && type->lane_kind == value.as_vector().lane_kind;
        case PersistenceValueKind::Enum:
            return type->ref < document_.enums_.size() &&
                   document_.name(document_.enums_[type->ref].name) == value.as_enum().type;
        case PersistenceValueKind::Struct:
            return type->ref < document_.structs_.size() &&
                   fits_fields(value.as_struct().fields, document_.structs_[type->ref]);
        case PersistenceValueKind::List:
            return type->ref == kNoIndex || std::ranges::all_of(value.as_list().items, [&](const Value& item) {
                       return fits(item, type->ref);
                   });
        default:
            return true;
    }
}

bool SnapshotBuilder::fits_fields(const std::vector<StructField>& values,
                                  const std::vector<DocumentField>& layout) const {
    return values.size() == layout.size() && std::ranges::all_of(layout, [&](const DocumentField& field) {
               const auto* value = find_named(values, document_.name(field.name));
               return value != nullptr && fits(value->value, field.type);
           });
}

std::uint32_t SnapshotBuilder::type_of(const Value& value) {
    switch (value.kind()) {
        case PersistenceValueKind::Vector:
            return type({.kind      = PersistenceValueKind::Vector,
                         .lanes     = value.as_vector().lanes,
                         .lane_kind = value.as_vector().lane_kind});
        case PersistenceValueKind::Enum:
            return type({.kind = PersistenceValueKind::Enum, .ref = enumeration(value.as_enum().type)});
        case PersistenceValueKind::Struct:
            return type({.kind = PersistenceValueKind::Struct, .ref = struct_shape(layout_of(value.as_struct().fields))});
        case PersistenceValueKind::List: {
            const auto& items  = value.as_list().items;
            std::uint32_t element = kNoIndex;
            if (!items.empty()) {
                element = type_of(items.front());
                for (const auto& item : std::span(items).subspan(1)) {
                    if (type_of(item) != element) {
                        element = kNoIndex;
                        break;
                    }
                }
            }
            return type({.kind = PersistenceValueKind::List, .ref = element});
        }
        default:
            return type({.kind = value.kind()});
    }
}

std::vector<DocumentField> SnapshotBuilder::layout_of(const std::vector<StructField>& values) {
    std::vector<DocumentField> layout;
    layout.reserve(values.size());
    for (const auto& field : values) {
        const auto field_type = type_of(field.value);
        layout.push_back(DocumentField{.name = name(field.name), .type = field_type});
    }
    return layout;
}

void SnapshotBuilder::put_value(const Value& value, std::uint32_t type_index) {
    const auto type = document_.types_[type_index];
    switch (type.kind) {
        case PersistenceValueKind::Bool:
            put_bool(value.as_bool());
            return;
        case PersistenceValueKind::Int:
            put_int(value.as_int());
            return;
        case PersistenceValueKind::Float:
            put_float(value.as_float());
            return;
        case PersistenceValueKind::Vector: {
            const auto& vector = value.as_vector();
            put_lanes(std::span<const float>(vector.components).first(std::min<std::size_t>(vector.lanes, 4)));
            return;
        }
        case PersistenceValueKind::String:
            put_name(value.as_string());
            return;
        case PersistenceValueKind::EntityRef:
            put_entity(value.as_entity());
            return;
        case PersistenceValueKind::AssetRef:
        case PersistenceValueKind::InputRef:
            put_name(value.as_declaration());
            return;
        case PersistenceValueKind::Enum: {
            const auto variant = name(value.as_enum().variant);
            auto& variants     = document_.enums_[type.ref].variants;
            auto found         = std::ranges::find(variants, variant);
            if (found == variants.end()) {
                variants.push_back(variant);
                found = variants.end() - 1;
            }
            put_index(static_cast<std::uint32_t>(found - variants.begin()));
            return;
        }
        case PersistenceValueKind::Struct: {
            const auto layout = document_.structs_[type.ref];
            put_fields(value.as_struct().fields, layout);
            return;
        }
        case PersistenceValueKind::List: {
            const auto& items = value.as_list().items;
            put_index(static_cast<std::uint32_t>(items.size()));
            for (const auto& item : items) {
                if (type.ref != kNoIndex) {
                    put_value(item, type.ref);
                    continue;
                }
                const auto item_type = type_of(item);
                put_index(item_type);
                put_value(item, item_type);
            }
            return;
        }
        case PersistenceValueKind::Unsupported:
            return;
    }
}

void SnapshotBuilder::put_fields(const std::vector<StructField>& values, const std::vector<DocumentField>& layout) {
    for (const auto& field : layout) {
        put_value(find_named(values, document_.name(field.name))->value, field.type);
    }
}

void SnapshotBuilder::add_relation_row(std::string_view relation,
                                       DocumentId from,
                                       DocumentId to,
                                       const std::vector<StructField>& fields) {
    const auto shape = shape_for(relation, fields, {});
    begin_relation_row(shape, from, to);
    put_fields(fields, document_.traits_[shape].construction);
    end_relation_row();
}

// A recorded layout of `trait` the values fit, or a new one.
std::uint32_t SnapshotBuilder::shape_for(std::string_view trait,
                                         const std::vector<StructField>& construction,
                                         const std::vector<StructField>& persisted) {
    const auto trait_name = name(trait);
    const auto found      = std::ranges::find_if(document_.traits_, [&](const DocumentTrait& candidate) {
        return candidate.name == trait_name && fits_fields(construction, candidate.construction) &&
               fits_fields(persisted, candidate.persisted);
    });
    if (found != document_.traits_.end()) {
        return static_cast<std::uint32_t>(found - document_.traits_.begin());
    }
    auto construction_layout = layout_of(construction);
    auto persisted_layout    = layout_of(persisted);
    return trait_shape(trait, std::move(construction_layout), std::move(persisted_layout));
}

void SnapshotBuilder::add_trait(std::string_view trait,
                                const std::vector<StructField>& construction,
                                const std::vector<StructField>& persisted) {
    const auto shape = shape_for(trait, construction, persisted);
    // Writing values never adds a trait layout, so this reference stays valid.
    const auto& layout = document_.traits_[shape];
    begin_trait(shape);
    put_fields(construction, layout.construction);
    begin_persisted();
    put_fields(persisted, layout.persisted);
    end_trait();
}

void SnapshotBuilder::add_absent_trait(std::string_view trait) {
    const auto trait_name = name(trait);
    const auto found      = std::ranges::find(document_.traits_, trait_name, &DocumentTrait::name);
    const auto shape      = found != document_.traits_.end()
                                ? static_cast<std::uint32_t>(found - document_.traits_.begin())
                                : trait_shape(trait, {}, {});
    begin_trait(shape, false);
    end_trait();
}

LayoutMatch match_trait_layouts(const Snapshot& document,
                                const Snapshot& canonical,
                                std::span<const std::uint32_t> canonical_shapes) {
    LayoutMatch match;
    match.traits.assign(document.trait_shapes().size(), kNoIndex);
    for (std::uint32_t shape = 0; shape < document.trait_shapes().size(); ++shape) {
        const auto name  = document.name(document.trait_shapes()[shape].name);
        const auto found = std::ranges::find_if(canonical_shapes, [&](std::uint32_t candidate) {
            return canonical.name(canonical.trait_shapes()[candidate].name) == name;
        });
        if (found == canonical_shapes.end()) {
            continue;
        }
        if (same_trait_layout(document, shape, canonical, *found)) {
            match.traits[shape] = static_cast<std::uint32_t>(found - canonical_shapes.begin());
        } else {
            match.complete = false;
        }
    }
    return match;
}

Snapshot relayout(const Snapshot& source, SnapshotBuilder target) {
    target.set_schema(source.schema_revision(), source.schema_fingerprint());
    target.set_module(source.module());
    if (source.truncated()) {
        target.mark_truncated();
    }
    for (const auto entity : source.entities()) {
        target.add_entity(entity.id(), entity.archetype(), entity.parent());
        for (std::size_t index = 0; index < entity.trait_count(); ++index) {
            const auto trait = entity.trait_at(index);
            if (trait.present()) {
                target.add_trait(trait.name(), trait.construction(), trait.persisted());
            } else {
                target.add_absent_trait(trait.name());
            }
        }
    }
    for (const auto& row : source.relation_rows()) {
        target.add_relation_row(source.relation_name(row), row.from, row.to, source.relation_fields(row));
    }
    return std::move(target).finish();
}

// ── Schema descriptor ───────────────────────────────────────────────────────

const TraitDescriptor* find_trait_descriptor(const SchemaDescriptor& schema, std::string_view trait) {
    const auto found = std::ranges::find(schema.traits, trait, &TraitDescriptor::trait);
    return found == schema.traits.end() ? nullptr : &*found;
}

const FieldDescriptor* find_field_descriptor(const TraitDescriptor& trait, std::string_view field) {
    const auto found = std::ranges::find(trait.fields, field, &FieldDescriptor::name);
    return found == trait.fields.end() ? nullptr : &*found;
}

const StructDescriptor* find_struct_descriptor(const SchemaDescriptor& schema, std::string_view name) {
    const auto found = std::ranges::find(schema.structs, name, &StructDescriptor::name);
    return found == schema.structs.end() ? nullptr : &*found;
}

const EnumDescriptor* find_enum_descriptor(const SchemaDescriptor& schema, std::string_view name) {
    const auto found = std::ranges::find(schema.enums, name, &EnumDescriptor::name);
    return found == schema.enums.end() ? nullptr : &*found;
}

const RelationDescriptor* find_relation_descriptor(const SchemaDescriptor& schema, std::string_view relation) {
    const auto found = std::ranges::find(schema.relations, relation, &RelationDescriptor::relation);
    return found == schema.relations.end() ? nullptr : &*found;
}

const ArchetypeDescriptor* find_archetype_descriptor(const SchemaDescriptor& schema, std::string_view node) {
    const auto found = std::ranges::find(schema.archetypes, node, &ArchetypeDescriptor::node);
    return found == schema.archetypes.end() ? nullptr : &*found;
}

bool schema_accepts(const SchemaDescriptor& schema, const Snapshot& snapshot) noexcept {
    return schema.revision == snapshot.schema_revision() && schema.fingerprint == snapshot.schema_fingerprint();
}

}  // namespace cactus::persistence
