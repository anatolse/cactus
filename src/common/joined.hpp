#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cactus {

[[nodiscard]] inline std::string joined(const std::vector<std::string>& parts, std::string_view separator) {
    std::string text;
    for (const auto& part : parts) {
        if (!text.empty()) {
            text += separator;
        }
        text += part;
    }
    return text;
}

}  // namespace cactus
