#pragma once

#include <string>
#include <unordered_set>

namespace cactus {

// The GLSL text of the named helpers and of the helpers they call, each once, callees first.
// Each gives the result `cactus_runtime` gives where a GLSL built-in differs from it.
[[nodiscard]] std::string glsl_helper_definitions(const std::unordered_set<std::string>& used);

}  // namespace cactus
