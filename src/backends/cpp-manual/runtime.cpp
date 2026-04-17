#include "backends/cpp-manual/runtime.h"

namespace cactus::runtime::manual_backend {

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

}  // namespace cactus::runtime::manual_backend