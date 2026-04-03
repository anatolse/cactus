#pragma once

#include "common/source_location.h"

#include <string>

namespace cactus {

enum class TokenType {
    // Keywords — declarations
    MODULE,
    USE,
    CONST,
    STRUCT,
    ENUM,
    TRAIT,
    UNIT,
    TEMPLATE,
    SYSTEM,
    VIEW,
    EVENT,
    FUNC,
    EXTERN,
    INTERFACE,
    ASSET,
    INPUT,

    // Keywords — field modifiers
    LET,
    VAR,
    PERSIST,
    SYNC,
    PUB,

    // Keywords — control / actions
    ON,
    EMIT,
    IF,
    ELSE,
    MATCH,
    RETURN,
    SPAWN,
    DESTROY,
    LOAD,
    UNLOAD,
    ENABLE,
    DISABLE,
    FIXED_TICK,
    LATE_TICK,

    // Keywords — blocks
    APPLY,
    CONFIG,
    CHILD,
    FILTER,
    EXCLUDE,
    AFTER,
    TARGET,
    DISABLED,

    // Keywords — functional
    MAP,
    REDUCE,

    // Keywords — literals / logic
    TRUE_LIT,
    FALSE_LIT,
    AS,
    TO,
    AND,
    OR,
    NOT,

    // Identifiers and literals
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    HEX_COLOR,

    // Punctuation
    COLON,       // :
    COMMA,       // ,
    DOT,         // .
    ARROW,       // ->
    FAT_ARROW,   // =>
    LPAREN,      // (
    RPAREN,      // )
    LBRACKET,    // [
    RBRACKET,    // ]
    LBRACE,      // {
    RBRACE,      // }

    // Arithmetic operators
    PLUS,        // +
    MINUS,       // -
    STAR,        // *
    SLASH,       // /
    PERCENT,     // %

    // Bitwise operators
    AMPERSAND,   // &
    PIPE,        // |
    CARET,       // ^
    TILDE,       // ~

    // Comparison operators
    EQUALS,      // ==
    NOT_EQUALS,  // !=
    LESS,        // <
    GREATER,     // >
    LESS_EQ,     // <=
    GREATER_EQ,  // >=

    // Assignment operators
    ASSIGN,       // =
    PLUS_ASSIGN,  // +=
    MINUS_ASSIGN, // -=

    // Indentation tokens
    INDENT,
    DEDENT,
    NEWLINE,

    // End of file
    EOF_TOKEN
};

struct Token {
    TokenType type = TokenType::EOF_TOKEN;
    std::string value;
    SourceLocation location;

    Token() = default;
    Token(TokenType t, std::string val, SourceLocation loc)
        : type(t), value(std::move(val)), location(std::move(loc)) {}
};

// Utility: convert TokenType to string for debugging
inline const char* token_type_to_string(TokenType t) {
    switch (t) {
        case TokenType::MODULE: return "MODULE";
        case TokenType::USE: return "USE";
        case TokenType::CONST: return "CONST";
        case TokenType::STRUCT: return "STRUCT";
        case TokenType::ENUM: return "ENUM";
        case TokenType::TRAIT: return "TRAIT";
        case TokenType::UNIT: return "UNIT";
        case TokenType::TEMPLATE: return "TEMPLATE";
        case TokenType::SYSTEM: return "SYSTEM";
        case TokenType::VIEW: return "VIEW";
        case TokenType::EVENT: return "EVENT";
        case TokenType::FUNC: return "FUNC";
        case TokenType::EXTERN: return "EXTERN";
        case TokenType::INTERFACE: return "INTERFACE";
        case TokenType::ASSET: return "ASSET";
        case TokenType::INPUT: return "INPUT";
        case TokenType::LET: return "LET";
        case TokenType::VAR: return "VAR";
        case TokenType::PERSIST: return "PERSIST";
        case TokenType::SYNC: return "SYNC";
        case TokenType::PUB: return "PUB";
        case TokenType::ON: return "ON";
        case TokenType::EMIT: return "EMIT";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::MATCH: return "MATCH";
        case TokenType::RETURN: return "RETURN";
        case TokenType::SPAWN: return "SPAWN";
        case TokenType::DESTROY: return "DESTROY";
        case TokenType::LOAD: return "LOAD";
        case TokenType::UNLOAD: return "UNLOAD";
        case TokenType::ENABLE: return "ENABLE";
        case TokenType::DISABLE: return "DISABLE";
        case TokenType::FIXED_TICK: return "FIXED_TICK";
        case TokenType::LATE_TICK: return "LATE_TICK";
        case TokenType::APPLY: return "APPLY";
        case TokenType::CONFIG: return "CONFIG";
        case TokenType::CHILD: return "CHILD";
        case TokenType::FILTER: return "FILTER";
        case TokenType::EXCLUDE: return "EXCLUDE";
        case TokenType::AFTER: return "AFTER";
        case TokenType::TARGET: return "TARGET";
        case TokenType::DISABLED: return "DISABLED";
        case TokenType::MAP: return "MAP";
        case TokenType::REDUCE: return "REDUCE";
        case TokenType::TRUE_LIT: return "TRUE";
        case TokenType::FALSE_LIT: return "FALSE";
        case TokenType::AS: return "AS";
        case TokenType::TO: return "TO";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INT_LITERAL: return "INT_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::HEX_COLOR: return "HEX_COLOR";
        case TokenType::COLON: return "COLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::DOT: return "DOT";
        case TokenType::ARROW: return "ARROW";
        case TokenType::FAT_ARROW: return "FAT_ARROW";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::PIPE: return "PIPE";
        case TokenType::CARET: return "CARET";
        case TokenType::TILDE: return "TILDE";
        case TokenType::EQUALS: return "EQUALS";
        case TokenType::NOT_EQUALS: return "NOT_EQUALS";
        case TokenType::LESS: return "LESS";
        case TokenType::GREATER: return "GREATER";
        case TokenType::LESS_EQ: return "LESS_EQ";
        case TokenType::GREATER_EQ: return "GREATER_EQ";
        case TokenType::ASSIGN: return "ASSIGN";
        case TokenType::PLUS_ASSIGN: return "PLUS_ASSIGN";
        case TokenType::MINUS_ASSIGN: return "MINUS_ASSIGN";
        case TokenType::INDENT: return "INDENT";
        case TokenType::DEDENT: return "DEDENT";
        case TokenType::NEWLINE: return "NEWLINE";
        case TokenType::EOF_TOKEN: return "EOF";
        default: return "UNKNOWN";
    }
}

}  // namespace cactus
