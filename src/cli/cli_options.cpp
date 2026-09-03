#include "cli/cli_options.hpp"

#include <ostream>
#include <string_view>

namespace cactus::cli {
namespace {

/// Raw flag values captured during the argument sweep. Validation happens after
/// every argument is consumed so results do not depend on flag order.
struct RawArgs {
    std::optional<std::string> emit;
    std::optional<std::string> format;
    std::optional<std::string> backend;
    std::optional<std::string> output;
    std::optional<std::string> input_file;
    std::vector<std::string> module_paths;
};

std::optional<OutputKind> parse_output_kind(std::string_view value) {
    if (value == "cpp") {
        return OutputKind::Cpp;
    }
    if (value == "cir") {
        return OutputKind::Cir;
    }
    return std::nullopt;
}

std::optional<CirFormat> parse_cir_format(std::string_view value) {
    if (value == "json") {
        return CirFormat::Json;
    }
    if (value == "dot") {
        return CirFormat::Dot;
    }
    if (value == "mermaid") {
        return CirFormat::Mermaid;
    }
    return std::nullopt;
}

/// Maps a single-value flag to the raw slot it fills, or nullptr when `flag` is
/// not such a flag.
std::optional<std::string>* slot_for(std::string_view flag, RawArgs& raw) {
    if (flag == "--emit") {
        return &raw.emit;
    }
    if (flag == "--format") {
        return &raw.format;
    }
    if (flag == "--backend") {
        return &raw.backend;
    }
    if (flag == "--output" || flag == "-o") {
        return &raw.output;
    }
    return nullptr;
}

/// Consumes the value following the flag at `i`, advancing `i`. Returns nullptr
/// and reports when the flag ends the argument list.
const char* take_value(int argc, char** argv, int& i, std::string_view flag, std::ostream& err) {
    if (i + 1 >= argc) {
        err << "error: " << flag << " requires an argument\n";
        return nullptr;
    }
    return argv[++i];
}

/// Sweeps argv into `raw`. Returns an exit code to stop on, or nullopt.
std::optional<int> collect_arguments(int argc, char** argv, RawArgs& raw, std::ostream& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0], err);
            return 0;
        }
        if (arg == "--module-path") {
            const char* value = take_value(argc, argv, i, arg, err);
            if (value == nullptr) {
                return 1;
            }
            raw.module_paths.emplace_back(value);
            continue;
        }
        if (std::optional<std::string>* slot = slot_for(arg, raw); slot != nullptr) {
            const char* value = take_value(argc, argv, i, arg, err);
            if (value == nullptr) {
                return 1;
            }
            *slot = value;
            continue;
        }
        if (arg.starts_with('-')) {
            err << "error: unknown option '" << arg << "'\n";
            return 1;
        }
        raw.input_file = arg;
    }
    return std::nullopt;
}

/// Resolves captured flag values into `out`. Returns an exit code on a bad
/// value, or nullopt.
std::optional<int> resolve_values(const RawArgs& raw, CliOptions& out, std::ostream& err) {
    if (raw.emit.has_value()) {
        const auto kind = parse_output_kind(*raw.emit);
        if (!kind.has_value()) {
            err << "error: unknown output kind '" << *raw.emit << "' (use cpp or cir)\n";
            return 1;
        }
        out.emit = *kind;
    }
    if (raw.backend.has_value()) {
        out.backend          = *raw.backend;
        out.backend_explicit = true;
        if (out.backend != "cpp-entt") {
            err << "error: unknown backend '" << out.backend << "' (use cpp-entt)\n";
            return 1;
        }
    }
    if (raw.format.has_value()) {
        const auto format = parse_cir_format(*raw.format);
        if (!format.has_value()) {
            err << "error: unknown CIR format '" << *raw.format << "' (use json, dot, or mermaid)\n";
            return 1;
        }
        out.cir_format      = *format;
        out.format_explicit = true;
    }
    return std::nullopt;
}

/// Checks constraints that span several options, once all of them are known.
std::optional<int> check_cross_option_constraints(const CliOptions& out, std::ostream& err) {
    if (out.format_explicit && out.emit != OutputKind::Cir) {
        err << "error: --format requires --emit cir\n";
        return 1;
    }
    if (out.backend_explicit && out.emit == OutputKind::Cir) {
        err << "error: --backend cannot be combined with --emit cir (CIR is backend-neutral)\n";
        return 1;
    }
    if (out.input_file.empty()) {
        err << "error: no input file specified\n";
        return 1;
    }
    return std::nullopt;
}

}  // namespace

void print_usage(const char* program, std::ostream& err) {
    err << "Usage: " << program << " <input.cactus> [options]\n"
        << "\nOptions:\n"
        << "  --emit <cpp|cir>                     What to emit (default: cpp)\n"
        << "  --backend <cpp-entt>                Code generation backend (default: cpp-entt)\n"
        << "  --format <json|dot|mermaid>          CIR serialization, requires --emit cir (default: json)\n"
        << "  --output <file>                      Output file (default: stdout)\n"
        << "  --module-path <dir>                  Additional module search directory (repeatable)\n"
        << "  --help                               Show this help message\n";
}

std::optional<int> parse_cli_args(int argc, char** argv, CliOptions& out, std::ostream& err) {
    RawArgs raw;
    if (auto exit_code = collect_arguments(argc, argv, raw, err); exit_code.has_value()) {
        return exit_code;
    }

    out.input_file  = raw.input_file.value_or(std::string{});
    out.output_file = raw.output.value_or(std::string{});
    out.module_paths.assign(raw.module_paths.begin(), raw.module_paths.end());

    if (auto exit_code = resolve_values(raw, out, err); exit_code.has_value()) {
        return exit_code;
    }
    return check_cross_option_constraints(out, err);
}

}  // namespace cactus::cli
