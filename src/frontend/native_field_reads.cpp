#include "frontend/native_field_reads.hpp"

#include <functional>
#include <map>
#include <string>

namespace cactus {

bool stdlib_module_observes_world(std::string_view module) {
    return module == "std.query" || module == "std.transform" || module.starts_with("std.transform.");
}

const std::vector<NativeFieldRead>* native_field_reads(std::string_view canonical) {
    // std.query filters test membership only, and Parent.parent is `let`: both change only at commit.
    static const std::map<std::string, std::vector<NativeFieldRead>, std::less<>> table{
        {"std.query.exists", {}},
        {"std.query.count", {}},
        {"std.query.first", {}},
        {"std.query.all", {}},
        {"std.query.parent", {}},
        {"std.query.children", {}},
        {"std.query.hierarchy_preorder", {}},
        {"std.query.hierarchy_postorder", {}},
        {"std.transform.flat.world_position",
         {{.module = "std.transform.flat", .trait = "WorldTransform", .field = "position"}}},
        {"std.transform.volume.world_position",
         {{.module = "std.transform.volume", .trait = "WorldTransform", .field = "position"}}},
    };
    const auto found = table.find(canonical);
    return found == table.end() ? nullptr : &found->second;
}

std::optional<bool> native_is_world_scan(std::string_view canonical) {
    static const std::map<std::string, bool, std::less<>> table{
        {"std.query.exists", false},
        {"std.query.parent", false},
        {"std.query.children", false},
        {"std.query.count", true},
        {"std.query.first", true},
        {"std.query.all", true},
        {"std.query.hierarchy_preorder", true},
        {"std.query.hierarchy_postorder", true},
        {"std.physics.flat.query.nearest", true},
        {"std.physics.flat.query.overlap_box", true},
        {"std.physics.flat.query.overlap_circle", true},
        {"std.physics.flat.query.raycast", true},
        {"std.physics.volume.query.nearest", true},
        {"std.physics.volume.query.overlap_box", true},
        {"std.physics.volume.query.overlap_sphere", true},
        {"std.physics.volume.query.raycast", true},
        {"std.physics.flat.query_cast_nearest", true},
        {"std.physics.flat.query_overlap_deepest", true},
        {"std.physics.flat.query_overlap_all", true},
    };
    const auto found = table.find(canonical);
    return found == table.end() ? std::nullopt : std::optional<bool>{found->second};
}

}  // namespace cactus
