#pragma once

#include "common/cactus_runtime.h"

namespace cactus::runtime::entt_backend {

struct RuntimeBinding {
    GeneratedProjectInfo project;
};

[[nodiscard]] RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept;

}  // namespace cactus::runtime::entt_backend