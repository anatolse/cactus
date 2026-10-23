#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace cactus_test {

// Runs `executable` with each argument double-quoted, robust to spaces in any
// path. cmd.exe /c strips the first and last quote of a line with more than
// two quote characters, so an extra outer pair is sacrificed to that rule.
inline int run_quoted(const std::string& executable, const std::vector<std::string>& arguments) {
    std::string inner = "\"" + executable + "\"";
    for (const auto& argument : arguments) {
        inner += " \"" + argument + "\"";
    }
    const std::string command = "\"" + inner + "\"";
    return std::system(command.c_str());  // NOLINT(cert-env33-c,concurrency-mt-unsafe)
}

}  // namespace cactus_test
