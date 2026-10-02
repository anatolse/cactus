#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cactus {

// Which stdlib WorldTransform variant(s) the root module's declarations
// (entities, templates, rule filters) resolve to. Falls back to the merged
// program's single variant when the root references neither explicitly.
struct WorldTransformUsage {
    bool flat   = false;
    bool volume = false;
};

class EnttCodegenUtils {
public:
    static std::string type_to_cpp(const TypeInfo& type);
    // Stored-value type: entity ids use the live registry entity type.
    static std::string value_type_to_cpp(const TypeInfo& type);
    static std::string emit_enum(const ResolvedEnum& e);
    static std::string emit_expr(const ExprNode& expr, const ProgramNode* ast = nullptr);
    static std::string emit_expr(const ExprNode& expr, const DecoratedProgram& program);
    // Comma-joined emit_expr(*args[i], program) for a call/query argument list.
    static std::string join_emitted_args(const std::vector<std::unique_ptr<ExprNode>>& args,
                                         const DecoratedProgram& program);

    // Canonical generated names. These overloads are the codegen-facing path:
    // resolved SymbolIds are lowered directly without alias/module lookup.
    static std::string symbol_cpp_name(const SymbolId& symbol);
    // Named entities: the global handle slot, and the per-pass reference a
    // handler hoists for one of its traits.
    static std::string named_slot_name(const SymbolId& entity);
    static std::string named_trait_ref_name(const NamedTraitRef& ref, const DecoratedProgram& program);
    // The slot or hoisted reference a resolved named member chain lowers to.
    static std::optional<std::string> named_member_cpp(const MemberExpr& member, const DecoratedProgram& program);
    static std::vector<const EntityNode*> declared_entities(const DecoratedProgram& program);
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

    // The runtime occurrence an `on added T` / `on removed T` trigger receives;
    // nullopt for any other trigger kind.
    static std::optional<std::string> lifecycle_occurrence_cpp_type(const ResolvedHandlerTrigger& trigger,
                                                                    const DecoratedProgram& program);
    // Traits named by an `on added` / `on removed` trigger, in canonical order.
    static std::vector<SymbolId> watched_lifecycle_traits(const DecoratedProgram& program);
    // Whether generated code tracks trait lifecycle (graph-driven programs with a trigger only).
    static bool tracks_lifecycle(const DecoratedProgram& program);
    // The generated handler function's name suffix, e.g. `tick`, `combat__Hit`, `added_Dying`.
    static std::string handler_function_suffix(const EventHandlerNode& handler, const DecoratedProgram& program);
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
    static const ResolvedStruct* find_struct(const DecoratedProgram& program, const SymbolId& symbol);
    static const ResolvedFunc*   find_func(const DecoratedProgram& program, const SymbolId& symbol);
    static const ResolvedConst*  find_const(const DecoratedProgram& program, const SymbolId& symbol);
    // Whether evaluating `expr` may have effects: a call not proven pure, a query, or a spawn.
    static bool has_effects(const ExprNode& expr, const DecoratedProgram& program);
    // `Struct{.field = ...}` in the struct's field order; arguments written in another
    // order are evaluated into temporaries first when one of them has effects.
    static std::string emit_struct_construction(const CallExpr& call,
                                                const DecoratedProgram& program,
                                                const std::function<std::string(const ExprNode&)>& emit_argument);

    // Editor/rig dimensionality (D2): derived from the root module's resolved
    // WorldTransform references, never from merged-map presence — std.editor
    // transitively imports both flat and volume variants into every program.
    static WorldTransformUsage world_transform_usage(const DecoratedProgram& program);

    // A trait field's declared `= expression` default, read from the AST so the
    // backend never keeps a copy that can drift from the declaration. Tracks the
    // enclosing module while scanning: trait names collide across modules (e.g.
    // flat and volume both declare `WorldTransform`, with different field types).
    static const ExprNode* find_trait_field_default(const DecoratedProgram& program,
                                                    const std::string& module_name,
                                                    const std::string& trait_name,
                                                    const std::string& field_name);
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