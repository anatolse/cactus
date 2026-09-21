#pragma once

#include "common/persistence_schema.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The format-independent world snapshot an adapter reads and writes. Compiles
// as C++20: generated projects include this, and they build one standard behind
// the compiler. Deliberately free of EnTT, raylib, and frontend types — an
// adapter sees declarations and document-local identities, never live handles.

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
struct FieldValue;

struct StructValue {
    std::vector<FieldValue> fields;

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

struct FieldValue {
    std::string name;
    Value value;

    friend bool operator==(const FieldValue&, const FieldValue&) = default;
};

// One trait incarnation on a captured entity. `present == false` records a
// baseline trait gameplay removed: membership information, no payloads.
struct TraitRecord {
    std::string trait;  // canonical trait declaration
    bool present = true;
    std::vector<FieldValue> construction;  // values evaluated once at creation
    std::vector<FieldValue> persisted;     // current values of persist fields

    friend bool operator==(const TraitRecord&, const TraitRecord&) = default;
};

struct EntityRecord {
    DocumentId id = 0;
    std::string archetype;  // canonical archetype node, "game.Tank/turret" for a role
    EntityRef parent;
    std::vector<TraitRecord> traits;

    friend bool operator==(const EntityRecord&, const EntityRecord&) = default;
};

// Per-collection and per-struct-nesting-level bounds capture enforces so one
// pathological field cannot grow a document or its call stack without bound.
// Both are configured constants rather than a program-specific setting: no
// mechanism exists yet for authors to raise or lower them.
inline constexpr std::size_t kMaxPersistenceCollectionSize = 4096;
inline constexpr std::size_t kMaxPersistenceNestingDepth   = 32;

struct Snapshot {
    std::uint32_t schema_revision     = kPersistenceSchemaRevision;
    std::uint64_t schema_fingerprint  = 0;
    std::string module;                  // active world/module context
    std::vector<EntityRecord> entities;  // relative creation order
    // Set when capture hit a collection or nesting limit: some persisted
    // values were truncated or omitted rather than growing without bound.
    bool truncated = false;

    friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

[[nodiscard]] const TraitRecord* find_trait(const EntityRecord& record, std::string_view trait);
[[nodiscard]] const FieldValue* find_field(const std::vector<FieldValue>& fields, std::string_view name);
[[nodiscard]] const EntityRecord* find_record(const Snapshot& snapshot, DocumentId id);

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
