#include "backends/cpp-entt/runtime.h"

namespace cactus::runtime::entt_backend {

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

}  // namespace cactus::runtime::entt_backend