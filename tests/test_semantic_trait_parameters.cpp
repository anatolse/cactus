// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/lexer.hpp"
#include "frontend/module_artifact.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace cactus;
namespace fs = std::filesystem;

namespace {

const std::string kPrelude =
    "module game\n"
    "extern event frame:\n"
    "    dt: float\n"
    "phase fixed_tick:\n"
    "    from:\n"
    "        frame\n"
    "event Hit:\n"
    "    damage: int\n"
    "event Heal:\n"
    "    amount: int\n"
    "event Warn\n"
    "trait Health:\n"
    "    var current: float = 100.0\n"
    "    var max: float = 100.0\n"
    "trait Armor:\n"
    "    var value: float = 0.0\n"
    "trait Body:\n"
    "    var position: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var scale: float = 1.0\n"
    "trait Spike\n"
    "trait Match:\n"
    "    var over: bool = false\n"
    "    var round: int = 0\n"
    "state EnemyMode:\n"
    "    Idle\n"
    "    Chase:\n"
    "        var speed: float = 2.0\n"
    "relation Walk:\n"
    "    from: Body\n"
    "    to: Body\n"
    "entity Game:\n"
    "    Match\n";

struct Analysis {
    std::unique_ptr<ProgramNode> ast;
    DecoratedProgram program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has(const std::string& text) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error && d.message.contains(text);
        });
    }
    [[nodiscard]] bool has_all(std::initializer_list<std::string> parts) const {
        return std::ranges::any_of(diagnostics, [&](const Diagnostic& d) {
            return d.level == DiagnosticLevel::Error &&
                   std::ranges::all_of(parts, [&](const std::string& part) { return d.message.contains(part); });
        });
    }
    [[nodiscard]] bool clean() const {
        return std::ranges::none_of(diagnostics, [](const Diagnostic& d) { return d.level == DiagnosticLevel::Error; });
    }
};

std::string describe(const Analysis& analysis) {
    std::string text;
    for (const auto& d : analysis.diagnostics) {
        text += std::to_string(d.location.line) + ": " + d.message + "\n";
    }
    return text;
}

Analysis analyze_source(const std::string& source, const std::string& module = "game",
                        const ModuleImports& imports = {}) {
    Analysis analysis;
    ErrorReporter errors;
    Lexer lexer(source, module + ".cactus", errors);
    Parser parser(lexer.tokenize(), errors);
    analysis.ast = std::make_unique<ProgramNode>(parser.parse_program());
    INFO((errors.diagnostics().empty() ? std::string() : errors.diagnostics().front().message));
    REQUIRE_FALSE(errors.has_errors());
    SemanticAnalyzer analyzer(errors);
    analysis.program     = analyzer.analyze(*analysis.ast, imports);
    analysis.diagnostics = errors.diagnostics();
    return analysis;
}

Analysis analyze(const std::string& body) {
    return analyze_source(kPrelude + body);
}

// A module compiled and exported through a saved artifact, the way an importing module sees it.
ImportedSymbols exported_module(const std::string& name, const std::string& source) {
    auto analysis = analyze_source(source, name);
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto build_dir = fs::path(CACTUS_TEST_FIXTURES_DIR) / ("trait_parameters_build_" + name);
    ErrorReporter errors;
    ModuleArtifact artifact(errors);
    REQUIRE(artifact.save(analysis.program, name, build_dir));
    auto symbols = artifact.extract_pub_symbols(build_dir / ModuleArtifact::artifact_filename(name));
    REQUIRE(symbols.has_value());
    std::error_code ec;
    fs::remove_all(build_dir, ec);
    return *symbols;
}

const std::string kShapes =
    "module shapes\n"
    "pub trait Place:\n"
    "    var position: vec3 = vec3(0.0, 0.0, 0.0)\n"
    "    var size: float = 1.0\n";

}  // namespace

// ── 1. Types and declarations ───────────────────────────────────────────────

TEST_CASE("trait parameter: a func may take a trait", "[trait-parameters]") {
    const auto analysis = analyze("func is_low(h: Health) bool:\n"
                                  "    return h.current < 30.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    const auto& param = analysis.program.funcs.at("is_low").params.at(0);
    CHECK(param.type.kind == TypeKind::TraitRef);
    REQUIRE(param.type.symbol_id.has_value());
    CHECK(param.type.symbol_id->local_name == "Health");
}

TEST_CASE("trait parameter: an imported trait is accepted", "[trait-parameters]") {
    ModuleImports imports;
    imports.add("sh", exported_module("shapes", kShapes));
    const auto analysis = analyze_source("module game\n"
                                         "use shapes as sh\n"
                                         "func height(t: sh.Place) float:\n"
                                         "    return t.position.y\n",
                                         "game", imports);
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(analysis.program.funcs.at("height").params.at(0).type.kind == TypeKind::TraitRef);
}

TEST_CASE("trait parameter: a state variant counts as a trait", "[trait-parameters]") {
    const auto analysis = analyze("func fast(c: EnemyMode.Chase) bool:\n"
                                  "    return c.speed > 1.0\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(analysis.program.funcs.at("fast").params.at(0).type.kind == TypeKind::TraitRef);
}

TEST_CASE("trait parameter: a func may take an event", "[trait-parameters]") {
    const auto analysis = analyze("func is_critical(hit: Hit) bool:\n"
                                  "    return hit.damage > 50\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
    CHECK(analysis.program.funcs.at("is_critical").params.at(0).type.kind == TypeKind::EventRef);
}

TEST_CASE("trait parameter: a marker trait is rejected", "[trait-parameters]") {
    const auto analysis = analyze("func sharp(s: Spike) bool:\n"
                                  "    return true\n");
    INFO(describe(analysis));
    CHECK(analysis.has_all({"'s'", "'Spike'", "marker"}));
}

TEST_CASE("trait parameter: a state slot, relation or phase is rejected", "[trait-parameters]") {
    SECTION("state slot") {
        const auto analysis = analyze("func f(m: EnemyMode) bool:\n"
                                      "    return true\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'m'", "'EnemyMode'"}));
    }
    SECTION("relation") {
        const auto analysis = analyze("func f(w: Walk) bool:\n"
                                      "    return true\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'w'", "'Walk'"}));
    }
    SECTION("phase") {
        const auto analysis = analyze("phase settle:\n"
                                      "    after:\n"
                                      "        fixed_tick\n"
                                      "func f(p: settle) bool:\n"
                                      "    return true\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'p'", "'settle'"}));
    }
}

TEST_CASE("trait parameter: an extern func can't take a trait or event", "[trait-parameters]") {
    SECTION("trait") {
        const auto analysis = analyze("extern func clamp_view(camera: Body) vec2\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'camera'", "extern func", "trait or event parameter"}));
    }
    SECTION("event") {
        const auto analysis = analyze("extern func log_hit(hit: Hit) bool\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'hit'", "extern func", "trait or event parameter"}));
    }
}

TEST_CASE("trait parameter: a trait or event is not a value type elsewhere", "[trait-parameters]") {
    SECTION("return type") {
        const auto analysis = analyze("func f(h: Health) Health:\n"
                                      "    return h\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Health'", "not a value type"}));
    }
    SECTION("event return type") {
        const auto analysis = analyze("func f(h: Hit) Hit:\n"
                                      "    return h\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Hit'", "not a value type"}));
    }
    SECTION("local annotation") {
        const auto analysis = analyze("func f(h: Health) float:\n"
                                      "    let copy: Health = h\n"
                                      "    return 1.0\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Health'", "not a value type"}));
    }
    SECTION("struct field") {
        const auto analysis = analyze("struct Pack:\n"
                                      "    h: Health\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Health'", "not a value type"}));
    }
    SECTION("trait field") {
        const auto analysis = analyze("trait Holder:\n"
                                      "    var h: Health\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Health'", "not a value type"}));
    }
}

// ── 2. Read-only use in the body ────────────────────────────────────────────

TEST_CASE("trait parameter: field and nested member reads compile", "[trait-parameters]") {
    const auto analysis = analyze("func above(b: Body) bool:\n"
                                  "    let y = b.position.y\n"
                                  "    return y > b.scale\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("trait parameter: passing on to the same type compiles", "[trait-parameters]") {
    const auto analysis = analyze("func has_area(b: Body) bool:\n"
                                  "    return b.scale > 0.0\n"
                                  "func visible(b: Body, min_y: float) bool:\n"
                                  "    return has_area(b) and b.position.y > min_y\n"
                                  "func critical(hit: Hit) bool:\n"
                                  "    return hit.damage > 50\n"
                                  "func very_critical(hit: Hit) bool:\n"
                                  "    return critical(hit) and hit.damage > 90\n");
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

TEST_CASE("trait parameter: the parameter is read-only", "[trait-parameters]") {
    SECTION("field assignment") {
        const auto analysis = analyze("func f(h: Health) float:\n"
                                      "    h.current = 0.0\n"
                                      "    return 1.0\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "read-only"}));
    }
    SECTION("compound assignment") {
        const auto analysis = analyze("func f(h: Health) float:\n"
                                      "    h.current += 1.0\n"
                                      "    return 1.0\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "read-only"}));
    }
    SECTION("event field assignment") {
        const auto analysis = analyze("func f(hit: Hit) int:\n"
                                      "    hit.damage = 0\n"
                                      "    return 1\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'hit'", "read-only"}));
    }
}

TEST_CASE("trait parameter: any value use is rejected", "[trait-parameters]") {
    const auto rejected = [](const std::string& body) {
        const auto analysis = analyze("func f(h: Health, other: Health) float:\n" + body);
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "read field by field"}));
    };
    SECTION("let initializer") {
        rejected("    let copy = h\n"
                 "    return 1.0\n");
    }
    SECTION("comparison") {
        rejected("    if h == other:\n"
                 "        return 0.0\n"
                 "    return 1.0\n");
    }
    SECTION("operand") {
        rejected("    return h + 1.0\n");
    }
    SECTION("match subject") {
        rejected("    match h:\n"
                 "        _ =>\n"
                 "            return 0.0\n"
                 "    return 1.0\n");
    }
    SECTION("return") {
        const auto analysis = analyze("func f(hit: Hit) int:\n"
                                      "    return hit\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'hit'", "read field by field"}));
    }
}

TEST_CASE("trait parameter: passing to a different type is rejected", "[trait-parameters]") {
    SECTION("different trait") {
        const auto analysis = analyze("func g(a: Armor) float:\n"
                                      "    return a.value\n"
                                      "func f(h: Health) float:\n"
                                      "    return g(h)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Health'", "'Armor'"}));
    }
    SECTION("value parameter") {
        const auto analysis = analyze("func g(x: float) float:\n"
                                      "    return x\n"
                                      "func f(h: Health) float:\n"
                                      "    return g(h)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'x'", "'Health'"}));
    }
    SECTION("trait to event") {
        const auto analysis = analyze("func g(hit: Hit) int:\n"
                                      "    return hit.damage\n"
                                      "func f(h: Health) int:\n"
                                      "    return g(h)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Hit'", "'Health'"}));
    }
}

TEST_CASE("trait parameter: a named read in a func stays a purity error", "[trait-parameters]") {
    const auto analysis = analyze("func over() bool:\n"
                                  "    return Game[Match].over\n");
    INFO(describe(analysis));
    CHECK_FALSE(analysis.clean());
}

// ── 3. Call sites ───────────────────────────────────────────────────────────

namespace {

const std::string kFuncs =
    "func is_low(h: Health) bool:\n"
    "    return h.current < 30.0\n"
    "func above(b: Body) bool:\n"
    "    return b.position.y > 0.0\n"
    "func is_over(m: Match) bool:\n"
    "    return m.over\n"
    "func is_critical(hit: Hit) bool:\n"
    "    return hit.damage > 50\n"
    "struct Pack:\n"
    "    current: float\n";

Analysis analyze_calls(const std::string& rules) {
    return analyze(kFuncs + rules);
}

void check_clean(const Analysis& analysis) {
    INFO(describe(analysis));
    CHECK(analysis.clean());
}

}  // namespace

TEST_CASE("trait argument: legal argument forms", "[trait-parameters]") {
    SECTION("each alias in where and in the body") {
        check_clean(analyze_calls("rule Low:\n"
                                  "    each:\n"
                                  "        Health as hp\n"
                                  "    where:\n"
                                  "        is_low(hp)\n"
                                  "    on fixed_tick:\n"
                                  "        if is_low(hp):\n"
                                  "            emit Warn\n"));
    }
    SECTION("pair alias and bracketed binding") {
        check_clean(analyze_calls("rule Touch:\n"
                                  "    pairs:\n"
                                  "        mover:\n"
                                  "            Body as mb\n"
                                  "        other:\n"
                                  "            Body\n"
                                  "    where:\n"
                                  "        above(mb)\n"
                                  "        above(other[Body])\n"
                                  "    on fixed_tick:\n"
                                  "        let up = above(mb) and above(other[Body])\n"));
    }
    SECTION("named read") {
        check_clean(analyze_calls("rule End:\n"
                                  "    on fixed_tick:\n"
                                  "        if is_over(Game[Match]):\n"
                                  "            emit Warn\n"));
    }
    SECTION("trait-match arm alias") {
        check_clean(analyze_calls("rule Scan:\n"
                                  "    on fixed_tick:\n"
                                  "        for e in query.all[Health]():\n"
                                  "            match e:\n"
                                  "                Health as h =>\n"
                                  "                    let low = is_low(h)\n"));
    }
    SECTION("event alias") {
        check_clean(analyze_calls("rule Crit:\n"
                                  "    on Hit as h:\n"
                                  "        if is_critical(h):\n"
                                  "            emit Warn\n"));
    }
}

TEST_CASE("trait argument: rejected argument forms", "[trait-parameters]") {
    SECTION("entity_id") {
        const auto analysis = analyze_calls("rule Low:\n"
                                            "    each:\n"
                                            "        Health as hp\n"
                                            "    on fixed_tick:\n"
                                            "        let target = self\n"
                                            "        let low = is_low(target)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "must be a trait the rule binds"}));
    }
    SECTION("struct value") {
        const auto analysis = analyze_calls("rule Low:\n"
                                            "    on fixed_tick:\n"
                                            "        let p = Pack(current = 1.0)\n"
                                            "        let low = is_low(p)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "must be a trait the rule binds"}));
    }
    SECTION("call result") {
        const auto analysis = analyze_calls("rule Low:\n"
                                            "    each:\n"
                                            "        Health as hp\n"
                                            "    on fixed_tick:\n"
                                            "        let low = is_low(is_low(hp))\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "must be a trait the rule binds"}));
    }
    SECTION("a trait without an alias") {
        const auto analysis = analyze_calls("rule Low:\n"
                                            "    each:\n"
                                            "        Health\n"
                                            "    on fixed_tick:\n"
                                            "        let low = is_low(Health)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'h'", "must be a trait the rule binds"}));
    }
    SECTION("wrong trait") {
        const auto analysis = analyze_calls("rule Low:\n"
                                            "    each:\n"
                                            "        Armor as ar\n"
                                            "    on fixed_tick:\n"
                                            "        let low = is_low(ar)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Health'", "'Armor'"}));
    }
    SECTION("a bracketed trait passed as a value") {
        const auto analysis = analyze_calls("func twice(x: float) float:\n"
                                            "    return x * 2.0\n"
                                            "rule End:\n"
                                            "    on fixed_tick:\n"
                                            "        let t = twice(Game[Match])\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'x'", "'Match'"}));
    }
    SECTION("wrong event") {
        const auto analysis = analyze_calls("rule Crit:\n"
                                            "    on Heal as h:\n"
                                            "        if is_critical(h):\n"
                                            "            emit Warn\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'Hit'", "'Heal'"}));
    }
    SECTION("a trait passed as an event") {
        const auto analysis = analyze_calls("rule Crit:\n"
                                            "    each:\n"
                                            "        Health as hp\n"
                                            "    on fixed_tick:\n"
                                            "        let crit = is_critical(hp)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'hit'", "must be the alias of the handler's event"}));
    }
}

TEST_CASE("trait argument: inline-read restrictions apply unchanged", "[trait-parameters]") {
    SECTION("when: matches the inline-read error") {
        const auto inline_read = analyze_calls("rule Low:\n"
                                               "    each:\n"
                                               "        Health as hp\n"
                                               "    when:\n"
                                               "        hp.current < 30.0\n"
                                               "    on fixed_tick:\n"
                                               "        emit Warn\n");
        const auto passed = analyze_calls("rule Low:\n"
                                          "    each:\n"
                                          "        Health as hp\n"
                                          "    when:\n"
                                          "        is_low(hp)\n"
                                          "    on fixed_tick:\n"
                                          "        emit Warn\n");
        INFO(describe(passed));
        CHECK(inline_read.has("when: cannot read 'hp'"));
        CHECK(passed.has("when: cannot read 'hp'"));
    }
    SECTION("a binding removed by reduce") {
        const auto analysis = analyze_calls("rule Count:\n"
                                            "    each:\n"
                                            "        Health as hp\n"
                                            "    reduce:\n"
                                            "        n = count()\n"
                                            "    on fixed_tick:\n"
                                            "        let low = is_low(hp)\n"
                                            "        Game[Match].round = n\n");
        INFO(describe(analysis));
        CHECK(analysis.has("'hp' is not available after `reduce:`"));
    }
    SECTION("an event argument in a keep field") {
        const auto analysis = analyze_calls("trait Danger:\n"
                                            "    var critical: bool = false\n"
                                            "rule Mark:\n"
                                            "    each:\n"
                                            "        Health as hp\n"
                                            "    keep Danger:\n"
                                            "        critical = is_critical(hp)\n");
        INFO(describe(analysis));
        CHECK(analysis.has_all({"'hit'", "must be the alias of the handler's event"}));
    }
}

// ── 4. Contracts ────────────────────────────────────────────────────────────

namespace {

const InferredHandlerContract& contract_of(const Analysis& analysis, const std::string& rule_name) {
    const auto found = std::ranges::find_if(analysis.program.handler_contracts,
                                            [&](const auto& contract) { return contract.rule.local_name == rule_name; });
    REQUIRE(found != analysis.program.handler_contracts.end());
    return *found;
}

const FieldAccess& reads_of(const HandlerContract& contract, const std::string& trait) {
    const auto found = std::ranges::find_if(contract.read_fields,
                                            [&](const auto& entry) { return entry.first.local_name == trait; });
    REQUIRE(found != contract.read_fields.end());
    return found->second;
}

bool reads_trait(const HandlerContract& contract, const std::string& trait) {
    return std::ranges::any_of(contract.reads, [&](const SymbolId& symbol) { return symbol.local_name == trait; });
}

const std::string kBodyFuncs =
    "func above(b: Body) bool:\n"
    "    return b.position.y > 0.0\n"
    "func has_area(b: Body) bool:\n"
    "    return b.scale > 0.0\n"
    "func visible(b: Body) bool:\n"
    "    return has_area(b) and b.position.y > 0.0\n";

}  // namespace

TEST_CASE("trait contract: only read fields are recorded", "[trait-parameters]") {
    const auto analysis = analyze(kBodyFuncs + "rule Up:\n"
                                               "    each:\n"
                                               "        Body as b\n"
                                               "    on fixed_tick:\n"
                                               "        if above(b):\n"
                                               "            emit Warn\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reads = reads_of(contract_of(analysis, "Up"), "Body");
    CHECK_FALSE(reads.all);
    CHECK(reads.fields == std::set<std::string>{"position"});
}

TEST_CASE("trait contract: reads through a passed-on parameter are included", "[trait-parameters]") {
    const auto analysis = analyze(kBodyFuncs + "rule Show:\n"
                                               "    each:\n"
                                               "        Body as b\n"
                                               "    where:\n"
                                               "        visible(b)\n"
                                               "    on fixed_tick:\n"
                                               "        emit Warn\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(analysis.program.funcs.at("visible").params.at(0).reads.fields == std::set<std::string>{"position", "scale"});
    const auto& reads = reads_of(contract_of(analysis, "Show"), "Body");
    CHECK_FALSE(reads.all);
    CHECK(reads.fields == std::set<std::string>{"position", "scale"});
}

TEST_CASE("trait contract: a pair argument is binding-qualified", "[trait-parameters]") {
    const auto analysis = analyze(kBodyFuncs + "rule Touch:\n"
                                               "    pairs:\n"
                                               "        mover:\n"
                                               "            Body\n"
                                               "        other:\n"
                                               "            Armor\n"
                                               "    where:\n"
                                               "        above(mover[Body])\n"
                                               "    on fixed_tick:\n"
                                               "        emit Warn\n");
    INFO(describe(analysis));
    const auto& contract = contract_of(analysis, "Touch");
    CHECK(std::ranges::any_of(contract.bound_reads, [](const BoundTraitAccess& access) {
        return access.binding_index == 0 && access.trait.local_name == "Body";
    }));
    CHECK(reads_of(contract, "Body").fields == std::set<std::string>{"position"});
}

TEST_CASE("trait contract: a named-read argument is a named-entity access", "[trait-parameters]") {
    const auto analysis = analyze("func is_over(m: Match) bool:\n"
                                  "    return m.over\n"
                                  "rule End:\n"
                                  "    on fixed_tick:\n"
                                  "        if is_over(Game[Match]):\n"
                                  "            emit Warn\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& contract = contract_of(analysis, "End");
    REQUIRE(contract.named_reads.size() == 1);
    const auto& [entity, traits] = *contract.named_reads.begin();
    CHECK(entity.local_name == "Game");
    REQUIRE(traits.size() == 1);
    CHECK(traits.begin()->second.fields == std::set<std::string>{"over"});
}

TEST_CASE("trait contract: an event argument adds no trait read", "[trait-parameters]") {
    const auto analysis = analyze("func is_critical(hit: Hit) bool:\n"
                                  "    return hit.damage > 50\n"
                                  "rule Crit:\n"
                                  "    on Hit as h:\n"
                                  "        if is_critical(h):\n"
                                  "            emit Warn\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(contract_of(analysis, "Crit").reads.empty());
}

TEST_CASE("trait contract: a writer of an unread field is not ordered before the caller", "[trait-parameters]") {
    const auto analysis = analyze(kBodyFuncs + "rule Grow:\n"
                                               "    each:\n"
                                               "        Body as b\n"
                                               "    on fixed_tick:\n"
                                               "        b.scale = 2.0\n"
                                               "rule Up:\n"
                                               "    each:\n"
                                               "        Body as b\n"
                                               "    on fixed_tick:\n"
                                               "        if above(b):\n"
                                               "            emit Warn\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    CHECK(std::ranges::none_of(analysis.program.execution_graph.schedule_edges, [](const ScheduleEdge& edge) {
        const auto names = std::set<std::string>{edge.before.rule.local_name, edge.after.rule.local_name};
        return names == std::set<std::string>{"Grow", "Up"};
    }));
}

TEST_CASE("trait contract: a whole-trait read is derived, not assumed", "[trait-parameters]") {
    const auto analysis = analyze("func summary(m: Match) int:\n"
                                  "    if m.over:\n"
                                  "        return m.round\n"
                                  "    return 0\n"
                                  "rule Report:\n"
                                  "    each:\n"
                                  "        Match as m\n"
                                  "    on fixed_tick:\n"
                                  "        let s = summary(m)\n");
    INFO(describe(analysis));
    REQUIRE(analysis.clean());
    const auto& reads = reads_of(contract_of(analysis, "Report"), "Match");
    CHECK(reads.covers("over"));
    CHECK(reads.covers("round"));
    CHECK(reads_trait(contract_of(analysis, "Report"), "Match"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
