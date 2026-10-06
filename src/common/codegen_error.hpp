#pragma once

#include "common/source_location.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace cactus {

// A backend met a construct it cannot lower; the CLI reports it like any other diagnostic.
class CodegenError : public std::runtime_error {
public:
    CodegenError(SourceLocation location, const std::string& message)
        : std::runtime_error(message)
        , location_(std::move(location)) {}

    [[nodiscard]] const SourceLocation& location() const {
        return location_;
    }

private:
    SourceLocation location_;
};

}  // namespace cactus
