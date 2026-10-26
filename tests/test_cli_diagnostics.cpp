// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct CliResult {
    int exit_code = -1;
    std::vector<std::string> stderr_lines;
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

CliResult run_cactus(const fs::path& input) {
    const fs::path work_dir    = fs::path{CACTUS_TEST_BINARY_DIR} / "cli_diagnostics";
    const fs::path output_file = work_dir / (input.stem().string() + ".out.cpp");
    const fs::path stderr_file = work_dir / (input.stem().string() + ".stderr.txt");
    const std::string command = quote(fs::path{CACTUS_COMPILER_PATH}) + " " + quote(input) + " -o " +
                                quote(output_file) + " 2> " + quote(stderr_file);
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
        if (line.contains(": error: ") || line.contains(": warning: ")) {
            result.stderr_lines.push_back(line);
        }
    }
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

TEST_CASE("CLI: a call to a function a module does not declare is rejected", "[cli][diagnostics]") {
    const auto result = run_cactus(stage_fixture("removed_camera_helper.cactus"));
    CHECK(result.exit_code == 1);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("error: unknown function 'screen_to_plane' in module 'std.camera.volume'"));
}

TEST_CASE("CLI: a parse error in an imported module is printed once", "[cli][diagnostics]") {
    const auto project = stage_fixture("module_parse_error");
    const auto result  = run_cactus(project / "main.cactus");
    CHECK(result.exit_code == 1);
    REQUIRE(result.stderr_lines.size() == 1);
    CHECK(result.stderr_lines[0].contains("broken.cactus:4:11: error: expected ':'"));
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

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
