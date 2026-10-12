// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/program_linker.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

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
    "    var parent: entity_id\n"
    "trait Position:\n"
    "    var x: float = 0.0\n"
    "trait Health:\n"
    "    var hp: int = 10\n"
    "trait Link:\n"
    "    var target: entity_id\n"
    "trait Stats:\n"
    "    var n: int = 0\n"
    "trait Body\n"
    "trait Crate\n"
    "trait Dying\n"
    "trait Broken\n"
    "trait Frozen\n"
    "trait Flash\n"
    "trait Selected\n"
    "state Door:\n"
    "    Closed\n"
    "    Open\n"
    "event Exploded\n";

using TraitNames = std::set<std::string>;

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
    }
    [[nodiscard]] std::size_t error_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(
            diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; }));
    }
    [[nodiscard]] std::set<TraitNames> sets() const {
        std::set<TraitNames> result;
        for (const auto& set : program.reachability.trait_sets) {
            TraitNames names;
            for (const auto& trait : set) {
                names.insert(trait.local_name);
            }
            result.insert(std::move(names));
        }
        return result;
    }
    [[nodiscard]] TraitNames free_traits() const {
        TraitNames names;
        for (const auto& trait : program.reachability.free_traits) {
            names.insert(trait.local_name);
        }
        return names;
    }
    [[nodiscard]] bool reachable(const std::string& rule) const {
        return std::ranges::none_of(program.reachability.unreachable_handlers,
                                    [&](const HandlerIdentity& handler) { return handler.rule.local_name == rule; });
    }
    [[nodiscard]] std::set<TraitNames> origin_sets(const std::string& node) const {
        std::set<TraitNames> result;
        for (const auto& origin : program.reachability.origins) {
            if (canonical_node_string(origin.node) != node) {
                continue;
            }
            for (const auto& set : origin.trait_sets) {
                TraitNames names;
                for (const auto& trait : set) {
                    names.insert(trait.local_name);
                }
                result.insert(std::move(names));
            }
        }
        return result;
    }
    [[nodiscard]] std::string describe() const {
        std::string text;
        for (const auto& d : diagnostics) {
            text += std::to_string(d.location.line) + ": " + d.message + "\n";
        }
        return text;
    }
};

// Analyzes one module and runs the pass on it, as a single-module build does.
Analysis analyze(const std::string& body, const std::string& module = "game", ReachabilityOptions options = {}) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer("module " + module + "\n" + kPrelude + body, module + ".cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program = analyzer.analyze(*analysis.ast);
    INFO((errors.diagnostics().empty() ? std::string{} : errors.diagnostics().front().message));
    REQUIRE_FALSE(errors.has_errors());
    derive_archetype_reachability(analysis.program, errors, options);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

class ModuleSet {
public:
    explicit ModuleSet(fs::path build_dir) : build_dir_(std::move(build_dir)) {
        std::error_code ec;
        fs::remove_all(build_dir_, ec);
    }
    ModuleSet(const ModuleSet&)            = delete;
    ModuleSet& operator=(const ModuleSet&) = delete;
    ModuleSet(ModuleSet&&)                 = delete;
    ModuleSet& operator=(ModuleSet&&)      = delete;
    ~ModuleSet() {
        std::error_code ec;
        fs::remove_all(build_dir_, ec);
    }

    void compile(const std::string& module, const std::vector<std::string>& uses, const std::string& source) {
        ModuleImports imports;
        for (const auto& used : uses) {
            ErrorReporter errors;
            ModuleArtifact artifact(errors);
            auto symbols = artifact.extract_pub_symbols(artifacts_.at(used));
            REQUIRE(symbols.has_value());
            imports.add(used, std::move(*symbols));
        }
        ErrorReporter errors;
        Lexer lexer(source, module + ".cactus", errors);
        Parser parser(lexer.tokenize(), errors);
        asts_.push_back(std::make_unique<ProgramNode>(parser.parse_program()));
        SemanticAnalyzer analyzer(errors);
        const auto program = analyzer.analyze(*asts_.back(), imports);
        INFO((errors.diagnostics().empty() ? "" : errors.diagnostics().front().message));
        REQUIRE_FALSE(errors.has_errors());
        ModuleArtifact artifact(errors);
        REQUIRE(artifact.save(program, module, build_dir_));
        artifacts_[module] = build_dir_ / (module + ".cmod");
    }

    std::optional<DecoratedProgram> link(const std::vector<std::string>& order, ErrorReporter& errors) const {
        std::vector<fs::path> paths;
        paths.reserve(order.size());
        for (const auto& module : order) {
            paths.push_back(artifacts_.at(module));
        }
        ProgramLinker linker(errors);
        return linker.link(paths);
    }

private:
    fs::path build_dir_;
    std::vector<std::unique_ptr<ProgramNode>> asts_;
    std::map<std::string, fs::path> artifacts_;
};

fs::path reachability_build_dir(const std::string& name) {
    return fs::path(CACTUS_TEST_FIXTURES_DIR) / "reachability_test_build" / name;
}

const std::string kSpawnEnemy =
    "template Enemy:\n"
    "    Position\n"
    "    Health\n"
    "entity Spawner:\n"
    "    Stats\n"
    "rule Wave:\n"
    "    on tick:\n"
    "        spawn Enemy()\n";

}  // namespace

// ── Least fixpoint ───────────────────────────────────────────────────────────

TEST_CASE("Reachability: a template and a self add make two archetypes", "[archetype-reachability]") {
    const auto analysis = analyze(kSpawnEnemy +
                                  "rule Doom:\n"
                                  "    each:\n"
                                  "        Health\n"
                                  "    on tick:\n"
                                  "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
    CHECK(analysis.sets() == std::set<TraitNames>{{"Stats"}, {"Position", "Health"}, {"Position", "Health", "Dying"}});
    CHECK(analysis.origin_sets("game.Enemy") == std::set<TraitNames>{{"Position", "Health"}, {"Position", "Health", "Dying"}});
    CHECK(analysis.origin_sets("game.Spawner") == std::set<TraitNames>{{"Stats"}});
}

TEST_CASE("Reachability: an unspawned template contributes nothing", "[archetype-reachability]") {
    const auto analysis = analyze(
        "template Enemy:\n"
        "    Health\n"
        "entity Spawner:\n"
        "    Stats\n");
    CHECK(analysis.sets() == std::set<TraitNames>{{"Stats"}});
}

TEST_CASE("Reachability: a state swap replaces the variant", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Gate:\n"
        "    Door.Closed\n"
        "rule Opens:\n"
        "    each:\n"
        "        Door.Closed\n"
        "    on tick:\n"
        "        add Door.Open\n");
    INFO(analysis.describe());
    CHECK(analysis.sets() == std::set<TraitNames>{{"Door", "Door.Closed"}, {"Door", "Door.Open"}});
}

TEST_CASE("Reachability: removing a state drops its slot and variant", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Gate:\n"
        "    Door.Closed\n"
        "    Stats\n"
        "rule Clears:\n"
        "    each:\n"
        "        Door\n"
        "    on tick:\n"
        "        remove Door\n");
    CHECK(analysis.sets() == std::set<TraitNames>{{"Door", "Door.Closed", "Stats"}, {"Stats"}});
}

TEST_CASE("Reachability: hierarchical children carry Parent", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Tank:\n"
        "    Body\n"
        "    children:\n"
        "        entity turret:\n"
        "            Crate\n");
    INFO(analysis.describe());
    CHECK(analysis.origin_sets("game.Tank") == std::set<TraitNames>{{"Body"}});
    CHECK(analysis.origin_sets("game.Tank/turret") == std::set<TraitNames>{{"Crate", "Parent"}});
}

TEST_CASE("Reachability: a spawned template brings its children", "[archetype-reachability]") {
    const auto analysis = analyze(
        "template Tank:\n"
        "    Body\n"
        "    children:\n"
        "        entity turret:\n"
        "            Crate\n"
        "rule Build:\n"
        "    on tick:\n"
        "        spawn Tank()\n");
    CHECK(analysis.sets() == std::set<TraitNames>{{"Body"}, {"Crate", "Parent"}});
}

TEST_CASE("Reachability: a projection overlays its archetype", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Hit:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        project Flash\n");
    CHECK(analysis.sets() == std::set<TraitNames>{{"Health"}, {"Health", "Flash"}});
}

TEST_CASE("Reachability: commands apply transitively", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule First:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Dying\n"
        "rule Second:\n"
        "    each:\n"
        "        Dying\n"
        "    on tick:\n"
        "        add Broken\n"
        "        remove Health\n");
    CHECK(analysis.sets() == std::set<TraitNames>{{"Health"},
                                                  {"Health", "Dying"},
                                                  {"Health", "Dying", "Broken"},
                                                  {"Dying"},
                                                  {"Dying", "Broken"}});
}

TEST_CASE("Reachability: a command on a named entity and on a spawn result reaches only that entity",
          "[archetype-reachability]") {
    const auto analysis = analyze(
        "template Enemy:\n"
        "    Health\n"
        "entity Game:\n"
        "    Stats\n"
        "entity Hero:\n"
        "    Health\n"
        "    Stats\n"
        "rule Mark:\n"
        "    on tick:\n"
        "        add Dying to Game\n"
        "        let fresh = spawn Enemy()\n"
        "        add Frozen to fresh\n");
    CHECK(analysis.origin_sets("game.Game") == std::set<TraitNames>{{"Stats"}, {"Stats", "Dying"}});
    CHECK(analysis.origin_sets("game.Hero") == std::set<TraitNames>{{"Health", "Stats"}});
    CHECK(analysis.origin_sets("game.Enemy") == std::set<TraitNames>{{"Health"}, {"Health", "Frozen"}});
}

TEST_CASE("Reachability: the result is independent of declaration order", "[archetype-reachability]") {
    const std::string first =
        "rule Doom:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Dying\n";
    const std::string second =
        "rule Clean:\n"
        "    each:\n"
        "        Dying\n"
        "    on tick:\n"
        "        remove Position\n";
    const auto forward  = analyze(kSpawnEnemy + first + second);
    const auto backward = analyze(second + first + kSpawnEnemy);
    CHECK(forward.program.reachability == backward.program.reachability);
}

TEST_CASE("Reachability: the result is independent of module link order", "[archetype-reachability][linker]") {
    ModuleSet modules(reachability_build_dir("link_order"));
    modules.compile("std.core",
                    {},
                    "module std.core\n"
                    "extern event frame:\n"
                    "    dt: float\n"
                    "pub phase tick:\n"
                    "    from:\n"
                    "        frame\n"
                    "pub trait Health:\n"
                    "    var hp: int = 10\n"
                    "pub trait Dying\n");
    modules.compile("spawner",
                    {"std.core"},
                    "module spawner\n"
                    "use std.core\n"
                    "pub template Enemy:\n"
                    "    Health\n"
                    "rule Wave:\n"
                    "    on tick:\n"
                    "        spawn Enemy()\n");
    modules.compile("doom",
                    {"std.core"},
                    "module doom\n"
                    "use std.core\n"
                    "rule Doom:\n"
                    "    each:\n"
                    "        Health\n"
                    "    on tick:\n"
                    "        add Dying\n");
    ErrorReporter forward_errors;
    const auto forward = modules.link({"std.core", "spawner", "doom"}, forward_errors);
    ErrorReporter backward_errors;
    const auto backward = modules.link({"std.core", "doom", "spawner"}, backward_errors);
    REQUIRE(forward.has_value());
    REQUIRE(backward.has_value());
    CHECK(forward->reachability.trait_sets.size() == 2);
    CHECK(forward->reachability == backward->reachability);
}

TEST_CASE("Reachability: a global reduction over an empty domain still runs", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Board:\n"
        "    Stats\n"
        "rule Count:\n"
        "    each: Crate as box\n"
        "    reduce:\n"
        "        boxes = count()\n"
        "    on tick:\n"
        "        Board[Stats].n = boxes\n"
        "        emit Exploded\n"
        "rule Listen:\n"
        "    each:\n"
        "        Stats\n"
        "    on Exploded:\n"
        "        add Dying\n");
    CHECK(analysis.reachable("Count"));
    CHECK(analysis.sets().contains(TraitNames{"Stats", "Dying"}));
}

TEST_CASE("Reachability: the editor's spawn_template can spawn every pub template", "[archetype-reachability]") {
    const auto analysis = analyze(
        "pub extern func spawn_template(template_name: string, position_2d: vec2, position_3d: vec3) entity_id\n"
        "pub template Robot:\n"
        "    Health\n"
        "template Hidden:\n"
        "    Crate\n"
        "entity Board:\n"
        "    Stats\n"
        "rule Palette:\n"
        "    on tick:\n"
        "        let id = spawn_template(\"Robot\", vec2(0.0, 0.0), vec3(0.0, 0.0, 0.0))\n",
        "std.editor");
    INFO(analysis.describe());
    const auto& handlers = analysis.program.execution_graph.handlers;
    REQUIRE(handlers.size() == 1);
    CHECK(handlers[0].contract.native_spawns ==
          std::vector{make_symbol_id(SymbolKind::Func, "std.editor", "spawn_template")});
    CHECK(analysis.sets() == std::set<TraitNames>{{"Stats"}, {"Health"}});
}

TEST_CASE("Reachability: native creations name stdlib functions", "[archetype-reachability]") {
    CHECK(native_creation("std.editor.spawn_template")->pub_templates);
    CHECK(native_creation("std.editor.camera_enter")->trait_sets.size() == 2);
    CHECK(native_creation("std.editor.palette_label_slot")->trait_sets ==
          std::vector<std::vector<std::string>>{{"std.render.text.ScreenLabel"}});
    CHECK(native_creation("game.spawn_template") == nullptr);
}

// ── Unknown targets ──────────────────────────────────────────────────────────

TEST_CASE("Reachability: an add to an unknown entity makes the trait free", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "entity Chain:\n"
        "    Link\n"
        "rule Freeze:\n"
        "    each:\n"
        "        Link\n"
        "    on tick:\n"
        "        add Frozen to target\n");
    CHECK(analysis.free_traits() == TraitNames{"Frozen"});
    CHECK(analysis.sets() == std::set<TraitNames>{{"Health"}, {"Link"}});
}

TEST_CASE("Reachability: a free trait matches as present and as absent", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "entity Chain:\n"
        "    Link\n"
        "rule Freeze:\n"
        "    each:\n"
        "        Link\n"
        "    on tick:\n"
        "        add Frozen to target\n"
        "rule Thaw:\n"
        "    each:\n"
        "        Health\n"
        "        Frozen\n"
        "    on tick:\n"
        "        add Broken\n"
        "rule Warm:\n"
        "    each:\n"
        "        Health\n"
        "    without:\n"
        "        Frozen\n"
        "    on tick:\n"
        "        add Dying\n");
    CHECK(analysis.reachable("Thaw"));
    CHECK(analysis.reachable("Warm"));
    CHECK(analysis.sets().contains(TraitNames{"Health", "Broken"}));
    CHECK(analysis.sets().contains(TraitNames{"Health", "Dying"}));
}

TEST_CASE("Reachability: an add to a pair binding stays in that binding's domain", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Ball:\n"
        "    Body\n"
        "entity Box:\n"
        "    Crate\n"
        "rule Smash:\n"
        "    pairs:\n"
        "        body:\n"
        "            Body\n"
        "        box:\n"
        "            Crate\n"
        "    on tick:\n"
        "        add Broken to box\n");
    CHECK(analysis.free_traits().empty());
    CHECK(analysis.sets() == std::set<TraitNames>{{"Body"}, {"Crate"}, {"Crate", "Broken"}});
}

TEST_CASE("Reachability: extern rule commands are approximated", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "extern rule HostSelect:\n"
        "    on tick:\n"
        "        commands:\n"
        "            add Selected\n");
    CHECK(analysis.free_traits() == TraitNames{"Selected"});
}

TEST_CASE("Reachability: an unknown-target state add maps each archetype without doubling", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "entity Chain:\n"
        "    Link\n"
        "rule Shut:\n"
        "    each:\n"
        "        Link\n"
        "    on tick:\n"
        "        add Door.Closed to target\n"
        "        remove Door from target\n");
    CHECK(analysis.free_traits().empty());
    CHECK(analysis.sets() == std::set<TraitNames>{{"Health"},
                                                  {"Health", "Door", "Door.Closed"},
                                                  {"Link"},
                                                  {"Link", "Door", "Door.Closed"}});
}

// ── Widening ─────────────────────────────────────────────────────────────────

TEST_CASE("Reachability: past the bound the most splitting trait becomes free, deterministically",
          "[archetype-reachability]") {
    const std::string source =
        "entity Hero:\n"
        "    Health\n"
        "rule A:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Dying\n"
        "rule B:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Broken\n"
        "rule C:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Frozen\n";
    const auto unbounded = analyze(source);
    CHECK(unbounded.sets().size() == 8);
    CHECK(unbounded.program.reachability.widened_traits.empty());

    const auto widened = analyze(source, "game", ReachabilityOptions{.widening_bound = 4});
    REQUIRE_FALSE(widened.program.reachability.widened_traits.empty());
    CHECK(widened.program.reachability.trait_sets.size() <= 4);
    for (const auto& trait : widened.program.reachability.widened_traits) {
        CHECK(std::ranges::contains(widened.program.reachability.free_traits, trait));
    }
    const auto again = analyze(source, "game", ReachabilityOptions{.widening_bound = 4});
    CHECK(widened.program.reachability == again.program.reachability);
}

// ── Trigger producers ────────────────────────────────────────────────────────

TEST_CASE("Reachability: phase, extern and load triggers always have a producer", "[archetype-reachability]") {
    const auto analysis = analyze(
        "event load\n"
        "entity Hero:\n"
        "    Health\n"
        "rule OnTick:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Dying\n"
        "rule OnFrame:\n"
        "    each:\n"
        "        Health\n"
        "    on frame:\n"
        "        add Broken\n");
    CHECK(analysis.reachable("OnTick"));
    CHECK(analysis.reachable("OnFrame"));
    CHECK(analysis.sets().contains(TraitNames{"Health", "Dying", "Broken"}));
}

TEST_CASE("Reachability: an event has a producer only when a reachable handler emits it", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Emitter:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        emit Exploded\n"
        "rule Listener:\n"
        "    each:\n"
        "        Health\n"
        "    on Exploded:\n"
        "        add Dying\n"
        "rule DeadEmitter:\n"
        "    each:\n"
        "        Crate\n"
        "    on tick:\n"
        "        emit Exploded\n");
    CHECK(analysis.reachable("Listener"));
    CHECK_FALSE(analysis.reachable("DeadEmitter"));
    CHECK(analysis.sets().contains(TraitNames{"Health", "Dying"}));
}

TEST_CASE("Reachability: an event emitted only by an unreachable handler has no producer", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule DeadEmitter:\n"
        "    each:\n"
        "        Crate\n"
        "    on tick:\n"
        "        emit Exploded\n"
        "rule Listener:\n"
        "    each:\n"
        "        Health\n"
        "    on Exploded:\n"
        "        add Dying\n");
    CHECK_FALSE(analysis.reachable("Listener"));
    CHECK(analysis.sets() == std::set<TraitNames>{{"Health"}});
}

TEST_CASE("Reachability: on added has a producer from a seed or a gaining transition", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Doom:\n"
        "    each:\n"
        "        Health\n"
        "    on tick:\n"
        "        add Dying\n"
        "rule Seeded:\n"
        "    each:\n"
        "        Health\n"
        "    on added Health:\n"
        "        add Stats\n"
        "rule Gained:\n"
        "    each:\n"
        "        Dying\n"
        "    on added Dying:\n"
        "        add Broken\n"
        "rule Never:\n"
        "    each:\n"
        "        Frozen\n"
        "    on added Frozen:\n"
        "        add Broken\n");
    CHECK(analysis.reachable("Seeded"));
    CHECK(analysis.reachable("Gained"));
    CHECK_FALSE(analysis.reachable("Never"));
}

TEST_CASE("Reachability: on removed has a producer from a remove", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "    Dying\n"
        "rule Heal:\n"
        "    each:\n"
        "        Dying\n"
        "    on tick:\n"
        "        remove Dying\n"
        "rule Healed:\n"
        "    each:\n"
        "        Health\n"
        "    on removed Dying:\n"
        "        add Stats\n"
        "rule NeverRemoved:\n"
        "    each:\n"
        "        Dying\n"
        "    on removed Health:\n"
        "        add Broken\n");
    CHECK(analysis.reachable("Healed"));
    CHECK_FALSE(analysis.reachable("NeverRemoved"));
}

TEST_CASE("Reachability: destroying an entity produces no on removed", "[archetype-reachability]") {
    const auto analysis = analyze(
        "entity Box:\n"
        "    Crate\n"
        "    Body\n"
        "rule Smash:\n"
        "    each:\n"
        "        Crate\n"
        "    on tick:\n"
        "        destroy\n"
        "rule Smashed:\n"
        "    each:\n"
        "        Body\n"
        "    on removed Crate:\n"
        "        add Broken\n");
    CHECK_FALSE(analysis.reachable("Smashed"));
}
// ── Self-contradictory domains ───────────────────────────────────────────────

TEST_CASE("Reachability: a domain requiring two variants of one state is an error in any module",
          "[archetype-reachability][diagnostics]") {
    const std::string rule =
        "entity Gate:\n"
        "    Door.Closed\n"
        "rule Both:\n"
        "    each:\n"
        "        Door.Open\n"
        "        Door.Closed\n"
        "    on tick:\n"
        "        add Broken\n";
    for (const std::string module : {"game", "std.gates"}) {
        const auto analysis = analyze(rule, module);
        INFO(analysis.describe());
        CHECK(analysis.error_count() == 1);
        CHECK(analysis.has("rule '" + module + ".Both' can never match"));
        CHECK(analysis.has("two variants of state '" + module + ".Door'"));
    }
}

TEST_CASE("Reachability: on removed of a trait the domain requires is self-contradictory",
          "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Box:\n"
        "    Crate\n"
        "rule Gone:\n"
        "    each:\n"
        "        Crate\n"
        "    on removed Crate:\n"
        "        add Broken\n",
        "std.boxes");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("rule 'std.boxes.Gone' can never match: it requires and excludes 'std.boxes.Crate'"));
}

// ── Dead domains and filters ─────────────────────────────────────────────────

TEST_CASE("Reachability: a user rule whose traits never meet is an error", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Ball:\n"
        "    Body\n"
        "entity Box:\n"
        "    Crate\n"
        "rule Both:\n"
        "    each:\n"
        "        Body\n"
        "        Crate\n"
        "    on tick:\n"
        "        add Broken\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("rule 'game.Both' never runs: no reachable archetype carries game.Body and game.Crate together"));
}

TEST_CASE("Reachability: a reached domain compiles", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(kSpawnEnemy +
                                  "rule Doom:\n"
                                  "    each:\n"
                                  "        Health\n"
                                  "    on tick:\n"
                                  "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

TEST_CASE("Reachability: an unreached pair binding is an error", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Ball:\n"
        "    Body\n"
        "rule Smash:\n"
        "    pairs:\n"
        "        body:\n"
        "            Body\n"
        "        box:\n"
        "            Crate\n"
        "    on tick:\n"
        "        add Broken to box\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("rule 'game.Smash' never runs: pair binding 'box' matches no reachable archetype"));
}

TEST_CASE("Reachability: a without: trait no matching archetype carries is an error",
          "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "entity Ice:\n"
        "    Frozen\n"
        "rule Warm:\n"
        "    each:\n"
        "        Health\n"
        "    without:\n"
        "        Frozen\n"
        "    on tick:\n"
        "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("rule 'game.Warm': `without: game.Frozen` excludes nothing"));
}

TEST_CASE("Reachability: a live without: compiles", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "    Frozen\n"
        "entity Other:\n"
        "    Health\n"
        "rule Warm:\n"
        "    each:\n"
        "        Health\n"
        "    without:\n"
        "        Frozen\n"
        "    on tick:\n"
        "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

// ── Missing producers ────────────────────────────────────────────────────────

TEST_CASE("Reachability: an event nobody emits is an error", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Boom:\n"
        "    each:\n"
        "        Health\n"
        "    on Exploded:\n"
        "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("handler 'game.Boom/on game.Exploded' never runs: nothing produces 'game.Exploded'"));
}

TEST_CASE("Reachability: on added of a trait nothing gains is an error", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Doomed:\n"
        "    each:\n"
        "        Health\n"
        "    on added Dying:\n"
        "        add Broken\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("nothing produces 'added game.Dying'"));
}

TEST_CASE("Reachability: an extern event always has a producer", "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule OnFrame:\n"
        "    each:\n"
        "        Health\n"
        "    on frame:\n"
        "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

TEST_CASE("Reachability: a dead domain and a missing producer on one handler give one error",
          "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Boom:\n"
        "    each:\n"
        "        Crate\n"
        "    on Exploded:\n"
        "        add Dying\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("rule 'game.Boom' never runs"));
}

TEST_CASE("Reachability: an emitter in an unreachable stdlib handler does not count",
          "[archetype-reachability][diagnostics][linker]") {
    ModuleSet modules(reachability_build_dir("dead_stdlib_emitter"));
    modules.compile("std.core",
                    {},
                    "module std.core\n"
                    "extern event frame:\n"
                    "    dt: float\n"
                    "pub phase tick:\n"
                    "    from:\n"
                    "        frame\n"
                    "pub trait Health:\n"
                    "    var hp: int = 10\n"
                    "pub trait Shield\n"
                    "pub trait Dying\n"
                    "pub event Hit\n"
                    "rule Deflect:\n"
                    "    each:\n"
                    "        Shield\n"
                    "    on tick:\n"
                    "        emit Hit\n");
    modules.compile("game",
                    {"std.core"},
                    "module game\n"
                    "use std.core\n"
                    "entity Hero:\n"
                    "    Health\n"
                    "rule Struck:\n"
                    "    each:\n"
                    "        Health\n"
                    "    on Hit:\n"
                    "        add Dying\n");
    ErrorReporter errors;
    const auto linked = modules.link({"std.core", "game"}, errors);
    CHECK_FALSE(linked.has_value());
    const auto& diagnostics = errors.diagnostics();
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message.contains("handler 'game.Struck/on std.core.Hit' never runs"));
}

// ── Stdlib pruning ───────────────────────────────────────────────────────────

TEST_CASE("Reachability: unreached stdlib rules and unproduced stdlib triggers compile and are recorded",
          "[archetype-reachability][diagnostics]") {
    const auto analysis = analyze(
        "entity Hero:\n"
        "    Health\n"
        "rule Physics:\n"
        "    each:\n"
        "        Body\n"
        "    on tick:\n"
        "        add Dying\n"
        "rule Save:\n"
        "    each:\n"
        "        Health\n"
        "    on Exploded:\n"
        "        add Dying\n"
        "rule Cold:\n"
        "    each:\n"
        "        Health\n"
        "    without:\n"
        "        Frozen\n"
        "    on tick:\n"
        "        add Broken\n",
        "std.physics");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
    CHECK_FALSE(analysis.reachable("Physics"));
    CHECK_FALSE(analysis.reachable("Save"));
    CHECK(analysis.reachable("Cold"));
}

// ── Observable spawn order ───────────────────────────────────────────────────

namespace {

const std::string kPickups =
    "trait Pickup:\n"
    "    var value: int = 0\n"
    "template Coin:\n"
    "    Pickup\n"
    "template Gem:\n"
    "    Pickup\n"
    "    Crate\n";

std::string spawners(const std::string& gem_extra = "") {
    return "rule SpawnCoins:\n"
           "    on tick:\n"
           "        spawn Coin()\n"
           "rule SpawnGems:\n" +
           gem_extra +
           "    on tick:\n"
           "        spawn Gem()\n";
}

const std::string kLimitReader =
    "rule Take:\n"
    "    each:\n"
    "        Pickup as p\n"
    "    limit: 1\n"
    "    on tick:\n"
    "        p.value = 1\n";

}  // namespace

TEST_CASE("Reachability: two unordered spawners feeding a limit: rule are an error",
          "[archetype-reachability][spawn-order]") {
    const auto analysis = analyze(kPickups + spawners() + kLimitReader);
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 1);
    CHECK(analysis.has("the order of spawns by 'game.SpawnCoins/on game.tick' and 'game.SpawnGems/on game.tick' is "
                       "observable by 'game.Take/on game.tick'"));
    CHECK(analysis.has("after: game.SpawnCoins"));
    CHECK(analysis.has("after: game.SpawnGems"));
}

TEST_CASE("Reachability: ordering the spawners either way compiles", "[archetype-reachability][spawn-order]") {
    const auto gems_after = analyze(kPickups + spawners("    after:\n        SpawnCoins\n") + kLimitReader);
    INFO(gems_after.describe());
    CHECK(gems_after.error_count() == 0);
    const auto coins_after = analyze(kPickups +
                                     "rule SpawnGems:\n"
                                     "    on tick:\n"
                                     "        spawn Gem()\n"
                                     "rule SpawnCoins:\n"
                                     "    after:\n"
                                     "        SpawnGems\n"
                                     "    on tick:\n"
                                     "        spawn Coin()\n" +
                                     kLimitReader);
    INFO(coins_after.describe());
    CHECK(coins_after.error_count() == 0);
}

TEST_CASE("Reachability: spawners whose shared readers only commute compile", "[archetype-reachability][spawn-order]") {
    const auto analysis = analyze(kPickups + spawners() +
                                  "rule Grow:\n"
                                  "    each:\n"
                                  "        Pickup as p\n"
                                  "    on tick:\n"
                                  "        p.value += 1\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

TEST_CASE("Reachability: a reader that sees only one spawner's entities compiles",
          "[archetype-reachability][spawn-order]") {
    const auto analysis = analyze(kPickups + spawners() +
                                  "rule Take:\n"
                                  "    each:\n"
                                  "        Pickup as p\n"
                                  "        Crate\n"
                                  "    limit: 1\n"
                                  "    on tick:\n"
                                  "        p.value = 1\n");
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

TEST_CASE("Reachability: a lifecycle spawner runs after the phase spawner that feeds it",
          "[archetype-reachability][spawn-order]") {
    const auto analysis = analyze(kPickups +
                                  "rule SpawnCoins:\n"
                                  "    on tick:\n"
                                  "        spawn Coin()\n"
                                  "rule SpawnGems:\n"
                                  "    each:\n"
                                  "        Pickup\n"
                                  "    on added Pickup:\n"
                                  "        spawn Gem()\n" +
                                  kLimitReader);
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

TEST_CASE("Reachability: spawners in different phases compile", "[archetype-reachability][spawn-order]") {
    const auto analysis = analyze(kPickups +
                                  "phase late:\n"
                                  "    after:\n"
                                  "        tick\n"
                                  "rule SpawnCoins:\n"
                                  "    on tick:\n"
                                  "        spawn Coin()\n"
                                  "rule SpawnGems:\n"
                                  "    on late:\n"
                                  "        spawn Gem()\n" +
                                  kLimitReader);
    INFO(analysis.describe());
    CHECK(analysis.error_count() == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
