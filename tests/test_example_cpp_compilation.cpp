// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
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

std::optional<fs::path> compilation_database_dir() {
    const auto compile_commands = build_root() / "compile_commands.json";
    if (fs::exists(compile_commands)) {
        return build_root();
    }
    return std::nullopt;
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
        ExampleCase{
            .name           = "model-renderer",
            .source_file    = repo_root() / CACTUS_EXAMPLE_MODEL_RENDERER_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_MODEL_RENDERER_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_MODEL_RENDERER_TARGET,
        },
        ExampleCase{
            .name           = "platformer",
            .source_file    = repo_root() / CACTUS_EXAMPLE_PLATFORMER_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_PLATFORMER_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_PLATFORMER_TARGET,
        },
        ExampleCase{
            .name           = "waving-label-2d",
            .source_file    = repo_root() / CACTUS_EXAMPLE_WAVING_LABEL_2D_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_WAVING_LABEL_2D_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_WAVING_LABEL_2D_TARGET,
        },
        ExampleCase{
            .name           = "waving-label-3d",
            .source_file    = repo_root() / CACTUS_EXAMPLE_WAVING_LABEL_3D_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_WAVING_LABEL_3D_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_WAVING_LABEL_3D_TARGET,
        },
        ExampleCase{
            .name           = "editor-test",
            .source_file    = repo_root() / CACTUS_EXAMPLE_EDITOR_TEST_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_EDITOR_TEST_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_EDITOR_TEST_TARGET,
        },
        ExampleCase{
            .name           = "editor-3d",
            .source_file    = repo_root() / CACTUS_EXAMPLE_EDITOR_3D_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_EDITOR_3D_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_EDITOR_3D_TARGET,
        },
        ExampleCase{
            .name           = "split-screen",
            .source_file    = repo_root() / CACTUS_EXAMPLE_SPLIT_SCREEN_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_SPLIT_SCREEN_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_SPLIT_SCREEN_TARGET,
        },
        ExampleCase{
            .name           = "split-screen-forest-bombs",
            .source_file    = repo_root() / CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_SOURCE,
            .backend        = "cpp-entt",
            .generated_cpp  = {CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_GENERATED_CPP},
            .compile_target = CACTUS_EXAMPLE_SPLIT_SCREEN_FOREST_BOMBS_TARGET,
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

std::string clang_tidy_command(const ExampleCase& example) {
    std::ostringstream command;
    command << quote(fs::path(CACTUS_CLANG_TIDY_PATH)) << " " << quote(example.generated_cpp);

    if (const auto db_dir = compilation_database_dir()) {
        command << " -p " << quote(*db_dir);
    } else {
        command << " --extra-arg=-std=c++20";
        command << " --extra-arg=-I" << quote(repo_root() / "src");
        command << " --extra-arg=-I" << quote(build_root() / "_deps" / "entt-src" / "src");
        command << " --extra-arg=-I" << quote(build_root() / "_deps" / "raylib-src" / "src");
    }

    command << " --header-filter=" << quote(example.generated_cpp.string()) << " --warnings-as-errors=*"
            << " --extra-arg=-Wno-deprecated-literal-operator";
    return command.str();
}

struct DriftPattern {
    std::string name;
    std::regex pattern;
};

std::vector<fs::path> cactus_example_sources() {
    std::vector<fs::path> sources;
    const auto examples_dir = repo_root() / "examples";
    for (const auto& entry : fs::recursive_directory_iterator(examples_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cactus") {
            continue;
        }
        sources.push_back(entry.path());
    }
    return sources;
}

}  // namespace

TEST_CASE("integration: curated examples generate compilable C++ with lint-clean output",
          "[integration][examples][cpp-compilation]") {
    ensure_tools_present();

    for (const auto& example : curated_examples()) {
        SECTION(example.name) {
            reset_generated_output(example);

            require_success(example,
                            "parse-semantic-codegen",
                            repo_root(),
                            quote(compiler_path()) + " " + quote(example.source_file) + " --backend " +
                                quote(example.backend) + " --output " + quote(example.generated_cpp));

            REQUIRE(fs::exists(example.generated_cpp));

            require_success(example, "cpp-compile-link", repo_root(), build_target_command(example.compile_target));

            require_success(example,
                            "clang-format-fix",
                            repo_root(),
                            quote(fs::path(CACTUS_CLANG_FORMAT_PATH)) + " -i " + quote(example.generated_cpp));

            require_success(
                example,
                "clang-format",
                repo_root(),
                quote(fs::path(CACTUS_CLANG_FORMAT_PATH)) + " --dry-run --Werror " + quote(example.generated_cpp));

            require_success(example, "clang-tidy", repo_root(), clang_tidy_command(example));
        }
    }
}

TEST_CASE("integration: explicit std.core import with lifecycle handler generates without duplicate symbols",
          "[integration][examples][std-core]") {
    ensure_tools_present();

    const auto source_file = build_root() / "generated_examples" / "std-core-explicit.cactus";
    const auto output_file = build_root() / "generated_examples" / "std-core-explicit.generated.cpp";
    fs::create_directories(source_file.parent_path());

    {
        std::ofstream out(source_file);
        out << "module std_core_explicit\n\n"
            << "use std.core\n\n"
            << "trait Counter:\n"
            << "    var value: int\n\n"
            << "pub entity CounterEntity:\n"
            << "    Counter:\n"
            << "        value = 0\n"
            << "    Persistent\n\n"
            << "system CountTicks:\n"
            << "    filter:\n"
            << "        Counter\n\n"
            << "    on tick:\n"
            << "        value = value + 1\n";
    }

    const ExampleCase example{
        .name           = "std-core-explicit",
        .source_file    = source_file,
        .backend        = "cpp-entt",
        .generated_cpp  = output_file,
        .compile_target = {},
    };

    reset_generated_output(example);
    require_success(example,
                    "parse-semantic-codegen",
                    repo_root(),
                    quote(compiler_path()) + " " + quote(example.source_file) + " --backend " + quote(example.backend) +
                        " --output " + quote(example.generated_cpp));

    REQUIRE(fs::exists(example.generated_cpp));
}

TEST_CASE("integration: hierarchical entity templates example generates cpp-entt output",
          "[integration][examples][hierarchy]") {
    ensure_tools_present();

    const ExampleCase example{
        .name           = "hierarchical-entities",
        .source_file    = repo_root() / "examples" / "hierarchical_entities.cactus",
        .backend        = "cpp-entt",
        .generated_cpp  = build_root() / "generated_examples" / "hierarchical-entities.generated.cpp",
        .compile_target = {},
    };

    reset_generated_output(example);
    require_success(example,
                    "parse-semantic-codegen",
                    repo_root(),
                    quote(compiler_path()) + " " + quote(example.source_file) + " --backend " + quote(example.backend) +
                        " --output " + quote(example.generated_cpp));

    REQUIRE(fs::exists(example.generated_cpp));

    std::ifstream in(example.generated_cpp);
    std::ostringstream generated;
    generated << in.rdbuf();
    const auto code = generated.str();

    // Tree creation wires Parent on every non-root node to its immediate parent.
    CHECK(code.find("create_hierarchical_entities__tree__node__crown__gem__sparkle") != std::string::npos);
    CHECK(code.find("registry.emplace_or_replace<std_core__Parent>(child_0, std_core__Parent{.parent = entity});") !=
          std::string::npos);
}

TEST_CASE("integration: removed cpp-manual backend is rejected", "[integration][cli][backend]") {
    REQUIRE(fs::exists(compiler_path()));

    const auto source_file = build_root() / "generated_examples" / "removed-manual-backend.cactus";
    const auto output_file = build_root() / "generated_examples" / "removed-manual-backend.generated.cpp";
    fs::create_directories(source_file.parent_path());

    {
        std::ofstream out(source_file);
        out << "trait Marker:\n\n"
            << "entity One:\n"
            << "    Marker\n";
    }

    const ExampleCase example{
        .name           = "removed-manual-backend",
        .source_file    = source_file,
        .backend        = "cpp-manual",
        .generated_cpp  = output_file,
        .compile_target = {},
    };

    const auto result = run_command(repo_root(),
                                    example.name + "-reject",
                                    quote(compiler_path()) + " " + quote(example.source_file) + " --backend " +
                                        quote(example.backend) + " --output " + quote(example.generated_cpp));

    CHECK(result.exit_code != 0);
    CHECK(result.output.find("unknown backend 'cpp-manual'") != std::string::npos);
}

TEST_CASE("integration: user library links canonical callbacks independently per external handler",
          "[integration][examples][external-handler-abi]") {
    REQUIRE(fs::exists(compiler_path()));

    const ExampleCase example{
        .name           = "external-handler-user-library",
        .source_file    = repo_root() / "tests" / "fixtures" / "external_handler_linking.cactus",
        .backend        = "cpp-entt",
        .generated_cpp  = {},
        .compile_target = CACTUS_EXTERNAL_HANDLER_LINK_TARGET,
    };
    require_success(example, "cpp-compile-link", repo_root(), build_target_command(example.compile_target));
}

TEST_CASE("integration: missing user implementations diagnose both canonical handler symbols",
          "[integration][examples][external-handler-abi]") {
    REQUIRE(fs::exists(compiler_path()));

    const ExampleCase example{
        .name           = "external-handler-missing-implementations",
        .source_file    = repo_root() / "tests" / "fixtures" / "external_handler_linking.cactus",
        .backend        = "cpp-entt",
        .generated_cpp  = {},
        .compile_target = CACTUS_EXTERNAL_HANDLER_MISSING_TARGET,
    };
    const auto result = run_command(repo_root(), example.name + "-link", build_target_command(example.compile_target));
    CHECK(result.exit_code != 0);
    CHECK(result.output.find(
              "cactus_external__external_handler_linking__NativeMovement__on__external_handler_linking__fixed_tick") !=
          std::string::npos);
    CHECK(result.output.find(
              "cactus_external__external_handler_linking__NativeMovement__on__external_handler_linking__Reset") !=
          std::string::npos);
}

TEST_CASE("examples do not present removed syntax as current syntax", "[examples][drift]") {
    static const std::array<DriftPattern, 5> REMOVED_PATTERNS{{
        {"legacy apply block", std::regex{R"(\bapply\s*:)", std::regex::icase}},
        {"legacy config block", std::regex{R"(\bconfig\s*:)", std::regex::icase}},
        {"parenthesized add statement", std::regex{R"(\badd\s+[A-Za-z_][A-Za-z0-9_]*\s*\()"}},
        {"parenthesized emit statement", std::regex{R"(\bemit\s+[A-Za-z_][A-Za-z0-9_]*\s*\()"}},
        {"parenthesized spawn statement", std::regex{R"(\bspawn\s+[A-Za-z_][A-Za-z0-9_]*\s*\()"}},
    }};

    const auto sources = cactus_example_sources();
    REQUIRE_FALSE(sources.empty());

    for (const auto& source : sources) {
        CAPTURE(source.string());
        std::ifstream in(source);
        REQUIRE(in.is_open());

        std::string line;
        std::size_t line_number = 0;
        while (std::getline(in, line)) {
            ++line_number;
            for (const auto& drift : REMOVED_PATTERNS) {
                INFO("Example '" << source.string() << "' contains " << drift.name << " at line " << line_number << ": "
                                 << line);
                CHECK_FALSE(std::regex_search(line, drift.pattern));
            }
        }
    }
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
