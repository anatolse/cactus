#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace cactus {

// Objects a rule body calls members of to reach host state, as in
// `input.pressed(Jump)`; the cpp-entt backend lowers each such call to a
// runtime function that reads input devices or editor state.
inline constexpr std::array<std::string_view, 4> kHostFacadeObjects{"input", "camera2d", "transform2d", "transform3d"};

[[nodiscard]] inline bool is_host_facade(std::string_view object) {
    return std::ranges::contains(kHostFacadeObjects, object);
}

}  // namespace cactus
