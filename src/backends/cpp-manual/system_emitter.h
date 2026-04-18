#pragma once

#include "backends/cpp-manual/soa_emitter.h"
#include "frontend/ast.h"
#include "frontend/semantic_analyzer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

// ── Codegen context for dynamic ECS model ────────────────────────────────────
/// All program-wide information needed by the dynamic ECS code generator.
struct CodegenContext {
    /// trait name → bit position (used to build TraitBits:: mask expressions)
    std::unordered_map<std::string, int> trait_bit_index;
    /// all resolved traits (field names + types)
    std::unordered_map<std::string, ResolvedTrait> traits;
    /// full AST (non-owning pointer — valid for lifetime of generate() call)
    ProgramNode* ast = nullptr;
    /// trait names in declaration order (defines bit assignments)
    std::vector<std::string> trait_names_ordered;

    // ── AST lookup maps (populated from ast during generate()) ──────────────
    std::unordered_map<std::string, const TraitNode*>    trait_ast;
    std::unordered_map<std::string, const TemplateNode*> template_ast;
    std::unordered_map<std::string, const UnitNode*>     unit_ast;

    /// template_name → (field_name → C++ expression string for config default)
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> template_config;

    /// trait_name → (field_name → C++ expression string for field default value)
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> trait_defaults;
};

// ── System emitter ─────────────────────────────────────────────────────────

class ManualSystemEmitter {
public:
    // ── Legacy (backward-compatible) interface ────────────────────────────

    /// Emit all handler functions for one system using the old per-trait storage model.
    static std::string emit_system(const SystemNode& sys, const DecoratedProgram& program);

    /// Emit a statement (legacy: appends [i] on VarAssign).
    static std::string emit_stmt(const StmtNode& stmt, int indent);

    // ── Dynamic ECS model ─────────────────────────────────────────────────

    /// Emit all handler functions for one system using the bitmask model (tasks 7.3–7.14).
    static std::string emit_system_dynamic(const SystemNode& sys, const CodegenContext& ctx);
    static std::string emit_extern_system_dynamic(const ExternSystemNode& sys, const CodegenContext& ctx);
    static std::string emit_extern_system_forward_decl(const ExternSystemNode& sys, const CodegenContext& ctx);

    /// Emit forward declarations for all handler functions declared in a system.
    static std::string emit_system_forward_decls(const SystemNode& sys);

    /// Emit a statement for the dynamic ECS model.
    /// entity_index_var: C++ variable holding the current entity slot index.
    /// in_loop: true when inside a loop handler (enables __destroyed flag idiom).
    static std::string emit_stmt_dynamic(const StmtNode& stmt, int indent,
                                         const CodegenContext& ctx,
                                         const std::string& entity_index_var = "i",
                                         bool in_loop = true);

    /// Compute the C++ bitmask expression (e.g. "TraitBits::A | TraitBits::B")
    /// for a filter or exclude clause.  Returns "0ULL" for empty clauses.
    static std::string compute_mask_expr(const FilterClause& clause,
                                         const CodegenContext& ctx);

    static std::string emit_expr(const ExprNode& expr, const ProgramNode* ast = nullptr);
    static std::string emit_expr_dynamic(const ExprNode& expr,
                                         const std::string& entity_index_var = "i",
                                         const ProgramNode* ast = nullptr);
    static std::string indent_str(int level);

private:
    /// Emit a spawn_TemplateName(...) call, filling all positional args.
    static std::string emit_spawn_call(const SpawnStmt& s, const CodegenContext& ctx);
};

}  // namespace cactus
