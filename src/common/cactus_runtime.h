#pragma once

#include <string_view>

namespace cactus::runtime {

struct GeneratedProjectInfo {
    std::string_view backend;
    std::string_view project_name;
};

[[nodiscard]] constexpr std::string_view common_runtime_name() noexcept {
    return "cactus-runtime-common";
}

[[nodiscard]] constexpr GeneratedProjectInfo make_project_info(std::string_view backend,
                                                               std::string_view project_name) noexcept {
    return GeneratedProjectInfo{backend, project_name};
}

}  // namespace cactus::runtime