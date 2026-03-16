#include <catch2/catch_test_macros.hpp>

#include "common/error_reporter.h"
#include "common/types.h"
#include "frontend/semantic_analyzer.h"

using namespace cactus;

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Build a minimal ProgramNode containing a single trait with a field
/// whose type is the given TypeRef name.
static ProgramNode make_program_with_trait_field(const std::string& trait_name,
                                                   const std::string& field_name,
                                                   const std::string& field_type_name) {
    ProgramNode prog;
    TraitNode trait;
    trait.name = trait_name;
    trait.is_pub = false;
    FieldNode field;
    field.name = field_name;
    field.type.name = field_type_name;
    field.modifiers.is_var = true;
    trait.fields.push_back(std::move(field));
    prog.declarations.push_back(std::move(trait));
    return prog;
}

/// Build a ProgramNode with a system that has filter entries.
static ProgramNode make_program_with_system(const std::string& sys_name,
                                              const std::vector<FilterEntry>& entries) {
    ProgramNode prog;
    SystemNode sys;
    sys.name = sys_name;
    sys.filter.entries = entries;
    for (auto& e : entries) {
        // Also populate backward-compat trait_names with the last component
        auto dot = e.qualified_name.find('.');
        sys.filter.trait_names.push_back(
            dot != std::string::npos ? e.qualified_name.substr(dot + 1) : e.qualified_name);
    }
    prog.declarations.push_back(std::move(sys));
    return prog;
}

/// Build an ImportedSymbols with a single pub struct.
static ImportedSymbols make_module_with_struct(const std::string& module_name,
                                                 const std::string& struct_name) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    ResolvedStruct rs;
    rs.name = struct_name;
    syms.structs[struct_name] = rs;
    return syms;
}

/// Build an ImportedSymbols with a single pub trait.
static ImportedSymbols make_module_with_trait(const std::string& module_name,
                                               const std::string& trait_name) {
    ImportedSymbols syms;
    syms.module_name = module_name;
    ResolvedTrait rt;
    rt.name = trait_name;
    rt.is_pub = true;
    syms.traits[trait_name] = rt;
    return syms;
}

// ── Task 4.2: Qualified symbol resolution ────────────────────────────────────

TEST_CASE("semantic_modules: qualified struct type resolved", "[semantic][modules][4.2]") {
    // Trait field uses "player.Vec2Pos" (qualified reference to an imported struct)
    auto prog = make_program_with_trait_field("EnemyAI", "pos", "player.Vec2Pos");

    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    ResolvedStruct rs;
    rs.name = "Vec2Pos";
    player_syms.structs["Vec2Pos"] = rs;

    ModuleImports imports;
    imports.add("player", std::move(player_syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    REQUIRE(result.traits.count("EnemyAI") == 1);
    auto& t = result.traits.at("EnemyAI");
    REQUIRE(t.fields.size() == 1);
    CHECK(t.fields[0].type.kind == TypeKind::Struct);
    CHECK(t.fields[0].type.name == "player.Vec2Pos");
}

TEST_CASE("semantic_modules: qualified enum type resolved", "[semantic][modules][4.2]") {
    auto prog = make_program_with_trait_field("State", "dir", "physics.Direction");

    ImportedSymbols phys_syms;
    phys_syms.module_name = "physics";
    ResolvedEnum re;
    re.name = "Direction";
    re.variants = {"Up", "Down"};
    phys_syms.enums["Direction"] = re;

    ModuleImports imports;
    imports.add("physics", std::move(phys_syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    auto& t = result.traits.at("State");
    CHECK(t.fields[0].type.kind == TypeKind::Enum);
    CHECK(t.fields[0].type.name == "physics.Direction");
}

TEST_CASE("semantic_modules: alias resolution (use player as p)", "[semantic][modules][4.2]") {
    // Module is registered under alias "p", field uses "p.Vec2Pos"
    auto prog = make_program_with_trait_field("EnemyAI", "pos", "p.Vec2Pos");

    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    ResolvedStruct rs;
    rs.name = "Vec2Pos";
    player_syms.structs["Vec2Pos"] = rs;

    ModuleImports imports;
    imports.add("p", std::move(player_syms));  // alias "p"

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    auto& t = result.traits.at("EnemyAI");
    CHECK(t.fields[0].type.kind == TypeKind::Struct);
    CHECK(t.fields[0].type.name == "p.Vec2Pos");
}

TEST_CASE("semantic_modules: unknown module qualifier error", "[semantic][modules][4.2]") {
    auto prog = make_program_with_trait_field("T", "x", "unknown_mod.Foo");

    ModuleImports imports;  // empty

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("unknown module qualifier") != std::string::npos);
}

// ── Task 4.3: Unqualified unique import lookup ───────────────────────────────

TEST_CASE("semantic_modules: unqualified unique struct resolved from import", "[semantic][modules][4.3]") {
    // Struct "Velocity" is only in one module → unqualified reference OK
    auto prog = make_program_with_trait_field("Mover", "vel", "Velocity");

    auto syms = make_module_with_struct("physics", "Velocity");

    ModuleImports imports;
    imports.add("physics", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
    auto& t = result.traits.at("Mover");
    CHECK(t.fields[0].type.kind == TypeKind::Struct);
    CHECK(t.fields[0].type.name == "Velocity");
}

TEST_CASE("semantic_modules: unqualified ambiguous type reports error", "[semantic][modules][4.3]") {
    // "Config" is in both modA and modB → ambiguous
    auto prog = make_program_with_trait_field("Service", "cfg", "Config");

    ImportedSymbols syms_a;
    syms_a.module_name = "modA";
    syms_a.structs["Config"] = ResolvedStruct{"Config", {}};

    ImportedSymbols syms_b;
    syms_b.module_name = "modB";
    syms_b.structs["Config"] = ResolvedStruct{"Config", {}};

    ModuleImports imports;
    imports.add("modA", std::move(syms_a));
    imports.add("modB", std::move(syms_b));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("ambiguous") != std::string::npos);
    CHECK(msg.find("Config") != std::string::npos);
}

// ── Task 4.4: Filter clause alias resolution ─────────────────────────────────

TEST_CASE("semantic_modules: filter entry with qualified trait resolved", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "player.Position";
    auto prog = make_program_with_system("MoveSystem", {entry});

    auto syms = make_module_with_trait("player", "Position");
    ModuleImports imports;
    imports.add("player", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: filter entry with alias resolves trait", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "p.Position";  // module registered as "p"
    entry.alias = "pos";
    auto prog = make_program_with_system("MoveSystem", {entry});

    auto syms = make_module_with_trait("player", "Position");
    ModuleImports imports;
    imports.add("p", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: filter entry with unqualified trait from import", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "Position";  // unqualified, unique in imports
    auto prog = make_program_with_system("MoveSystem", {entry});

    auto syms = make_module_with_trait("player", "Position");
    ModuleImports imports;
    imports.add("player", std::move(syms));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK_FALSE(errors.has_errors());
}

TEST_CASE("semantic_modules: ambiguous unqualified trait in filter", "[semantic][modules][4.4]") {
    FilterEntry entry;
    entry.qualified_name = "Config";
    auto prog = make_program_with_system("Worker", {entry});

    auto syms_a = make_module_with_trait("modA", "Config");
    auto syms_b = make_module_with_trait("modB", "Config");

    ModuleImports imports;
    imports.add("modA", std::move(syms_a));
    imports.add("modB", std::move(syms_b));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("ambiguous") != std::string::npos);
}

// ── Task 4.6: Non-pub helpful error ──────────────────────────────────────────

TEST_CASE("semantic_modules: non-pub type reference suggests adding pub", "[semantic][modules][4.6]") {
    auto prog = make_program_with_trait_field("Enemy", "health", "player.PlayerPhysics");

    // player module exports no pub traits, but PlayerPhysics is listed as non-pub
    ImportedSymbols player_syms;
    player_syms.module_name = "player";
    // No pub traits/structs/enums

    ModuleImports imports;
    imports.add("player", std::move(player_syms), {"PlayerPhysics"});  // non-pub set

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    auto& msg = errors.diagnostics()[0].message;
    CHECK(msg.find("not public") != std::string::npos);
    CHECK(msg.find("PlayerPhysics") != std::string::npos);
    CHECK(msg.find("pub") != std::string::npos);
}

TEST_CASE("semantic_modules: non-pub filter trait suggests adding pub", "[semantic][modules][4.6]") {
    FilterEntry entry;
    entry.qualified_name = "player.Secret";
    auto prog = make_program_with_system("Worker", {entry});

    ImportedSymbols player_syms;
    player_syms.module_name = "player";

    ModuleImports imports;
    imports.add("player", std::move(player_syms), {"Secret"});  // non-pub

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog, imports);

    CHECK(errors.has_errors());
    CHECK(errors.diagnostics()[0].message.find("not public") != std::string::npos);
}

// ── Task 4.7: Backward compatibility ─────────────────────────────────────────

TEST_CASE("semantic_modules: backward compat — no imports works as before", "[semantic][modules][4.7]") {
    // Single-file program: local trait, no imports
    ProgramNode prog;
    TraitNode trait;
    trait.name = "Position";
    FieldNode f;
    f.name = "x";
    f.type.name = "float";
    f.modifiers.is_var = true;
    trait.fields.push_back(std::move(f));
    prog.declarations.push_back(std::move(trait));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    auto result = analyzer.analyze(prog);  // no imports — default empty

    CHECK_FALSE(errors.has_errors());
    REQUIRE(result.traits.count("Position") == 1);
    CHECK(result.traits.at("Position").fields[0].type.kind == TypeKind::Float);
}

TEST_CASE("semantic_modules: backward compat — local filter trait still works", "[semantic][modules][4.7]") {
    // Define a local trait, then a system filtering on it (old style: trait_names, no entries)
    ProgramNode prog;
    TraitNode trait;
    trait.name = "Health";
    prog.declarations.push_back(std::move(trait));

    SystemNode sys;
    sys.name = "HealSystem";
    sys.filter.trait_names = {"Health"};
    // entries is empty — backward-compat path
    prog.declarations.push_back(std::move(sys));

    ErrorReporter errors;
    SemanticAnalyzer analyzer(errors);
    analyzer.analyze(prog);

    CHECK_FALSE(errors.has_errors());
}
