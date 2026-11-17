// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/archetype_reachability.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/module_resolver.hpp"
#include "frontend/parser.hpp"
#include "frontend/query_costs.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace cactus;
namespace fs = std::filesystem;

namespace {

const std::string kPrelude =
    "module game\n"
    "use std.query as query\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase tick:\n"
    "    from:\n"
    "        frame\n"
    "phase late:\n"
    "    from:\n"
    "        frame\n"
    "event Ping\n"
    "trait Unit:\n"
    "    let id: int = 0\n"
    "    var team: int = 0\n"
    "    var owner: int = 0\n"
    "trait Health:\n"
    "    var current: float = 1.0\n"
    "trait Want:\n"
    "    var id: int = 0\n"
    "trait Hero:\n"
    "    var id: int = 0\n"
    "trait Session\n"
    "entity W:\n"
    "    Want\n"
    "entity Solo:\n"
    "    Hero\n"
    "entity Game:\n"
    "    Session\n"
    "template Grunt:\n"
    "    Unit\n"
    "    Health\n"
    "rule Make:\n"
    "    on frame:\n"
    "        spawn Grunt()\n"
    "        emit Ping\n";

std::string read_file(const fs::path& path) {
    std::ifstream file(path);
    std::stringstream source;
    source << file.rdbuf();
    return source.str();
}

fs::path stdlib_root() {
    return fs::path(CACTUS_TEST_SOURCE_DIR) / "stdlib";
}

struct Compiled {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
};

Compiled compile_source(const std::string& source,
                        const std::string& filename,
                        const ModuleImports& imports,
                        ErrorReporter& errors) {
    Compiled compiled;
    Lexer lexer(source, filename, errors);
    Parser parser(lexer.tokenize(), errors);
    compiled.ast = std::make_unique<ProgramNode>(parser.parse_program());
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    compiled.program = analyzer.analyze(*compiled.ast, imports);
    return compiled;
}

const ModuleImports& query_imports() {
    static const ModuleImports imports = [] {
        ErrorReporter errors;
        const auto query = compile_source(read_file(stdlib_root() / "std" / "query.cactus"), "query.cactus", {}, errors);
        ModuleImports result;
        result.add("query", ModuleArtifact::extract_pub_symbols("std.query", query.program), {}, query.program.non_pub_templates);
        return result;
    }();
    return imports;
}

std::vector<Diagnostic> diagnose(const std::string& body) {
    ErrorReporter errors;
    auto compiled = compile_source(kPrelude + body, "game.cactus", query_imports(), errors);
    for (const auto& diagnostic : errors.diagnostics()) {
        UNSCOPED_INFO(diagnostic.location.line << ": " << diagnostic.message);
    }
    REQUIRE_FALSE(errors.has_errors());
    derive_archetype_reachability(compiled.program, errors);
    REQUIRE_FALSE(errors.has_errors());
    check_query_costs(compiled.program, errors);
    return errors.diagnostics();
}

std::size_t cost_warnings(const std::vector<Diagnostic>& diagnostics, const std::string& rule) {
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics, [&](const Diagnostic& d) {
        return d.level == DiagnosticLevel::Warning && d.message.contains("rule '" + rule + "'") &&
               !d.message.starts_with("pair rule");
    }));
}

// The one rule named R of each test.
std::size_t warnings_for(const std::string& rule_body) {
    return cost_warnings(diagnose("rule R:\n" + rule_body), "R");
}

}  // namespace

TEST_CASE("Query costs: a scan runs once per row of a phase pass", "[query-costs]") {
    const auto diagnostics = diagnose(
        "rule R:\n"
        "    each: Unit\n"
        "    on tick:\n"
        "        let n = query.count[Unit]()\n");
    REQUIRE(cost_warnings(diagnostics, "R") == 1);
    const auto warning = std::ranges::find_if(diagnostics, [](const Diagnostic& d) {
        return d.level == DiagnosticLevel::Warning && d.message.contains("rule 'R'");
    });
    CHECK(warning->location.filename == "game.cactus");
    CHECK(warning->location.line > 1);
    CHECK(warning->message.contains("std.query.count"));
}

TEST_CASE("Query costs: a pair pass multiplies a scan", "[query-costs]") {
    CHECK(warnings_for("    pairs:\n"
                       "        a: Unit\n"
                       "        b: Unit\n"
                       "    on tick:\n"
                       "        let n = query.count[Unit]()\n") == 1);
}

TEST_CASE("Query costs: a pair pass with one unnamed binding still multiplies", "[query-costs]") {
    CHECK(warnings_for("    pairs:\n"
                       "        a: Unit\n"
                       "        b: Hero\n"
                       "    on tick:\n"
                       "        let n = query.count[Unit]()\n") == 1);
}

TEST_CASE("Query costs: a named-only filter is a bounded scan", "[query-costs]") {
    CHECK(warnings_for("    each: Unit\n"
                       "    on tick:\n"
                       "        let n = query.first[Session]()\n") == 0);
}

TEST_CASE("Query costs: a single filter is a bounded scan", "[query-costs][single-traits]") {
    const std::string land =
        "single trait Land\n"
        "template Plot:\n"
        "    Land\n"
        "rule MakeLand:\n"
        "    on frame:\n"
        "        spawn Plot()\n";
    CHECK(cost_warnings(diagnose(land + "rule R:\n"
                                        "    each: Unit\n"
                                        "    on tick:\n"
                                        "        let n = query.first[Land]()\n"),
                        "R") == 0);
    CHECK(cost_warnings(diagnose(land + "rule R:\n"
                                        "    each: Land\n"
                                        "    on tick:\n"
                                        "        let n = query.count[Unit]()\n"),
                        "R") == 0);
}

TEST_CASE("Query costs: exists, parent and children are not world scans", "[query-costs]") {
    CHECK(warnings_for("    each: Unit\n"
                       "    on tick:\n"
                       "        let any = query.exists[Unit]()\n"
                       "        let up = query.parent[Unit](of = self)\n"
                       "        let kids = query.children[Unit](of = self)\n") == 0);
}

TEST_CASE("Query costs: a scan nested in a scan loop warns on any trigger", "[query-costs]") {
    const std::string nested =
        "        for a in query.all[Unit]():\n"
        "            let n = query.count[Unit]()\n";
    CHECK(warnings_for("    on tick:\n" + nested) == 1);
    CHECK(warnings_for("    on Ping:\n" + nested) == 1);
}

TEST_CASE("Query costs: a loop over a list bound to a scan is a scan loop", "[query-costs]") {
    CHECK(warnings_for("    on tick:\n"
                       "        let units = query.all[Unit]()\n"
                       "        for a in units:\n"
                       "            let n = query.count[Unit]()\n") == 1);
}

TEST_CASE("Query costs: a scan after a scan loop is not nested", "[query-costs]") {
    CHECK(warnings_for("    on tick:\n"
                       "        for a in query.all[Unit]():\n"
                       "            let x = 1\n"
                       "        let n = query.count[Unit]()\n") == 0);
}

TEST_CASE("Query costs: a loop over a bounded scan does not multiply", "[query-costs]") {
    CHECK(warnings_for("    on tick:\n"
                       "        for a in query.all[Session]():\n"
                       "            let n = query.count[Unit]()\n") == 0);
}

TEST_CASE("Query costs: an unbounded loop outside a bounded loop still multiplies", "[query-costs]") {
    CHECK(warnings_for("    on tick:\n"
                       "        for a in query.all[Unit]():\n"
                       "            for b in query.all[Session]():\n"
                       "                let n = query.count[Unit]()\n") == 1);
}

TEST_CASE("Query costs: one scan per activation is linear", "[query-costs]") {
    CHECK(warnings_for("    on tick:\n"
                       "        let units = query.all[Unit]()\n") == 0);
}

TEST_CASE("Query costs: a named row is not a multiplier", "[query-costs]") {
    CHECK(warnings_for("    each: Hero\n"
                       "    on tick:\n"
                       "        let units = query.all[Unit]()\n") == 0);
}

TEST_CASE("Query costs: event handlers are not multiplied", "[query-costs]") {
    CHECK(warnings_for("    each: Unit\n"
                       "    on Ping:\n"
                       "        let n = query.count[Unit]()\n") == 0);
}

TEST_CASE("Query costs: a key lookup shared by two handlers reports once", "[query-costs]") {
    CHECK(warnings_for("    each: Unit as u\n"
                       "    where:\n"
                       "        u.id == W[Want].id\n"
                       "    on tick:\n"
                       "        u.owner = 1\n"
                       "    on late:\n"
                       "        u.owner = 2\n") == 1);
}

TEST_CASE("Query costs: a handler no entity can reach is not reported", "[query-costs]") {
    ErrorReporter errors;
    auto compiled = compile_source(kPrelude +
                                       "rule R:\n"
                                       "    each: Unit\n"
                                       "    on tick:\n"
                                       "        let n = query.count[Unit]()\n",
                                   "game.cactus",
                                   query_imports(),
                                   errors);
    REQUIRE_FALSE(errors.has_errors());
    derive_archetype_reachability(compiled.program, errors);
    for (const auto& contract : compiled.program.handler_contracts) {
        compiled.program.reachability.unreachable_handlers.push_back(
            HandlerIdentity{.rule = contract.rule, .trigger = contract.trigger});
    }
    check_query_costs(compiled.program, errors);
    CHECK(cost_warnings(errors.diagnostics(), "R") == 0);
}

TEST_CASE("Query costs: a key lookup reads another entity", "[query-costs]") {
    const auto diagnostics = diagnose(
        "rule R:\n"
        "    each: Unit as u\n"
        "    where:\n"
        "        u.id == W[Want].id\n"
        "    on tick:\n"
        "        u.team = 1\n");
    REQUIRE(cost_warnings(diagnostics, "R") == 1);
    const auto warning = std::ranges::find_if(diagnostics, [](const Diagnostic& d) {
        return d.level == DiagnosticLevel::Warning && d.message.contains("rule 'R'");
    });
    CHECK(warning->message.contains("id"));
}

TEST_CASE("Query costs: any key that does not read the row is a key lookup", "[query-costs]") {
    SECTION("constant") {
        CHECK(warnings_for("    each: Unit as u\n"
                           "    where:\n"
                           "        u.team == 3\n"
                           "    on tick:\n"
                           "        u.owner = 1\n") == 1);
    }
    SECTION("the key on the left") {
        CHECK(warnings_for("    each: Unit as u\n"
                           "    where:\n"
                           "        W[Want].id == u.id\n"
                           "    on tick:\n"
                           "        u.owner = 1\n") == 1);
    }
    SECTION("self") {
        CHECK(warnings_for("    each: Unit\n"
                           "    where:\n"
                           "        self == Solo\n"
                           "    on tick:\n"
                           "        let x = 1\n") == 1);
    }
    SECTION("one conjunct of several") {
        CHECK(warnings_for("    each: Unit as u\n"
                           "    where:\n"
                           "        u.team > 0 and u.id == W[Want].id\n"
                           "    on tick:\n"
                           "        u.owner = 1\n") == 1);
    }
    SECTION("an event trigger") {
        CHECK(warnings_for("    each: Unit as u\n"
                           "    where:\n"
                           "        u.id == W[Want].id\n"
                           "    on Ping:\n"
                           "        u.owner = 1\n") == 1);
    }
}

TEST_CASE("Query costs: a range filter is the cost of the pass", "[query-costs]") {
    CHECK(warnings_for("    each: Health as hp\n"
                       "    where:\n"
                       "        hp.current < 30.0\n"
                       "    on tick:\n"
                       "        hp.current = 1.0\n") == 0);
}

TEST_CASE("Query costs: comparing two row fields is not a key lookup", "[query-costs]") {
    CHECK(warnings_for("    each: Unit as u\n"
                       "    where:\n"
                       "        u.team == u.owner\n"
                       "    on tick:\n"
                       "        u.team = 1\n") == 0);
}

TEST_CASE("Query costs: an equality on a computed row value is not a key lookup", "[query-costs]") {
    CHECK(warnings_for("    each: Unit as u\n"
                       "    where:\n"
                       "        u.team + 1 == 3\n"
                       "    on tick:\n"
                       "        u.owner = 1\n") == 0);
}

TEST_CASE("Query costs: an equality inside an or is not a top-level conjunct", "[query-costs]") {
    CHECK(warnings_for("    each: Unit as u\n"
                       "    where:\n"
                       "        u.team == 1 or u.team == 2\n"
                       "    on tick:\n"
                       "        u.owner = 1\n") == 0);
}

TEST_CASE("Query costs: a named domain bounds a key lookup", "[query-costs]") {
    CHECK(warnings_for("    each: Hero as h\n"
                       "    where:\n"
                       "        h.id == W[Want].id\n"
                       "    on tick:\n"
                       "        h.id = 1\n") == 0);
}

TEST_CASE("Query costs: facts survive a module artifact", "[query-costs]") {
    ErrorReporter errors;
    auto compiled = compile_source(kPrelude +
                                       "rule R:\n"
                                       "    each: Unit as u\n"
                                       "    where:\n"
                                       "        u.id == W[Want].id\n"
                                       "    on tick:\n"
                                       "        for a in query.all[Unit]():\n"
                                       "            let n = query.count[Health, not Want]()\n",
                                   "game.cactus",
                                   query_imports(),
                                   errors);
    REQUIRE_FALSE(errors.has_errors());
    const auto dir = fs::path(CACTUS_TEST_BINARY_DIR) / "query_cost_artifact";
    fs::create_directories(dir);
    ModuleArtifact writer(errors);
    REQUIRE(writer.save(compiled.program, "game", dir));
    ModuleArtifact reader(errors);
    std::string name;
    const auto loaded = reader.load(dir / "game.cmod", name);
    REQUIRE(loaded.has_value());
    const auto find_rule = [](const DecoratedProgram& program) {
        return std::ranges::find_if(program.handler_contracts,
                                    [](const auto& contract) { return contract.rule.local_name == "R"; });
    };
    const auto before = find_rule(compiled.program);
    const auto after  = find_rule(*loaded);
    REQUIRE(after != loaded->handler_contracts.end());
    REQUIRE(before->world_scans.size() == 2);
    CHECK(before->world_scans[1].enclosing == std::optional<std::size_t>{0});
    CHECK(before->world_scans[1].filter.size() == 1);
    CHECK(after->world_scans == before->world_scans);
    REQUIRE(before->key_lookups.size() == 1);
    CHECK(after->key_lookups == before->key_lookups);
}

namespace {

// Every stdlib module, compiled in dependency order.
class StdlibBuild {
public:
    StdlibBuild() {
        const auto dir = fs::path(CACTUS_TEST_BINARY_DIR) / "query_cost_stdlib";
        fs::create_directories(dir);
        const auto root = dir / "all_stdlib.cactus";
        std::ofstream out(root);
        out << "module all_stdlib\n";
        for (const auto& entry : fs::recursive_directory_iterator(stdlib_root())) {
            if (entry.path().extension() == ".cactus" && !entry.path().string().contains("build")) {
                auto name = fs::relative(entry.path(), stdlib_root()).replace_extension().generic_string();
                std::ranges::replace(name, '/', '.');
                out << "use " << name << "\n";
            }
        }
        out.close();
        ErrorReporter errors;
        ModuleResolver resolver(errors);
        const auto resolved = resolver.resolve(root.string(), {stdlib_root()});
        REQUIRE_FALSE(errors.has_errors());
        compile("std.core", (stdlib_root() / "std" / "core.cactus"), {});
        for (const auto& module : resolved) {
            if (module.qualified_name != "all_stdlib" && !compiled_.contains(module.qualified_name)) {
                compile(module.qualified_name, module.file_path, module.dependencies);
            }
        }
    }

    // Analyzer warnings, plus query cost warnings with every domain and filter unbounded.
    [[nodiscard]] std::vector<std::string> worst_case_warnings() const {
        auto warnings = analysis_warnings_;
        for (const auto& [name, compiled] : compiled_) {
            ErrorReporter errors;
            check_query_costs(compiled.program, errors);
            collect_warnings(name, errors, warnings);
        }
        return warnings;
    }

    [[nodiscard]] std::size_t size() const { return compiled_.size(); }

private:
    static void collect_warnings(const std::string& module, const ErrorReporter& errors, std::vector<std::string>& out) {
        for (const auto& diagnostic : errors.diagnostics()) {
            if (diagnostic.level == DiagnosticLevel::Warning) {
                out.push_back(module + ":" + std::to_string(diagnostic.location.line) + ": " + diagnostic.message);
            }
        }
    }

    void add_import(ModuleImports& imports, const std::string& qualifier, const std::string& module) const {
        const auto& program = compiled_.at(module).program;
        imports.add(qualifier, ModuleArtifact::extract_pub_symbols(module, program), {}, program.non_pub_templates);
    }

    void compile(const std::string& name, const fs::path& path, const std::vector<std::string>& dependencies) {
        const auto source = read_file(path);
        ErrorReporter parse_errors;
        Lexer lexer(source, path.string(), parse_errors);
        Parser parser(lexer.tokenize(), parse_errors);
        const auto ast = parser.parse_program();
        ModuleImports imports;
        if (name != "std.core") {
            add_import(imports, "std.core", "std.core");
        }
        for (const auto& dependency : dependencies) {
            if (!compiled_.contains(dependency)) {
                continue;
            }
            bool aliased = false;
            for (const auto& declaration : ast.declarations) {
                const auto* use = std::get_if<UseNode>(&declaration);
                if (use != nullptr && use->module_name == dependency) {
                    add_import(imports, use->alias.value_or(dependency), dependency);
                    aliased = true;
                }
            }
            if (!aliased) {
                add_import(imports, dependency, dependency);
            }
        }
        ErrorReporter errors;
        auto compiled = compile_source(source, path.string(), imports, errors);
        for (const auto& diagnostic : errors.diagnostics()) {
            UNSCOPED_INFO(name << ":" << diagnostic.location.line << ": " << diagnostic.message);
        }
        REQUIRE_FALSE(errors.has_errors());
        collect_warnings(name, errors, analysis_warnings_);
        compiled_.emplace(name, std::move(compiled));
    }

    std::map<std::string, Compiled> compiled_;
    std::vector<std::string> analysis_warnings_;
};

}  // namespace

TEST_CASE("Query costs: the standard library reports no warning", "[query-costs]") {
    const StdlibBuild stdlib;
    REQUIRE(stdlib.size() > 10);
    const auto warnings = stdlib.worst_case_warnings();
    for (const auto& warning : warnings) {
        UNSCOPED_INFO(warning);
    }
    CHECK(warnings.empty());
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
