// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct CliResult {
    int exit_code = -1;
    std::vector<std::string> stderr_lines;
    std::vector<std::string> info_lines;
    std::string stderr_text;
    bool wrote_output = false;
};

std::string quote(fs::path value) {
    value.make_preferred();
    return "\"" + value.string() + "\"";
}

// Copies the fixture into the build tree so the CLI's sibling build/ directory stays out of the sources.
fs::path stage_fixture(const std::string& name) {
    const fs::path source = fs::path{CACTUS_TEST_SOURCE_DIR} / "tests" / "fixtures" / "cli_diagnostics" / name;
    const fs::path staged = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics" / name;
    fs::remove_all(staged);
    fs::create_directories(staged.parent_path());
    fs::copy(source, staged, fs::copy_options::recursive);
    return staged;
}

CliResult run_cactus(const fs::path& input, const std::string& flags = "") {
    const fs::path work_dir    = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics";
    const fs::path output_file = work_dir / (input.stem().string() + ".out.cpp");
    const fs::path stderr_file = work_dir / (input.stem().string() + ".stderr.txt");
    fs::remove(output_file);
    const std::string command = quote(fs::path{CACTUS_COMPILER_PATH}) + " " + quote(input) + " -o " +
                                quote(output_file) + flags + " 2> " + quote(stderr_file);
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
    result.wrote_output = fs::exists(output_file);
    return result;
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

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
