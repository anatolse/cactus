#pragma once

#include <ostream>
#include <string>

namespace cactus {

struct SourceLocation {
    std::string filename;
    int line   = 0;
    int column = 0;

    SourceLocation() = default;
    SourceLocation(std::string fname, int ln, int col)
        : filename(std::move(fname))
        , line(ln)
        , column(col) {}

    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;

    friend std::ostream& operator<<(std::ostream& out, const SourceLocation& location) {
        return out << location.filename << ':' << location.line << ':' << location.column;
    }
};

}  // namespace cactus
