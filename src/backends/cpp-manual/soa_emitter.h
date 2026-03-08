#pragma once

#include "frontend/semantic_analyzer.h"

#include <sstream>
#include <string>

namespace cactus {

class SoaEmitter {
public:
    // Generate SoA storage class for a trait
    static std::string emit_soa_storage(const ResolvedTrait& trait);

    // Generate POD struct for a Cactus struct
    static std::string emit_pod_struct(const ResolvedStruct& s);

    // Generate enum
    static std::string emit_enum(const ResolvedEnum& e);

    // Map Cactus type to C++ type string
    static std::string type_to_cpp(const TypeInfo& type);
};

}  // namespace cactus
