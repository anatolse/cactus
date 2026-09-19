// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/persistence_document.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

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
    slot.fields.push_back(FieldValue{.name = "count", .value = int_value(3)});
    slot.fields.push_back(FieldValue{.name = "kind", .value = Value::of_enum("game.Item", "Potion")});
    slot.fields.push_back(
        FieldValue{.name = "owner", .value = Value::of_entity(EntityRef{.id = 2, .present = true})});

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
    CHECK(first.as_struct().fields[1].value.as_enum().variant == "Potion");
    CHECK(first.as_struct().fields[2].value.as_entity().id == 2);
    CHECK(inventory.as_list().items[1].as_struct().fields.empty());
}

TEST_CASE("an unset value is absent rather than a defaulted zero", "[persistence][document][values]") {
    const Value unset;
    CHECK(unset.kind() == PersistenceValueKind::Unsupported);
    CHECK(unset.is_absent());
    CHECK_FALSE(int_value(0).is_absent());
}

// ── Document shape ──────────────────────────────────────────────────────────

TEST_CASE("a snapshot names its module and schema, and orders entities by creation",
          "[persistence][document][snapshot]") {
    Snapshot snapshot;
    snapshot.module            = "game";
    snapshot.schema_revision   = 1;
    snapshot.schema_fingerprint = 0xABCDEF;
    snapshot.entities.push_back(EntityRecord{.id = 1, .archetype = "game.Boss"});
    snapshot.entities.push_back(EntityRecord{.id = 2, .archetype = "game.Enemy"});

    CHECK(snapshot.module == "game");
    CHECK(snapshot.schema_fingerprint == 0xABCDEF);
    REQUIRE(snapshot.entities.size() == 2);
    CHECK(snapshot.entities[0].archetype == "game.Boss");
    CHECK(snapshot.entities[1].id == 2);
}

TEST_CASE("a record separates construction data from persisted values, and marks removed traits absent",
          "[persistence][document][snapshot]") {
    EntityRecord record{.id = 1, .archetype = "game.Boss"};
    record.parent = EntityRef{.id = 0, .present = false};

    TraitRecord health{.trait = "game.Health"};
    health.construction.push_back(FieldValue{.name = "maximum", .value = int_value(500)});
    health.persisted.push_back(FieldValue{.name = "current", .value = int_value(137)});
    record.traits.push_back(health);

    // A baseline trait that gameplay removed contributes membership information
    // and no field payloads.
    record.traits.push_back(TraitRecord{.trait = "game.Shield", .present = false});

    const auto* found = find_trait(record, "game.Health");
    REQUIRE(found != nullptr);
    CHECK(found->present);
    CHECK(found->construction[0].value.as_int() == 500);
    CHECK(found->persisted[0].value.as_int() == 137);

    const auto* removed = find_trait(record, "game.Shield");
    REQUIRE(removed != nullptr);
    CHECK_FALSE(removed->present);
    CHECK(removed->construction.empty());
    CHECK(removed->persisted.empty());

    CHECK(find_trait(record, "game.Missing") == nullptr);
    CHECK_FALSE(record.parent.present);
}

TEST_CASE("a marker trait is recorded as membership with no fields", "[persistence][document][snapshot]") {
    EntityRecord record{.id = 1, .archetype = "game.Enemy"};
    record.traits.push_back(TraitRecord{.trait = "game.Poisoned"});

    const auto* marker = find_trait(record, "game.Poisoned");
    REQUIRE(marker != nullptr);
    CHECK(marker->present);
    CHECK(marker->construction.empty());
    CHECK(marker->persisted.empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
