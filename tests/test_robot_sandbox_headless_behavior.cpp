// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>

namespace {

namespace backend = cactus::runtime::entt_backend;

using Session   = game__Session;
using Mode      = game__Mode;
using Script    = small_world__Script;
using Cell      = std_voxel_volume__Cell;
using Volume    = std_voxel_volume__Volume;
using Solid     = std_physics_volume__Solid;
using Body      = std_physics_volume__CharacterBody;
using RobotTransform = std_transform_volume__WorldTransform;
using Robot     = robot__Robot;
using Editor    = editing__Editor;

// The small world: x/z 0..15, y -4..7.
constexpr int kCells = 16 * 12 * 16;
constexpr int kMinY  = -4;
constexpr float kDt  = 1.0F / 60.0F;

using CellKey = std::tuple<int, int, int>;

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        cactus_raylib_fake::set_screen_size(1280, 720);
        backend::release_render_resources();
        backend::reset_quit_request();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
        backend::generated_run_boundary_activation(registry, std_core__loadEvent{});
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            backend::generated_inject_external_event(std_core__frameEvent{.dt = kDt});
            backend::generated_drain_external_events(registry);
        }
    }

    Session& session() { return registry.get<Session>(generated_named_slots().game__Game); }
    Script& script() { return registry.get<Script>(generated_named_slots().small_world__Tester); }

    // Frames until the session reaches `mode`, at most `limit`.
    bool run_until(Mode mode, int limit = 120) {
        for (int i = 0; i < limit && session().mode != mode; ++i) {
            frame();
        }
        return session().mode == mode;
    }

    void new_game() {
        script().new_game = true;
        REQUIRE(run_until(Mode::Playing));
    }

    entt::entity robot() {
        const auto view = registry.view<Robot>();
        REQUIRE(view.size() == 1);
        return *view.begin();
    }

    entt::entity cell_at(int x, int y, int z) {
        for (const auto [entity, cell] : registry.view<Cell>().each()) {
            if (cell.x == x && cell.y == y && cell.z == z) {
                return entity;
            }
        }
        return entt::null;
    }

    bool solid(int x, int y, int z) { return registry.all_of<Solid>(cell_at(x, y, z)); }

    std::map<CellKey, std::uint32_t> cells() {
        std::map<CellKey, std::uint32_t> result;
        for (const auto [entity, cell] : registry.view<Cell>().each()) {
            result[{cell.x, cell.y, cell.z}] = registry.all_of<Solid>(entity) ? cell.texture + 1U : 0U;
        }
        return result;
    }

    template <typename Trait>
    std::size_t count() {
        return registry.view<Trait>().size();
    }

    const world__Generation& recipe() { return registry.get<world__Generation>(generated_named_slots().world__Generator); }

    robot__StrategyView& overview() {
        const auto view = registry.view<robot__StrategyView>();
        REQUIRE(view.size() == 1);
        return registry.get<robot__StrategyView>(*view.begin());
    }

    void press(int button) {
        cactus_raylib_fake::set_mouse_button_pressed_this_frame(button, true);
        cactus_raylib_fake::set_mouse_button_down(button, true);
        frame();
        cactus_raylib_fake::set_mouse_button_pressed_this_frame(button, false);
    }

    void release(int button) {
        cactus_raylib_fake::set_mouse_button_down(button, false);
        cactus_raylib_fake::set_mouse_button_released_this_frame(button, true);
        frame();
        cactus_raylib_fake::set_mouse_button_released_this_frame(button, false);
    }

    void tap_key(int key) {
        cactus_raylib_fake::set_key_pressed_this_frame(key, true);
        frame();
        cactus_raylib_fake::set_key_pressed_this_frame(key, false);
    }

    // Clicks the button drawn with this label; `occurrence` picks among
    // buttons sharing a label, in drawing order.
    void click_label(const std::string& label, int occurrence = 0) {
        const auto mark = cactus_raylib_fake::call_log().size();
        frame();
        const auto& log = cactus_raylib_fake::call_log();
        std::optional<Vector2> found;
        for (auto i = mark; i < log.size() && !found.has_value(); ++i) {
            const auto* text = std::get_if<cactus_raylib_fake::RecordedDrawTextEx>(&log[i]);
            if (text != nullptr && text->text == label && occurrence-- == 0) {
                found = text->position;
            }
        }
        REQUIRE(found.has_value());
        cactus_raylib_fake::set_mouse_position(Vector2{.x = found->x + 2.0F, .y = found->y + 2.0F});
        frame();
        press(MOUSE_BUTTON_LEFT);
        release(MOUSE_BUTTON_LEFT);
    }

    // Points the strategy camera so the screen centre lands on cell (x, z)
    // just above layer -1, then waits a frame for the camera to follow.
    void aim_strategy_at(int x, int z) {
        overview().target = Vector3{.x = x + 0.5F, .y = 0.0F, .z = z + 0.5F};
        frame();
    }

    // Edits the inclusive region x0..x1 / z0..z1 of the current layer range.
    void commit_region(int x0, int z0, int x1, int z1) {
        auto& editor   = registry.get<Editor>(robot());
        editor.start_x = x0;
        editor.start_z = z0;
        editor.end_x   = x1;
        editor.end_z   = z1;
        script().commit_region = true;
        frame(2);
    }
};

}  // namespace

// ── Startup and generation ──────────────────────────────────────────────────

TEST_CASE("the sandbox starts on the start menu with no world", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.frame();
    CHECK(world.session().mode == Mode::Start);
    CHECK(world.count<Volume>() == 0);
    bool start_menu_shown = false;
    for (const auto [entity, on_screen, node] : world.registry.view<hud__OnScreen, std_ui__Node>().each()) {
        if (on_screen.screen == hud__Screen::StartMenu) {
            start_menu_shown = node.visible;
        }
        if (on_screen.screen == hud__Screen::PauseMenu) {
            CHECK_FALSE(node.visible);
        }
    }
    CHECK(start_menu_shown);
    for (const auto [entity, viewport] : world.registry.view<hud__MenuView, std_camera_viewport__Viewport>().each()) {
        CHECK(viewport.active);
    }
}

TEST_CASE("a new game generates the whole world before play and drops the robot into the spawn pocket",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.script().new_game = true;
    world.frame();
    CHECK(world.session().mode == Mode::Generating);
    REQUIRE(world.run_until(Mode::Playing));
    CHECK(world.count<Cell>() == static_cast<std::size_t>(kCells));
    CHECK(world.count<robot__FirstPersonView>() == 1);
    CHECK(world.count<robot__StrategyView>() == 1);
    CHECK(world.count<sky__SkyBody>() == 4);

    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            const auto bottom = world.cell_at(x, kMinY, z);
            CHECK(world.registry.all_of<Solid, world__Bedrock>(bottom));
        }
    }
    const auto& recipe = world.registry.get<world__Generation>(generated_named_slots().world__Generator);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            CHECK(world.solid(recipe.spawn_x + dx, recipe.spawn_floor, recipe.spawn_z + dz));
            for (int dy = 1; dy <= 3; ++dy) {
                CHECK_FALSE(world.solid(recipe.spawn_x + dx, recipe.spawn_floor + dy, recipe.spawn_z + dz));
            }
        }
    }
    world.frame(90);
    const auto robot = world.robot();
    CHECK(world.registry.get<Body>(robot).grounded);
    const auto& place = world.registry.get<RobotTransform>(robot);
    CHECK(place.position.x == Catch::Approx(recipe.spawn_x + 0.5F).margin(0.05));
    CHECK(place.position.y == Catch::Approx(recipe.spawn_floor + 1.0F + 0.8F).margin(0.05));
}

TEST_CASE("the same seed makes the same world whatever the batch size", "[runtime][codegen-entt][robot-sandbox]") {
    std::map<CellKey, std::uint32_t> first;
    {
        World world;
        world.new_game();
        first = world.cells();
    }
    World world;
    world.registry.get<world__WorldConfig>(generated_named_slots().small_world__Settings).batch = 100;
    world.new_game();
    const auto second = world.cells();
    REQUIRE(second.size() == first.size());
    int differing = 0;
    for (const auto& [key, value] : first) {
        const auto found = second.find(key);
        differing += found == second.end() || found->second != value ? 1 : 0;
    }
    CHECK(differing == 0);
    int air_below_surface = 0;
    for (const auto& [key, value] : first) {
        if (value == 0 && std::get<1>(key) < 0) {
            ++air_below_surface;
        }
    }
    CHECK(first.size() == static_cast<std::size_t>(kCells));
    INFO("caves below the surface: " << air_below_surface);
}

// ── Pause and calendar ──────────────────────────────────────────────────────

TEST_CASE("pausing during a fall keeps the pose, velocity and time until Continue",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    const auto robot = world.robot();
    world.registry.get<RobotTransform>(robot).position.y += 3.0F;
    backend::advance_binding_index_epoch();
    world.frame(5);
    world.script().pause = true;
    world.frame();
    REQUIRE(world.session().mode == Mode::Paused);
    world.frame();
    const auto position = world.registry.get<RobotTransform>(robot).position;
    const auto velocity = world.registry.get<Body>(robot).velocity;
    const auto minute   = world.session().minute;
    REQUIRE(velocity.y < 0.0F);
    world.frame(30);
    CHECK(world.registry.get<RobotTransform>(robot).position.y == position.y);
    CHECK(world.registry.get<Body>(robot).velocity.y == velocity.y);
    CHECK(world.session().minute == minute);
    world.script().resume = true;
    world.frame(2);
    CHECK(world.session().mode == Mode::Playing);
    CHECK(world.registry.get<RobotTransform>(robot).position.y < position.y);
}

TEST_CASE("the calendar runs one game minute per second at 1x", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.session().minute = 600.0F;
    world.frame(60);
    CHECK(world.session().minute == Catch::Approx(601.0F).margin(0.02));
}

TEST_CASE("an accelerated clock crosses midnight into the next day", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.session().minute = 1439.0F;
    world.session().speed  = 3;
    const int day          = world.session().day;
    world.frame(60);
    CHECK(world.session().day == day + 1);
    CHECK(world.session().minute == Catch::Approx(1.0F).margin(0.05));

    // One long frame at 8x crosses two midnights at once.
    world.session().minute = 0.0F;
    world.session().speed  = 5;
    backend::generated_inject_external_event(std_core__frameEvent{.dt = 400.0F});
    backend::generated_drain_external_events(world.registry);
    CHECK(world.session().day == day + 3);
    CHECK(world.session().minute == Catch::Approx(3200.0F - 2880.0F).margin(0.5));
}

TEST_CASE("speed steps clamp and 0x freezes only the sky", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().speed_step = 10;
    world.frame();
    CHECK(world.session().speed == 5);
    world.script().speed_step = -10;
    world.frame();
    CHECK(world.session().speed == 0);
    const auto minute = world.session().minute;
    const auto robot  = world.robot();
    world.frame(60);
    const auto start = world.registry.get<RobotTransform>(robot).position;
    cactus_raylib_fake::set_key_down(KEY_W, true);
    world.frame(20);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    CHECK(world.session().minute == minute);
    const auto moved = world.registry.get<RobotTransform>(robot).position;
    CHECK(std::abs(moved.x - start.x) + std::abs(moved.z - start.z) > 0.5F);
}

// ── Editing ─────────────────────────────────────────────────────────────────

TEST_CASE("looking down and clicking digs the cell under the robot", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.frame(60);
    const auto robot = world.robot();
    world.registry.get<Robot>(robot).pitch = -1.45F;
    world.frame();
    const auto& probe = world.registry.get<std_voxel_volume__RayProbe>(robot);
    REQUIRE(probe.hit);
    const int x = probe.x;
    const int y = probe.y;
    const int z = probe.z;
    REQUIRE(world.solid(x, y, z));
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, true);
    world.frame();
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_LEFT, false);
    world.frame();
    CHECK_FALSE(world.solid(x, y, z));
    CHECK(world.session().dirty);
}

TEST_CASE("building into the robot's own space is skipped", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.frame(60);
    const auto robot = world.robot();
    world.registry.get<Robot>(robot).pitch = -1.45F;
    world.frame();
    const auto& probe = world.registry.get<std_voxel_volume__RayProbe>(robot);
    REQUIRE(probe.has_adjacent);
    const int ax = probe.adjacent_x;
    const int ay = probe.adjacent_y;
    const int az = probe.adjacent_z;
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_RIGHT, true);
    world.frame();
    cactus_raylib_fake::set_mouse_button_pressed_this_frame(MOUSE_BUTTON_RIGHT, false);
    world.frame();
    CHECK_FALSE(world.solid(ax, ay, az));
}

TEST_CASE("a region dig removes eligible solids but never bedrock", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -2;
    world.session().height    = 3;
    world.commit_region(1, 1, 3, 2);
    for (int x = 1; x <= 3; ++x) {
        for (int z = 1; z <= 2; ++z) {
            for (int y = -4; y <= -2; ++y) {
                CHECK(world.solid(x, y, z) == (y == kMinY));
            }
        }
    }
}

TEST_CASE("a region dig given its corners in reverse order removes the same cells, and repeating it changes nothing",
          "[runtime][codegen-entt][robot-sandbox]") {
    std::map<CellKey, std::uint32_t> forward;
    {
        World world;
        world.new_game();
        world.session().edit_kind = game__EditKind::Dig;
        world.session().layer     = -1;
        world.session().height    = 2;
        world.commit_region(1, 1, 3, 2);
        forward = world.cells();
    }
    World world;
    world.new_game();
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -1;
    world.session().height    = 2;
    world.commit_region(3, 2, 1, 1);
    const auto reversed = world.cells();
    CHECK(reversed == forward);
    world.session().dirty = false;
    world.commit_region(3, 2, 1, 1);
    CHECK(world.cells() == reversed);
    CHECK_FALSE(world.session().dirty);
}

TEST_CASE("a strategy drag with the mouse selects inclusive cells either way and previews the count",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().toggle_view = true;
    world.frame();
    REQUIRE(world.session().camera == game__View::Strategy);
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -1;
    world.session().height    = 1;
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 640.0F, .y = 360.0F});
    world.aim_strategy_at(6, 5);
    world.press(MOUSE_BUTTON_LEFT);
    auto& editor = world.registry.get<Editor>(world.robot());
    REQUIRE(editor.dragging);
    CHECK(editor.start_x == 6);
    CHECK(editor.start_z == 5);
    world.aim_strategy_at(4, 3);
    world.frame();
    CHECK(editor.end_x == 4);
    CHECK(editor.end_z == 3);
    CHECK(editor.total == 9);
    int solid_before = 0;
    for (int x = 4; x <= 6; ++x) {
        for (int z = 3; z <= 5; ++z) {
            solid_before += world.solid(x, -1, z) ? 1 : 0;
        }
    }
    CHECK(editor.solid == solid_before);
    CHECK(world.session().preview.find("Dig " + std::to_string(solid_before) + " cells, layers -1..-1") == 0);
    world.release(MOUSE_BUTTON_LEFT);
    world.frame();
    CHECK_FALSE(editor.dragging);
    for (int x = 4; x <= 6; ++x) {
        for (int z = 3; z <= 5; ++z) {
            CHECK_FALSE(world.solid(x, -1, z));
        }
    }
    CHECK(world.session().status ==
          "Changed " + std::to_string(solid_before) + " cells, skipped " + std::to_string(9 - solid_before));
}

TEST_CASE("escape cancels a strategy drag without editing", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().toggle_view = true;
    world.frame();
    world.session().layer  = -1;
    world.session().height = 1;
    cactus_raylib_fake::set_mouse_position(Vector2{.x = 640.0F, .y = 360.0F});
    world.aim_strategy_at(6, 5);
    const auto before = world.cells();
    world.press(MOUSE_BUTTON_LEFT);
    REQUIRE(world.registry.get<Editor>(world.robot()).dragging);
    world.tap_key(KEY_ESCAPE);
    CHECK_FALSE(world.registry.get<Editor>(world.robot()).dragging);
    CHECK(world.session().mode == Mode::Playing);
    world.release(MOUSE_BUTTON_LEFT);
    world.frame();
    CHECK(world.cells() == before);
}

TEST_CASE("clicking toolbar buttons picks the material and edit kind without editing terrain",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().toggle_view = true;
    world.frame();
    world.session().layer = -1;
    const auto before     = world.cells();
    world.click_label("Iron");
    CHECK(world.session().material == game__Material::Iron);
    world.click_label("Build");
    CHECK(world.session().edit_kind == game__EditKind::Build);
    CHECK_FALSE(world.registry.get<Editor>(world.robot()).dragging);
    world.frame();
    CHECK(world.cells() == before);
}

TEST_CASE("a region build fills air with the chosen material and leaves solids alone",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -1;
    world.session().height    = 3;
    world.commit_region(1, 1, 4, 3);
    world.session().edit_kind = game__EditKind::Build;
    world.session().material  = game__Material::Iron;
    world.session().layer     = -2;
    world.session().height    = 2;
    world.commit_region(1, 1, 4, 3);
    int filled = 0;
    for (int x = 1; x <= 4; ++x) {
        for (int z = 1; z <= 3; ++z) {
            for (int y = -3; y <= -2; ++y) {
                CHECK(world.solid(x, y, z));
                filled += world.registry.get<Cell>(world.cell_at(x, y, z)).texture == IronTexture ? 1 : 0;
            }
            CHECK_FALSE(world.solid(x, -1, z));
        }
    }
    CHECK(filled == 24);
}

TEST_CASE("look sensitivity changes how far the mouse turns the robot", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    const auto robot          = world.robot();
    world.session().sensitivity = 0.01F;
    const float yaw            = world.registry.get<Robot>(robot).yaw;
    cactus_raylib_fake::set_mouse_delta(Vector2{.x = 10.0F, .y = 0.0F});
    world.frame();
    cactus_raylib_fake::set_mouse_delta(Vector2{.x = 0.0F, .y = 0.0F});
    CHECK(world.registry.get<Robot>(robot).yaw == Catch::Approx(yaw - 0.1F).margin(1e-4));
}

TEST_CASE("options step sensitivity and pan speed within limits, and invert Y flips vertical look",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().pause = true;
    world.frame();
    world.click_label("Options");
    REQUIRE(world.session().mode == Mode::Options);
    world.session().sensitivity = 0.0095F;
    world.session().pan_speed   = 39.0F;
    world.click_label("+", 0);
    world.click_label("+", 0);
    world.click_label("+", 1);
    CHECK(world.session().sensitivity == Catch::Approx(0.01F));
    CHECK(world.session().pan_speed == Catch::Approx(40.0F));
    world.session().pan_speed = 3.0F;
    world.click_label("-", 1);
    CHECK(world.session().pan_speed == Catch::Approx(2.0F));
    world.click_label("Y");
    REQUIRE(world.session().invert_y);
    world.click_label("Back");
    REQUIRE(world.session().mode == Mode::Paused);
    world.script().resume = true;
    world.frame();
    REQUIRE(world.session().mode == Mode::Playing);
    const auto robot                       = world.robot();
    world.registry.get<Robot>(robot).pitch = 0.0F;
    world.session().sensitivity            = 0.01F;
    cactus_raylib_fake::set_mouse_delta(Vector2{.x = 0.0F, .y = 10.0F});
    world.frame();
    cactus_raylib_fake::set_mouse_delta(Vector2{.x = 0.0F, .y = 0.0F});
    CHECK(world.registry.get<Robot>(robot).pitch == Catch::Approx(0.1F).margin(1e-4));
}

// ── Cameras and movement ────────────────────────────────────────────────────

TEST_CASE("the strategy view pans, zooms within limits and centres on the robot",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().toggle_view = true;
    world.frame(2);
    REQUIRE(world.session().camera == game__View::Strategy);
    bool strategy_active = false;
    for (const auto [entity, overview, viewport] :
         world.registry.view<robot__StrategyView, std_camera_viewport__Viewport>().each()) {
        strategy_active = viewport.active;
    }
    CHECK(strategy_active);
    CHECK_FALSE(cactus_raylib_fake::cursor_captured());

    const auto start = world.overview().target;
    cactus_raylib_fake::set_key_down(KEY_D, true);
    world.frame(60);
    cactus_raylib_fake::set_key_down(KEY_D, false);
    CHECK(world.overview().target.x - start.x == Catch::Approx(world.session().pan_speed).margin(0.5));
    CHECK(world.overview().target.z == Catch::Approx(start.z));

    const auto robot_start = world.registry.get<RobotTransform>(world.robot()).position;
    cactus_raylib_fake::set_mouse_wheel_move(100.0F);
    world.frame();
    cactus_raylib_fake::set_mouse_wheel_move(0.0F);
    CHECK(world.overview().zoom == Catch::Approx(8.0F));
    cactus_raylib_fake::set_mouse_wheel_move(-100.0F);
    world.frame();
    cactus_raylib_fake::set_mouse_wheel_move(0.0F);
    CHECK(world.overview().zoom == Catch::Approx(70.0F));

    world.tap_key(KEY_C);
    const auto robot_now = world.registry.get<RobotTransform>(world.robot()).position;
    CHECK(world.overview().target.x == Catch::Approx(robot_now.x));
    CHECK(world.overview().target.z == Catch::Approx(robot_now.z));
    CHECK(robot_now.x == Catch::Approx(robot_start.x).margin(0.01));
}

TEST_CASE("the robot jumps up onto a one-block step", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.frame(60);
    const auto& recipe = world.recipe();
    const int floor    = recipe.spawn_floor;
    // The robot starts facing -Z; put one block in front of it.
    world.session().edit_kind = game__EditKind::Build;
    world.session().layer     = floor + 1;
    world.session().height    = 1;
    world.commit_region(recipe.spawn_x, recipe.spawn_z - 1, recipe.spawn_x, recipe.spawn_z - 1);
    REQUIRE(world.solid(recipe.spawn_x, floor + 1, recipe.spawn_z - 1));
    REQUIRE_FALSE(world.solid(recipe.spawn_x, floor + 2, recipe.spawn_z - 1));
    REQUIRE_FALSE(world.solid(recipe.spawn_x, floor + 3, recipe.spawn_z - 1));

    const auto robot = world.robot();
    cactus_raylib_fake::set_key_down(KEY_W, true);
    world.frame(20);
    CHECK(world.registry.get<RobotTransform>(robot).position.z > recipe.spawn_z + 0.3F);
    world.tap_key(KEY_SPACE);
    world.frame(40);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    world.frame(30);
    const auto& place = world.registry.get<RobotTransform>(robot).position;
    CHECK(place.y == Catch::Approx(floor + 2.0F + 0.8F).margin(0.05));
    CHECK(place.z < static_cast<float>(recipe.spawn_z));
    CHECK(world.registry.get<Body>(robot).grounded);
}

TEST_CASE("the robot walks through a doorway into a constructed room and stops at its far wall",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    const auto& recipe = world.recipe();
    const int sx       = recipe.spawn_x;
    const int sz       = recipe.spawn_z;
    const int floor    = recipe.spawn_floor;
    const auto region  = [&](game__EditKind kind, int layer, int height, int x0, int z0, int x1, int z1) {
        world.session().edit_kind = kind;
        world.session().material  = game__Material::Wood;
        world.session().layer     = layer;
        world.session().height    = height;
        world.commit_region(x0, z0, x1, z1);
    };
    // A hall north of the spawn, a wall with a one-cell, two-high doorway, and a back wall.
    region(game__EditKind::Dig, floor + 3, 3, sx - 3, sz - 6, sx + 3, sz + 1);
    region(game__EditKind::Build, floor, 1, sx - 3, sz - 6, sx + 3, sz + 1);
    region(game__EditKind::Build, floor + 3, 3, sx - 3, sz - 2, sx + 3, sz - 2);
    region(game__EditKind::Dig, floor + 2, 2, sx, sz - 2, sx, sz - 2);
    region(game__EditKind::Build, floor + 3, 3, sx - 3, sz - 7, sx + 3, sz - 7);
    REQUIRE_FALSE(world.solid(sx, floor + 1, sz - 2));
    REQUIRE_FALSE(world.solid(sx, floor + 2, sz - 2));
    REQUIRE(world.solid(sx, floor + 3, sz - 2));
    REQUIRE(world.solid(sx - 1, floor + 1, sz - 2));

    const auto robot = world.robot();
    world.frame(30);
    cactus_raylib_fake::set_key_down(KEY_W, true);
    world.frame(240);
    cactus_raylib_fake::set_key_down(KEY_W, false);
    world.frame(30);
    const auto& place = world.registry.get<RobotTransform>(robot).position;
    CHECK(place.z < static_cast<float>(sz - 2));
    CHECK(place.z == Catch::Approx(sz - 6 + 0.35F).margin(0.05));
    CHECK(place.y == Catch::Approx(floor + 1.0F + 0.8F).margin(0.05));
    CHECK(world.registry.get<Body>(robot).grounded);
}

TEST_CASE("planning underground in the strategy view keeps the robot in place and the edits in the world",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.frame(60);
    const auto robot = world.robot();
    const auto start = world.registry.get<RobotTransform>(robot).position;
    world.script().toggle_view = true;
    world.frame(2);
    REQUIRE(world.session().camera == game__View::Strategy);
    world.session().cutaway   = true;
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -2;
    world.session().height    = 1;
    world.commit_region(1, 1, 3, 3);
    world.frame(30);
    world.script().toggle_view = true;
    world.frame(2);
    REQUIRE(world.session().camera == game__View::FirstPerson);
    world.frame(30);
    const auto now = world.registry.get<RobotTransform>(robot).position;
    CHECK(now.x == Catch::Approx(start.x).margin(1e-3));
    CHECK(now.y == Catch::Approx(start.y).margin(1e-3));
    CHECK(now.z == Catch::Approx(start.z).margin(1e-3));
    for (int x = 1; x <= 3; ++x) {
        for (int z = 1; z <= 3; ++z) {
            CHECK_FALSE(world.solid(x, -2, z));
        }
    }
}

TEST_CASE("recovering puts the robot back in a reopened spawn pocket", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    const auto& recipe        = world.recipe();
    const int floor           = recipe.spawn_floor;
    world.session().edit_kind = game__EditKind::Build;
    world.session().layer     = floor + 1;
    world.session().height    = 1;
    world.commit_region(recipe.spawn_x + 1, recipe.spawn_z - 1, recipe.spawn_x + 1, recipe.spawn_z + 1);
    REQUIRE(world.solid(recipe.spawn_x + 1, floor + 1, recipe.spawn_z));
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = floor;
    world.commit_region(recipe.spawn_x - 1, recipe.spawn_z - 1, recipe.spawn_x - 1, recipe.spawn_z - 1);
    REQUIRE_FALSE(world.solid(recipe.spawn_x - 1, floor, recipe.spawn_z - 1));

    const auto robot = world.robot();
    world.registry.get<RobotTransform>(robot).position.x += 4.0F;
    world.registry.get<Body>(robot).velocity = Vector3{.x = 0.0F, .y = -5.0F, .z = 0.0F};
    backend::advance_binding_index_epoch();
    world.script().recover = true;
    world.frame(2);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            CHECK(world.solid(recipe.spawn_x + dx, floor, recipe.spawn_z + dz));
            CHECK_FALSE(world.solid(recipe.spawn_x + dx, floor + 1, recipe.spawn_z + dz));
        }
    }
    world.frame(60);
    const auto& place = world.registry.get<RobotTransform>(robot).position;
    CHECK(place.x == Catch::Approx(recipe.spawn_x + 0.5F).margin(0.05));
    CHECK(place.z == Catch::Approx(recipe.spawn_z + 0.5F).margin(0.05));
    CHECK(world.registry.get<Body>(robot).grounded);
}

TEST_CASE("the HUD draws over the world in first person", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.frame();
    const auto mark = cactus_raylib_fake::call_log().size();
    world.frame();
    const auto& log = cactus_raylib_fake::call_log();
    std::size_t last_mesh = 0;
    std::size_t first_ui  = log.size();
    for (auto i = mark; i < log.size(); ++i) {
        if (std::holds_alternative<cactus_raylib_fake::RecordedDrawMesh>(log[i])) {
            last_mesh = i;
        }
        const auto* text = std::get_if<cactus_raylib_fake::RecordedDrawTextEx>(&log[i]);
        if (text != nullptr && text->text == "Wood" && first_ui == log.size()) {
            first_ui = i;
        }
    }
    REQUIRE(last_mesh > mark);
    REQUIRE(first_ui < log.size());
    CHECK(first_ui > last_mesh);
}

TEST_CASE("first person hides the robot's own body and the strategy view shows it",
          "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.frame();
    const auto visible = [&] {
        const auto view = world.registry.view<robot__RobotVisual, std_render_models__ModelRenderer>();
        REQUIRE(view.size_hint() >= 1);
        return world.registry.get<std_render_models__ModelRenderer>(*view.begin()).visible;
    };
    CHECK_FALSE(visible());
    world.script().toggle_view = true;
    world.frame(2);
    CHECK(visible());
}

// ── Sky ─────────────────────────────────────────────────────────────────────

TEST_CASE("the sun lights noon from above and the moon lights midnight", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.session().speed = 0;

    const auto light = [&](bool night) {
        for (const auto [entity, body, sky_light] :
             world.registry.view<sky__SkyBody, std_render_meshes__DirectionalLight>().each()) {
            if (body.night == night) {
                return sky_light;
            }
        }
        FAIL("no sky light");
        return std_render_meshes__DirectionalLight{};
    };
    const auto disc_height = [&](bool night) {
        for (const auto [entity, body, place] : world.registry.view<sky__SkyBody, RobotTransform>().each()) {
            if (body.night == night) {
                return place.position.y;
            }
        }
        FAIL("no sky disc");
        return 0.0F;
    };

    world.session().minute = 720.0F;
    world.frame();
    CHECK(light(false).intensity == Catch::Approx(1.0F));
    CHECK(light(false).direction.y < -0.9F);
    CHECK(light(true).intensity == Catch::Approx(0.0F));
    CHECK(disc_height(false) > 100.0F);
    CHECK(disc_height(true) < -100.0F);

    world.session().minute = 0.0F;
    world.frame();
    CHECK(light(false).intensity == Catch::Approx(0.0F));
    CHECK(light(true).intensity == Catch::Approx(0.3F));
    CHECK(disc_height(false) < -100.0F);

    // Dawn: the sun sits on the eastern horizon and only partly lights the world.
    world.session().minute = 360.0F;
    world.frame();
    CHECK(light(false).intensity > 0.0F);
    CHECK(light(false).intensity < 1.0F);
    CHECK(light(false).direction.x < -0.8F);
}

// ── Save, load and exit ─────────────────────────────────────────────────────

TEST_CASE("a save restores dug cells, the robot, the calendar and one set of views",
          "[runtime][codegen-entt][robot-sandbox][persistence]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    cactus_test::ScopedPersistenceAdapter scoped(memory.as_adapter());
    World world;
    world.new_game();
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -1;
    world.session().height    = 2;
    world.commit_region(2, 2, 4, 4);
    world.session().minute = 777.0F;
    world.session().day    = 3;
    world.frame(30);
    const auto saved_position = world.registry.get<RobotTransform>(world.robot()).position;
    world.script().pause = true;
    world.frame();
    world.script().save = true;
    world.frame(6);
    REQUIRE(world.session().mode == Mode::Paused);
    REQUIRE_FALSE(world.session().busy);
    CHECK_FALSE(world.session().dirty);
    const float saved_minute = world.session().minute;

    world.session().edit_kind = game__EditKind::Build;
    world.script().resume     = true;
    world.frame();
    world.commit_region(2, 2, 4, 4);
    world.session().minute = 100.0F;
    world.registry.get<RobotTransform>(world.robot()).position.x += 3.0F;
    world.script().pause = true;
    world.frame();
    world.script().restore = true;
    world.frame(6);

    REQUIRE(world.session().mode == Mode::Playing);
    // The clock runs on for the frames after the load completes.
    CHECK(world.session().minute == Catch::Approx(saved_minute).margin(0.2));
    CHECK(world.session().day == 3);
    CHECK_FALSE(world.solid(3, -1, 3));
    CHECK_FALSE(world.solid(3, -2, 3));
    CHECK(world.count<Cell>() == static_cast<std::size_t>(kCells));
    CHECK(world.count<robot__FirstPersonView>() == 1);
    CHECK(world.count<robot__StrategyView>() == 1);
    CHECK(world.count<sky__SkyBody>() == 4);
    int start_menus = 0;
    for (const auto [entity, on_screen] : world.registry.view<hud__OnScreen>().each()) {
        start_menus += on_screen.screen == hud__Screen::StartMenu ? 1 : 0;
    }
    CHECK(start_menus == 1);
    const auto restored = world.registry.get<RobotTransform>(world.robot()).position;
    CHECK(restored.x == Catch::Approx(saved_position.x).margin(0.05));
    CHECK(restored.z == Catch::Approx(saved_position.z).margin(0.05));
}

TEST_CASE("loading from the start menu, twice, restores the save each time with one set of views",
          "[runtime][codegen-entt][robot-sandbox][persistence]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    cactus_test::ScopedPersistenceAdapter scoped(memory.as_adapter());
    std::map<CellKey, std::uint32_t> saved;
    {
        World world;
        world.new_game();
        world.session().edit_kind = game__EditKind::Dig;
        world.session().layer     = -1;
        world.session().height    = 2;
        world.commit_region(5, 5, 7, 6);
        world.session().material = game__Material::Stone;
        world.session().camera   = game__View::Strategy;
        world.script().pause     = true;
        world.frame();
        world.script().save = true;
        world.frame(6);
        REQUIRE_FALSE(world.session().busy);
        saved = world.cells();
    }
    World world;
    world.frame();
    for (int load = 0; load < 2; ++load) {
        world.script().restore = true;
        world.frame(6);
        REQUIRE(world.session().mode == Mode::Playing);
        CHECK(world.cells() == saved);
        CHECK(world.session().material == game__Material::Stone);
        CHECK(world.session().camera == game__View::Strategy);
        CHECK(world.count<robot__FirstPersonView>() == 1);
        CHECK(world.count<robot__StrategyView>() == 1);
        CHECK(world.count<sky__SkyBody>() == 4);
        CHECK(world.count<Robot>() == 1);
        world.script().pause = true;
        world.frame();
    }
}

TEST_CASE("loading without a save reports the failure and keeps the start menu",
          "[runtime][codegen-entt][robot-sandbox][persistence]") {
    cactus_test::InMemoryPersistenceAdapter memory;
    cactus_test::ScopedPersistenceAdapter scoped(memory.as_adapter());
    World world;
    world.frame();
    world.script().restore = true;
    world.frame(6);
    CHECK(world.session().mode == Mode::Start);
    CHECK_FALSE(world.session().busy);
    CHECK(world.session().status.find("Load failed") == 0);
    CHECK(world.count<Volume>() == 0);
}

TEST_CASE("a corrupt save reports the failure and keeps the current game",
          "[runtime][codegen-entt][robot-sandbox][persistence]") {
    cactus_test::ScopedPersistenceAdapter scoped(backend::PersistenceAdapter{
        .write =
            [](const std::string&, const cactus::persistence::SchemaDescriptor&, const cactus::persistence::Snapshot&) {
                return backend::PersistenceWriteResult{.ok = true};
            },
        .read =
            [](const std::string&, const cactus::persistence::SchemaDescriptor&) {
                return backend::PersistenceReadResult{.ok = false, .code = "decode_failure", .message = "bad document"};
            }});
    World world;
    world.new_game();
    world.session().edit_kind = game__EditKind::Dig;
    world.session().layer     = -1;
    world.session().height    = 1;
    world.commit_region(2, 2, 3, 3);
    const auto before    = world.cells();
    world.script().pause = true;
    world.frame();
    world.script().restore = true;
    world.frame(6);
    CHECK(world.session().mode == Mode::Paused);
    CHECK_FALSE(world.session().busy);
    CHECK(world.session().status.find("Load failed") == 0);
    CHECK(world.cells() == before);
    CHECK(world.count<Robot>() == 1);
}

TEST_CASE("a second save while one is in progress is ignored", "[runtime][codegen-entt][robot-sandbox][persistence]") {
    static int writes = 0;
    writes            = 0;
    cactus_test::ScopedPersistenceAdapter scoped(backend::PersistenceAdapter{
        .write =
            [](const std::string&, const cactus::persistence::SchemaDescriptor&, const cactus::persistence::Snapshot&) {
                ++writes;
                return backend::PersistenceWriteResult{.ok = true};
            },
        .read =
            [](const std::string&, const cactus::persistence::SchemaDescriptor&) {
                return backend::PersistenceReadResult{.ok = false, .code = "io_failure", .message = "none"};
            }});
    World world;
    world.new_game();
    world.script().pause = true;
    world.frame();
    world.script().save = true;
    world.frame();
    REQUIRE(world.session().busy);
    world.script().save = true;
    world.frame();
    world.script().restore = true;
    world.frame(6);
    CHECK(writes == 1);
    CHECK(world.session().mode == Mode::Paused);
    CHECK_FALSE(world.session().busy);
}

TEST_CASE("exit quits at once without unsaved changes", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.frame();
    world.script().exit = true;
    world.frame();
    CHECK(backend::quit_requested());
}

TEST_CASE("exit with unsaved changes asks first, and discarding quits", "[runtime][codegen-entt][robot-sandbox]") {
    World world;
    world.new_game();
    world.script().pause = true;
    world.frame();
    world.script().exit = true;
    world.frame();
    CHECK(world.session().mode == Mode::ConfirmExit);
    CHECK_FALSE(backend::quit_requested());
    world.script().exit_discard = true;
    world.frame();
    CHECK(backend::quit_requested());
}

TEST_CASE("a failed save-and-exit keeps the game open with the error", "[runtime][codegen-entt][robot-sandbox][persistence]") {
    cactus_test::ScopedPersistenceAdapter scoped(backend::PersistenceAdapter{
        .write =
            [](const std::string&, const cactus::persistence::SchemaDescriptor&, const cactus::persistence::Snapshot&) {
                return backend::PersistenceWriteResult{.ok = false, .code = "io_failure", .message = "disk full"};
            },
        .read =
            [](const std::string&, const cactus::persistence::SchemaDescriptor&) {
                return backend::PersistenceReadResult{.ok = false, .code = "io_failure", .message = "none"};
            }});
    World world;
    world.new_game();
    world.script().pause = true;
    world.frame();
    world.script().exit = true;
    world.frame();
    REQUIRE(world.session().mode == Mode::ConfirmExit);
    world.script().exit_save = true;
    world.frame(6);
    CHECK_FALSE(backend::quit_requested());
    CHECK(world.session().mode == Mode::Paused);
    CHECK(world.session().status.find("disk full") != std::string::npos);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
