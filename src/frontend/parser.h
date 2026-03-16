#pragma once

#include "common/error_reporter.h"
#include "frontend/ast.h"
#include "frontend/token.h"

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
    void skip_newlines();
    void expect_newline();
    void expect_indent();
    void expect_dedent();

    // Top-level declarations
    Declaration parse_declaration();
    ModuleNode parse_module();
    UseNode parse_use();
    ConstBlockNode parse_const_block();
    StructNode parse_struct();
    EnumNode parse_enum();
    TraitNode parse_trait();
    UnitNode parse_unit(bool is_pub);
    SystemNode parse_system();
    ViewNode parse_view();
    EventNode parse_event();
    FuncNode parse_func(bool is_pub);
    InterfaceNode parse_interface();

    // Helpers
    std::string parse_dotted_name();

    // Sub-parsers
    FieldNode parse_field();
    FieldModifiers parse_field_modifiers();
    EventHandlerNode parse_event_handler();
    TypeRef parse_type_ref();
    FuncParam parse_param();
    std::vector<FuncParam> parse_param_list();
    ApplyBlock parse_apply_block();
    ConfigBlock parse_config_block();
    ChildBlock parse_child_block();
    FilterClause parse_filter_clause();
    ViewElement parse_view_element();

    // Statements
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
