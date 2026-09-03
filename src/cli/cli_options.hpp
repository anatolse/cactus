#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace cactus::cli {

/// What the compiler emits: generated target code, or the backend-neutral
/// Cactus Intermediate Representation.
enum class OutputKind : std::uint8_t { Cpp, Cir };

/// Serialization selected for CIR emission. JSON is the complete versioned
/// form; DOT and Mermaid are graphical projections.
enum class CirFormat : std::uint8_t { Json, Dot, Mermaid };

/// Parsed CLI arguments (see print_usage for the flag reference).
struct CliOptions {
    std::string input_file;
    std::string backend = "cpp-entt";
    std::string output_file;
    std::vector<std::filesystem::path> module_paths;
    OutputKind emit       = OutputKind::Cpp;
    CirFormat cir_format  = CirFormat::Json;
    bool backend_explicit = false;
    bool format_explicit  = false;
};

void print_usage(const char* program, std::ostream& err);

/// Parses argv into `out`. Returns an exit code if the caller should return
/// immediately (0 for --help, 1 for a usage error), or nullopt to continue.
/// Cross-option constraints are checked after every argument is consumed, so
/// results do not depend on flag order.
std::optional<int> parse_cli_args(int argc, char** argv, CliOptions& out, std::ostream& err);

}  // namespace cactus::cli
