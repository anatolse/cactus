#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <string>

namespace cactus {

// Which stdlib WorldTransform variant(s) the root module's declarations
// (entities, templates, system filters) resolve to. Falls back to the merged
// program's single variant when the root references neither explicitly.
struct WorldTransformUsage {
    bool flat   = false;
    bool volume = false;
};

class EnttCodegenUtils {
public:
    static std::string type_to_cpp(const TypeInfo& type);
    static std::string emit_enum(const ResolvedEnum& e);
    static std::string emit_expr(const ExprNode& expr, const ProgramNode* ast = nullptr);
    static std::string emit_expr(const ExprNode& expr, const DecoratedProgram& program);

    // Canonical generated names. These overloads are the codegen-facing path:
    // resolved SymbolIds are lowered directly without alias/module lookup.
    static std::string symbol_cpp_name(const SymbolId& symbol);
    static std::string trait_cpp_name(const SymbolId& symbol);
    static std::string struct_cpp_name(const SymbolId& symbol);
    static std::string enum_cpp_name(const SymbolId& symbol);
    static std::string trait_cpp_name(const std::optional<SymbolId>& symbol,
                                      const std::string& fallback_source_name,
                                      const DecoratedProgram& program);
    static std::string struct_cpp_name(const std::optional<SymbolId>& symbol,
                                       const std::string& fallback_source_name,
                                       const DecoratedProgram& program);
    static std::string enum_cpp_name(const std::optional<SymbolId>& symbol,
                                     const std::string& fallback_source_name,
                                     const DecoratedProgram& program);

    // Legacy fallback for call sites that have not yet been threaded with a
    // resolved SymbolId. This does not resolve aliases or scan UseNode imports;
    // it only consults already-resolved declaration metadata on DecoratedProgram.
    static std::string trait_cpp_name(const std::string& source_name, const DecoratedProgram& program);
    static std::string struct_cpp_name(const std::string& source_name, const DecoratedProgram& program);
    static std::string enum_cpp_name(const std::string& source_name, const DecoratedProgram& program);

    // Lookup helpers that work with both simple-name-keyed (single-module) and
    // canonical-id-keyed (multi-module) program maps. `name` may be a canonical
    // id ("std.transform.flat.WorldTransform") or a simple name; a simple name
    // matching two traits with different canonical ids throws an internal
    // codegen error — such call sites must pass a canonical id.
    static const ResolvedTrait*  find_trait(const DecoratedProgram& program, const std::string& name);
    static bool                  has_trait(const DecoratedProgram& program, const std::string& name);
    static const ResolvedEnum*   find_enum(const DecoratedProgram& program, const std::string& simple_name);
    static const ResolvedStruct* find_struct(const DecoratedProgram& program, const std::string& simple_name);

    // Editor/rig dimensionality (D2): derived from the root module's resolved
    // WorldTransform references, never from merged-map presence — std.editor
    // transitively imports both flat and volume variants into every program.
    static WorldTransformUsage world_transform_usage(const DecoratedProgram& program);
};

// Canonical C++ name for a generated system handler function.
// Used by both cpp_entt_codegen.cpp and system_emitter.cpp; kept here to avoid
// having two identical copies in anonymous namespaces.
std::string system_function_name(const std::string& module_name,
                                 const std::string& system_name,
                                 const std::string& suffix);
std::string system_function_name(const SymbolId& system_id, const std::string& suffix);
std::string event_cpp_type_name(const SymbolId& event_id);

}  // namespace cactus