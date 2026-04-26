#pragma once

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
};

}  // namespace cactus
