#pragma once

#include "common/persistence_schema.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// The format-independent world snapshot an adapter reads and writes. Generated
// projects include this. Deliberately free of EnTT, raylib, and frontend types —
// an adapter sees declarations and document-local identities, never live handles.

namespace cactus::persistence {

// Identifies an entity within one document. Allocated for every referenced
// entity, including ones the document does not include, so two references to
// the same excluded target stay equal without resurrecting it.
using DocumentId = std::uint32_t;

struct EntityRef {
    DocumentId id  = 0;
    bool present   = false;  // false: the target is not a record in this document

    friend bool operator==(const EntityRef&, const EntityRef&) = default;
};

struct EnumRef {
    std::string type;  // canonical enum declaration
    std::string variant;

    friend bool operator==(const EnumRef&, const EnumRef&) = default;
};

// A fixed-lane numeric aggregate. `lane_kind` distinguishes vec2/vec3/quat from
// color; float carries every byte value a color lane can hold exactly, so the
// one carrier loses nothing. Lane widths live in the schema descriptor.
struct VectorValue {
    PersistenceValueKind lane_kind = PersistenceValueKind::Float;
    std::uint8_t lanes             = 0;
    std::array<float, 4> components{};

    friend bool operator==(const VectorValue&, const VectorValue&) = default;
};

class Value;
struct StructField;

struct StructValue {
    std::vector<StructField> fields;

    [[nodiscard]] const Value* find(std::string_view name) const;

    friend bool operator==(const StructValue&, const StructValue&) = default;
};

struct ListValue {
    std::vector<Value> items;

    friend bool operator==(const ListValue&, const ListValue&) = default;
};

// A named declaration referenced by identity rather than by runtime handle:
// assets and input actions. `kind` separates the two.
struct DeclarationRef {
    std::string declaration;

    friend bool operator==(const DeclarationRef&, const DeclarationRef&) = default;
};

// One materialized value: what views return and what the name-based builder
// takes. Generated capture and restore never build one.
class Value {
public:
    Value() = default;

    [[nodiscard]] static Value of_bool(bool value);
    [[nodiscard]] static Value of_int(std::int32_t value);
    [[nodiscard]] static Value of_float(float value);
    [[nodiscard]] static Value of_string(std::string value);
    [[nodiscard]] static Value of_vector(PersistenceValueKind lane_kind,
                                         std::uint8_t lanes,
                                         std::array<float, 4> components);
    [[nodiscard]] static Value of_entity(EntityRef reference);
    [[nodiscard]] static Value of_enum(std::string type, std::string variant);
    [[nodiscard]] static Value of_asset(std::string declaration);
    [[nodiscard]] static Value of_input(std::string declaration);
    [[nodiscard]] static Value of_struct(StructValue value);
    [[nodiscard]] static Value of_list(ListValue value);

    [[nodiscard]] PersistenceValueKind kind() const noexcept { return kind_; }

    // A default-constructed Value: no payload was recorded, as distinct from a
    // recorded zero.
    [[nodiscard]] bool is_absent() const noexcept { return kind_ == PersistenceValueKind::Unsupported; }

    // Each accessor requires the matching kind(); calling the wrong one is a
    // programming error, not a runtime condition to branch on.
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] std::int32_t as_int() const;
    [[nodiscard]] float as_float() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const VectorValue& as_vector() const;
    [[nodiscard]] const EntityRef& as_entity() const;
    [[nodiscard]] const EnumRef& as_enum() const;
    [[nodiscard]] const std::string& as_declaration() const;
    [[nodiscard]] const StructValue& as_struct() const;
    [[nodiscard]] const ListValue& as_list() const;

    friend bool operator==(const Value&, const Value&) = default;

private:
    using Storage = std::variant<std::monostate,
                                 bool,
                                 std::int32_t,
                                 float,
                                 std::string,
                                 VectorValue,
                                 EntityRef,
                                 EnumRef,
                                 DeclarationRef,
                                 StructValue,
                                 ListValue>;

    Value(PersistenceValueKind kind, Storage storage);

    PersistenceValueKind kind_ = PersistenceValueKind::Unsupported;
    Storage storage_;
};

struct StructField {
    std::string name;
    Value value;

    friend bool operator==(const StructField&, const StructField&) = default;
};

// Per-collection and per-struct-nesting-level bounds capture enforces so one
// pathological field cannot grow a document or its call stack without bound.
// Both are configured constants rather than a program-specific setting: no
// mechanism exists yet for authors to raise or lower them.
inline constexpr std::size_t kMaxPersistenceCollectionSize = 4096;
inline constexpr std::size_t kMaxPersistenceNestingDepth   = 32;
// A whole world's entity records; large enough for every cell of a big voxel world.
inline constexpr std::size_t kMaxPersistenceEntityCount = std::size_t{1} << 22;

inline constexpr std::uint32_t kNoIndex = 0xFFFFFFFFU;

// ── Document tables ─────────────────────────────────────────────────────────
// A snapshot describes itself: every name, value type and trait layout it uses
// is stored once in these tables, and rows refer to them by document-local
// index. The indexes mean nothing outside their own document.

struct DocumentType {
    PersistenceValueKind kind      = PersistenceValueKind::Unsupported;
    std::uint8_t lanes             = 0;
    PersistenceValueKind lane_kind = PersistenceValueKind::Float;
    // Enum: an enums() entry. Struct: a struct_shapes() entry. List: the element
    // type, or kNoIndex when each item is preceded by its own type index.
    std::uint32_t ref = kNoIndex;

    friend bool operator==(const DocumentType&, const DocumentType&) = default;
};

struct DocumentField {
    std::uint32_t name = kNoIndex;
    std::uint32_t type = kNoIndex;

    friend bool operator==(const DocumentField&, const DocumentField&) = default;
};

struct DocumentEnum {
    std::uint32_t name = kNoIndex;
    std::vector<std::uint32_t> variants;  // a value is a position in this list

    friend bool operator==(const DocumentEnum&, const DocumentEnum&) = default;
};

// One trait's value layout: its values are stored in exactly this field order.
// A document may hold several layouts for one trait name.
struct DocumentTrait {
    std::uint32_t name = kNoIndex;
    std::vector<DocumentField> construction;
    std::vector<DocumentField> persisted;

    friend bool operator==(const DocumentTrait&, const DocumentTrait&) = default;
};

struct EntityRow {
    DocumentId id             = 0;
    std::uint32_t archetype   = kNoIndex;  // canonical node name, "game.Tank/turret" for a role
    EntityRef parent;
    std::uint32_t first_trait = 0;
    std::uint32_t trait_count = 0;

    friend bool operator==(const EntityRow&, const EntityRow&) = default;
};

inline constexpr std::uint32_t kNoShape = 0x7FFFFFFFU;
// One arena holds at most this many bytes, so a row's offsets fit 32 bits.
inline constexpr std::size_t kMaxSnapshotArenaBytes = 0xFFFFFFFFU;

// `present == false` records a baseline trait gameplay removed: membership
// information, no payloads. Packed to 16 bytes: a world has several per entity.
struct TraitRow {
    std::uint32_t begin              = 0;  // arena offset of the construction values
    std::uint32_t construction_bytes = 0;
    std::uint32_t persisted_bytes    = 0;
    std::uint32_t shape : 31         = kNoShape;
    std::uint32_t present : 1        = 1;

    friend bool operator==(const TraitRow&, const TraitRow&) = default;
};

class Snapshot;

// A document's tables on their own, as a decoder reads them.
struct DocumentTables {
    std::vector<std::string> names;
    std::vector<DocumentType> types;
    std::vector<std::vector<DocumentField>> structs;
    std::vector<DocumentEnum> enums;
    std::vector<DocumentTrait> traits;
};

// Bounds-checked reads over value bytes. A read past the end returns zero and
// sets failed(); nothing reads outside the span.
class ValueReader {
public:
    ValueReader() = default;
    ValueReader(const Snapshot& document, std::span<const std::uint8_t> bytes)
        : document_(&document),
          bytes_(bytes) {}

    [[nodiscard]] bool get_bool() { return take<std::uint8_t>() != 0; }
    [[nodiscard]] std::int32_t get_int() { return take<std::int32_t>(); }
    [[nodiscard]] float get_float() { return take<float>(); }
    [[nodiscard]] std::uint32_t get_index() { return take<std::uint32_t>(); }

    [[nodiscard]] EntityRef get_entity() {
        const auto present = get_bool();
        return EntityRef{.id = get_index(), .present = present};
    }

    void get_lanes(std::span<float> lanes) {
        for (auto& lane : lanes) {
            lane = get_float();
        }
    }

    // A pooled name: string values, enum variants, asset and input declarations.
    [[nodiscard]] std::string_view get_name();

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] const Snapshot& document() const noexcept { return *document_; }
    void fail() noexcept { failed_ = true; }

private:
    template <class T>
    [[nodiscard]] T take() {
        T value{};
        if (failed_ || bytes_.size() - offset_ < sizeof(T)) {
            failed_ = true;
            return value;
        }
        std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
        offset_ += sizeof(T);
        return value;
    }

    const Snapshot* document_ = nullptr;
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    bool failed_        = false;
};

class TraitView {
public:
    TraitView(const Snapshot& document, std::uint32_t row)
        : document_(&document),
          row_(row) {}

    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] bool present() const;
    [[nodiscard]] std::uint32_t shape() const;
    [[nodiscard]] const TraitRow& row() const;

    // nullopt when the layout has no such field; an absent Value when its bytes
    // are malformed.
    [[nodiscard]] std::optional<Value> field(std::string_view name) const;
    [[nodiscard]] std::optional<Value> construction_field(std::string_view name) const;
    [[nodiscard]] std::optional<Value> persisted_field(std::string_view name) const;
    [[nodiscard]] std::vector<StructField> construction() const;
    [[nodiscard]] std::vector<StructField> persisted() const;

    // Construction values, then persisted ones, in layout order.
    [[nodiscard]] ValueReader reader() const;
    [[nodiscard]] ValueReader construction_reader() const;
    [[nodiscard]] ValueReader persisted_reader() const;

private:
    const Snapshot* document_;
    std::uint32_t row_;
};

class EntityView {
public:
    EntityView(const Snapshot& document, std::size_t index)
        : document_(&document),
          index_(index) {}

    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] const EntityRow& row() const;
    [[nodiscard]] DocumentId id() const;
    [[nodiscard]] std::string_view archetype() const;
    [[nodiscard]] EntityRef parent() const;
    [[nodiscard]] std::size_t trait_count() const;
    [[nodiscard]] TraitView trait_at(std::size_t index) const;
    [[nodiscard]] std::optional<TraitView> trait(std::string_view name) const;

private:
    const Snapshot* document_;
    std::size_t index_;
};

class EntityRange {
public:
    class iterator {
    public:
        using value_type        = EntityView;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;
        iterator(const Snapshot* document, std::size_t index)
            : document_(document),
              index_(index) {}

        [[nodiscard]] EntityView operator*() const { return {*document_, index_}; }
        iterator& operator++() {
            ++index_;
            return *this;
        }
        iterator operator++(int) {
            auto copy = *this;
            ++index_;
            return copy;
        }
        friend bool operator==(const iterator&, const iterator&) = default;

    private:
        const Snapshot* document_ = nullptr;
        std::size_t index_        = 0;
    };

    EntityRange(const Snapshot& document, std::size_t count)
        : document_(&document),
          count_(count) {}

    [[nodiscard]] iterator begin() const { return {document_, 0}; }
    [[nodiscard]] iterator end() const { return {document_, count_}; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    const Snapshot* document_;
    std::size_t count_;
};

class Snapshot {
public:
    [[nodiscard]] std::uint32_t schema_revision() const noexcept { return schema_revision_; }
    [[nodiscard]] std::uint64_t schema_fingerprint() const noexcept { return schema_fingerprint_; }
    void set_schema(std::uint32_t revision, std::uint64_t fingerprint) noexcept {
        schema_revision_    = revision;
        schema_fingerprint_ = fingerprint;
    }
    // The active world/module context.
    [[nodiscard]] const std::string& module() const noexcept { return module_; }
    void set_module(std::string module) { module_ = std::move(module); }
    // Set when capture hit a collection or nesting limit: some persisted
    // values were truncated or omitted rather than growing without bound.
    [[nodiscard]] bool truncated() const noexcept { return truncated_; }
    void set_truncated(bool truncated) noexcept { truncated_ = truncated; }

    // Entities in relative creation order.
    [[nodiscard]] std::size_t entity_count() const noexcept { return entities_.size(); }
    [[nodiscard]] EntityView entity(std::size_t index) const { return {*this, index}; }
    [[nodiscard]] EntityRange entities() const { return {*this, entities_.size()}; }
    [[nodiscard]] std::optional<EntityView> find_entity(DocumentId id) const;

    [[nodiscard]] std::span<const std::string> names() const noexcept { return names_; }
    [[nodiscard]] std::span<const DocumentType> types() const noexcept { return types_; }
    [[nodiscard]] std::span<const std::vector<DocumentField>> struct_shapes() const noexcept { return structs_; }
    [[nodiscard]] std::span<const DocumentEnum> enums() const noexcept { return enums_; }
    [[nodiscard]] std::span<const DocumentTrait> trait_shapes() const noexcept { return traits_; }
    [[nodiscard]] std::span<const EntityRow> entity_rows() const noexcept { return entities_; }
    [[nodiscard]] std::span<const TraitRow> trait_rows() const noexcept { return trait_rows_; }
    [[nodiscard]] std::span<const std::uint8_t> arena() const noexcept { return arena_; }

    // Empty for an index outside the table.
    [[nodiscard]] std::string_view name(std::uint32_t index) const noexcept {
        return index < names_.size() ? std::string_view{names_[index]} : std::string_view{};
    }
    // nullptr for an index outside the table.
    [[nodiscard]] const DocumentType* type(std::uint32_t index) const noexcept {
        return index < types_.size() ? &types_[index] : nullptr;
    }

    // The bytes of one row's construction or persisted values; empty when the
    // row's range lies outside the arena.
    [[nodiscard]] std::span<const std::uint8_t> payload(const TraitRow& row, bool persisted) const noexcept;

    // Reads (or steps over) one value of `type`, failing the reader instead of
    // reading past its bytes or nesting past the configured depth.
    [[nodiscard]] Value read_value(std::uint32_t type, ValueReader& in, std::size_t depth = 0) const;
    void skip_value(std::uint32_t type, ValueReader& in, std::size_t depth = 0) const;

    friend bool operator==(const Snapshot&, const Snapshot&) = default;

private:
    friend class SnapshotBuilder;

    std::uint32_t schema_revision_    = kPersistenceSchemaRevision;
    std::uint64_t schema_fingerprint_ = 0;
    std::string module_;
    bool truncated_ = false;
    std::vector<std::string> names_;
    std::vector<DocumentType> types_;
    std::vector<std::vector<DocumentField>> structs_;
    std::vector<DocumentEnum> enums_;
    std::vector<DocumentTrait> traits_;
    std::vector<EntityRow> entities_;
    std::vector<TraitRow> trait_rows_;
    std::vector<std::uint8_t> arena_;
};

inline std::string_view ValueReader::get_name() {
    const auto index = get_index();
    if (failed_ || index >= document_->names().size()) {
        failed_ = true;
        return {};
    }
    return document_->name(index);
}

// Whether two documents' types (or trait layouts) store values identically.
[[nodiscard]] bool same_type(const Snapshot& left, std::uint32_t left_type, const Snapshot& right,
                             std::uint32_t right_type);
[[nodiscard]] bool same_trait_layout(const Snapshot& left, std::uint32_t left_shape, const Snapshot& right,
                                     std::uint32_t right_shape);

// Builds a Snapshot. Two levels share one document:
// - typed writes (tables, begin_*/put_*), used by generated capture and decoders;
// - name-based writes (add_*), used by tests and migrating adapters. These reuse
//   a recorded layout when the values fit it and record a new one otherwise.
class SnapshotBuilder {
public:
    SnapshotBuilder() = default;

    // Continues with `document`'s header and tables, but none of its rows.
    [[nodiscard]] static SnapshotBuilder with_tables_of(const Snapshot& document);
    // Starts from tables exactly as given, entry for entry.
    [[nodiscard]] static SnapshotBuilder with_tables(DocumentTables tables);

    void set_schema(std::uint32_t revision, std::uint64_t fingerprint) noexcept {
        document_.set_schema(revision, fingerprint);
    }
    void set_module(std::string module) { document_.set_module(std::move(module)); }
    void mark_truncated() noexcept { document_.set_truncated(true); }

    [[nodiscard]] Snapshot finish() && { return std::move(document_); }
    [[nodiscard]] const Snapshot& document() const noexcept { return document_; }

    // ── Tables ──
    [[nodiscard]] std::uint32_t name(std::string_view text);
    [[nodiscard]] std::uint32_t type(const DocumentType& type);
    [[nodiscard]] std::uint32_t struct_shape(std::vector<DocumentField> fields);
    // A struct layout filled in later, so a struct can reach itself through a list.
    [[nodiscard]] std::uint32_t declare_struct();
    void define_struct(std::uint32_t shape, std::vector<DocumentField> fields);
    // One entry per enum name; variants missing from it are appended.
    [[nodiscard]] std::uint32_t enumeration(std::string_view enum_name, std::span<const std::string_view> variants = {});
    [[nodiscard]] std::uint32_t trait_shape(std::string_view trait,
                                            std::vector<DocumentField> construction,
                                            std::vector<DocumentField> persisted);

    // ── Typed rows ──
    void reserve(std::size_t entities, std::size_t traits, std::size_t bytes);
    // Rows and bytes then grow at the rate the first entities used, instead of
    // doubling, so a large document holds little unused capacity.
    void expect_entities(std::size_t total) {
        expected_entities_ = total;
        const auto sample  = std::min(total, kReserveSample);
        reserve(total, sample * kSampleRowsPerEntity, sample * kSampleBytesPerEntity);
    }
    void begin_entity(DocumentId id, std::uint32_t archetype, EntityRef parent);
    void begin_trait(std::uint32_t shape, bool present = true);
    void begin_persisted();
    void end_trait();

    void put_bool(bool value) { put(static_cast<std::uint8_t>(value ? 1 : 0)); }
    void put_int(std::int32_t value) { put(value); }
    void put_float(float value) { put(value); }
    void put_index(std::uint32_t value) { put(value); }
    void put_entity(EntityRef value) {
        put_bool(value.present);
        put_index(value.id);
    }
    void put_lanes(std::span<const float> lanes) {
        for (const float lane : lanes) {
            put_float(lane);
        }
    }
    void put_name(std::string_view text) { put_index(name(text)); }
    void put_bytes(std::span<const std::uint8_t> bytes) {
        document_.arena_.insert(document_.arena_.end(), bytes.begin(), bytes.end());
    }
    // Appends a copy of bytes already written.
    void put_repeat(std::size_t begin, std::size_t size) {
        auto& arena    = document_.arena_;
        const auto end = arena.size();
        if (begin > end || end - begin < size) {
            return;
        }
        arena.resize(end + size);
        std::copy_n(arena.begin() + static_cast<std::ptrdiff_t>(begin), size,
                    arena.begin() + static_cast<std::ptrdiff_t>(end));
    }

    // ── Name-based rows ──
    void add_entity(DocumentId id, std::string_view archetype, EntityRef parent = {});
    void add_trait(std::string_view trait,
                   const std::vector<StructField>& construction = {},
                   const std::vector<StructField>& persisted    = {});
    void add_absent_trait(std::string_view trait);

private:
    static constexpr std::size_t kReserveSample        = 1024;
    static constexpr std::size_t kSampleRowsPerEntity  = 4;
    static constexpr std::size_t kSampleBytesPerEntity = 64;

    struct NameHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view text) const noexcept {
            return std::hash<std::string_view>{}(text);
        }
    };

    template <class T>
    void put(T value) {
        auto& arena    = document_.arena_;
        const auto end = arena.size();
        arena.resize(end + sizeof(T));
        std::memcpy(arena.data() + end, &value, sizeof(T));
    }

    [[nodiscard]] bool fits(const Value& value, std::uint32_t type) const;
    [[nodiscard]] bool fits_fields(const std::vector<StructField>& values,
                                   const std::vector<DocumentField>& layout) const;
    [[nodiscard]] std::uint32_t type_of(const Value& value);
    [[nodiscard]] std::vector<DocumentField> layout_of(const std::vector<StructField>& values);
    void put_value(const Value& value, std::uint32_t type);
    void put_fields(const std::vector<StructField>& values, const std::vector<DocumentField>& layout);

    void grow_toward_expected();

    Snapshot document_;
    std::size_t expected_entities_ = 0;
    bool split_ = false;  // begin_persisted() was called for the open trait
    std::unordered_map<std::string, std::uint32_t, NameHash, std::equal_to<>> names_;
};

// Which program trait each of a document's layouts stores, by position in
// `canonical_shapes` (layouts recorded in `canonical`), or kNoIndex. `complete`
// is false when some layout names a program trait but stores it differently.
struct LayoutMatch {
    std::vector<std::uint32_t> traits;
    bool complete = true;
};

[[nodiscard]] LayoutMatch match_trait_layouts(const Snapshot& document,
                                              const Snapshot& canonical,
                                              std::span<const std::uint32_t> canonical_shapes);

// Copies every record of `source` into `target` by name, so each trait whose
// values fit a layout `target` already records is stored in that layout.
[[nodiscard]] Snapshot relayout(const Snapshot& source, SnapshotBuilder target);

// ── Schema descriptor ───────────────────────────────────────────────────────
// What a document's values mean, generated per program as static constexpr data
// an adapter reads without allocating. Value types form one shared table so a
// list's element type is describable to any depth by index.

inline constexpr std::uint32_t kNoValueType = 0xFFFFFFFFU;

struct ValueTypeDescriptor {
    PersistenceValueKind kind      = PersistenceValueKind::Unsupported;
    std::uint8_t bit_width         = 0;
    std::uint8_t lanes             = 1;
    PersistenceValueKind lane_kind = PersistenceValueKind::Float;
    std::string_view declared_type;              // canonical enum/struct id, else empty
    std::uint32_t element = kNoValueType;        // a list's element type
};

struct FieldDescriptor {
    std::string_view name;
    std::uint32_t type = kNoValueType;  // index into SchemaDescriptor::value_types
    bool persist       = false;
};

struct TraitDescriptor {
    std::string_view trait;  // canonical declaration
    std::span<const FieldDescriptor> fields;
};

struct StructDescriptor {
    std::string_view name;
    std::span<const FieldDescriptor> fields;
};

struct EnumDescriptor {
    std::string_view name;
    std::span<const std::string_view> variants;  // declaration order: these are the values
};

struct ArchetypeDescriptor {
    std::string_view node;
    std::span<const std::string_view> baseline_traits;
    std::span<const std::string_view> parameters;
    // Every trait set an entity of this node can reach, each sorted and without
    // the schema's unchecked traits.
    std::span<const std::span<const std::string_view>> reachable_trait_sets;
};

struct SchemaDescriptor {
    std::uint32_t revision    = kPersistenceSchemaRevision;
    std::uint64_t fingerprint = 0;
    std::string_view module;
    std::span<const ValueTypeDescriptor> value_types;
    std::span<const TraitDescriptor> traits;
    std::span<const StructDescriptor> structs;
    std::span<const EnumDescriptor> enums;
    std::span<const ArchetypeDescriptor> archetypes;
    // Set when the program's reachable trait sets are known; restore then
    // rejects a record whose trait set is not one of its node's.
    bool checks_trait_sets = false;
    std::span<const std::string_view> unchecked_traits;  // sorted: free, state and Parent traits
    // Set when the program never reparents, so every parent was created before
    // its children; restore then rejects a child recorded before its parent.
    bool parents_first = false;
};

[[nodiscard]] const TraitDescriptor* find_trait_descriptor(const SchemaDescriptor& schema, std::string_view trait);
[[nodiscard]] const FieldDescriptor* find_field_descriptor(const TraitDescriptor& trait, std::string_view field);
[[nodiscard]] const StructDescriptor* find_struct_descriptor(const SchemaDescriptor& schema, std::string_view name);
[[nodiscard]] const EnumDescriptor* find_enum_descriptor(const SchemaDescriptor& schema, std::string_view name);
[[nodiscard]] const ArchetypeDescriptor* find_archetype_descriptor(const SchemaDescriptor& schema,
                                                                   std::string_view node);

// First-version compatibility is exact: a document is readable only by a build
// whose schema matches it in both revision and fingerprint.
[[nodiscard]] bool schema_accepts(const SchemaDescriptor& schema, const Snapshot& snapshot) noexcept;

}  // namespace cactus::persistence
