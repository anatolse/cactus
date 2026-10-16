#include "frontend/native_result_ordering.hpp"

#include <array>
#include <functional>
#include <map>
#include <string>

namespace cactus {

const NativeOrder* native_result_order(std::string_view canonical) {
    using enum NativeOrderKind;
    static const std::map<std::string, NativeOrder, std::less<>> table{
        {"std.query.first", {.kind = Creation}},
        {"std.query.all", {.kind = Creation}},
        {"std.query.children", {.kind = Creation}},
        {"std.query.parent", {.kind = Creation}},
        {"std.query.hierarchy_preorder", {.kind = Hierarchy}},
        {"std.query.hierarchy_postorder", {.kind = Hierarchy}},
        {"std.physics.flat.query.nearest", {.kind = Keyed, .name = "distance"}},
        {"std.physics.flat.query.raycast", {.kind = Keyed, .name = "ray distance"}},
        {"std.physics.flat.query.overlap_box", {.kind = Creation}},
        {"std.physics.flat.query.overlap_circle", {.kind = Creation}},
        {"std.physics.volume.query.nearest", {.kind = Keyed, .name = "distance"}},
        {"std.physics.volume.query.raycast", {.kind = Keyed, .name = "ray distance"}},
        {"std.physics.volume.query.overlap_box", {.kind = Creation}},
        {"std.physics.volume.query.overlap_sphere", {.kind = Creation}},
        {"std.physics.flat.query_cast_nearest", {.kind = Keyed, .name = "cast distance"}},
        {"std.physics.flat.query_overlap_deepest", {.kind = Keyed, .name = "overlap depth, deepest first"}},
        {"std.physics.flat.query_overlap_all", {.kind = Creation}},
        {"std.physics.volume.sweep", {.kind = Creation}},
        {"std.pointer.top_target",
         {.kind = Keyed, .name = "window draw order, then flat priority or volume ray distance"}},
        {"std.editor.hit_test_2d", {.kind = Creation}},
        {"std.editor.raycast_3d", {.kind = Keyed, .name = "ray distance"}},
        {"std.editor.spawn_template", {.kind = Creation}},
        {"std.editor.camera_enter", {.kind = Creation}},
        {"std.editor.palette_label_slot", {.kind = Creation}},
        {"std.editor.template_names", {.kind = Sequence, .name = "pub template declaration order"}},
        {"std.ui.RoutePointer", {.kind = Sequence, .name = "leave, enter, press, release, click"}},
        {"std.render.models.ModelAnimation", {.kind = Creation}},
        {"std.persistence.SaveRequested", {.kind = Sequence, .name = "arrival"}},
        {"std.persistence.RestoreRequested", {.kind = Sequence, .name = "arrival"}},
    };
    const auto found = table.find(canonical);
    return found == table.end() ? nullptr : &found->second;
}

std::span<const NativeConsumer> native_consumers() {
    static constexpr std::array consumers{
        NativeConsumer{.event = "std.persistence.SaveRequested", .effect = "persistence"},
        NativeConsumer{.event = "std.persistence.RestoreRequested", .effect = "persistence"},
    };
    return consumers;
}

}  // namespace cactus
