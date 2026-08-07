#include "frontend/lexer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace cactus {

namespace {

const std::unordered_map<std::string, TokenType>& keyword_map() {
    static const std::unordered_map<std::string, TokenType> KW = {
        {"module", TokenType::MODULE},
        {"use", TokenType::USE},
        {"const", TokenType::CONST},
        {"struct", TokenType::STRUCT},
        {"enum", TokenType::ENUM},
        {"trait", TokenType::TRAIT},
        {"entity", TokenType::ENTITY},
        {"rule", TokenType::RULE},
        {"view", TokenType::VIEW},
        {"event", TokenType::EVENT},
        {"func", TokenType::FUNC},
        {"extern", TokenType::EXTERN},
        {"let", TokenType::LET},
        {"var", TokenType::VAR},
        {"persist", TokenType::PERSIST},
        {"sync", TokenType::SYNC},
        {"pub", TokenType::PUB},
        {"on", TokenType::ON},
        {"emit", TokenType::EMIT},
        {"if", TokenType::IF},
        {"else", TokenType::ELSE},
        {"match", TokenType::MATCH},
        {"for", TokenType::FOR},
        {"in", TokenType::IN},
        {"return", TokenType::RETURN},
        {"self", TokenType::SELF},
        {"filter", TokenType::FILTER},
        {"exclude", TokenType::EXCLUDE},
        {"after", TokenType::AFTER},
        {"target", TokenType::TARGET},
        {"template", TokenType::TEMPLATE},
        {"spawn", TokenType::SPAWN},
        {"destroy", TokenType::DESTROY},
        {"load", TokenType::LOAD},
        {"unload", TokenType::UNLOAD},
        {"add", TokenType::ADD},
        {"remove", TokenType::REMOVE},
        {"project", TokenType::PROJECT},
        {"asset", TokenType::ASSET},
        {"input", TokenType::INPUT},
        {"phase", TokenType::PHASE},
        {"fixed_tick", TokenType::FIXED_TICK},
        {"late_tick", TokenType::LATE_TICK},
        {"map", TokenType::MAP},
        {"reduce", TokenType::REDUCE},
        {"true", TokenType::TRUE_LIT},
        {"false", TokenType::FALSE_LIT},
        {"as", TokenType::AS},
        {"to", TokenType::TO},
        {"from", TokenType::FROM},
        {"and", TokenType::AND},
        {"or", TokenType::OR},
        {"not", TokenType::NOT},
    };
    return KW;
}

bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}  // namespace

Lexer::Lexer(std::string source, std::string filename, ErrorReporter& errors)
    : source_(std::move(source))
    , filename_(std::move(filename))
    , errors_(errors) {}

char Lexer::peek_char() const {
    if (is_at_end()) {
        return '\0';
    }
    return source_[pos_];
}

char Lexer::peek_next() const {
    if (pos_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + 1];
}

char Lexer::advance_char() {
    char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

bool Lexer::is_at_end() const {
    return pos_ >= source_.size();
}

SourceLocation Lexer::current_location() const {
    return {filename_, line_, col_};
}

Token Lexer::read_identifier() {
    auto loc = current_location();
    std::string value;
    while (!is_at_end() && ((std::isalnum(peek_char()) != 0) || peek_char() == '_')) {
        value += advance_char();
    }
    const auto& kw = keyword_map();
    auto it        = kw.find(value);
    if (it != kw.end()) {
        return {it->second, value, loc};
    }
    return {TokenType::IDENTIFIER, value, loc};
}

Token Lexer::read_number() {
    auto loc = current_location();
    std::string value;
    while (!is_at_end() && (std::isdigit(peek_char()) != 0)) {
        value += advance_char();
    }
    if (!is_at_end() && peek_char() == '.' && (std::isdigit(peek_next()) != 0)) {
        value += advance_char();  // consume '.'
        while (!is_at_end() && (std::isdigit(peek_char()) != 0)) {
            value += advance_char();
        }
        return {TokenType::FLOAT_LITERAL, value, loc};
    }
    return {TokenType::INT_LITERAL, value, loc};
}

Token Lexer::read_string() {
    auto loc = current_location();
    advance_char();  // consume opening '"'
    std::string value;
    while (!is_at_end() && peek_char() != '"' && peek_char() != '\n') {
        value += advance_char();
    }
    if (is_at_end() || peek_char() == '\n') {
        errors_.error(loc, "unterminated string literal");
    } else {
        advance_char();  // consume closing '"'
    }
    return {TokenType::STRING_LITERAL, value, loc};
}

Token Lexer::read_hex_color() {
    auto loc = current_location();
    advance_char();  // consume '#'
    std::string value;
    while (!is_at_end() && is_hex_digit(peek_char())) {
        value += advance_char();
    }
    if (value.size() != 6 && value.size() != 8) {
        errors_.error(loc, "hex color must be 6 (RGB) or 8 (RGBA) hex digits, got " + std::to_string(value.size()));
    }
    return {TokenType::HEX_COLOR, value, loc};
}

void Lexer::skip_comment() {
    while (!is_at_end() && peek_char() != '\n') {
        advance_char();
    }
}

void Lexer::process_line_start(std::vector<Token>& tokens) {
    int spaces = 0;
    while (!is_at_end() && peek_char() == ' ') {
        advance_char();
        ++spaces;
    }

    // Check for tab
    if (!is_at_end() && peek_char() == '\t') {
        errors_.error(current_location(), "tabs are not allowed for indentation, use spaces");
        return;
    }

    // Skip blank lines and comment-only lines
    if (is_at_end() || peek_char() == '\n' || peek_char() == '#') {
        return;
    }

    int current_indent = indent_stack_.back();

    if (spaces > current_indent) {
        indent_stack_.push_back(spaces);
        tokens.emplace_back(TokenType::INDENT, "", current_location());
    } else if (spaces < current_indent) {
        while (indent_stack_.back() > spaces) {
            indent_stack_.pop_back();
            tokens.emplace_back(TokenType::DEDENT, "", current_location());
        }
        if (indent_stack_.back() != spaces) {
            errors_.error(current_location(), "inconsistent indentation");
        }
    }
}

std::vector<Token> Lexer::tokenize() {  // NOLINT(readability-function-cognitive-complexity) -- still 43 after table-driving the two-char/single-char operator dispatch; not further in scope here
    std::vector<Token> tokens;

    while (!is_at_end()) {
        // Handle line start (indentation)
        if (at_line_start_) {
            at_line_start_ = false;
            process_line_start(tokens);
            if (is_at_end()) {
                break;
            }
        }

        char c = peek_char();

        // Skip spaces (not at line start)
        if (c == ' ') {
            advance_char();
            continue;
        }

        // Newline
        if (c == '\n') {
            // Only emit NEWLINE if last token is not already NEWLINE/INDENT/DEDENT
            if (!tokens.empty()) {
                auto last = tokens.back().type;
                if (last != TokenType::NEWLINE && last != TokenType::INDENT && last != TokenType::DEDENT) {
                    tokens.emplace_back(TokenType::NEWLINE, "\\n", current_location());
                }
            }
            advance_char();
            at_line_start_ = true;
            continue;
        }

        // Tab rejection
        if (c == '\t') {
            errors_.error(current_location(), "tabs are not allowed, use spaces");
            advance_char();
            continue;
        }

        // Comment or hex color
        if (c == '#') {
            // Check if this is a hex color: # followed by hex digits
            if (!is_at_end() && is_hex_digit(peek_next())) {
                tokens.push_back(read_hex_color());
            } else {
                skip_comment();
            }
            continue;
        }

        // String literal
        if (c == '"') {
            tokens.push_back(read_string());
            continue;
        }

        // Number literal
        if (std::isdigit(c) != 0) {
            tokens.push_back(read_number());
            continue;
        }

        // Identifier or keyword
        if ((std::isalpha(c) != 0) || c == '_') {
            tokens.push_back(read_identifier());
            continue;
        }

        // Two-character operators
        auto loc = current_location();
        struct TwoCharOperator {
            char first;
            char second;
            TokenType type;
            const char* spelling;
        };
        static constexpr std::array<TwoCharOperator, 8> kTwoCharOperators{{
            {.first = '-', .second = '>', .type = TokenType::ARROW, .spelling = "->"},
            {.first = '=', .second = '>', .type = TokenType::FAT_ARROW, .spelling = "=>"},
            {.first = '=', .second = '=', .type = TokenType::EQUALS, .spelling = "=="},
            {.first = '!', .second = '=', .type = TokenType::NOT_EQUALS, .spelling = "!="},
            {.first = '<', .second = '=', .type = TokenType::LESS_EQ, .spelling = "<="},
            {.first = '>', .second = '=', .type = TokenType::GREATER_EQ, .spelling = ">="},
            {.first = '+', .second = '=', .type = TokenType::PLUS_ASSIGN, .spelling = "+="},
            {.first = '-', .second = '=', .type = TokenType::MINUS_ASSIGN, .spelling = "-="},
        }};
        const auto two_char_op = std::ranges::find_if(
            kTwoCharOperators, [&](const auto& op) { return op.first == c && op.second == peek_next(); });
        if (two_char_op != kTwoCharOperators.end()) {
            advance_char();
            advance_char();
            tokens.emplace_back(two_char_op->type, two_char_op->spelling, loc);
            continue;
        }

        // Single-character operators and punctuation
        static const std::unordered_map<char, TokenType> kSingleCharTokens{
            {':', TokenType::COLON},     {',', TokenType::COMMA},    {'.', TokenType::DOT},
            {'(', TokenType::LPAREN},    {')', TokenType::RPAREN},   {'[', TokenType::LBRACKET},
            {']', TokenType::RBRACKET},  {'{', TokenType::LBRACE},   {'}', TokenType::RBRACE},
            {'+', TokenType::PLUS},      {'-', TokenType::MINUS},    {'*', TokenType::STAR},
            {'/', TokenType::SLASH},     {'%', TokenType::PERCENT},  {'&', TokenType::AMPERSAND},
            {'|', TokenType::PIPE},      {'^', TokenType::CARET},    {'~', TokenType::TILDE},
            {'<', TokenType::LESS},      {'>', TokenType::GREATER},  {'=', TokenType::ASSIGN},
        };
        advance_char();
        if (const auto single_char_op = kSingleCharTokens.find(c); single_char_op != kSingleCharTokens.end()) {
            tokens.emplace_back(single_char_op->second, std::string(1, c), loc);
        } else {
            errors_.error(loc, std::string("unexpected character '") + c + "'");
        }
    }

    // Emit remaining DEDENTs
    while (indent_stack_.size() > 1) {
        indent_stack_.pop_back();
        tokens.emplace_back(TokenType::DEDENT, "", current_location());
    }

    // Ensure final NEWLINE
    if (!tokens.empty() && tokens.back().type != TokenType::NEWLINE) {
        tokens.emplace_back(TokenType::NEWLINE, "\\n", current_location());
    }

    tokens.emplace_back(TokenType::EOF_TOKEN, "", current_location());
    return tokens;
}

}  // namespace cactus
