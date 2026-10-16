#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace cactus {

// How a native function, emitter or consumer orders what it hands back.
enum class NativeOrderKind : std::uint8_t {
    Creation,   // stable creation order; also a result with no choice to make
    Hierarchy,  // std.query hierarchy snapshot order
    Keyed,      // `name` first, exact ties by stable creation order
    Sequence,   // the fixed sequence `name` describes
};

struct NativeOrder {
    NativeOrderKind kind = NativeOrderKind::Creation;
    std::string_view name;
};

// The declared order of a stdlib extern function, native emitter (by extern
// rule) or native consumer (by event), by canonical id; null when undeclared.
[[nodiscard]] const NativeOrder* native_result_order(std::string_view canonical);

// A stdlib event the runtime consumes itself, in arrival order, acting on `effect`.
struct NativeConsumer {
    std::string_view event;
    std::string_view effect;
};

[[nodiscard]] std::span<const NativeConsumer> native_consumers();

}  // namespace cactus
