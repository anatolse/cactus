// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include "backends/cpp-entt/construction_retention.hpp"
#include "backends/cpp-entt/cpp_entt_codegen.hpp"
#include "backends/cpp-entt/persistence_emitter.hpp"
#include "backends/cpp-entt/trait_writers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace cactus;

namespace {

struct Compilation {
    ErrorReporter errors;
    ProgramNode ast;
    DecoratedProgram program;
};

void require_clean(const ErrorReporter& errors) {
    for (const auto& diagnostic : errors.diagnostics()) {
        UNSCOPED_INFO(diagnostic.message);
    }
    REQUIRE_FALSE(errors.has_errors());
}

std::unique_ptr<Compilation> analyze(const std::string& source) {
    auto unit = std::make_unique<Compilation>();
    Lexer lexer(source, "world.cactus", unit->errors);
    Parser parser(lexer.tokenize(), unit->errors);
    unit->ast = parser.parse_program();
    require_clean(unit->errors);
    SemanticAnalyzer analyzer(unit->errors);
    unit->program = analyzer.analyze(unit->ast, ModuleImports{});
    require_clean(unit->errors);
    derive_archetype_reachability(unit->program, unit->errors);
    require_clean(unit->errors);
    return unit;
}

SymbolId trait(const std::string& name) {
    return make_symbol_id(SymbolKind::Trait, ModuleId{.name = "world"}, name);
}

std::uint32_t node_of(const DecoratedProgram& program, SymbolKind kind, const std::string& name) {
    const auto node =
        EnttPersistenceEmitter::archetype_origin_index(program, make_symbol_id(kind, ModuleId{.name = "world"}, name));
    REQUIRE(node.has_value());
    return *node;
}

HandlerContract& contract_of(DecoratedProgram& program, const std::string& rule) {
    const auto found = std::ranges::find_if(program.execution_graph.handlers, [&](const HandlerNode& handler) {
        return handler.identity.rule.local_name == rule;
    });
    REQUIRE(found != program.execution_graph.handlers.end());
    return found->contract;
}

std::string function_body(const std::string& code, const std::string& name) {
    for (auto start = code.find(name + "("); start != std::string::npos; start = code.find(name + "(", start + 1)) {
        const auto open = code.find('{', start);
        if (open == std::string::npos || code.find(';', start) < open) {
            continue;
        }
        const auto end = code.find("\n}", open);
        REQUIRE(end != std::string::npos);
        return code.substr(open, end - open);
    }
    FAIL("no definition of " + name + " in the generated code");
    return {};
}

std::string retain(const std::string& trait_cpp) {
    return "retain_construction<" + trait_cpp + ">(registry, ";
}

// Box is never written, Cell only through its persist field, Health through a
// field of an each: pass that needs Box too, Score through a named entity and
// Glow by a projection onto Cell carriers.
constexpr const char* kProgram = R"(module world

pub extern event Step:
    dt: float

trait Cell:
    let x: int = 0
    persist var texture: int = 0

trait Box:
    let size: float = 1.0

trait Health:
    var current: int = 100
    persist var best: int = 0

trait Score:
    var value: int = 0

trait Glow:
    var strength: float = 0.0

entity Block:
    Cell
    Box
    Health
    Score

entity Rock:
    Cell
    Health

rule Paint:
    each:
        Cell as cell
    on Step:
        cell.texture = 1

rule Wear:
    each:
        Health as health
        Box
    on Step:
        health.current -= 1

rule Count:
    on Step:
        Block[Score].value += 1

rule Shine:
    each:
        Cell
    on Step:
        project Glow:
            strength = 1.0
)";

}  // namespace

// ── The field-aware writer query ────────────────────────────────────────────

TEST_CASE("a persist-only write does not count as a construction writer", "[codegen-entt][persistence][retention]") {
    const auto unit = analyze(kProgram);

    CHECK(trait_writers(unit->program, {trait("Cell")}, construction_writer_filter(unit->program)).target_sets.empty());
    // Trait-level callers such as index classification still see the write.
    CHECK(trait_writers(unit->program, {trait("Cell")}).target_sets ==
          std::vector<std::vector<SymbolId>>{{trait("Cell")}});
}

TEST_CASE("a write with no field entry counts as a write of every field", "[codegen-entt][persistence][retention]") {
    auto unit = analyze(kProgram);
    contract_of(unit->program, "Paint").write_fields.erase(trait("Cell"));

    CHECK(trait_writers(unit->program, {trait("Cell")}, construction_writer_filter(unit->program)).target_sets ==
          std::vector<std::vector<SymbolId>>{{trait("Cell")}});
}

TEST_CASE("a set of a non-persist field counts as a construction writer", "[codegen-entt][persistence][retention]") {
    const auto unit = analyze(R"(module world

pub extern event Step:
    dt: float

trait Box:
    var size: float = 1.0
    persist var tint: int = 0

trait Aim:
    var other: entity_id

entity Block:
    Box

entity Gun:
    Aim

rule Grow:
    each:
        Aim as aim
    on Step:
        set Box on aim.other:
            size = 2.0

rule Tint:
    each:
        Aim as aim
    on Step:
        set Box on aim.other:
            tint = 2
)");
    const auto writers = trait_writers(unit->program, {trait("Box")}, construction_writer_filter(unit->program));

    CHECK(writers.target_sets == std::vector<std::vector<SymbolId>>{{trait("Box")}});
}

TEST_CASE("a projection counts as a writer of its targets", "[codegen-entt][persistence][retention]") {
    const auto unit    = analyze(kProgram);
    const auto writers = trait_writers(unit->program, {trait("Glow")}, construction_writer_filter(unit->program));

    CHECK(std::ranges::contains(writers.target_sets, std::vector{trait("Cell")}));
}

TEST_CASE("a native writer counts for every carrier", "[codegen-entt][persistence][retention]") {
    const auto unit = analyze(R"(module world

pub extern event frame:
    dt: float

trait Box:
    let size: float = 1.0
    persist var tint: int = 0

entity Block:
    Box

phase simulate:
    from:
        frame

extern rule Native:
    on simulate:
        writes:
            Box
)");

    CHECK(trait_writers(unit->program, {trait("Box")}, construction_writer_filter(unit->program)).target_sets ==
          std::vector<std::vector<SymbolId>>{{trait("Box")}});
}

// ── The retention table ─────────────────────────────────────────────────────

TEST_CASE("construction copies are kept only where a writer can reach", "[codegen-entt][persistence][retention]") {
    const auto unit     = analyze(kProgram);
    const auto& program = unit->program;
    const auto block    = node_of(program, SymbolKind::Entity, "Block");
    const auto rock     = node_of(program, SymbolKind::Entity, "Rock");
    const ConstructionRetention retention(
        program, {trait("Cell"), trait("Box"), trait("Health"), trait("Score")});

    SECTION("an unwritten trait keeps no copy") {
        CHECK_FALSE(retention.retains(block, trait("Box")));
        CHECK_FALSE(retention.retains_anywhere(trait("Box")));
    }
    SECTION("a persist-only write keeps no copy") {
        CHECK_FALSE(retention.retains_anywhere(trait("Cell")));
    }
    SECTION("a written trait keeps its copy only on nodes the writer selects") {
        CHECK(retention.retains(block, trait("Health")));
        CHECK_FALSE(retention.retains(rock, trait("Health")));
        CHECK(retention.retains_anywhere(trait("Health")));
        CHECK_FALSE(retention.retains_everywhere(trait("Health")));
    }
    SECTION("a named-entity writer keeps the named entity's copy") {
        CHECK(retention.retains(block, trait("Score")));
    }
}

TEST_CASE("a projection onto a carrier keeps its copy", "[codegen-entt][persistence][retention]") {
    const auto unit = analyze(R"(module world

pub extern event Step:
    dt: float

trait Cell:
    let x: int = 0
    persist var texture: int = 0

trait Glow:
    var strength: float = 0.0

entity Lamp:
    Cell
    Glow

rule Shine:
    each:
        Cell
    on Step:
        project Glow:
            strength = 1.0
)");
    const ConstructionRetention retention(unit->program, {trait("Glow")});

    CHECK(retention.retains(node_of(unit->program, SymbolKind::Entity, "Lamp"), trait("Glow")));
}

TEST_CASE("the test switch keeps every copy", "[codegen-entt][persistence][retention]") {
    auto unit                             = analyze(kProgram);
    unit->program.retain_all_construction = true;
    const ConstructionRetention retention(unit->program, {trait("Box"), trait("Cell")});

    CHECK(retention.retains_everywhere(trait("Box")));
    CHECK(retention.retains_everywhere(trait("Cell")));
}

// ── Codegen ─────────────────────────────────────────────────────────────────

TEST_CASE("creation sites copy only the traits a writer can reach", "[codegen-entt][persistence][retention]") {
    auto unit       = analyze(kProgram);
    const auto code = CppEnttCodegen::generate(unit->program);
    const auto block = function_body(code, "create_block_at");
    const auto rock  = function_body(code, "create_rock_at");

    CHECK_FALSE(block.contains(retain("::cactus_gen::world::Box")));
    CHECK_FALSE(block.contains(retain("::cactus_gen::world::Cell")));
    CHECK(block.contains(retain("::cactus_gen::world::Health")));
    CHECK(block.contains(retain("::cactus_gen::world::Score")));
    CHECK_FALSE(rock.contains(retain("::cactus_gen::world::Health")));
}

TEST_CASE("spawn and add sites skip copies no node keeps", "[codegen-entt][persistence][retention]") {
    auto unit = analyze(R"(module world

pub extern event Step:
    dt: float

trait Cell:
    let x: int = 0
    persist var texture: int = 0

trait Box:
    let size: float = 1.0

trait Spawner:
    var pending: int = 1

template Brick:
    Cell
    Box

entity Director:
    Spawner

rule Build:
    each:
        Spawner as spawner
    on Step:
        if spawner.pending > 0:
            spawner.pending -= 1
            spawn Brick:
                Box:
                    size = 2.0
                Cell:
                    x = 3

rule Mark:
    each:
        Cell
    on Step:
        add Box:
            size = 4.0
)");
    const auto code = CppEnttCodegen::generate(unit->program);

    CHECK_FALSE(function_body(code, "build_on").contains(retain("::cactus_gen::world::Box")));
    CHECK_FALSE(function_body(code, "build_on").contains(retain("::cactus_gen::world::Cell")));
    CHECK_FALSE(function_body(code, "mark_on").contains(retain("::cactus_gen::world::Box")));
    CHECK_FALSE(function_body(code, "create_brick_at").contains(retain("::cactus_gen::world::Box")));
}

TEST_CASE("capture reads the live value of a trait no node copies", "[codegen-entt][persistence][retention]") {
    auto unit       = analyze(kProgram);
    const auto code = CppEnttCodegen::generate(unit->program);
    const auto box    = function_body(code, "generated_capture_trait<::cactus_gen::world::Box>");
    const auto health = function_body(code, "generated_capture_trait<::cactus_gen::world::Health>");

    CHECK_FALSE(box.contains("Construction<"));
    CHECK(box.contains("live->size"));
    CHECK(health.contains("registry.try_get<Construction<::cactus_gen::world::Health>>(entity)"));
    CHECK(health.contains("built != nullptr ? built->value : *live"));
}

TEST_CASE("restore copies a trait only on the nodes that keep one", "[codegen-entt][persistence][retention]") {
    auto unit          = analyze(kProgram);
    const auto code    = CppEnttCodegen::generate(unit->program);
    const auto restore = function_body(code, "generated_restore_node_traits");

    CHECK_FALSE(restore.contains("retain_construction<::cactus_gen::world::Box>"));
    CHECK_FALSE(restore.contains("retain_construction<::cactus_gen::world::Cell>"));
    CHECK(restore.contains("retain_construction<::cactus_gen::world::Health>"));
    CHECK(restore.contains("kKeepsCopy[node]"));
    CHECK(code.contains("generated_restore_node_traits(staged, entity, node, record, ids);"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
