// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "frontend/symbol_identity.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

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

TEST_CASE("symbol identity: string helpers derive from explicit modules", "[symbol-identity]") {
    CHECK(make_canonical_id("game.components", "Position") == "game.components.Position");
}

TEST_CASE("symbol identity: empty module identities are rejected", "[symbol-identity]") {
    CHECK_THROWS_AS(make_symbol_id(SymbolKind::Trait, "", "Position"), std::invalid_argument);
    CHECK_THROWS_AS(make_canonical_id("", "Position"), std::invalid_argument);
}

TEST_CASE("symbol identity: generated names mirror module namespaces", "[symbol-identity][cpp-names]") {
    CHECK(cpp_namespace_path(ModuleId{.name = "game"}) == "cactus_gen::game");
    CHECK(cpp_namespace_path("game.components") == "cactus_gen::game::components");
    CHECK(cpp_namespace_path("std.camera.volume") == "cactus_gen::stdlib::camera::volume");
    CHECK(cpp_namespace_path("") == "cactus_gen");
    CHECK(cpp_namespace_segments("std.camera.volume") == std::vector<std::string>{"stdlib", "camera", "volume"});
    CHECK(cpp_namespace_segments("").empty());

    const auto session = make_symbol_id(SymbolKind::Trait, "game", "Session");
    CHECK(cpp_decl_name(session) == "Session");
    CHECK(cpp_type_ref(session) == "::cactus_gen::game::Session");

    const auto camera = make_symbol_id(SymbolKind::Trait, "std.camera.volume", "Camera");
    CHECK(cpp_type_ref(camera) == "::cactus_gen::stdlib::camera::volume::Camera");

    const auto flat   = make_symbol_id(SymbolKind::Trait, "std.transform.flat", "WorldTransform");
    const auto volume = make_symbol_id(SymbolKind::Trait, "std.transform.volume", "WorldTransform");
    CHECK(cpp_type_ref(flat) != cpp_type_ref(volume));
}

TEST_CASE("symbol identity: events and phases keep kind suffixes", "[symbol-identity][cpp-names]") {
    const auto reset = make_symbol_id(SymbolKind::Event, "game", "Reset");
    CHECK(cpp_decl_name(reset) == "ResetEvent");
    CHECK(cpp_type_ref(reset) == "::cactus_gen::game::ResetEvent");
    CHECK(cpp_decl_name(make_symbol_id(SymbolKind::Event, "std.core", "frame")) == "frameEvent");

    const auto fixed_tick = make_symbol_id(SymbolKind::Phase, "std.core", "fixed_tick");
    CHECK(cpp_decl_name(fixed_tick) == "FixedTickPhase");
    CHECK(cpp_type_ref(fixed_tick) == "::cactus_gen::stdlib::core::FixedTickPhase");
    CHECK(cpp_decl_name(make_symbol_id(SymbolKind::Phase, "std.core", "tick")) == "TickPhase");
}

TEST_CASE("symbol identity: state variants nest in their slot", "[symbol-identity][cpp-names]") {
    const auto state = make_symbol_id(SymbolKind::State, "game", "EnemyMode");
    CHECK(cpp_type_ref(state_slot_trait(state)) == "::cactus_gen::game::EnemyMode");
    CHECK(cpp_type_ref(state_variant_trait(state, "Idle")) == "::cactus_gen::game::EnemyMode::Idle");
    CHECK(cpp_decl_name(state_variant_trait(state, "Idle")) == "EnemyMode::Idle");
}

TEST_CASE("symbol identity: reserved names get a trailing underscore", "[symbol-identity][cpp-names]") {
    CHECK(cpp_escaped("default") == "default_");
    CHECK(cpp_escaped("class") == "class_");
    CHECK(cpp_escaped("co_await") == "co_await_");
    CHECK(cpp_escaped("PI") == "PI_");
    CHECK(cpp_escaped("DEG2RAD") == "DEG2RAD_");
    CHECK(cpp_escaped("RED") == "RED_");
    CHECK(cpp_escaped("std") == "std_");
    CHECK(cpp_escaped("Vector3") == "Vector3_");
    CHECK(cpp_escaped("Session") == "Session");
    CHECK(cpp_escaped("speed") == "speed");

    CHECK(cpp_type_ref(make_symbol_id(SymbolKind::Const, "std.math", "PI")) == "::cactus_gen::stdlib::math::PI_");
    CHECK(cpp_namespace_path("game.class") == "cactus_gen::game::class_");
    // A user top-level module must not land on a namespace the backend reserves.
    CHECK(cpp_namespace_path("stdlib.util") == "cactus_gen::stdlib_::util");
    CHECK(cpp_namespace_path("detail") == "cactus_gen::detail_");
    CHECK(cpp_namespace_path("game.std") == "cactus_gen::game::std_");
}

TEST_CASE("symbol identity: every raylib.h macro is escaped", "[symbol-identity][cpp-names]") {
    std::ifstream header(CACTUS_RAYLIB_HEADER);
    REQUIRE(header.is_open());
    const std::regex define(R"(^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*))");
    std::size_t macros = 0;
    for (std::string line; std::getline(header, line);) {
        std::smatch match;
        if (!std::regex_search(line, match, define)) {
            continue;
        }
        ++macros;
        const auto name = match[1].str();
        INFO(name);
        CHECK(cpp_escaped(name) == name + "_");
    }
    CHECK(macros > 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)