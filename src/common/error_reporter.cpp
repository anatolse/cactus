#include "common/error_reporter.hpp"

#include <algorithm>
#include <iostream>

namespace cactus {

bool ErrorReporter::record(DiagnosticLevel level, const SourceLocation& loc, const std::string& msg) {
    Diagnostic diagnostic{.level = level, .location = loc, .message = msg};
    if (std::ranges::find(diagnostics_, diagnostic) != diagnostics_.end()) {
        return false;
    }
    diagnostics_.push_back(std::move(diagnostic));
    return true;
}

void ErrorReporter::error(const SourceLocation& loc, const std::string& msg) {
    if (record(DiagnosticLevel::Error, loc, msg)) {
        ++error_count_;
    }
}

void ErrorReporter::warning(const SourceLocation& loc, const std::string& msg) {
    if (record(DiagnosticLevel::Warning, loc, msg)) {
        ++warning_count_;
    }
}

void ErrorReporter::print_summary() const {
    if (error_count_ > 0 || warning_count_ > 0) {
        std::cerr << error_count_ << " error(s), " << warning_count_ << " warning(s)\n";
    }
}

}  // namespace cactus
