#include "common/error_reporter.h"

#include <iostream>

namespace cactus {

void ErrorReporter::error(const SourceLocation& loc, const std::string& msg) {
    diagnostics_.push_back({DiagnosticLevel::Error, loc, msg});
    ++error_count_;
    std::cerr << loc.filename << ":" << loc.line << ":" << loc.column << ": error: " << msg << "\n";
}

void ErrorReporter::warning(const SourceLocation& loc, const std::string& msg) {
    diagnostics_.push_back({DiagnosticLevel::Warning, loc, msg});
    ++warning_count_;
    std::cerr << loc.filename << ":" << loc.line << ":" << loc.column << ": warning: " << msg << "\n";
}

void ErrorReporter::print_summary() const {
    if (error_count_ > 0 || warning_count_ > 0) {
        std::cerr << error_count_ << " error(s), " << warning_count_ << " warning(s)\n";
    }
}

}  // namespace cactus
