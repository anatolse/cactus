// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "cli/cli_driver.hpp"
#include "cli/cli_options.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using cactus::cli::CirFormat;
using cactus::cli::CliOptions;
using cactus::cli::OutputKind;
using cactus::cli::parse_cli_args;

namespace {

/// Owns argument storage and hands out a stable `char**` for the CLI entry
/// points, which take argv the way the C runtime supplies it.
class Argv {
public:
    Argv(std::initializer_list<std::string> args)
        : storage_(args) {
        pointers_.reserve(storage_.size());
        for (auto& arg : storage_) {
            pointers_.push_back(arg.data());
        }
    }

    [[nodiscard]] int argc() const {
        return static_cast<int>(pointers_.size());
    }
    char** argv() {
        return pointers_.data();
    }

private:
    std::vector<std::string> storage_;
    std::vector<char*> pointers_;
};

/// Parses `args` and returns the resulting options, requiring that parsing
/// asked the caller to continue rather than exit.
CliOptions parse_ok(std::initializer_list<std::string> args) {
    Argv argv(args);
    CliOptions options;
    std::ostringstream err;
    const auto exit_code = parse_cli_args(argv.argc(), argv.argv(), options, err);
    INFO("stderr: " << err.str());
    REQUIRE_FALSE(exit_code.has_value());
    return options;
}

/// Parses `args` expecting a usage error, and returns what was written to the
/// error stream.
std::string parse_error(std::initializer_list<std::string> args) {
    Argv argv(args);
    CliOptions options;
    std::ostringstream err;
    const auto exit_code = parse_cli_args(argv.argc(), argv.argv(), options, err);
    REQUIRE(exit_code.has_value());
    CHECK(*exit_code == 1);
    return err.str();
}

std::string fixture(const std::string& relative) {
    return std::string{CACTUS_TEST_FIXTURES_DIR} + "/" + relative;
}

}  // namespace

// ── Accepted invocations (task 1.4) ─────────────────────────────────────────

TEST_CASE("CLI defaults to C++ emission with the cpp-entt backend", "[cli][options]") {
    const auto options = parse_ok({"cactus", "game.cactus"});

    CHECK(options.input_file == "game.cactus");
    CHECK(options.emit == OutputKind::Cpp);
    CHECK(options.backend == "cpp-entt");
    CHECK_FALSE(options.backend_explicit);
    CHECK_FALSE(options.format_explicit);
}

TEST_CASE("CLI accepts an explicit --emit cpp", "[cli][options]") {
    const auto options = parse_ok({"cactus", "game.cactus", "--emit", "cpp"});

    CHECK(options.emit == OutputKind::Cpp);
    CHECK(options.backend == "cpp-entt");
}

TEST_CASE("CLI --emit cir selects JSON by default", "[cli][options]") {
    const auto options = parse_ok({"cactus", "game.cactus", "--emit", "cir"});

    CHECK(options.emit == OutputKind::Cir);
    CHECK(options.cir_format == CirFormat::Json);
    CHECK_FALSE(options.format_explicit);
}

TEST_CASE("CLI accepts every explicit CIR format", "[cli][options]") {
    CHECK(parse_ok({"cactus", "g.cactus", "--emit", "cir", "--format", "json"}).cir_format == CirFormat::Json);
    CHECK(parse_ok({"cactus", "g.cactus", "--emit", "cir", "--format", "dot"}).cir_format == CirFormat::Dot);
    CHECK(parse_ok({"cactus", "g.cactus", "--emit", "cir", "--format", "mermaid"}).cir_format == CirFormat::Mermaid);

    CHECK(parse_ok({"cactus", "g.cactus", "--emit", "cir", "--format", "dot"}).format_explicit);
}

TEST_CASE("CLI option parsing is independent of argument order", "[cli][options]") {
    const auto forward  = parse_ok({"cactus", "g.cactus", "--emit", "cir", "--format", "dot"});
    const auto reversed = parse_ok({"cactus", "--format", "dot", "--emit", "cir", "g.cactus"});

    CHECK(forward.emit == reversed.emit);
    CHECK(forward.cir_format == reversed.cir_format);
    CHECK(forward.input_file == reversed.input_file);
}

TEST_CASE("CLI records an explicitly supplied backend", "[cli][options]") {
    const auto options = parse_ok({"cactus", "g.cactus", "--backend", "cpp-entt"});

    CHECK(options.backend == "cpp-entt");
    CHECK(options.backend_explicit);
}

TEST_CASE("CLI preserves output file and repeatable module paths", "[cli][options]") {
    const auto options = parse_ok(
        {"cactus", "main.cactus", "--output", "game.cpp", "--module-path", "./lib", "--module-path", "./vendor"});

    CHECK(options.output_file == "game.cpp");
    REQUIRE(options.module_paths.size() == 2);
    CHECK(options.module_paths[0] == "./lib");
    CHECK(options.module_paths[1] == "./vendor");
}

// ── Rejected invocations (task 1.5) ─────────────────────────────────────────

TEST_CASE("CLI rejects flags that are missing their value", "[cli][options]") {
    CHECK(parse_error({"cactus", "g.cactus", "--emit"}).contains("--emit requires an argument"));
    CHECK(parse_error({"cactus", "g.cactus", "--format"}).contains("--format requires an argument"));
    CHECK(parse_error({"cactus", "g.cactus", "--backend"}).contains("--backend requires an argument"));
    CHECK(parse_error({"cactus", "g.cactus", "--output"}).contains("--output requires an argument"));
    CHECK(parse_error({"cactus", "g.cactus", "--module-path"}).contains("--module-path requires an argument"));
}

TEST_CASE("CLI rejects an unknown output kind", "[cli][options]") {
    const auto message = parse_error({"cactus", "g.cactus", "--emit", "llvm"});
    CHECK(message.contains("llvm"));
    CHECK(message.contains("output kind"));
}

TEST_CASE("CLI rejects an unknown CIR format", "[cli][options]") {
    const auto message = parse_error({"cactus", "g.cactus", "--emit", "cir", "--format", "yaml"});
    CHECK(message.contains("yaml"));
    CHECK(message.contains("CIR format"));
}

TEST_CASE("CLI rejects --format without --emit cir", "[cli][options]") {
    const auto message = parse_error({"cactus", "g.cactus", "--format", "dot"});
    CHECK(message.contains("--format"));
    CHECK(message.contains("--emit cir"));
}

TEST_CASE("CLI rejects an explicit --backend combined with --emit cir", "[cli][options]") {
    const auto message = parse_error({"cactus", "g.cactus", "--emit", "cir", "--backend", "cpp-entt"});
    CHECK(message.contains("--backend"));
    CHECK(message.contains("--emit cir"));
}

TEST_CASE("CLI rejects an unknown backend", "[cli][options]") {
    CHECK(parse_error({"cactus", "g.cactus", "--backend", "rust"}).contains("unknown backend 'rust'"));
    CHECK(parse_error({"cactus", "g.cactus", "--backend", "cpp-manual"}).contains("cpp-manual"));
}

TEST_CASE("CLI rejects an unknown option and a missing input file", "[cli][options]") {
    CHECK(parse_error({"cactus", "g.cactus", "--nope"}).contains("unknown option '--nope'"));
    CHECK(parse_error({"cactus", "--output", "out.cpp"}).contains("no input file specified"));
}

TEST_CASE("CLI --help prints usage and exits successfully", "[cli][options]") {
    Argv argv({"cactus", "--help"});
    CliOptions options;
    std::ostringstream err;

    const auto exit_code = parse_cli_args(argv.argc(), argv.argv(), options, err);

    REQUIRE(exit_code.has_value());
    CHECK(*exit_code == 0);
    CHECK(err.str().contains("Usage:"));
    CHECK(err.str().contains("--emit"));
    CHECK(err.str().contains("--format"));
}

// ── run() entry point (task 1.3) ────────────────────────────────────────────

TEST_CASE("run() generates C++ on stdout for a single-module program", "[cli][run]") {
    Argv argv({"cactus", fixture("multi_module/standalone.cactus")});
    std::ostringstream out;
    std::ostringstream err;

    const int exit_code = cactus::cli::run(argv.argc(), argv.argv(), out, err);

    INFO("stderr: " << err.str());
    CHECK(exit_code == 0);
    CHECK(out.str().contains("Generated by Cactus DSL Compiler"));
}

TEST_CASE("run() reports usage errors through the injected error stream", "[cli][run]") {
    Argv argv({"cactus", "game.cactus", "--backend", "rust"});
    std::ostringstream out;
    std::ostringstream err;

    const int exit_code = cactus::cli::run(argv.argc(), argv.argv(), out, err);

    CHECK(exit_code == 1);
    CHECK(out.str().empty());
    CHECK(err.str().contains("unknown backend 'rust'"));
}

TEST_CASE("run() reports a missing input file through the injected error stream", "[cli][run]") {
    Argv argv({"cactus", "does_not_exist.cactus"});
    std::ostringstream out;
    std::ostringstream err;

    const int exit_code = cactus::cli::run(argv.argc(), argv.argv(), out, err);

    CHECK(exit_code == 1);
    CHECK(err.str().contains("cannot open file"));
}

// ── CIR emission (task 5.1) ─────────────────────────────────────────────────

namespace {

struct RunResult {
    int exit_code = 0;
    std::string out;
    std::string err;
};

RunResult run_cli(std::initializer_list<std::string> args) {
    Argv argv(args);
    std::ostringstream out;
    std::ostringstream err;
    const int exit_code = cactus::cli::run(argv.argc(), argv.argv(), out, err);
    return RunResult{.exit_code = exit_code, .out = out.str(), .err = err.str()};
}

std::filesystem::path cli_output_dir() {
    return std::filesystem::path{CACTUS_TEST_FIXTURES_DIR} / "cli_output";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

}  // namespace

TEST_CASE("run() writes CIR JSON to stdout by default", "[cli][run][cir]") {
    const auto result = run_cli({"cactus", fixture("multi_module/standalone.cactus"), "--emit", "cir"});

    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
    CHECK(result.out.starts_with("{\n  \"schema\": \"cactus-cir\",\n  \"version\": 1,\n"));
    CHECK(result.out.contains("\"standalone\""));
    CHECK(result.out.ends_with("}\n"));
    // The C++ backend must not have run.
    CHECK_FALSE(result.out.contains("Generated by Cactus DSL Compiler"));
}

TEST_CASE("run() writes each CIR format to the requested file without C++ formatting", "[cli][run][cir]") {
    const auto dir = cli_output_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    struct Case {
        const char* format;
        const char* file;
        const char* prefix;
    };
    for (const auto& [format, file, prefix] :
         {Case{.format = "json", .file = "standalone.json", .prefix = "{\n  \"schema\""},
          Case{.format = "dot", .file = "standalone.dot", .prefix = "digraph cactus_cir {\n"},
          Case{.format = "mermaid", .file = "standalone.mmd", .prefix = "flowchart LR\n"}}) {
        const auto path   = dir / file;
        const auto result = run_cli({"cactus",
                                     fixture("multi_module/standalone.cactus"),
                                     "--emit",
                                     "cir",
                                     "--format",
                                     format,
                                     "--output",
                                     path.string()});

        INFO("format: " << format << " stderr: " << result.err);
        REQUIRE(result.exit_code == 0);
        CHECK(result.out.empty());
        CHECK(result.err.contains("CIR v1"));
        // Written verbatim: byte-identical to the serializer, no line-ending
        // translation and no C++ formatter pass.
        CHECK(read_file(path).starts_with(prefix));
        CHECK_FALSE(read_file(path).contains('\r'));
    }

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("run() emits one linked CIR artifact for a multi-module program", "[cli][run][cir]") {
    const auto result = run_cli({"cactus", fixture("multi_module/main.cactus"), "--emit", "cir"});

    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
    // Every participating module appears in the linked module list.
    CHECK(result.out.contains("\"player\""));
    CHECK(result.out.contains("\"level\""));
    CHECK(result.out.contains("\"main\""));
    CHECK(result.out.contains("\"player.Position\""));
    CHECK(result.out.contains("\"level.LevelData\""));
}

TEST_CASE("run() rejects invalid CIR flag combinations before compiling", "[cli][run][cir]") {
    const auto format_without_cir = run_cli({"cactus", fixture("multi_module/standalone.cactus"), "--format", "dot"});
    CHECK(format_without_cir.exit_code == 1);
    CHECK(format_without_cir.out.empty());
    CHECK(format_without_cir.err.contains("--format requires --emit cir"));

    const auto backend_with_cir =
        run_cli({"cactus", fixture("multi_module/standalone.cactus"), "--emit", "cir", "--backend", "cpp-entt"});
    CHECK(backend_with_cir.exit_code == 1);
    CHECK(backend_with_cir.out.empty());
    CHECK(backend_with_cir.err.contains("--backend cannot be combined with --emit cir"));
}

TEST_CASE("run() reports an unwritable CIR output path", "[cli][run][cir]") {
    const auto path = (std::filesystem::path{CACTUS_TEST_FIXTURES_DIR} / "no_such_dir" / "out.json").string();
    const auto result =
        run_cli({"cactus", fixture("multi_module/standalone.cactus"), "--emit", "cir", "--output", path});

    CHECK(result.exit_code == 1);
    CHECK(result.err.contains("cannot open output file"));
}

TEST_CASE("run() emits identical CIR for repeated invocations", "[cli][run][cir]") {
    const auto first  = run_cli({"cactus", fixture("multi_module/main.cactus"), "--emit", "cir"});
    const auto second = run_cli({"cactus", fixture("multi_module/main.cactus"), "--emit", "cir"});

    REQUIRE(first.exit_code == 0);
    REQUIRE(second.exit_code == 0);
    CHECK(first.out == second.out);
}

TEST_CASE("run() still generates C++ by default and for an explicit --emit cpp", "[cli][run][cir]") {
    const auto implicit = run_cli({"cactus", fixture("multi_module/standalone.cactus")});
    const auto explicit_cpp =
        run_cli({"cactus", fixture("multi_module/standalone.cactus"), "--emit", "cpp", "--backend", "cpp-entt"});

    INFO("stderr: " << implicit.err);
    REQUIRE(implicit.exit_code == 0);
    REQUIRE(explicit_cpp.exit_code == 0);
    CHECK(implicit.out == explicit_cpp.out);
    CHECK(implicit.out.contains("Generated by Cactus DSL Compiler"));
    CHECK_FALSE(implicit.out.contains("cactus-cir"));
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity,bugprone-unchecked-optional-access)
