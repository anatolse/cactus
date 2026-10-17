#pragma once

#include <string_view>
#include <vector>

namespace cactus {

// One trait field a stdlib native reads; `trait` is local to `module`.
struct NativeFieldRead {
    std::string_view module;
    std::string_view trait;
    std::string_view field;

    friend bool operator==(const NativeFieldRead&, const NativeFieldRead&) = default;
};

// Whether the stdlib extern functions of `module` observe the `world` effect domain.
[[nodiscard]] bool stdlib_module_observes_world(std::string_view module);

// The trait fields a stdlib extern function reads natively, by canonical id;
// null when no summary is declared.
[[nodiscard]] const std::vector<NativeFieldRead>* native_field_reads(std::string_view canonical);

}  // namespace cactus
