// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/hierarchy_forest.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace cactus;
namespace fs = std::filesystem;

namespace {

const std::string kPrelude =
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "trait Parent:\n"
    "    let parent: entity_id\n"
    "trait Hand\n"
    "trait Item\n"
    "trait Link:\n"
    "    var target: entity_id\n"
    "pub extern event Grab:\n"
    "    item: entity_id\n"
    "    hand: entity_id\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] std::vector<std::string> errors() const {
        std::vector<std::string> messages;
        for (const auto& d : diagnostics) {
            if (d.level == DiagnosticLevel::Error) {
                messages.push_back(d.message);
            }
        }
        return messages;
    }
    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(errors(), [&](const std::string& message) { return message.contains(text); });
    }
    [[nodiscard]] bool clean() const {
        return errors().empty();
    }
    [[nodiscard]] std::string describe() const {
        std::string text;
        for (const auto& d : diagnostics) {
            text += std::to_string(d.location.line) + ": " + d.message + "\n";
        }
        return text;
    }
};

Analysis analyze_source(const std::string& source,
                        const std::string& module,
                        const ModuleImports& imports = {},
                        bool reachability = false) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(source, module + ".cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    INFO((errors.diagnostics().empty() ? std::string{} : errors.diagnostics().front().message));
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast, imports);
    if (reachability && !errors.has_errors()) {
        derive_archetype_reachability(analysis.program, errors);
    }
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

Analysis analyze(const std::string& body) {
    return analyze_source("module game\n" + kPrelude + body, "game");
}

// Semantic analysis plus the link-time passes, as a single-module build runs them.
Analysis link(const std::string& body, const std::string& module = "game") {
    auto analysis = analyze_source("module " + module + "\n" + kPrelude + body, module, {}, true);
    INFO(analysis.describe());
    REQUIRE(analysis.clean());
    return analysis;
}

std::string read_stdlib(const std::string& relative_path) {
    std::ifstream file(fs::path(CACTUS_TEST_SOURCE_DIR) / "stdlib" / relative_path);
    REQUIRE(file);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// The real std.core, exported through a saved artifact as an importer sees it.
const ModuleImports& core_imports() {
    static const ModuleImports imports = [] {
        auto core = analyze_source(read_stdlib("std/core.cactus"), "std.core");
        INFO(core.describe());
        REQUIRE(core.clean());
        const auto build_dir = fs::path(CACTUS_TEST_FIXTURES_DIR) / "hierarchy_forest_build";
        ErrorReporter errors;
        ModuleArtifact artifact(errors);
        REQUIRE(artifact.save(core.program, "std.core", build_dir));
        auto symbols = artifact.extract_pub_symbols(build_dir / ModuleArtifact::artifact_filename("std.core"));
        REQUIRE(symbols.has_value());
        std::error_code ec;
        fs::remove_all(build_dir, ec);
        ModuleImports result;
        result.add("core", *symbols);
        return result;
    }();
    return imports;
}

Analysis analyze_with_core(const std::string& body) {
    return analyze_source("module game\nuse std.core as core\n" + body, "game", core_imports());
}

const std::string kLetParent = "can't assign let field 'Parent.parent'";
const std::string kSelfParent = "can't be its own parent";

std::vector<ParentSourceKind> kinds(const HierarchyFacts& facts) {
    std::vector<ParentSourceKind> result;
    result.reserve(facts.sources.size());
    for (const auto& source : facts.sources) {
        result.push_back(source.kind);
    }
    return result;
}

}  // namespace

// ── Parent.parent is let ────────────────────────────────────────────────────

TEST_CASE("Hierarchy forest: std.core.Parent.parent is a let field", "[hierarchy-forest]") {
    const auto core = analyze_source(read_stdlib("std/core.cactus"), "std.core");
    REQUIRE(core.clean());
    const auto found = core.program.traits.find("Parent");
    REQUIRE(found != core.program.traits.end());
    REQUIRE(found->second.fields.size() == 1);
    CHECK(found->second.fields.front().name == "parent");
    CHECK(found->second.fields.front().is_let);
}

TEST_CASE("Hierarchy forest: a field write to std.core.Parent is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze_with_core("rule Rewire:\n"
                                            "    each:\n"
                                            "        core.Parent as p\n"
                                            "    on core.tick:\n"
                                            "        p.parent = self\n");
    INFO(analysis.describe());
    CHECK(analysis.has(kLetParent));
}

TEST_CASE("Hierarchy forest: a set of std.core.Parent is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze_with_core("trait Link:\n"
                                            "    var target: entity_id\n"
                                            "rule Rewire:\n"
                                            "    each:\n"
                                            "        Link as l\n"
                                            "    on core.tick:\n"
                                            "        set core.Parent on l.target:\n"
                                            "            parent = self\n");
    INFO(analysis.describe());
    CHECK(analysis.has(kLetParent));
}

TEST_CASE("Hierarchy forest: a spawn override of Parent compiles", "[hierarchy-forest]") {
    const auto analysis = analyze_with_core("trait Segment\n"
                                            "template Tail:\n"
                                            "    Segment\n"
                                            "    core.Parent\n"
                                            "rule Grow:\n"
                                            "    each:\n"
                                            "        Segment\n"
                                            "    on core.tick:\n"
                                            "        spawn Tail:\n"
                                            "            core.Parent:\n"
                                            "                parent = self\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

// ── Pinned self-parent ──────────────────────────────────────────────────────

TEST_CASE("Hierarchy forest: add Parent with parent = self and no target is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze("rule Loop:\n"
                                  "    each:\n"
                                  "        Item\n"
                                  "    on tick:\n"
                                  "        add Parent:\n"
                                  "            parent = self\n");
    INFO(analysis.describe());
    CHECK(analysis.has(kSelfParent));
}

TEST_CASE("Hierarchy forest: add Parent to self with parent = self is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze("rule Loop:\n"
                                  "    each:\n"
                                  "        Item\n"
                                  "    on tick:\n"
                                  "        add Parent to self:\n"
                                  "            parent = self\n");
    INFO(analysis.describe());
    CHECK(analysis.has(kSelfParent));
}

TEST_CASE("Hierarchy forest: the same pair binding as target and parent is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze("rule Loop:\n"
                                  "    pairs:\n"
                                  "        hand:\n"
                                  "            Hand\n"
                                  "        item:\n"
                                  "            Item\n"
                                  "    on tick:\n"
                                  "        add Parent to item:\n"
                                  "            parent = item\n");
    INFO(analysis.describe());
    CHECK(analysis.has(kSelfParent));
}

TEST_CASE("Hierarchy forest: the same local as target and parent is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze("rule Loop:\n"
                                  "    on Grab:\n"
                                  "        let item = Grab.item\n"
                                  "        add Parent to item:\n"
                                  "            parent = item\n");
    INFO(analysis.describe());
    CHECK(analysis.has(kSelfParent));
}

TEST_CASE("Hierarchy forest: an unpinned reparent compiles", "[hierarchy-forest]") {
    const auto analysis = analyze("rule PickUp:\n"
                                  "    each:\n"
                                  "        Hand\n"
                                  "    on Grab:\n"
                                  "        add Parent to Grab.item:\n"
                                  "            parent = self\n"
                                  "rule Attach:\n"
                                  "    pairs:\n"
                                  "        hand:\n"
                                  "            Hand\n"
                                  "        item:\n"
                                  "            Item\n"
                                  "    on tick:\n"
                                  "        add Parent to item:\n"
                                  "            parent = hand\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

// ── Extern rules ────────────────────────────────────────────────────────────

TEST_CASE("Hierarchy forest: an extern rule can't write Parent", "[hierarchy-forest]") {
    const auto analysis = analyze("extern rule HostRig:\n"
                                  "    on tick:\n"
                                  "        writes:\n"
                                  "            Parent\n");
    INFO(analysis.describe());
    CHECK(analysis.has("extern rule 'HostRig' handler 'on tick' can't write 'Parent'"));
    CHECK(analysis.has("use an 'add Parent' command"));
}

TEST_CASE("Hierarchy forest: an extern rule can't set Parent", "[hierarchy-forest]") {
    const auto analysis = analyze("extern rule HostRig:\n"
                                  "    on tick:\n"
                                  "        commands:\n"
                                  "            set Parent\n");
    INFO(analysis.describe());
    CHECK(analysis.has("extern rule 'HostRig' handler 'on tick' can't set 'Parent'"));
    CHECK(analysis.has("use an 'add Parent' command"));
}

TEST_CASE("Hierarchy forest: an extern rule may add Parent", "[hierarchy-forest]") {
    const auto analysis = analyze("extern rule HostRig:\n"
                                  "    on tick:\n"
                                  "        reads:\n"
                                  "            Parent\n"
                                  "        commands:\n"
                                  "            add Parent\n");
    INFO(analysis.describe());
    CHECK(analysis.clean());
}

// ── Declared entities ───────────────────────────────────────────────────────

namespace {

std::vector<std::string> load_order(const Analysis& analysis) {
    std::vector<const EntityNode*> entities;
    for (const auto& declaration : analysis.ast->declarations) {
        if (const auto* entity = std::get_if<EntityNode>(&declaration)) {
            entities.push_back(entity);
        }
    }
    std::vector<std::string> names;
    for (const auto* entity : parent_first_order(entities).order) {
        names.push_back(entity->name);
    }
    return names;
}

}  // namespace

TEST_CASE("Hierarchy forest: a parent declared later loads first", "[hierarchy-forest]") {
    const auto analysis = analyze("entity Sword:\n"
                                  "    Item\n"
                                  "    Parent:\n"
                                  "        parent = Palm\n"
                                  "entity Shield:\n"
                                  "    Item\n"
                                  "entity Palm:\n"
                                  "    Hand\n");
    INFO(analysis.describe());
    REQUIRE(analysis.clean());
    CHECK(load_order(analysis) == std::vector<std::string>{"Shield", "Palm", "Sword"});
}

TEST_CASE("Hierarchy forest: declared entities without parent references keep their order", "[hierarchy-forest]") {
    const auto analysis = analyze("entity C:\n"
                                  "    Item\n"
                                  "entity A:\n"
                                  "    Hand\n"
                                  "entity B:\n"
                                  "    Item\n"
                                  "    Link:\n"
                                  "        target = A\n");
    REQUIRE(analysis.clean());
    CHECK(load_order(analysis) == std::vector<std::string>{"C", "A", "B"});
}

TEST_CASE("Hierarchy forest: a declared Parent cycle is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze("entity B:\n"
                                  "    Item\n"
                                  "    Parent:\n"
                                  "        parent = A\n"
                                  "entity A:\n"
                                  "    Item\n"
                                  "    Parent:\n"
                                  "        parent = B\n"
                                  "entity C:\n"
                                  "    Item\n"
                                  "    Parent:\n"
                                  "        parent = A\n");
    INFO(analysis.describe());
    CHECK(analysis.errors().size() == 1);
    CHECK(analysis.has("declared entities form a Parent cycle: game.A, game.B"));
}

TEST_CASE("Hierarchy forest: a declared entity naming itself as parent is rejected", "[hierarchy-forest]") {
    const auto analysis = analyze("entity Loop:\n"
                                  "    Item\n"
                                  "    Parent:\n"
                                  "        parent = Loop\n");
    INFO(analysis.describe());
    CHECK(analysis.has("declared entities form a Parent cycle: game.Loop"));
}

// ── Hierarchy facts ─────────────────────────────────────────────────────────

TEST_CASE("Hierarchy facts: a children-only program has a static depth", "[hierarchy-forest][hierarchy-facts]") {
    const auto analysis = link("template Arm:\n"
                               "    Hand\n"
                               "    children:\n"
                               "        entity Finger:\n"
                               "            Item\n"
                               "            children:\n"
                               "                entity Nail:\n"
                               "                    Item\n"
                               "entity Root:\n"
                               "    Hand\n"
                               "rule Wave:\n"
                               "    each:\n"
                               "        Hand\n"
                               "    on tick:\n"
                               "        spawn Arm()\n");
    const auto& facts = analysis.program.hierarchy;
    CHECK(facts.derived);
    CHECK_FALSE(facts.has_reparent);
    CHECK(facts.depth_bound == std::optional<std::uint32_t>{2});
    CHECK(kinds(facts) == std::vector{ParentSourceKind::Structural, ParentSourceKind::Structural});
    CHECK(facts.sources.front().site == "game.Arm/Finger");
}

TEST_CASE("Hierarchy facts: a Parent in a template body is attached", "[hierarchy-forest][hierarchy-facts]") {
    const auto analysis = link("template Segment:\n"
                               "    Item\n"
                               "    Parent\n"
                               "rule Grow:\n"
                               "    each:\n"
                               "        Hand\n"
                               "    on tick:\n"
                               "        spawn Segment:\n"
                               "            Parent:\n"
                               "                parent = self\n"
                               "entity Root:\n"
                               "    Hand\n");
    const auto& facts = analysis.program.hierarchy;
    CHECK_FALSE(facts.has_reparent);
    CHECK_FALSE(facts.depth_bound.has_value());
    CHECK(kinds(facts) == std::vector{ParentSourceKind::Attached});
    CHECK(facts.sources.front().site == "game.Segment");
}

TEST_CASE("Hierarchy facts: remove Parent is a detach", "[hierarchy-forest][hierarchy-facts]") {
    const auto analysis = link("template Arm:\n"
                               "    Hand\n"
                               "    children:\n"
                               "        entity Finger:\n"
                               "            Item\n"
                               "entity Root:\n"
                               "    Hand\n"
                               "rule Drop:\n"
                               "    each:\n"
                               "        Item\n"
                               "    on tick:\n"
                               "        remove Parent\n"
                               "rule Wave:\n"
                               "    each:\n"
                               "        Hand\n"
                               "    on tick:\n"
                               "        spawn Arm()\n");
    const auto& facts = analysis.program.hierarchy;
    CHECK_FALSE(facts.has_reparent);
    CHECK(facts.depth_bound == std::optional<std::uint32_t>{1});
    CHECK(kinds(facts) == std::vector{ParentSourceKind::Structural, ParentSourceKind::Detach});
    CHECK(facts.sources.back().site == "game.Drop/on game.tick");
}

TEST_CASE("Hierarchy facts: add Parent is a reparent", "[hierarchy-forest][hierarchy-facts]") {
    const auto analysis = link("entity Root:\n"
                               "    Hand\n"
                               "rule PickUp:\n"
                               "    each:\n"
                               "        Hand\n"
                               "    on Grab:\n"
                               "        add Parent to Grab.item:\n"
                               "            parent = self\n");
    const auto& facts = analysis.program.hierarchy;
    CHECK(facts.has_reparent);
    CHECK_FALSE(facts.depth_bound.has_value());
    CHECK(kinds(facts) == std::vector{ParentSourceKind::Reparent});
}

TEST_CASE("Hierarchy facts: an extern add Parent capability is a reparent", "[hierarchy-forest][hierarchy-facts]") {
    const auto analysis = link("entity Root:\n"
                               "    Hand\n"
                               "extern rule HostRig:\n"
                               "    on tick:\n"
                               "        commands:\n"
                               "            add Parent\n");
    const auto& facts = analysis.program.hierarchy;
    CHECK(facts.has_reparent);
    CHECK(kinds(facts) == std::vector{ParentSourceKind::Reparent});
    CHECK(facts.sources.front().site == "game.HostRig/on game.tick");
}

TEST_CASE("Hierarchy facts: an add Parent in an unreachable stdlib rule is ignored",
          "[hierarchy-forest][hierarchy-facts]") {
    const auto analysis = link("entity Root:\n"
                               "    Hand\n"
                               "rule Mount:\n"
                               "    each:\n"
                               "        Link as l\n"
                               "    on tick:\n"
                               "        add Parent to l.target:\n"
                               "            parent = self\n",
                               "std.rig");
    const auto& facts = analysis.program.hierarchy;
    CHECK(facts.derived);
    CHECK_FALSE(facts.has_reparent);
    CHECK(facts.sources.empty());
    CHECK(facts.depth_bound == std::optional<std::uint32_t>{0});
}

TEST_CASE("Hierarchy facts: the result doesn't depend on declaration order", "[hierarchy-forest][hierarchy-facts]") {
    const std::string arm = "template Arm:\n"
                            "    Hand\n"
                            "    children:\n"
                            "        entity Finger:\n"
                            "            Item\n";
    const std::string drop = "rule Drop:\n"
                             "    each:\n"
                             "        Item\n"
                             "    on tick:\n"
                             "        remove Parent\n";
    const std::string pick = "rule PickUp:\n"
                             "    each:\n"
                             "        Hand\n"
                             "    on Grab:\n"
                             "        add Parent to Grab.item:\n"
                             "            parent = self\n"
                             "entity Root:\n"
                             "    Hand\n"
                             "rule Wave:\n"
                             "    each:\n"
                             "        Hand\n"
                             "    on tick:\n"
                             "        spawn Arm()\n";
    const auto forward  = link(arm + drop + pick);
    const auto backward = link(pick + drop + arm);
    CHECK(forward.program.hierarchy == backward.program.hierarchy);
    CHECK(kinds(forward.program.hierarchy) ==
          std::vector{ParentSourceKind::Structural, ParentSourceKind::Detach, ParentSourceKind::Reparent});
}

TEST_CASE("Hierarchy facts: a program without a hierarchy trait has none", "[hierarchy-forest][hierarchy-facts]") {
    auto analysis = analyze_source("module game\n"
                                   "extern event frame:\n"
                                   "    dt: float\n"
                                   "phase tick:\n"
                                   "    from:\n"
                                   "        frame\n"
                                   "trait Hand\n"
                                   "entity Root:\n"
                                   "    Hand\n",
                                   "game",
                                   {},
                                   true);
    REQUIRE(analysis.clean());
    CHECK(analysis.program.hierarchy.derived);
    CHECK(analysis.program.hierarchy.sources.empty());
    CHECK_FALSE(analysis.program.hierarchy.has_reparent);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
