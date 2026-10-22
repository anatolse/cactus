#pragma once

#include "frontend/ast.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cactus {

// Which stdlib WorldTransform variant(s) the root module's declarations
// (entities, templates, rule filters) resolve to. Falls back to the merged
// program's single variant when the root references neither explicitly.
struct WorldTransformUsage {
    bool flat   = false;
    bool volume = false;
};

// A name the backend invents, declared under `cactus_gen::detail` plus a mirrored module path.
struct DetailName {
    std::string ns;
    std::string name;
    [[nodiscard]] std::string ref() const {
        return "::" + ns + "::" + name;
    }
};

// `body` declared inside namespace `ns`.
std::string namespace_block(const std::string& ns, const std::string& body);
// "cactus_gen::detail" plus the module's mirrored namespace path.
std::string detail_namespace_path(const std::string& module_name);
DetailName detail_name(const std::string& module_name, const std::string& name);
std::string join_strings(const std::vector<std::string>& items, std::string_view separator);
// The local a generated function binds a component to: `::cactus_gen::game::Session` -> `game_session_comp`.
std::string component_local(const std::string& type_ref);

class EnttCodegenUtils {
public:
    static std::string type_to_cpp(const TypeInfo& type);
    // Stored-value type: entity ids use the live registry entity type.
    static std::string value_type_to_cpp(const TypeInfo& type);
    // Member initializer for a field with no declared default; an unset entity id is stale, and a
    // class type with its own default constructor gets none.
    static std::string value_initializer(const TypeInfo& type);
    static std::string emit_enum(const ResolvedEnum& e);
    static std::string emit_expr(const ExprNode& expr, const ProgramNode* ast = nullptr);
    static std::string emit_expr(const ExprNode& expr, const DecoratedProgram& program);
    // `((s == p1) ? v1 : last)` from emitted `{pattern, value}` arms. It repeats the subject, so only
    // pure contexts (constants, defaults, shader stages) may use it. `wrap_nested` wraps each inner
    // level in `cactus_chosen(...)` for C++.
    static std::string match_ternary_chain(const std::string& subject,
                                           const std::vector<std::pair<std::string, std::string>>& tested_arms,
                                           const std::string& last_value,
                                           bool wrap_nested);
    // C++ ternaries for the conditional expressions, emitting operands through `emit`.
    static std::string emit_if_ternary(const IfExpr& if_expr, const std::function<std::string(const ExprNode&)>& emit);
    static std::string emit_match_ternary(const MatchExpr& match_expr,
                                          const std::function<std::string(const ExprNode&)>& emit);
    // Binary/unary operator text; `int` arithmetic calls the total `cactus::num` helpers.
    static std::string emit_binary(const BinaryExpr& binary, const std::string& left, const std::string& right);
    static std::string emit_unary(const UnaryExpr& unary, const std::string& operand);
    // `target op= value` as a statement body (no indent, no trailing `;`).
    static std::string emit_compound_assignment(const VarAssign& assign,
                                                const std::string& target,
                                                const std::string& value);
    // Comma-joined emit_expr(*args[i], program) for a call/query argument list.
    static std::string join_emitted_args(const std::vector<std::unique_ptr<ExprNode>>& args,
                                         const DecoratedProgram& program);

    // Canonical generated names. These overloads are the codegen-facing path:
    // resolved SymbolIds are lowered directly without alias/module lookup.
    static std::string symbol_cpp_name(const SymbolId& symbol);
    // The generated constant holding a periodic phase's fixed `dt`.
    static DetailName phase_dt_name(const SymbolId& phase);
    static std::string phase_dt_cpp_name(const SymbolId& phase);
    // The generated function building a collider descriptor from `shape_trait`,
    // or from whichever shape trait the entity has when none is given.
    static std::string collider_shape_builder(std::optional<std::string_view> shape_trait = std::nullopt);
    // Named entities: the global handle slot, and the per-pass reference a
    // handler hoists for one of its traits.
    static std::string named_slot_name(const SymbolId& entity);
    // The slot's member path in GeneratedNamedSlots: the mirrored module path, then the entity.
    static std::vector<std::string> named_slot_path(const SymbolId& entity);
    static std::string named_trait_ref_name(const NamedTraitRef& ref);
    // The slot or hoisted reference a resolved named member chain lowers to.
    static std::optional<std::string> named_member_cpp(const MemberExpr& member);
    // `.r/.g/.b/.a` on a color: a byte field read as a normalized float.
    static bool is_color_component_member(const std::string& member);
    static std::vector<const EntityNode*> declared_entities(const DecoratedProgram& program);
    // The program's hierarchy children index, as a C++ expression over `registry`.
    static std::string hierarchy_index_expr(const DecoratedProgram& program);
    // Whether a committed `add Parent` needs the cycle check: the program can
    // reparent, or its hierarchy facts were never derived.
    static bool checks_reparent(const DecoratedProgram& program);
    // The propagation strategy the hierarchy facts allow.
    static std::string hierarchy_strategy_cpp(const DecoratedProgram& program);
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
    static const ExprNode*       find_const_initializer(const ProgramNode& ast, const SymbolId& symbol);
    // Emitted `inline constexpr` (strings as `std::string_view`); other constants stay `inline const`.
    static bool is_constexpr_const(const DecoratedProgram& program, const SymbolId& symbol);
    // The constant `expr` reads when it is a bare constant name, else null.
    static const SymbolId* named_const(const ExprNode& expr);
    // A float literal, by `std::numbers` name when its float value is exactly that constant's.
    static std::string float_literal_cpp(const std::string& value);
    // Holds no string or list, so moving a value of it is a plain copy.
    static bool is_trivially_copyable(const TypeInfo& type, const DecoratedProgram& program);
    // A `std::string_view` constant read as a `string` value is copied into one at the read.
    static std::string const_reference_cpp(const DecoratedProgram& program, const SymbolId& symbol);
    static const ResolvedState* find_state(const DecoratedProgram& program, const SymbolId& state);

    struct StateVariant {
        std::string slot_cpp;
        std::size_t index = 0;
    };
    static std::optional<StateVariant> state_variant(const DecoratedProgram& program,
                                                     const std::optional<SymbolId>& trait);
    // The C++ name of a state's slot component, named like its slot trait.
    static std::string state_slot_cpp_name(const DecoratedProgram& program, const SymbolId& state);
    // The statement that puts `entity_expr` in `variant` with `value`, keeping the slot in sync.
    static std::string enter_state_call(const StateVariant& variant,
                                        const std::string& entity_expr,
                                        const std::string& value);
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

// A generated rule function. Cactus handlers are the overloads `<rule>_on`, keyed by trigger type.
DetailName system_function_name(const std::string& module_name,
                                const std::string& system_name,
                                const std::string& suffix);
// Creation functions of a template or entity, and the per-node helpers of a hierarchical one.
DetailName archetype_create_function_name(const std::string& module_name, const std::string& archetype_name);
DetailName archetype_create_at_function_name(const std::string& module_name, const std::string& archetype_name);
DetailName archetype_node_create_function_name(const std::string& module_name,
                                               const std::string& archetype_name,
                                               const std::vector<std::string>& role_path);
DetailName archetype_node_create_at_function_name(const std::string& module_name,
                                                  const std::string& archetype_name,
                                                  const std::vector<std::string>& role_path);
std::string event_cpp_type_name(const SymbolId& event_id);

}  // namespace cactus