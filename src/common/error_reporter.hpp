#pragma once

#include "common/source_location.hpp"

#include <string>
#include <vector>

namespace cactus {

enum class DiagnosticLevel { Error, Warning };

struct Diagnostic {
    DiagnosticLevel level;
    SourceLocation location;
    std::string message;
};

class ErrorReporter {
public:
    void error(const SourceLocation& loc, const std::string& msg);
    void warning(const SourceLocation& loc, const std::string& msg);

    [[nodiscard]] bool has_errors() const {
        return error_count_ > 0;
    }
    [[nodiscard]] int error_count() const {
        return error_count_;
    }
    [[nodiscard]] int warning_count() const {
        return warning_count_;
    }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const {
        return diagnostics_;
    }

    void print_summary() const;

private:
    std::vector<Diagnostic> diagnostics_;
    int error_count_   = 0;
    int warning_count_ = 0;
};

}  // namespace cactus
