// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <unordered_set>

using namespace cactus;

TEST_CASE("symbol identity: canonical string is derived from typed identity", "[symbol-identity]") {
    const SymbolId body{
        .kind = SymbolKind::Trait, .module = ModuleId{.name = "std.physics.flat"}, .local_name = "Body"};

    CHECK(symbol_kind_name(body.kind) == std::string("trait"));
    CHECK(canonical_string(body) == "std.physics.flat.Body");
    CHECK(make_canonical_id(body) == "std.physics.flat.Body");
    CHECK(make_canonical_id(body.module, body.local_name) == "std.physics.flat.Body");
}

TEST_CASE("symbol identity: equality and hash include kind, module, and local name", "[symbol-identity]") {
    const SymbolId flat_trait{
        .kind = SymbolKind::Trait, .module = ModuleId{.name = "std.transform.flat"}, .local_name = "WorldTransform"};
    const SymbolId flat_trait_copy{
        .kind = SymbolKind::Trait, .module = ModuleId{.name = "std.transform.flat"}, .local_name = "WorldTransform"};
    const SymbolId volume_trait{
        .kind = SymbolKind::Trait, .module = ModuleId{.name = "std.transform.volume"}, .local_name = "WorldTransform"};
    const SymbolId flat_struct{
        .kind = SymbolKind::Struct, .module = ModuleId{.name = "std.transform.flat"}, .local_name = "WorldTransform"};

    CHECK(flat_trait == flat_trait_copy);
    CHECK(flat_trait != volume_trait);
    CHECK(flat_trait != flat_struct);

    std::unordered_set<SymbolId> symbols;
    symbols.insert(flat_trait);
    symbols.insert(flat_trait_copy);
    symbols.insert(volume_trait);
    symbols.insert(flat_struct);

    CHECK(symbols.size() == 3);
    CHECK(symbols.contains(flat_trait));
    CHECK(symbols.contains(volume_trait));
    CHECK(symbols.contains(flat_struct));
}

TEST_CASE("symbol identity: phase is distinct from event with the same canonical spelling",
          "[symbol-identity][phase]") {
    const auto event = make_symbol_id(SymbolKind::Event, "std.core", "tick");
    const auto phase = make_symbol_id(SymbolKind::Phase, "std.core", "tick");

    CHECK(symbol_kind_name(event.kind) == std::string("event"));
    CHECK(symbol_kind_name(phase.kind) == std::string("phase"));
    CHECK(canonical_string(event) == "std.core.tick");
    CHECK(canonical_string(phase) == "std.core.tick");
    CHECK(event != phase);

    std::unordered_set<SymbolId> symbols{event, phase};
    CHECK(symbols.size() == 2);
    CHECK(symbols.contains(event));
    CHECK(symbols.contains(phase));
}

TEST_CASE("symbol identity: C++ identifiers derive from module and local name", "[symbol-identity]") {
    const SymbolId flat_transform{
        .kind = SymbolKind::Trait, .module = ModuleId{.name = "std.transform.flat"}, .local_name = "WorldTransform"};
    const SymbolId volume_transform{
        .kind = SymbolKind::Trait, .module = ModuleId{.name = "std.transform.volume"}, .local_name = "WorldTransform"};

    CHECK(cpp_identifier(flat_transform) == "std_transform_flat__WorldTransform");
    CHECK(canonical_to_cpp_name(flat_transform) == "std_transform_flat__WorldTransform");
    CHECK(cpp_identifier(volume_transform) == "std_transform_volume__WorldTransform");
    CHECK(cpp_identifier(flat_transform) != cpp_identifier(volume_transform));
}

TEST_CASE("symbol identity: string helpers derive from explicit modules", "[symbol-identity]") {
    CHECK(make_canonical_id("game.components", "Position") == "game.components.Position");
    CHECK(canonical_to_cpp_name("game.components", "Position") == "game_components__Position");
}

TEST_CASE("symbol identity: empty module identities are rejected", "[symbol-identity]") {
    CHECK_THROWS_AS(make_symbol_id(SymbolKind::Trait, "", "Position"), std::invalid_argument);
    CHECK_THROWS_AS(make_canonical_id("", "Position"), std::invalid_argument);
    // cpp_identifier / canonical_to_cpp_name permit empty module for legacy/test scenarios:
    // returns the local name unchanged rather than throwing.
    CHECK(canonical_to_cpp_name("", "Position") == "Position");
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)