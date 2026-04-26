// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ExampleCase {
    std::string name;
    fs::path source_file;
    std::string backend;
    fs::path generated_cpp;
    std::string compile_target;
};

struct CommandResult {
    int exit_code = -1;
    std::string command;
    std::string output;
};

class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const fs::path& next)
        : previous_(fs::current_path()) {
        fs::current_path(next);
    }

    ScopedCurrentPath(const ScopedCurrentPath&)            = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath(ScopedCurrentPath&&)                 = delete;
    ScopedCurrentPath& operator=(ScopedCurrentPath&&)      = delete;

    ~ScopedCurrentPath() {
        std::error_code ec;
        fs::current_path(previous_, ec);
    }

private:
    fs::path previous_;
};

fs::path repo_root() {
    return {CACTUS_TEST_SOURCE_DIR};
}

fs::path build_root() {
    return {CACTUS_TEST_BINARY_DIR};
}

fs::path compiler_path() {
    return {CACTUS_COMPILER_PATH};
}

std::string build_config() {
    return CACTUS_BUILD_CONFIG;
}

std::string quote(const fs::path& value) {
    auto normalized = value;
    normalized.make_preferred();
    return std::string{"\""} + normalized.string() + "\"";
}

std::string quote(const std::string& value) {
    return std::string{"\""} + value + "\"";
}

CommandResult run_command(const fs::path& working_dir, const std::string& stage, const std::string& command) {
    auto logs_dir = build_root() / "generated_examples" / "logs";
    fs::create_directories(logs_dir);

    const auto logFile = logs_dir / (stage + ".log");
    std::error_code ec;
    fs::remove(logFile, ec);

    ScopedCurrentPath cwd(working_dir);
    const std::string redirected = command + " > " + quote(logFile) + " 2>&1";
    const std::string shellReady = "\"" + redirected + "\"";
    const int exitCode           = std::system(shellReady.c_str());

    std::ifstream ifs(logFile);
    std::ostringstream output;
    output << ifs.rdbuf();

    return {.exit_code = exitCode, .command = command, .output = output.str()};
}

void fail_stage(const ExampleCase& example, const std::string& stage, const CommandResult& result) {
    FAIL("Example '" + example.name + "' failed during stage '" + stage + "'.\nCommand: " + result.command +
         "\nExit code: " + std::to_string(result.exit_code) + "\nOutput:\n" + result.output);
}

void require_success(const ExampleCase& example,
                     const std::string& stage,
                     const fs::path& working_dir,
                     const std::string& command) {
    const auto result = run_command(working_dir, example.name + "-" + stage, command);
    if (result.exit_code != 0) {
        fail_stage(example, stage, result);
    }
}

std::vector<ExampleCase> curated_examples() {
    return {
        ExampleCase{
            .name           = "blue-square",
            .source_file    = repo_root() / CACTUS_EXAMPLE_BLUE_SQUARE_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_BLUE_SQUARE_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_BLUE_SQUARE_TARGET,
        },
        ExampleCase{
            .name           = "stdlib-extern-entt",
            .source_file    = repo_root() / CACTUS_EXAMPLE_STDLIB_EXTERN_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_STDLIB_EXTERN_ENTT_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_STDLIB_EXTERN_ENTT_TARGET,
        },
        ExampleCase{
            .name           = "mesh-renderer",
            .source_file    = repo_root() / CACTUS_EXAMPLE_MESH_RENDERER_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_MESH_RENDERER_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_MESH_RENDERER_TARGET,
        },
    };
}

void ensure_tools_present() {
    REQUIRE(fs::exists(compiler_path()));
    REQUIRE_FALSE(std::string(CACTUS_CLANG_FORMAT_PATH).empty());
    REQUIRE_FALSE(std::string(CACTUS_CLANG_TIDY_PATH).empty());
}

void reset_generated_output(const ExampleCase& example) {
    std::error_code ec;
    fs::remove(example.generated_cpp, ec);
    fs::create_directories(example.generated_cpp.parent_path());
}

std::string build_target_command(const std::string& target) {
    std::ostringstream command;
    command << quote(fs::path(CACTUS_CMAKE_COMMAND)) << " --build " << quote(build_root()) << " --target "
            << quote(target);

    if (!build_config().empty()) {
        command << " --config " << quote(build_config());
    }

    return command.str();
}

}  // namespace

TEST_CASE("integration: curated examples generate compilable C++ with lint-clean output",
          "[integration][examples][cpp-compilation]") {
    ensure_tools_present();

    for (const auto& example : curated_examples()) {
        SECTION(example.name) {
            reset_generated_output(example);

            require_success(example,
                            "generate",
                            repo_root(),
                            quote(compiler_path()) + " " + quote(example.source_file) + " --backend " +
                                quote(example.backend) + " --output " + quote(example.generated_cpp));

            REQUIRE(fs::exists(example.generated_cpp));

            require_success(example, "compile", repo_root(), build_target_command(example.compile_target));

            require_success(example,
                            "clang-format-fix",
                            repo_root(),
                            quote(fs::path(CACTUS_CLANG_FORMAT_PATH)) + " -i " + quote(example.generated_cpp));

            require_success(
                example,
                "clang-format",
                repo_root(),
                quote(fs::path(CACTUS_CLANG_FORMAT_PATH)) + " --dry-run --Werror " + quote(example.generated_cpp));

            require_success(example,
                            "clang-tidy",
                            repo_root(),
                            quote(fs::path(CACTUS_CLANG_TIDY_PATH)) + " " + quote(example.generated_cpp) + " -p " +
                                quote(build_root()) + " --header-filter=" + quote(example.generated_cpp.string()) +
                                " --warnings-as-errors=*" + " --extra-arg=-Wno-deprecated-literal-operator");
        }
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
