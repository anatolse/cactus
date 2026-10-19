// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_HEADLESS_GENERATED_CPP
#error "CACTUS_HEADLESS_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace backend = cactus::runtime::entt_backend;

struct World {
    entt::registry registry;

    World() {
        cactus_raylib_fake::reset();
        cactus_raylib_fake::set_window_ready(true);
        backend::release_render_resources();
        backend::generated_init_project(registry);
        backend::generated_load_project(registry);
    }

    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            backend::generated_inject_external_event(std_core__frameEvent{.dt = 1.0F / 60.0F});
            backend::generated_drain_external_events(registry);
        }
    }

    const texture_assets__Measured& measured() {
        return registry.get<texture_assets__Measured>(generated_named_slots().texture_assets__Measures);
    }
};

// The most recent draw at destination x, or nullopt.
std::optional<cactus_raylib_fake::RecordedDrawTexturePro> draw_at(float x) {
    std::optional<cactus_raylib_fake::RecordedDrawTexturePro> found;
    for (const auto& call : cactus_raylib_fake::call_log()) {
        if (const auto* draw = std::get_if<cactus_raylib_fake::RecordedDrawTexturePro>(&call);
            draw != nullptr && draw->dest.x == x) {
            found = *draw;
        }
    }
    return found;
}

}  // namespace

TEST_CASE("a texture's decoded size reaches UI measurement and sprites", "[runtime][codegen-entt][asset-runtime]") {
    World world;
    world.frame();
    CHECK(world.measured().ground.x == 64.0F);
    CHECK(world.measured().ground.y == 32.0F);
    const auto ground = draw_at(0.0F);
    REQUIRE(ground.has_value());
    CHECK(ground->texture.width == 64);
    CHECK(ground->texture.height == 32);
    CHECK(ground->source.width == 64.0F);
}

TEST_CASE("consumers of one texture share a single load", "[runtime][codegen-entt][asset-runtime]") {
    World world;
    world.frame(3);
    const auto first  = draw_at(0.0F);
    const auto second = draw_at(100.0F);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->texture.id == second->texture.id);
    // Ground is read once, whatever the frame count; the missing file never is.
    CHECK(backend::render_debug_state().texture_file_loads == 1);
}

TEST_CASE("a missing texture is a checkerboard with one diagnostic and no retries",
          "[runtime][codegen-entt][asset-runtime]") {
    World world;
    world.frame(4);
    CHECK(world.measured().missing.x == 32.0F);
    CHECK(world.measured().missing.y == 32.0F);
    const auto& diagnostics = backend::render_debug_state().texture_diagnostics;
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().find("does_not_exist.png") != std::string::npos);
    const auto missing = draw_at(200.0F);
    REQUIRE(missing.has_value());
    CHECK(missing->texture.width == 32);
    CHECK(missing->texture.height == 32);
    CHECK(backend::render_debug_state().texture_file_loads == 1);
}

TEST_CASE("textures resolve from outside the source directory", "[runtime][codegen-entt][asset-runtime]") {
    const auto source_dir = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    {
        World world;
        world.frame();
        CHECK(world.measured().ground.x == 64.0F);
        CHECK(backend::render_debug_state().texture_diagnostics.size() == 1);
    }
    std::filesystem::current_path(source_dir);
}

TEST_CASE("releasing render resources forgets loaded textures", "[runtime][codegen-entt][asset-runtime]") {
    World world;
    world.frame();
    REQUIRE(backend::render_debug_state().texture_file_loads == 1);
    backend::release_render_resources();
    CHECK(backend::render_debug_state().texture_file_loads == 0);
    world.frame();
    CHECK(backend::render_debug_state().texture_file_loads == 1);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
