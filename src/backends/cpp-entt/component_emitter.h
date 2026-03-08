#pragma once

#include "backends/cpp-manual/soa_emitter.h"
#include "frontend/semantic_analyzer.h"

#include <string>

namespace cactus {

class EnttComponentEmitter {
public:
    static std::string emit_component(const ResolvedTrait& trait);
    static std::string emit_pod_struct(const ResolvedStruct& s);
    static std::string emit_enum(const ResolvedEnum& e);
};

}  // namespace cactus
