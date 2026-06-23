#pragma once

#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/token.hpp"

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
    SystemNode parse_system();
    ExternSystemNode parse_extern_system();
    ViewNode parse_view();
    EventNode parse_event(bool is_pub = false);
    FuncNode parse_func(bool is_pub);
    FuncNode parse_extern_func(bool is_pub);
    InterfaceNode parse_interface();
    AssetDeclNode parse_asset_decl(bool is_pub);
    InputDeclNode parse_input_decl(bool is_pub);

    // Helpers
    std::string parse_dotted_name();

    struct ParsedArchetypeBody {
        std::vector<ArchetypeBodyEntry> entries;
        std::vector<ArchetypeTemplateUseEntry> template_uses;
        std::vector<ArchetypeTraitEntry> traits;
    };

    // Sub-parsers
    FieldNode parse_field();
    FieldModifiers parse_field_modifiers();
    EventHandlerNode parse_event_handler();
    TypeRef parse_type_ref();
    FuncParam parse_param();
    std::vector<FuncParam> parse_param_list();
    FieldAssignment parse_field_assignment();
    std::vector<FieldAssignment> parse_field_assignment_block();
    ArchetypeTemplateUseEntry parse_archetype_template_use_entry();
    ArchetypeTraitEntry parse_archetype_trait_entry();
    ParsedArchetypeBody parse_archetype_body_entries();
    std::vector<ArchetypeTraitEntry> parse_archetype_trait_entries();
    std::vector<ArchetypeTraitEntry> parse_archetype_trait_entry_block();
    FilterClause parse_filter_clause();
    FilterClause parse_exclude_clause();
    std::vector<SortKey> parse_order_by_clause();
    std::string parse_lifecycle_event_name();  // handles spawn/destroy/load/unload as names
    ViewElement parse_view_element();

    // Statements
    AddTraitStmt parse_add_trait_stmt();
    RemoveTraitStmt parse_remove_trait_stmt();
    ProjectTraitStmt parse_project_trait_stmt();
    ForeachStmt parse_foreach_stmt();
    TraitMatchStmt parse_trait_match_stmt();
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

    std::vector<Token> tokens_;
    size_t current_ = 0;
    ErrorReporter& errors_;
};

}  // namespace cactus
