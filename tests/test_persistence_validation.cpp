// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/persistence_validation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace cactus::persistence;
using cactus::PersistenceValueKind;

namespace {

// value types: 0 int, 1 vec3, 2 Stance, 3 Slot, 4 list[int], 5 list[Slot], 6 Missing struct, 7 lists to any depth
constexpr std::array<ValueTypeDescriptor, 8> kTypes{{
    {.kind = PersistenceValueKind::Int, .bit_width = 32},
    {.kind = PersistenceValueKind::Vector, .bit_width = 32, .lanes = 3},
    {.kind = PersistenceValueKind::Enum, .declared_type = "game.Stance"},
    {.kind = PersistenceValueKind::Struct, .declared_type = "game.Slot"},
    {.kind = PersistenceValueKind::List, .element = 0},
    {.kind = PersistenceValueKind::List, .element = 3},
    {.kind = PersistenceValueKind::Struct, .declared_type = "game.Missing"},
    {.kind = PersistenceValueKind::List, .element = 7},
}};

constexpr std::array<FieldDescriptor, 2> kSlotFields{{
    {.name = "count", .type = 0},
    {.name = "stance", .type = 2},
}};

constexpr std::array<FieldDescriptor, 6> kGearFields{{
    {.name = "level", .type = 0},
    {.name = "aim", .type = 1, .persist = true},
    {.name = "stance", .type = 2, .persist = true},
    {.name = "slot", .type = 3, .persist = true},
    {.name = "tags", .type = 4, .persist = true},
    {.name = "slots", .type = 5, .persist = true},
}};

constexpr std::array<FieldDescriptor, 2> kOddFields{{
    {.name = "nest", .type = 7, .persist = true},
    {.name = "thing", .type = 6, .persist = true},
}};

constexpr std::array<std::string_view, 2> kStanceVariants{"Guarding", "Charging"};
constexpr std::array<EnumDescriptor, 1> kEnums{{{.name = "game.Stance", .variants = kStanceVariants}}};
constexpr std::array<StructDescriptor, 1> kStructs{{{.name = "game.Slot", .fields = kSlotFields}}};
constexpr std::array<TraitDescriptor, 2> kTraits{{
    {.trait = "game.Gear", .fields = kGearFields},
    {.trait = "game.Odd", .fields = kOddFields},
}};
constexpr std::array<std::string_view, 1> kBaseline{"game.Gear"};
constexpr std::array<ArchetypeDescriptor, 1> kArchetypes{{{.node = "game.Hero", .baseline_traits = kBaseline}}};

constexpr SchemaDescriptor kSchema{.fingerprint = 7,
                                   .module      = "game",
                                   .value_types = kTypes,
                                   .traits      = kTraits,
                                   .structs     = kStructs,
                                   .enums       = kEnums,
                                   .archetypes  = kArchetypes};

Value slot(int count, std::string variant) {
    StructValue value;
    value.fields.push_back({.name = "count", .value = Value::of_int(count)});
    value.fields.push_back({.name = "stance", .value = Value::of_enum("game.Stance", std::move(variant))});
    return Value::of_struct(std::move(value));
}

Value list_of(std::vector<Value> items) {
    ListValue list;
    list.items = std::move(items);
    return Value::of_list(std::move(list));
}

std::vector<StructField> gear_persisted() {
    return {
        {.name = "aim", .value = Value::of_vector(PersistenceValueKind::Float, 3, {1.0F, 2.0F, 3.0F, 0.0F})},
        {.name = "stance", .value = Value::of_enum("game.Stance", "Charging")},
        {.name = "slot", .value = slot(1, "Guarding")},
        {.name = "tags", .value = list_of({Value::of_int(1), Value::of_int(2)})},
        {.name = "slots", .value = list_of({slot(2, "Guarding")})},
    };
}

SnapshotBuilder hero_builder() {
    SnapshotBuilder builder;
    builder.set_schema(kSchema.revision, kSchema.fingerprint);
    builder.add_entity(1, "game.Hero");
    return builder;
}

Snapshot hero_with(const std::vector<StructField>& construction, const std::vector<StructField>& persisted) {
    auto builder = hero_builder();
    builder.add_trait("game.Gear", construction, persisted);
    return std::move(builder).finish();
}

std::vector<StructField> replaced(std::vector<StructField> fields, std::string_view name, Value value) {
    std::ranges::find(fields, name, &StructField::name)->value = std::move(value);
    return fields;
}

bool has(const std::vector<ValidationError>& errors, std::string_view path, std::string_view message) {
    return std::ranges::any_of(errors, [&](const ValidationError& error) {
        return error.field_path == path && error.message.starts_with(message);
    });
}

const std::vector<StructField> kLevel{{.name = "level", .value = Value::of_int(3)}};

}  // namespace

TEST_CASE("a document matching the schema validates cleanly", "[persistence][validation][unit]") {
    CHECK(validate_document(kSchema, hero_with(kLevel, gear_persisted())).empty());
}

TEST_CASE("unknown fields and fields in the wrong bucket are reported", "[persistence][validation][unit]") {
    auto persisted = gear_persisted();
    persisted.push_back({.name = "level", .value = Value::of_int(3)});
    persisted.push_back({.name = "extra", .value = Value::of_int(4)});
    const auto errors = validate_document(kSchema, hero_with({}, persisted));
    CHECK(has(errors, "game.Hero[1].game.Gear.extra", "unknown field"));
    CHECK(has(errors, "game.Hero[1].game.Gear.level", "missing required field"));
    CHECK_FALSE(has(errors, "game.Hero[1].game.Gear.level", "unknown field"));
}

TEST_CASE("vector lanes, enum variants and enum types are checked", "[persistence][validation][unit]") {
    auto persisted = replaced(gear_persisted(), "aim", Value::of_vector(PersistenceValueKind::Float, 2, {}));
    persisted      = replaced(persisted, "stance", Value::of_enum("game.Stance", "Sleeping"));
    persisted      = replaced(persisted, "slot", slot(1, "Flying"));
    const auto errors = validate_document(kSchema, hero_with(kLevel, persisted));
    CHECK(has(errors, "game.Hero[1].game.Gear.aim", "vector lane count"));
    CHECK(has(errors, "game.Hero[1].game.Gear.stance", "unknown enum variant 'Sleeping'"));
    CHECK(has(errors, "game.Hero[1].game.Gear.slot.stance", "unknown enum variant 'Flying'"));
    CHECK(std::ranges::all_of(errors, [](const ValidationError& error) {
        return error.category == ValidationErrorCategory::UnsupportedValue;
    }));

    const auto foreign = validate_document(
        kSchema, hero_with(kLevel, replaced(gear_persisted(), "stance", Value::of_enum("game.Mood", "Calm"))));
    CHECK(has(foreign, "game.Hero[1].game.Gear.stance", "unknown enum type 'game.Mood'"));
}

TEST_CASE("struct fields and list items are checked by path", "[persistence][validation][unit]") {
    StructValue partial;
    partial.fields.push_back({.name = "count", .value = Value::of_int(1)});
    partial.fields.push_back({.name = "bonus", .value = Value::of_int(1)});
    auto persisted = replaced(gear_persisted(), "slot", Value::of_struct(partial));
    persisted      = replaced(persisted, "tags", list_of({Value::of_int(1), Value::of_string("two")}));
    persisted      = replaced(persisted, "slots", list_of({slot(1, "Guarding"), Value::of_int(5)}));
    const auto errors = validate_document(kSchema, hero_with(kLevel, persisted));
    CHECK(has(errors, "game.Hero[1].game.Gear.slot.stance", "missing required field"));
    CHECK(has(errors, "game.Hero[1].game.Gear.tags[1]", "wrong value kind"));
    CHECK(has(errors, "game.Hero[1].game.Gear.slots[1]", "wrong value kind"));
}

TEST_CASE("a struct type the schema does not declare is reported", "[persistence][validation][unit]") {
    auto builder = hero_builder();
    builder.add_trait("game.Gear", kLevel, gear_persisted());
    StructValue thing;
    thing.fields.push_back({.name = "x", .value = Value::of_int(1)});
    builder.add_trait("game.Odd", {}, {{.name = "thing", .value = Value::of_struct(thing)}});
    const auto errors = validate_document(kSchema, std::move(builder).finish());
    CHECK(has(errors, "game.Hero[1].game.Odd.thing", "unknown struct type 'game.Missing'"));
}

TEST_CASE("values nested past the configured depth are reported", "[persistence][validation][unit]") {
    auto deep = list_of({});
    for (std::size_t level = 0; level < kMaxPersistenceNestingDepth + 2; ++level) {
        deep = list_of({deep});
    }
    auto builder = hero_builder();
    builder.add_trait("game.Gear", kLevel, gear_persisted());
    builder.add_trait("game.Odd", {}, {{.name = "nest", .value = deep}});
    const auto errors = validate_document(kSchema, std::move(builder).finish());
    CHECK(std::ranges::any_of(errors, [](const ValidationError& error) {
        return error.message == "exceeds configured nesting depth" && error.field_path.starts_with("game.Hero[1].game.Odd.nest[0]");
    }));
}

TEST_CASE("malformed rows are reported instead of being read", "[persistence][validation][unit]") {
    auto builder        = hero_builder();
    const auto int_type = builder.type({.kind = PersistenceValueKind::Int});
    const auto shape    = builder.trait_shape("game.Gear", {{.name = builder.name("level"), .type = int_type}}, {});
    builder.begin_trait(shape);
    builder.put_int(3);
    builder.put_int(4);  // one value too many
    builder.end_trait();
    builder.begin_trait(kNoShape - 1);
    builder.end_trait();
    builder.add_entity(2, "game.Hero", EntityRef{.id = 40, .present = true});
    builder.add_trait("game.Gear", kLevel, gear_persisted());
    builder.add_trait("game.Gear", kLevel, gear_persisted());
    builder.add_entity(3, "game.Villain");
    const auto errors = validate_document(kSchema, std::move(builder).finish());
    CHECK(has(errors, "game.Hero[1].game.Gear", "value data has unexpected trailing bytes"));
    CHECK(has(errors, "game.Hero[1]", "trait row names no recorded trait layout"));
    CHECK(has(errors, "game.Hero[2].parent", "reference names a document identity"));
    CHECK(has(errors, "game.Hero[2].game.Gear", "trait recorded more than once"));
    CHECK(has(errors, "game.Villain[3]", "unknown archetype"));
}

TEST_CASE("a reachable trait set check honors unchecked traits and absent rows", "[persistence][validation][unit]") {
    static constexpr std::array<std::string_view, 1> kGearOnly{"game.Gear"};
    static constexpr std::array<std::span<const std::string_view>, 1> kSets{kGearOnly};
    static constexpr std::array<ArchetypeDescriptor, 1> kReachable{
        {{.node = "game.Hero", .baseline_traits = kBaseline, .reachable_trait_sets = kSets}}};
    static constexpr std::array<std::string_view, 1> kUnchecked{"game.Odd"};
    auto schema              = kSchema;
    schema.archetypes        = kReachable;
    schema.checks_trait_sets = true;
    schema.unchecked_traits  = kUnchecked;

    auto builder = hero_builder();
    builder.add_trait("game.Gear", kLevel, gear_persisted());
    builder.add_absent_trait("game.Odd");
    CHECK(validate_document(schema, std::move(builder).finish()).empty());

    auto missing = hero_builder();
    missing.add_absent_trait("game.Gear");
    CHECK(has(validate_document(schema, std::move(missing).finish()), "game.Hero[1]", "illegal trait combination"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
