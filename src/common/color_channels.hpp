#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace cactus {

// A hex color literal's byte as the float channel every path (rules, data files, GLSL) uses.
[[nodiscard]] constexpr float color_channel_from_byte(int byte) noexcept {
    return static_cast<float>(byte) / 255.0F;
}

[[nodiscard]] constexpr int hex_digit_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

// A hex color literal's digits (`RRGGBB` or `RRGGBBAA`, no `#`) as four bytes; six digits mean opaque.
[[nodiscard]] constexpr std::array<int, 4> hex_color_bytes(std::string_view digits) noexcept {
    std::array<int, 4> bytes{0, 0, 0, 255};
    for (std::size_t i = 0; i < bytes.size() && (i * 2) + 1 < digits.size(); ++i) {
        bytes[i] = (hex_digit_value(digits[i * 2]) * 16) + hex_digit_value(digits[(i * 2) + 1]);
    }
    return bytes;
}

}  // namespace cactus
