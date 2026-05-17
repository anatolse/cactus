#pragma once

#include "common/error_reporter.hpp"
#include "common/types.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

// Note: ImportedSymbols is defined in semantic_analyzer.hpp (included above).

/// Serializes and deserializes a compiled module's `DecoratedProgram` to/from
/// a `.cmod` binary artifact file.
///
/// Binary format:
///   [magic: "CMOD"][version: uint8][module_name: str]
///   [traits section][structs section][enums section]
///   [dep graph section][string pool section]
///
/// The `ProgramNode* ast` pointer is NOT serialized — it is left null after
/// deserialization. Callers that need the AST must re-parse from source.
class ModuleArtifact {
public:
    static constexpr uint8_t CURRENT_VERSION = 5;
    static constexpr const char* MAGIC       = "CMOD";

    explicit ModuleArtifact(ErrorReporter& errors);

    /// Save `program` to `build_dir/<module_name>.cmod`.
    /// Creates `build_dir` if it does not exist (when create_dir == true).
    bool save(const DecoratedProgram& program,
              const std::string& module_name,
              const std::filesystem::path& build_dir,
              bool create_dir = true);

    /// Load a `.cmod` file. Returns nullopt on error.
    /// Populates `module_name_out` with the serialized module name.
    std::optional<DecoratedProgram> load(const std::filesystem::path& path, std::string& module_name_out);

    /// Extract only pub symbols from a `.cmod` file without loading the full program.
    std::optional<ImportedSymbols> extract_pub_symbols(const std::filesystem::path& path);

    /// Convert module_name to artifact filename: "enemies.walker" → "enemies.walker.cmod"
    static std::filesystem::path artifact_filename(const std::string& module_name);

private:
    // ── Write helpers ───────────────────────────────────────────────────────
    static void write_u8(std::ostream& out, uint8_t v);
    static void write_u32(std::ostream& out, uint32_t v);
    static void write_bool(std::ostream& out, bool v);
    static void write_str(std::ostream& out, const std::string& s);
    void write_type_info(std::ostream& out, const TypeInfo& t);

    void write_traits(std::ostream& out, const std::unordered_map<std::string, ResolvedTrait>& traits);
    void write_structs(std::ostream& out, const std::unordered_map<std::string, ResolvedStruct>& structs);
    static void write_enums(std::ostream& out, const std::unordered_map<std::string, ResolvedEnum>& enums);
    void write_funcs(std::ostream& out, const std::unordered_map<std::string, ResolvedFunc>& funcs);
    static void write_string_set(std::ostream& out, const std::unordered_set<std::string>& values);
    static void write_dep_graph(std::ostream& out, const std::vector<SystemDependency>& graph);
    static void write_string_pool(std::ostream& out, const StringPool& pool);

    // ── Read helpers ────────────────────────────────────────────────────────
    static uint8_t read_u8(std::istream& in);
    static uint32_t read_u32(std::istream& in);
    static bool read_bool(std::istream& in);
    static std::string read_str(std::istream& in);
    TypeInfo read_type_info(std::istream& in);

    std::unordered_map<std::string, ResolvedTrait> read_traits(std::istream& in);
    std::unordered_map<std::string, ResolvedStruct> read_structs(std::istream& in);
    static std::unordered_map<std::string, ResolvedEnum> read_enums(std::istream& in);
    std::unordered_map<std::string, ResolvedFunc> read_funcs(std::istream& in);
    static std::unordered_set<std::string> read_string_set(std::istream& in);
    static std::vector<SystemDependency> read_dep_graph(std::istream& in);
    static StringPool read_string_pool(std::istream& in);

    ErrorReporter& errors_;
};

}  // namespace cactus
