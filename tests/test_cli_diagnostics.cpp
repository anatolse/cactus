// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct CliResult {
    int exit_code = -1;
    std::vector<std::string> stderr_lines;
    std::vector<std::string> info_lines;
    std::string stderr_text;
    bool wrote_output = false;
    std::string output_text;
    std::string stdout_text;
};

std::string quote(fs::path value) {
    value.make_preferred();
    return "\"" + value.string() + "\"";
}

// Copies the fixture into the build tree so the CLI's sibling build/ directory stays out of the sources.
fs::path stage_fixture(const std::string& name, const std::string& root = "cli_diagnostics") {
    const fs::path source = fs::path{CACTUS_TEST_SOURCE_DIR} / "tests" / "fixtures" / root / name;
    const fs::path staged = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics" / root / name;
    fs::remove_all(staged);
    fs::create_directories(staged.parent_path());
    fs::copy(source, staged, fs::copy_options::recursive);
    return staged;
}

std::string read_lines(const fs::path& path) {
    std::ifstream stream(path);
    std::string text;
    for (std::string line; std::getline(stream, line);) {
        text += line + "\n";
    }
    return text;
}

CliResult run_cactus(const fs::path& input, const std::string& flags = "") {
    const fs::path work_dir    = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics";
    const fs::path output_file = work_dir / (input.stem().string() + ".out.cpp");
    const fs::path stderr_file = work_dir / (input.stem().string() + ".stderr.txt");
    const fs::path stdout_file = work_dir / (input.stem().string() + ".stdout.txt");
    fs::remove(output_file);
    const std::string command = quote(fs::path{CACTUS_COMPILER_PATH}) + " " + quote(input) + " -o " +
                                quote(output_file) + flags + " 2> " + quote(stderr_file) + " > " + quote(stdout_file);
    // NOLINTNEXTLINE(bugprone-command-processor,concurrency-mt-unsafe) -- the test drives the real CLI binary.
    const int raw_exit = std::system(("\"" + command + "\"").c_str());

    CliResult result;
#ifdef _WIN32
    result.exit_code = raw_exit;
#else
    result.exit_code = WEXITSTATUS(raw_exit);
#endif
    std::ifstream stream(stderr_file);
    for (std::string line; std::getline(stream, line);) {
        result.stderr_text += line + "\n";
        if (line.contains(": error: ") || line.contains(": warning: ")) {
            result.stderr_lines.push_back(line);
        }
        if (line.contains(": info: ")) {
            result.info_lines.push_back(line);
        }
    }
    result.stdout_text  = read_lines(stdout_file);
    result.wrote_output = fs::exists(output_file);
    if (result.wrote_output) {
        std::ifstream output(output_file);
        result.output_text.assign(std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>());
    }
    return result;
}

fs::path cost_fixture(const std::string& name) {
    return stage_fixture(name, "cost_report");
}

// The report's line for one handler, or empty when it prints none.
std::string handler_line(const std::string& report, const std::string& handler) {
    const auto needle = "\n  " + handler + "  ";
    const auto start  = report.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    return report.substr(start + 1, report.find('\n', start + 1) - start - 1);
}

// What follows "cost: " in an explanation, up to the end of its line.
std::string explained_cost(const std::string& text) {
    constexpr std::string_view label = "  cost: ";
    const auto start                 = text.find(label);
    if (start == std::string::npos) {
        return {};
    }
    const auto from = start + label.size();
    return text.substr(from, text.find('\n', from) - from);
}

}  // namespace

TEST_CASE("CLI: one semantic error is printed once", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("semantic_error.cactus"));
    CHECK(result.exit_code == 1);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("error: unknown type 'Missing'"));
}

TEST_CASE("CLI: a warning on a successful compile is printed once", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("one_warning.cactus"));
    CHECK(result.exit_code == 0);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("warning: pair rule 'P' is not accelerated"));
}

TEST_CASE("CLI: --warnings-as-errors fails a build with a warning and writes nothing", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("one_warning.cactus"), " --warnings-as-errors");
    CHECK(result.exit_code != 0);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("warning: pair rule 'P' is not accelerated"));
    CHECK(result.stderr_text.contains("error: 1 warning treated as an error"));
    CHECK_FALSE(result.wrote_output);
}

TEST_CASE("CLI: --warnings-as-errors passes a clean program", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("no_warning.cactus"), " --warnings-as-errors");
    CHECK(result.exit_code == 0);
    CHECK(result.stderr_lines.empty());
    CHECK(result.wrote_output);
}

TEST_CASE("CLI: warnings do not fail a build by default", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("one_warning.cactus"));
    CHECK(result.exit_code == 0);
    CHECK(result.wrote_output);
}

TEST_CASE("CLI: --warnings-as-errors counts a warning from a dependency", "[cli][diagnostics]") {
    const auto project = stage_fixture("warning_in_dependency");
    CHECK(run_cactus(project / "main.cactus").exit_code == 0);
    const auto result = run_cactus(project / "main.cactus", " --warnings-as-errors");
    CHECK(result.exit_code != 0);
    CHECK(result.stderr_text.contains("treated as an error"));
    CHECK_FALSE(result.wrote_output);
}

TEST_CASE("CLI: the 3D editor example compiles with --warnings-as-errors", "[cli][diagnostics]") {
    const fs::path staged = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics" / "editor_3d";
    fs::remove_all(staged);
    fs::create_directories(staged);
    const fs::path source = fs::path{CACTUS_TEST_SOURCE_DIR} / "examples" / "editor-3d" / "main.cactus";
    fs::copy_file(source, staged / "main.cactus");
    const auto result = run_cactus(staged / "main.cactus", " --warnings-as-errors");
    CHECK(result.exit_code == 0);
    CHECK(result.stderr_lines.empty());
}

TEST_CASE("CLI: a planned pair rule does not warn, even with indexed joins off", "[cli][diagnostics][pair-plans]") {
    for (const std::string flags : {"", " --no-indexed-joins"}) {
        INFO(flags);
        const auto result = run_cactus(stage_fixture("relation_rows.cactus"), flags);
        CHECK(result.exit_code == 0);
        CHECK(result.stderr_lines.empty());
    }
}

TEST_CASE("CLI: generated code runs on one thread by default", "[cli][parallel-handlers]") {
    const auto result = run_cactus(stage_fixture("parallel_levels.cactus"));
    REQUIRE(result.exit_code == 0);
    CHECK_FALSE(result.output_text.contains("run_parallel_batch"));
    CHECK_FALSE(result.output_text.contains("thread"));
}

TEST_CASE("CLI: --parallel-handlers runs eligible handlers on workers", "[cli][parallel-handlers]") {
    const auto result = run_cactus(stage_fixture("parallel_levels.cactus"), " --parallel-handlers");
    REQUIRE(result.exit_code == 0);
    CHECK(result.output_text.contains("run_parallel_batch"));
}

TEST_CASE("CLI: --parallel-handlers reports the same diagnostics", "[cli][parallel-handlers]") {
    const auto plain    = run_cactus(stage_fixture("parallel_levels.cactus"));
    const auto parallel = run_cactus(stage_fixture("parallel_levels.cactus"), " --parallel-handlers");
    CHECK(plain.exit_code == parallel.exit_code);
    CHECK_FALSE(plain.stderr_lines.empty());
    CHECK(plain.stderr_lines == parallel.stderr_lines);
    CHECK(plain.info_lines == parallel.info_lines);
}

TEST_CASE("CLI: a call to a function a module does not declare is rejected", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("removed_camera_helper.cactus"));
    CHECK(result.exit_code == 1);
    auto errors = result.stderr_lines | std::views::filter([](const auto& line) { return line.contains(": error: "); });
    REQUIRE(std::ranges::distance(errors) == 1);
    CHECK(errors.front().contains("error: unknown function 'screen_to_plane' in module 'std.camera.volume'"));
}

TEST_CASE("CLI: a parse error in an imported module is printed once", "[cli][diagnostics]") {
    const auto project = stage_fixture("module_parse_error");
    const auto result  = run_cactus(project / "main.cactus");
    CHECK(result.exit_code == 1);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("broken.cactus:4:11: error: expected ':'"));
}

TEST_CASE("CLI: public imported states resolve in archetypes and handlers", "[cli][diagnostics][exclusive-states]") {
    const auto project = stage_fixture("imported_states");
    const auto result  = run_cactus(project / "main.cactus");
    for (const auto& diagnostic : result.stderr_lines) {
        INFO(diagnostic);
    }
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_lines.empty());
    const auto output = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics" / "main.out.cpp";
    std::ifstream stream(output);
    const std::string generated((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(generated.contains("enter_state<::cactus_gen::orders::Mode, 0>"));
    CHECK(generated.contains("enter_state<::cactus_gen::orders::Mode, 1>"));
}

TEST_CASE("CLI: an add of an imported state variant is checked against its arrows at link",
          "[cli][diagnostics][state-transitions]") {
    const auto project = stage_fixture("imported_state_transitions");
    const auto result  = run_cactus(project / "main.cactus");
    for (const auto& diagnostic : result.stderr_lines) {
        INFO(diagnostic);
    }
    CHECK(result.exit_code == 1);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains(
        "main.cactus:21:9: error: `add Mode.Hold` can move an entity out of 'Auto', which has no arrow to 'Hold'"));
}

TEST_CASE("CLI: adding a container trait that another module ranges against is rejected at link",
          "[cli][diagnostics][ranged-fields]") {
    const auto project = stage_fixture("container_add_cross_module");
    const auto result  = run_cactus(project / "main.cactus");
    CHECK(result.exit_code == 1);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains(
        "error: rule 'Grow' can't add container trait 'Volume': ranged field 'Cell.x' reads it"));
}

TEST_CASE("CLI: a volume's geometry cannot change after it is made", "[cli][diagnostics][ranged-fields]") {
    const auto write = run_cactus(stage_fixture("voxel_geometry_write.cactus"));
    CHECK(write.exit_code == 1);
    REQUIRE(write.stderr_lines.size() == 1);
    CHECK(write.stderr_lines[0].contains("error: can't assign let field 'Volume.max_x'"));
    const auto add = run_cactus(stage_fixture("voxel_volume_add.cactus"));
    CHECK(add.exit_code == 1);
    REQUIRE(add.stderr_lines.size() == 1);
    CHECK(add.stderr_lines[0].contains("error: rule 'Regrow' can't add container trait 'Volume'"));
}

TEST_CASE("CLI: a delayed cycle prints one info line and does not fail the build", "[cli][diagnostics][delivery-cycles]") {
    const auto result = run_cactus(stage_fixture("delayed_cycle.cactus"));
    CHECK(result.exit_code == 0);
    CHECK(result.wrote_output);
    CHECK(result.stderr_lines.empty());
    REQUIRE(result.info_lines.size() == 1);
    CHECK(result.info_lines[0].contains("delayed_cycle.PingSide/on delayed_cycle.Ping, delayed_cycle.PongSide/on delayed_cycle.Pong"));
}

TEST_CASE("CLI: --warnings-as-errors does not count a delayed cycle", "[cli][diagnostics][delivery-cycles]") {
    const auto result = run_cactus(stage_fixture("delayed_cycle.cactus"), " --warnings-as-errors");
    CHECK(result.exit_code == 0);
    CHECK(result.wrote_output);
    CHECK(result.info_lines.size() == 1);
}

TEST_CASE("CLI: a cycle across modules is reported once", "[cli][diagnostics][delivery-cycles]") {
    const auto project = stage_fixture("cycle_across_modules");
    const auto result  = run_cactus(project / "main.cactus");
    CHECK(result.exit_code == 0);
    REQUIRE(result.info_lines.size() == 1);
    CHECK(result.info_lines[0].contains("echo.PingSide/on echo.Ping, main.PongSide/on echo.Pong"));
}

TEST_CASE("CLI: a multiplying cycle warns and does not fail the build", "[cli][diagnostics][delivery-cycles]") {
    const auto result = run_cactus(stage_fixture("multiplying_cycle.cactus"));
    CHECK(result.exit_code == 0);
    CHECK(result.wrote_output);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("warning: this cycle can multiply its work each activation"));
    CHECK(result.stderr_lines[0].contains("multiplying_cycle.Echo/on multiplying_cycle.Knock"));
}

TEST_CASE("CLI: a multiplying handler in a dependency is warned", "[cli][diagnostics][delivery-cycles]") {
    const auto project = stage_fixture("multiplying_across_modules");
    const auto result  = run_cactus(project / "main.cactus");
    CHECK(result.exit_code == 0);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("echo.PingSide/on echo.Ping"));
}

TEST_CASE("CLI: --report-cost prints the expected report", "[cli][cost-report]") {
    for (const std::string name : {"report", "keeps", "shapes"}) {
        INFO(name);
        const auto result = run_cactus(cost_fixture(name + ".cactus"), " --report-cost");
        CHECK(result.exit_code == 0);
        CHECK(result.wrote_output);
        const auto expected =
            read_lines(fs::path{CACTUS_TEST_SOURCE_DIR} / "tests" / "fixtures" / "cost_report" / (name + ".expected.txt"));
        CHECK(result.stdout_text == expected);
    }
}

TEST_CASE("CLI: --report-cost names population shapes", "[cli][cost-report]") {
    const auto report = run_cactus(cost_fixture("report.cactus"), " --report-cost").stdout_text;
    CHECK(handler_line(report, "report.Move/on std.core.tick").ends_with("  N(Unit)"));
    CHECK(handler_line(report, "report.Stand/on std.core.fixed_tick").ends_with("  N(Unit)·k"));
    CHECK(handler_line(report, "report.Crowd/on std.core.tick").ends_with("  N(Unit)·N(Unit)"));
    CHECK(handler_line(report, "report.Clash/on std.core.tick").ends_with("  N(Unit)·N(Unit)"));
    CHECK(handler_line(report, "report.Taunt/on std.core.tick").ends_with("  N(Unit)·2"));
    CHECK(handler_line(report, "report.Award/on std.core.tick").ends_with("  1"));
    CHECK(handler_line(report, "report.Flicker/on std.core.tick").ends_with("  N(Spark, never destroyed)"));
    CHECK(handler_line(report, "std.core.SceneCleanup/on std.core.unload").ends_with("  per event: N(*)"));
    CHECK(report.contains("\n  N(*)        all entities, for a rule that requires no trait\n"));
    CHECK_FALSE(report.contains("unbounded"));
    CHECK(report.contains("\nphase std.core.tick  depth "));
    CHECK(report.contains("\n  delayed cycle: report.PingSide/on report.Ping, report.PongSide/on report.Pong\n"));
}

TEST_CASE("CLI: --report-cost does not depend on rule order", "[cli][cost-report]") {
    const auto first  = run_cactus(cost_fixture("report.cactus"), " --report-cost");
    const auto second = run_cactus(cost_fixture("reordered/report.cactus"), " --report-cost");
    REQUIRE(first.exit_code == 0);
    CHECK_FALSE(first.stdout_text.empty());
    CHECK(first.stdout_text == second.stdout_text);
}

TEST_CASE("CLI: --report-cost leaves the generated code unchanged", "[cli][cost-report]") {
    const auto plain    = run_cactus(cost_fixture("report.cactus"));
    const auto reported = run_cactus(cost_fixture("report.cactus"), " --report-cost");
    CHECK(plain.stdout_text.empty());
    CHECK(plain.output_text == reported.output_text);
}

TEST_CASE("CLI: --report-cost shows keep levels and their reasons", "[cli][cost-report][keep]") {
    const auto report = run_cactus(cost_fixture("keeps.cactus"), " --report-cost").stdout_text;
    CHECK(report.contains("A keep rule is skipped when none of its inputs changed."));
    CHECK(handler_line(report, "keeps.LowFact/on std.core.tick").ends_with("  Δ(Health)  keep delta"));
    CHECK(handler_line(report, "keeps.HasFact/on std.core.tick").ends_with("  keep delta"));
    CHECK(handler_line(report, "keeps.HurtFact/on std.core.tick")
              .ends_with("  N(Health)  keep full: the named entity read `Door[Gate]`"));
    CHECK(handler_line(report, "keeps.LinkedFact/on std.core.tick").ends_with("  keep full: the relation test `Link`"));
    CHECK(handler_line(report, "keeps.ReachFact/on std.core.tick").ends_with("  keep full: the derived relation `Reach`"));
    CHECK(handler_line(report, "keeps.NearFact/on std.core.tick")
              .ends_with("  keep full: the pair plan, which can't look up rows from both bindings"));
}

TEST_CASE("CLI: one added where: term changes a keep's level", "[cli][cost-report][keep]") {
    const auto before = run_cactus(cost_fixture("level_before/levels.cactus"), " --report-cost").stdout_text;
    const auto after  = run_cactus(cost_fixture("level_after/levels.cactus"), " --report-cost").stdout_text;
    CHECK(handler_line(before, "levels.LowFact/on std.core.tick").ends_with("  Δ(Health)  keep delta"));
    CHECK(handler_line(after, "levels.LowFact/on std.core.tick")
              .ends_with("  N(Health)  keep full: the derived relation `Reach`"));
}

TEST_CASE("CLI: --report-cost shows repeats and cells", "[cli][cost-report]") {
    const auto report = run_cactus(cost_fixture("shapes.cactus"), " --report-cost").stdout_text;
    CHECK(handler_line(report, "shapes.Advance/on std.core.tick").ends_with("  N(Runner)·k ×3"));
    CHECK(handler_line(report, "shapes.Fill/on std.core.tick").ends_with("  cells(Room)"));
    CHECK(handler_line(report, "shapes.Snap/on std.core.tick").ends_with("  live(Room: gas)"));
    CHECK(handler_line(report, "shapes.Spread/on std.core.tick").ends_with("  live(Room: gas)·k"));
}

TEST_CASE("CLI: --explain-rule explains an inferred edge and a run-time filter", "[cli][cost-report][explain]") {
    const auto result = run_cactus(cost_fixture("report.cactus"), " --explain-rule report.Rally");
    REQUIRE(result.exit_code == 0);
    CHECK_FALSE(result.wrote_output);
    const auto& text = result.stdout_text;
    CHECK(text.starts_with("report.Rally/on std.core.tick\n"));
    CHECK(text.contains("report.cactus:"));
    CHECK(text.contains("\n  domain: each Unit\n"));
    CHECK(text.contains("\n  activation: phase std.core.tick\n"));
    CHECK(text.contains("\n  where: u.hp < 3  (run-time, per row)\n"));
    CHECK(text.contains("\n  writes: report.Unit.x (Accum)\n"));
    CHECK(text.contains("\n  after report.Crowd/on std.core.tick: report.Unit.hp"));
    CHECK(text.contains("writer before reader"));
    CHECK_FALSE(text.contains("match"));
}

TEST_CASE("CLI: --explain-rule lists an explicit after: edge", "[cli][cost-report][explain]") {
    const auto text = run_cactus(cost_fixture("report.cactus"), " --explain-rule report.Move").stdout_text;
    CHECK(text.contains("\n  after report.Cull/on std.core.tick: declared by `after:`"));
}

TEST_CASE("CLI: --explain-rule prints the cost term --report-cost prints", "[cli][cost-report][explain]") {
    const auto report = run_cactus(cost_fixture("report.cactus"), " --report-cost").stdout_text;
    for (const std::string rule : {"Crowd", "Stand", "Flicker", "Taunt"}) {
        INFO(rule);
        const auto text = run_cactus(cost_fixture("report.cactus"), " --explain-rule report." + rule).stdout_text;
        const auto cost = explained_cost(text);
        REQUIRE_FALSE(cost.empty());
        CHECK(handler_line(report, text.substr(0, text.find('\n'))).ends_with("  " + cost));
    }
    const auto keeps = run_cactus(cost_fixture("keeps.cactus"), " --report-cost").stdout_text;
    const auto text  = run_cactus(cost_fixture("keeps.cactus"), " --explain-rule keeps.HurtFact").stdout_text;
    REQUIRE_FALSE(explained_cost(text).empty());
    CHECK(handler_line(keeps, "keeps.HurtFact/on std.core.tick").ends_with("  " + explained_cost(text)));
}

TEST_CASE("CLI: --explain-rule rejects an unknown rule", "[cli][cost-report][explain]") {
    const auto result = run_cactus(cost_fixture("report.cactus"), " --explain-rule report.Missing");
    CHECK(result.exit_code == 1);
    CHECK_FALSE(result.wrote_output);
    CHECK(result.stderr_text.contains("error: unknown rule 'report.Missing'"));
}

TEST_CASE("CLI: --explain-rule keeps locations and edge reasons across modules", "[cli][cost-report][explain]") {
    const auto project = cost_fixture("across");
    const auto result  = run_cactus(project / "main.cactus", " --explain-rule lib.Grow");
    REQUIRE(result.exit_code == 0);
    CHECK(result.stdout_text.contains("lib.cactus:14:"));
    CHECK(result.stdout_text.contains("\n  before main.Trim/on std.core.tick: lib.Plant.size"));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
