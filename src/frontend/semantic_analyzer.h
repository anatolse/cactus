#pragma once

#include "common/error_reporter.h"
#include "common/string_pool.h"
#include "common/types.h"
#include "frontend/ast.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

// ── Decorated AST (Task 8.8 combined here) ──────────────────────────────────

struct ResolvedField {
    std::string name;
    TypeInfo type;
    bool is_let = false;
    bool is_var = false;
    bool is_persist = false;
    bool is_sync = false;
    bool is_pub = false;
};

struct ResolvedTrait {
    std::string name;
    std::vector<ResolvedField> fields;
    bool is_pub = false;
};

struct ResolvedStruct {
    std::string name;
    std::vector<ResolvedField> fields;
};

struct ResolvedEnum {
    std::string name;
    std::vector<std::string> variants;
};

struct SystemDependency {
    std::string system_name;
    std::unordered_set<std::string> reads;
    std::unordered_set<std::string> writes;
    std::unordered_set<std::string> emits;
};

struct DecoratedProgram {
    std::unordered_map<std::string, ResolvedTrait> traits;
    std::unordered_map<std::string, ResolvedStruct> structs;
    std::unordered_map<std::string, ResolvedEnum> enums;
    std::vector<SystemDependency> dependency_graph;
    StringPool string_pool;
    ProgramNode* ast = nullptr;  // non-owning pointer to original AST
};

// ── Semantic Analyzer ───────────────────────────────────────────────────────

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(ErrorReporter& errors);

    DecoratedProgram analyze(ProgramNode& program);

private:
    // Phase 1: Collect type declarations
    void collect_types(ProgramNode& program);

    // Phase 2: Resolve types in fields
    void resolve_all_types(ProgramNode& program);
    TypeInfo resolve_type_ref(const TypeRef& ref);

    // Phase 3: Semantic checks
    void check_const_strings(ProgramNode& program);
    void check_const_strings_expr(const ExprNode& expr, bool in_const);
    void check_func_purity(ProgramNode& program);
    void check_func_purity_stmt(const StmtNode& stmt, const std::string& func_name);
    void check_func_purity_expr(const ExprNode& expr, const std::string& func_name);
    void check_no_recursion(ProgramNode& program);
    void check_persist_sync(ProgramNode& program);
    void validate_system_filters(ProgramNode& program);
    void validate_event_usage(ProgramNode& program);

    // Phase 4: Build dependency graph
    void build_dependency_graph(ProgramNode& program);
    void collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts, SystemDependency& dep);

    // Helpers
    bool is_known_type(const std::string& name) const;

    ErrorReporter& errors_;
    DecoratedProgram result_;

    // Known type names
    std::unordered_set<std::string> struct_names_;
    std::unordered_set<std::string> enum_names_;
    std::unordered_set<std::string> trait_names_;
    std::unordered_set<std::string> event_names_;
    std::unordered_set<std::string> func_names_;

    // For recursion detection
    std::unordered_map<std::string, std::unordered_set<std::string>> call_graph_;
};

}  // namespace cactus
