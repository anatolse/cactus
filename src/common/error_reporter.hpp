#pragma once

#include "common/source_location.hpp"

#include <cstddef>
#include <ranges>
#include <string>
#include <vector>

namespace cactus {

// Joins the spelled items as "a", "a and b" or "a, b and c".
template <std::ranges::sized_range Range, typename Spell>
[[nodiscard]] std::string spelled_list(const Range& items, Spell spell) {
    std::string text;
    const auto count     = std::ranges::size(items);
    std::size_t position = 0;
    for (const auto& item : items) {
        if (position > 0) {
            text += position + 1 == count ? " and " : ", ";
        }
        text += spell(item);
        ++position;
    }
    return text;
}

enum class DiagnosticLevel { Error, Warning, Info };

struct Diagnostic {
    DiagnosticLevel level;
    SourceLocation location;
    std::string message;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

class ErrorReporter {
public:
    void error(const SourceLocation& loc, const std::string& msg);
    void warning(const SourceLocation& loc, const std::string& msg);
    // Facts about the generated code; counted neither as errors nor warnings.
    void info(const SourceLocation& loc, const std::string& msg);

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
    bool record(DiagnosticLevel level, const SourceLocation& loc, const std::string& msg);

    std::vector<Diagnostic> diagnostics_;
    int error_count_   = 0;
    int warning_count_ = 0;
};

}  // namespace cactus
