// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <sstream>

using namespace cactus;

namespace {

class ScopedStderrCapture {
public:
    ScopedStderrCapture()
        : previous_(std::cerr.rdbuf(captured_.rdbuf())) {}

    ScopedStderrCapture(const ScopedStderrCapture&)            = delete;
    ScopedStderrCapture& operator=(const ScopedStderrCapture&) = delete;
    ScopedStderrCapture(ScopedStderrCapture&&)                 = delete;
    ScopedStderrCapture& operator=(ScopedStderrCapture&&)      = delete;

    ~ScopedStderrCapture() {
        std::cerr.rdbuf(previous_);
    }

    [[nodiscard]] std::string text() const {
        return captured_.str();
    }

private:
    std::ostringstream captured_;
    std::streambuf* previous_;
};

}  // namespace

TEST_CASE("ErrorReporter: repeated error is recorded once", "[error_reporter]") {
    ErrorReporter reporter;
    const SourceLocation loc{"a.cactus", 3, 5};
    reporter.error(loc, "bad thing");
    reporter.error(loc, "bad thing");
    REQUIRE(reporter.diagnostics().size() == 1);
    REQUIRE(reporter.error_count() == 1);
}

TEST_CASE("ErrorReporter: same error message at two locations is kept twice", "[error_reporter]") {
    ErrorReporter reporter;
    reporter.error({"a.cactus", 3, 5}, "bad thing");
    reporter.error({"a.cactus", 4, 5}, "bad thing");
    REQUIRE(reporter.diagnostics().size() == 2);
    REQUIRE(reporter.error_count() == 2);
}

TEST_CASE("ErrorReporter: different error messages at one location are kept", "[error_reporter]") {
    ErrorReporter reporter;
    const SourceLocation loc{"a.cactus", 3, 5};
    reporter.error(loc, "bad thing");
    reporter.error(loc, "other thing");
    REQUIRE(reporter.diagnostics().size() == 2);
    REQUIRE(reporter.error_count() == 2);
}

TEST_CASE("ErrorReporter: repeated warning is recorded once", "[error_reporter]") {
    ErrorReporter reporter;
    const SourceLocation loc{"a.cactus", 3, 5};
    reporter.warning(loc, "odd thing");
    reporter.warning(loc, "odd thing");
    reporter.warning({"a.cactus", 9, 1}, "odd thing");
    reporter.warning(loc, "another odd thing");
    REQUIRE(reporter.diagnostics().size() == 3);
    REQUIRE(reporter.warning_count() == 3);
}

TEST_CASE("ErrorReporter: error and warning with the same text are distinct", "[error_reporter]") {
    ErrorReporter reporter;
    const SourceLocation loc{"a.cactus", 3, 5};
    reporter.error(loc, "thing");
    reporter.warning(loc, "thing");
    REQUIRE(reporter.diagnostics().size() == 2);
    REQUIRE(reporter.error_count() == 1);
    REQUIRE(reporter.warning_count() == 1);
}

TEST_CASE("ErrorReporter: recording writes nothing to stderr", "[error_reporter]") {
    const ScopedStderrCapture capture;
    ErrorReporter reporter;
    reporter.error({"a.cactus", 1, 1}, "bad thing");
    reporter.warning({"a.cactus", 2, 1}, "odd thing");
    REQUIRE(capture.text().empty());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
