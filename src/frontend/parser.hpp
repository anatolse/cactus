#pragma once

#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/token.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace cactus {

class Parser {
public:
    Parser(std::vector<Token> tokens, ErrorReporter& errors);

    ProgramNode parse_program();

private:
    // Token navigation
    [[nodiscard]] const Token& peek() const;
    [[nodiscard]] const Token& peek_next() const;
    Token advance();
    Token consume(TokenType type, const std::string& msg);
    [[nodiscard]] bool check(TokenType type) const;
    bool match(TokenType type);

    // Panic-mode error recovery.
    // When a parse step reports an error, synchronization skips forward until a
    // safe recovery boundary is reached so outer loops can continue making
    // progress instead of repeatedly re-reading the same unexpected token.
    void skip_newlines();
    void expect_newline();
    void expect_indent();
    void expect_dedent();
    void synchronize();
    [[nodiscard]] bool is_synchronization_point() const;

    // Helpers
    [[nodiscard]] std::string parse_field_name_or_keyword_error(const char* error_msg);

    // Top-level declarations
    Declaration parse_declaration();
    ModuleNode parse_module();
    UseNode parse_use();
    ConstBlockNode parse_const_block();
    StructNode parse_struct();
    EnumNode parse_enum();
    TraitNode parse_trait();
    EntityNode parse_entity(bool is_pub);
    TemplateNode parse_template(bool is_pub);
    RuleNode parse_rule();
    ExternRuleNode parse_extern_rule();

    struct CommonRuleClauses {
        FilterClause filter;
        FilterClause exclude;
        std::vector<SortKey> order_by;
        std::vector<std::string> after_rules;
    };
    // Parses the filter:/exclude:/order by:/after: clauses shared by
    // `rule` and `extern rule` declarations, in that order. on_clause_parsed,
    // when set, is invoked with each of filter:/exclude:/order by:'s start
    // location and name right after that clause is parsed (before its own
    // synchronize check) — never for after:, which is compatible
    // with `pairs:`. parse_rule uses this to reject a clause combined with
    // `pairs:` at the
    // same point in the sequence the un-extracted code did, rather than
    // batching all such diagnostics after every clause has been parsed.
    CommonRuleClauses parse_common_rule_clauses(
        const std::function<void(const SourceLocation&, const char*)>& on_clause_parsed = nullptr);
    ViewNode parse_view();
    EventNode parse_event(bool is_pub = false, bool is_external = false);
    PhaseNode parse_phase(bool is_pub = false);
    FuncNode parse_func(bool is_pub);
    FuncNode parse_extern_func(bool is_pub);
    AssetDeclNode parse_asset_decl(bool is_pub);
    InputDeclNode parse_input_decl(bool is_pub);

    // Helpers
    std::string parse_dotted_name();
    LocatedName parse_located_name();

    // How a contextual `children:` block inside an archetype body is read:
    // declarations for templates/inline entities, overrides for template-backed
    // entities and spawn sites.
    enum class ChildBlockMode : std::uint8_t { Declarations, Overrides };

    struct ParsedArchetypeBody {
        std::vector<ArchetypeBodyEntry> entries;
        std::vector<ArchetypeTemplateUseEntry> template_uses;
        std::vector<ArchetypeTraitEntry> traits;
        std::vector<ChildArchetypeNode> children;
        std::vector<ChildOverrideNode> child_overrides;
    };

    struct ParsedOverrideBody {
        std::vector<ArchetypeTraitEntry> traits;
        std::vector<ChildOverrideNode> child_overrides;
    };

    // Sub-parsers
    FieldNode parse_field();
    FieldModifiers parse_field_modifiers();
    EventHandlerNode parse_event_handler();
    ExternHandlerNode parse_extern_handler();
    TypeRef parse_type_ref();
    FuncParam parse_param();
    std::vector<FuncParam> parse_param_list();
    FieldAssignment parse_field_assignment();
    std::vector<FieldAssignment> parse_field_assignment_block();
    ArchetypeTemplateUseEntry parse_archetype_template_use_entry();
    ArchetypeTraitEntry parse_archetype_trait_entry();
    ParsedArchetypeBody parse_archetype_body_entries(ChildBlockMode child_mode);
    ParsedOverrideBody parse_archetype_override_block();
    [[nodiscard]] bool at_children_block() const;
    std::vector<ChildArchetypeNode> parse_children_declaration_block();
    ChildArchetypeNode parse_child_declaration();
    std::vector<ChildOverrideNode> parse_children_override_block();
    ChildOverrideNode parse_child_override();
    FilterClause parse_filter_clause();
    FilterClause parse_exclude_clause();
    std::vector<SortKey> parse_order_by_clause();
    PairClause parse_pairs_clause();
    [[nodiscard]] bool at_pairs_clause() const;
    WhereClause parse_where_clause();
    [[nodiscard]] bool at_where_clause() const;
    LimitClause parse_limit_clause();
    [[nodiscard]] bool at_limit_clause() const;
    std::vector<LocatedName> parse_name_block(TokenType keyword, const char* clause_name);
    std::vector<HandlerReferenceNode> parse_handler_order_block();
    std::vector<HandlerCommandNode> parse_command_block();
    std::string parse_lifecycle_event_name();  // handles spawn/destroy/load/unload as names
    ViewElement parse_view_element();

    // Statements
    AddTraitStmt parse_add_trait_stmt();
    RemoveTraitStmt parse_remove_trait_stmt();
    ProjectTraitStmt parse_project_trait_stmt();
    ForeachStmt parse_foreach_stmt();
    TraitMatchStmt parse_trait_match_stmt();
    LetStmt parse_let_stmt();
    EmitStmt parse_emit_stmt();
    ReturnStmt parse_return_stmt();
    IfStmt parse_if_stmt();
    SpawnStmt parse_spawn_stmt();
    DestroyStmt parse_destroy_stmt();
    LoadStmt parse_load_stmt();
    // Assignment target: IDENTIFIER (DOT IDENTIFIER)* (= | += | -=) expression,
    // backtracking to expression-statement parsing when the identifier (with
    // or without a dotted chain) isn't followed by an assignment operator.
    std::unique_ptr<StmtNode> parse_assign_or_expr_stmt();
    std::unique_ptr<StmtNode> parse_statement();
    std::vector<std::unique_ptr<StmtNode>> parse_block();

    // Expressions (precedence climbing)
    std::unique_ptr<ExprNode> parse_expression();
    std::unique_ptr<ExprNode> parse_or_expr();
    std::unique_ptr<ExprNode> parse_and_expr();
    std::unique_ptr<ExprNode> parse_equality_expr();
    std::unique_ptr<ExprNode> parse_comparison_expr();
    std::unique_ptr<ExprNode> parse_additive_expr();
    std::unique_ptr<ExprNode> parse_multiplicative_expr();
    std::unique_ptr<ExprNode> parse_unary_expr();
    std::unique_ptr<ExprNode> parse_postfix_expr();
    std::unique_ptr<ExprNode> parse_primary_expr();
    MatchExpr parse_match_expr();
    IfExpr parse_if_expr();

    std::vector<Token> tokens_;
    size_t current_ = 0;
    ErrorReporter& errors_;
};

}  // namespace cactus
