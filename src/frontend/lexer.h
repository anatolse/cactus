#pragma once

#include "common/error_reporter.h"
#include "frontend/token.h"

#include <string>
#include <vector>

namespace cactus {

class Lexer {
public:
    Lexer(std::string source, std::string filename, ErrorReporter& errors);

    std::vector<Token> tokenize();

private:
    // Character navigation
    [[nodiscard]] char peek_char() const;
    [[nodiscard]] char peek_next() const;
    char advance_char();
    [[nodiscard]] bool is_at_end() const;
    [[nodiscard]] SourceLocation current_location() const;

    // Token readers
    Token read_identifier();
    Token read_number();
    Token read_string();
    Token read_hex_color();
    void skip_comment();

    // Indentation
    void process_line_start(std::vector<Token>& tokens);

    std::string source_;
    std::string filename_;
    ErrorReporter& errors_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<int> indent_stack_{0};
    bool at_line_start_ = true;
};

}  // namespace cactus
