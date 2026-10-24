// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/persistence_document.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace cactus::persistence;
using cactus::PersistenceValueKind;

namespace {

Value int_value(std::int32_t value) {
    return Value::of_int(value);
}

}  // namespace

// ── Scalars and exact numeric representation ────────────────────────────────

TEST_CASE("scalar values report their kind and round-trip exactly", "[persistence][document][values]") {
    CHECK(Value::of_bool(true).kind() == PersistenceValueKind::Bool);
    CHECK(Value::of_bool(true).as_bool());

    CHECK(int_value(42).kind() == PersistenceValueKind::Int);
    CHECK(int_value(42).as_int() == 42);

    CHECK(Value::of_float(0.5F).kind() == PersistenceValueKind::Float);
    CHECK(Value::of_float(0.5F).as_float() == Catch::Approx(0.5F));

    CHECK(Value::of_string("slot1").kind() == PersistenceValueKind::String);
    CHECK(Value::of_string("slot1").as_string() == "slot1");
}

TEST_CASE("numeric extremes survive without narrowing", "[persistence][document][values]") {
    constexpr auto LOWEST  = std::numeric_limits<std::int32_t>::lowest();
    constexpr auto HIGHEST = std::numeric_limits<std::int32_t>::max();
    CHECK(int_value(LOWEST).as_int() == LOWEST);
    CHECK(int_value(HIGHEST).as_int() == HIGHEST);

    // A float the backend stores exactly must come back with the same bits, not
    // rounded through a wider carrier.
    constexpr float AWKWARD = 0.1F;
    CHECK(Value::of_float(AWKWARD).as_float() == AWKWARD);
    CHECK(Value::of_float(std::numeric_limits<float>::lowest()).as_float() ==
          std::numeric_limits<float>::lowest());
}

TEST_CASE("vectors keep their lane count and lane kind", "[persistence][document][values]") {
    const auto position = Value::of_vector(PersistenceValueKind::Float, 2, {1.5F, -2.5F, 0.0F, 0.0F});
    REQUIRE(position.kind() == PersistenceValueKind::Vector);
    CHECK(position.as_vector().lanes == 2);
    CHECK(position.as_vector().lane_kind == PersistenceValueKind::Float);
    CHECK(position.as_vector().components[0] == Catch::Approx(1.5F));
    CHECK(position.as_vector().components[1] == Catch::Approx(-2.5F));

    // color is four bytes in the backend, and every byte value is exact here.
    const auto tint = Value::of_vector(PersistenceValueKind::Int, 4, {255.0F, 128.0F, 0.0F, 255.0F});
    CHECK(tint.as_vector().lane_kind == PersistenceValueKind::Int);
    CHECK(tint.as_vector().components[1] == Catch::Approx(128.0F));
}

// ── Declared identities ─────────────────────────────────────────────────────

TEST_CASE("enum values carry their canonical type and variant", "[persistence][document][values]") {
    const auto state = Value::of_enum("game.Stance", "Guarding");
    REQUIRE(state.kind() == PersistenceValueKind::Enum);
    CHECK(state.as_enum().type == "game.Stance");
    CHECK(state.as_enum().variant == "Guarding");
}

TEST_CASE("asset and input references name declarations, never runtime handles",
          "[persistence][document][values]") {
    const auto model = Value::of_asset("game.hero_model");
    REQUIRE(model.kind() == PersistenceValueKind::AssetRef);
    CHECK(model.as_declaration() == "game.hero_model");

    const auto jump = Value::of_input("game.Jump");
    REQUIRE(jump.kind() == PersistenceValueKind::InputRef);
    CHECK(jump.as_declaration() == "game.Jump");
}

// ── Entity references and absence ───────────────────────────────────────────

TEST_CASE("entity references use document-local identities", "[persistence][document][references]") {
    const auto target = Value::of_entity(EntityRef{.id = 7, .present = true});
    REQUIRE(target.kind() == PersistenceValueKind::EntityRef);
    CHECK(target.as_entity().id == 7);
    CHECK(target.as_entity().present);
}

TEST_CASE("repeated references to one absent target stay equal, and distinct targets stay distinct",
          "[persistence][document][references]") {
    const EntityRef first_absent{.id = 3, .present = false};
    const EntityRef same_absent{.id = 3, .present = false};
    const EntityRef other_absent{.id = 4, .present = false};

    CHECK(first_absent == same_absent);
    CHECK_FALSE(first_absent == other_absent);
    // An absent target is never confused with an included one that happens to
    // share an identity slot.
    CHECK_FALSE(first_absent == EntityRef{.id = 3, .present = true});
}


// ── Nesting ─────────────────────────────────────────────────────────────────

TEST_CASE("a list of structs keeps every nested value's type", "[persistence][document][nesting]") {
    StructValue slot;
    slot.fields.push_back(StructField{.name = "count", .value = int_value(3)});
    slot.fields.push_back(StructField{.name = "kind", .value = Value::of_enum("game.Item", "Potion")});
    slot.fields.push_back(
        StructField{.name = "owner", .value = Value::of_entity(EntityRef{.id = 2, .present = true})});

    ListValue slots;
    slots.items.push_back(Value::of_struct(slot));
    slots.items.push_back(Value::of_struct(StructValue{}));

    const auto inventory = Value::of_list(slots);
    REQUIRE(inventory.kind() == PersistenceValueKind::List);
    REQUIRE(inventory.as_list().items.size() == 2);

    const auto& first = inventory.as_list().items[0];
    REQUIRE(first.kind() == PersistenceValueKind::Struct);
    REQUIRE(first.as_struct().fields.size() == 3);
    CHECK(first.as_struct().fields[0].value.as_int() == 3);
    REQUIRE(first.as_struct().find("kind") != nullptr);
    CHECK(first.as_struct().find("kind")->as_enum().variant == "Potion");
    CHECK(first.as_struct().find("missing") == nullptr);
    CHECK(inventory.as_list().items[1].as_struct().fields.empty());
}

TEST_CASE("an unset value is absent rather than a defaulted zero", "[persistence][document][values]") {
    const Value unset;
    CHECK(unset.kind() == PersistenceValueKind::Unsupported);
    CHECK(unset.is_absent());
    CHECK_FALSE(int_value(0).is_absent());
}

// ── Document shape ──────────────────────────────────────────────────────────

namespace {

Value sample_slots() {
    StructValue slot;
    slot.fields.push_back(StructField{.name = "count", .value = int_value(3)});
    slot.fields.push_back(StructField{.name = "kind", .value = Value::of_enum("game.Item", "Potion")});
    slot.fields.push_back(
        StructField{.name = "owner", .value = Value::of_entity(EntityRef{.id = 2, .present = true})});
    ListValue slots;
    slots.items.push_back(Value::of_struct(slot));
    slots.items.push_back(Value::of_struct(StructValue{}));
    return Value::of_list(std::move(slots));
}

std::vector<StructField> every_kind() {
    ListValue numbers;
    numbers.items = {int_value(1), int_value(-2), int_value(3)};
    return {
        {.name = "flag", .value = Value::of_bool(true)},
        {.name = "count", .value = int_value(std::numeric_limits<std::int32_t>::lowest())},
        {.name = "ratio", .value = Value::of_float(0.1F)},
        {.name = "label", .value = Value::of_string("slot1")},
        {.name = "position", .value = Value::of_vector(PersistenceValueKind::Float, 2, {1.5F, -2.5F, 0.0F, 0.0F})},
        {.name = "heading", .value = Value::of_vector(PersistenceValueKind::Float, 3, {1.0F, 2.0F, 3.0F, 0.0F})},
        {.name = "spin", .value = Value::of_vector(PersistenceValueKind::Float, 4, {0.0F, 0.0F, 0.0F, 1.0F})},
        {.name = "tint", .value = Value::of_vector(PersistenceValueKind::Int, 4, {255.0F, 128.0F, 0.0F, 255.0F})},
        {.name = "target", .value = Value::of_entity(EntityRef{.id = 7, .present = true})},
        {.name = "lost", .value = Value::of_entity(EntityRef{.id = 3, .present = false})},
        {.name = "stance", .value = Value::of_enum("game.Stance", "Guarding")},
        {.name = "model", .value = Value::of_asset("game.hero_model")},
        {.name = "jump", .value = Value::of_input("game.Jump")},
        {.name = "slots", .value = sample_slots()},
        {.name = "numbers", .value = Value::of_list(numbers)},
        {.name = "nothing", .value = Value::of_list(ListValue{})},
        {.name = "unset", .value = Value{}},
    };
}

}  // namespace

TEST_CASE("a snapshot names its module and schema, and orders entities by creation",
          "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    builder.set_module("game");
    builder.set_schema(1, 0xABCDEF);
    builder.add_entity(1, "game.Boss");
    builder.add_entity(2, "game.Enemy", EntityRef{.id = 1, .present = true});
    const auto snapshot = std::move(builder).finish();

    CHECK(snapshot.module() == "game");
    CHECK(snapshot.schema_revision() == 1);
    CHECK(snapshot.schema_fingerprint() == 0xABCDEF);
    REQUIRE(snapshot.entity_count() == 2);
    CHECK(snapshot.entity(0).archetype() == "game.Boss");
    CHECK(snapshot.entity(1).id() == 2);
    CHECK(snapshot.entity(1).parent() == EntityRef{.id = 1, .present = true});
    CHECK_FALSE(snapshot.entity(0).parent().present);

    std::vector<DocumentId> ids;
    for (const auto entity : snapshot.entities()) {
        ids.push_back(entity.id());
    }
    CHECK(ids == std::vector<DocumentId>{1, 2});
    REQUIRE(snapshot.find_entity(2).has_value());
    CHECK(snapshot.find_entity(2)->archetype() == "game.Enemy");
    CHECK_FALSE(snapshot.find_entity(9).has_value());
}

TEST_CASE("every value kind round-trips through the compact document", "[persistence][document][snapshot]") {
    const auto fields = every_kind();
    SnapshotBuilder builder;
    builder.add_entity(1, "game.Hero");
    builder.add_trait("game.Everything", {}, fields);
    const auto snapshot = std::move(builder).finish();

    const auto trait = snapshot.entity(0).trait("game.Everything");
    REQUIRE(trait.has_value());
    for (const auto& field : fields) {
        INFO("field " << field.name);
        const auto value = trait->field(field.name);
        REQUIRE(value.has_value());
        CHECK(*value == field.value);
    }
    CHECK(trait->persisted() == fields);
    CHECK(trait->construction().empty());
    CHECK_FALSE(trait->field("missing").has_value());
}

TEST_CASE("a record separates construction data from persisted values, and marks removed traits absent",
          "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    builder.add_entity(1, "game.Boss");
    builder.add_trait("game.Health", {{.name = "maximum", .value = int_value(500)}},
                      {{.name = "current", .value = int_value(137)}});
    // A baseline trait that gameplay removed contributes membership information
    // and no field payloads.
    builder.add_absent_trait("game.Shield");
    const auto snapshot = std::move(builder).finish();
    const auto record   = snapshot.entity(0);

    const auto health = record.trait("game.Health");
    REQUIRE(health.has_value());
    CHECK(health->present());
    CHECK(health->construction_field("maximum") == int_value(500));
    CHECK(health->persisted_field("current") == int_value(137));
    CHECK_FALSE(health->persisted_field("maximum").has_value());

    const auto removed = record.trait("game.Shield");
    REQUIRE(removed.has_value());
    CHECK_FALSE(removed->present());
    CHECK(removed->construction().empty());
    CHECK(removed->persisted().empty());

    CHECK_FALSE(record.trait("game.Missing").has_value());
    REQUIRE(record.trait_count() == 2);
    CHECK(record.trait_at(1).name() == "game.Shield");
}

TEST_CASE("a marker trait is recorded as membership with no fields", "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    builder.add_entity(1, "game.Enemy");
    builder.add_trait("game.Poisoned");
    const auto snapshot = std::move(builder).finish();

    const auto marker = snapshot.entity(0).trait("game.Poisoned");
    REQUIRE(marker.has_value());
    CHECK(marker->present());
    CHECK(marker->construction().empty());
    CHECK(marker->persisted().empty());
}

TEST_CASE("names and trait layouts are stored once per document", "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    for (DocumentId id = 1; id <= 3; ++id) {
        builder.add_entity(id, "game.Guard");
        builder.add_trait("game.Health", {{.name = "maximum", .value = int_value(100)}},
                          {{.name = "current", .value = int_value(static_cast<std::int32_t>(id))}});
        builder.add_trait("game.Stance", {},
                          {{.name = "stance", .value = Value::of_enum("game.Stance", "Guarding")},
                           {.name = "model", .value = Value::of_asset("game.guard_model")}});
    }
    const auto snapshot = std::move(builder).finish();

    for (const std::string_view name :
         {"game.Guard", "game.Health", "maximum", "current", "game.Stance", "Guarding", "game.guard_model"}) {
        INFO("name " << name);
        CHECK(std::ranges::count(snapshot.names(), name) == 1);
    }
    CHECK(snapshot.trait_shapes().size() == 2);
    CHECK(snapshot.entity_rows().size() == 3);
    CHECK(snapshot.trait_rows().size() == 6);
    CHECK(snapshot.entity(2).trait("game.Health")->field("current") == int_value(3));
}

TEST_CASE("a builder seeded with another document's tables reuses its layouts", "[persistence][document][snapshot]") {
    SnapshotBuilder first;
    first.add_entity(1, "game.Guard");
    first.add_trait("game.Health", {{.name = "maximum", .value = int_value(100)}},
                    {{.name = "current", .value = int_value(40)}});
    const auto original = std::move(first).finish();

    auto again = SnapshotBuilder::with_tables_of(original);
    again.add_entity(5, "game.Guard");
    again.add_trait("game.Health", {{.name = "maximum", .value = int_value(90)}},
                    {{.name = "current", .value = int_value(10)}});
    const auto copy = std::move(again).finish();

    CHECK(std::ranges::equal(copy.trait_shapes(), original.trait_shapes()));
    CHECK(copy.names().size() == original.names().size());
    CHECK(copy.entity(0).trait("game.Health")->field("maximum") == int_value(90));
}

TEST_CASE("a trait whose values disagree with its layout gets its own layout", "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    builder.add_entity(1, "game.Guard");
    builder.add_trait("game.Health", {}, {{.name = "current", .value = int_value(1)}});
    builder.add_entity(2, "game.Guard");
    builder.add_trait("game.Health", {}, {{.name = "current", .value = Value::of_string("broken")}});
    builder.add_entity(3, "game.Guard");
    builder.add_trait("game.Health", {}, {});
    const auto snapshot = std::move(builder).finish();

    CHECK(snapshot.trait_shapes().size() == 3);
    CHECK(snapshot.entity(1).trait("game.Health")->field("current") == Value::of_string("broken"));
    CHECK_FALSE(snapshot.entity(2).trait("game.Health")->field("current").has_value());
}

TEST_CASE("typed writes through the builder read back through views", "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    const auto int_type    = builder.type({.kind = PersistenceValueKind::Int});
    const auto vector_type = builder.type({.kind = PersistenceValueKind::Vector, .lanes = 3});
    const auto shape       = builder.trait_shape("game.Body", {{.name = builder.name("mass"), .type = int_type}},
                                                 {{.name = builder.name("velocity"), .type = vector_type}});
    builder.begin_entity(4, builder.name("game.Ball"), EntityRef{});
    builder.begin_trait(shape);
    builder.put_int(12);
    builder.begin_persisted();
    builder.put_lanes(std::array<float, 3>{1.0F, 2.0F, 3.0F});
    builder.end_trait();
    const auto snapshot = std::move(builder).finish();

    const auto body = snapshot.entity(0).trait("game.Body");
    REQUIRE(body.has_value());
    CHECK(body->construction_field("mass") == int_value(12));
    CHECK(body->persisted_field("velocity") ==
          Value::of_vector(PersistenceValueKind::Float, 3, {1.0F, 2.0F, 3.0F, 0.0F}));

    auto reader = body->reader();
    CHECK(reader.get_int() == 12);
    CHECK(reader.get_float() == 1.0F);
    CHECK_FALSE(reader.failed());
}

TEST_CASE("reads past a truncated payload fail safely", "[persistence][document][snapshot]") {
    SnapshotBuilder builder;
    const auto int_type = builder.type({.kind = PersistenceValueKind::Int});
    const auto shape    = builder.trait_shape("game.Health", {}, {{.name = builder.name("current"), .type = int_type}});
    builder.begin_entity(1, builder.name("game.Boss"), EntityRef{});
    builder.begin_trait(shape);
    builder.begin_persisted();
    builder.put_bool(true);  // one byte where four belong
    builder.end_trait();
    const auto snapshot = std::move(builder).finish();

    const auto health = snapshot.entity(0).trait("game.Health");
    REQUIRE(health.has_value());
    const auto current = health->field("current");
    REQUIRE(current.has_value());
    CHECK(current->is_absent());

    auto reader = health->reader();
    CHECK(reader.get_int() == 0);
    CHECK(reader.failed());
    CHECK(reader.get_index() == 0);
    CHECK(reader.failed());
}
TEST_CASE("layouts are matched to a program's own and copied into them by name", "[persistence][document][layout]") {
    SnapshotBuilder canonical;
    const auto int_type     = canonical.type({.kind = PersistenceValueKind::Int});
    const std::uint32_t own = canonical.trait_shape("game.Health", {{.name = canonical.name("maximum"), .type = int_type}},
                                                    {{.name = canonical.name("current"), .type = int_type},
                                                     {.name = canonical.name("armor"), .type = int_type}});
    const std::vector<std::uint32_t> shapes{own};

    SnapshotBuilder same = SnapshotBuilder::with_tables_of(canonical.document());
    same.add_entity(1, "game.Boss");
    same.add_trait("game.Health", {{.name = "maximum", .value = int_value(9)}},
                   {{.name = "armor", .value = int_value(2)}, {.name = "current", .value = int_value(5)}});
    same.add_trait("game.Unknown");
    const auto matching = match_trait_layouts(same.document(), canonical.document(), shapes);
    CHECK(matching.complete);
    CHECK(matching.traits[own] == 0);
    CHECK(matching.traits.back() == kNoIndex);

    SnapshotBuilder other;
    other.add_entity(1, "game.Boss");
    other.add_trait("game.Health", {{.name = "maximum", .value = int_value(9)}},
                    {{.name = "armor", .value = int_value(2)}, {.name = "current", .value = int_value(5)}});
    other.add_absent_trait("game.Shield");
    const auto written = std::move(other).finish();
    CHECK_FALSE(match_trait_layouts(written, canonical.document(), shapes).complete);

    const auto relaid = relayout(written, SnapshotBuilder::with_tables_of(canonical.document()));
    const auto again  = match_trait_layouts(relaid, relaid, shapes);
    CHECK(again.complete);
    const auto health = relaid.entity(0).trait("game.Health");
    REQUIRE(health.has_value());
    CHECK(health->shape() == own);
    CHECK(health->persisted_field("current") == int_value(5));
    CHECK(health->persisted_field("armor") == int_value(2));
    CHECK_FALSE(relaid.entity(0).trait("game.Shield")->present());
}

TEST_CASE("layout comparison looks through enums, structs and lists", "[persistence][document][layout]") {
    const auto document_with = [](const Value& value) {
        SnapshotBuilder builder;
        builder.add_entity(1, "game.Hero");
        builder.add_trait("game.Gear", {}, {{.name = "item", .value = value}});
        return std::move(builder).finish();
    };
    StructValue slot;
    slot.fields.push_back(StructField{.name = "kind", .value = Value::of_enum("game.Item", "Potion")});
    ListValue slots;
    slots.items.push_back(Value::of_struct(slot));
    const auto first  = document_with(Value::of_list(slots));
    const auto second = document_with(Value::of_list(slots));
    CHECK(same_trait_layout(first, 0, second, 0));

    StructValue renamed;
    renamed.fields.push_back(StructField{.name = "kind", .value = Value::of_enum("game.Tool", "Potion")});
    ListValue other_slots;
    other_slots.items.push_back(Value::of_struct(renamed));
    CHECK_FALSE(same_trait_layout(first, 0, document_with(Value::of_list(other_slots)), 0));
    CHECK_FALSE(same_trait_layout(first, 0, document_with(int_value(1)), 0));
    CHECK_FALSE(same_trait_layout(first, 0, second, 7));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
