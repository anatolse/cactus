// Plays a fixture, saves slot "played", restores it and saves slot "restored"
// into the directory `argv[1]`, so builds with and without
// --retain-all-construction can be compared byte for byte.
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "backends/cpp-entt/persistence_file_adapter.hpp"
#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace {

namespace backend = cactus::runtime::entt_backend;

constexpr float kStep = 1.0F / 60.0F;

void frames(entt::registry& registry, int count) {
    for (int i = 0; i < count; ++i) {
        cactus_headless_test::drive_frame(registry, kStep);
    }
}

#ifdef CACTUS_EQUIVALENCE_ROBOT_SANDBOX
using Session = cactus_gen::game::Session;
using Mode    = cactus_gen::game::Mode;
using Script  = cactus_gen::small_world::Script;
using Editor  = cactus_gen::editing::Editor;
using Robot   = cactus_gen::robot::Robot;

// A new game, two dug regions and a built one, so cells change after they are made.
void play(entt::registry& registry) {
    auto& slots   = cactus_gen::detail::generated_named_slots();
    auto script   = [&]() -> Script& { return registry.get<Script>(slots.small_world.Tester); };
    auto session  = [&]() -> Session& { return registry.get<Session>(slots.game.Game); };
    script().new_game = true;
    for (int i = 0; i < 120 && session().mode != Mode::Playing; ++i) {
        frames(registry, 1);
    }
    if (session().mode != Mode::Playing) {
        std::printf("PLAY:FAIL:the new game never started\n");
        std::exit(1);
    }
    const auto commit = [&](cactus_gen::game::EditKind kind, int x0, int z0, int x1, int z1) {
        session().edit_kind = kind;
        session().layer     = -1;
        session().height    = 2;
        auto& editor        = registry.get<Editor>(*registry.view<Robot>().begin());
        editor.start_x      = x0;
        editor.start_z      = z0;
        editor.end_x        = x1;
        editor.end_z        = z1;
        script().commit_region = true;
        frames(registry, 2);
    };
    commit(cactus_gen::game::EditKind::Dig, 2, 2, 4, 4);
    commit(cactus_gen::game::EditKind::Dig, 8, 8, 9, 12);
    commit(cactus_gen::game::EditKind::Build, 3, 3, 3, 3);
    frames(registry, 30);
}
#else
void play(entt::registry& registry) {
    frames(registry, 120);
}
#endif

void save(const char* slot, const entt::registry& registry) {
    const auto snapshot = backend::generated_capture_world_snapshot(registry);
    const auto outcome  = backend::generated_execute_save_request(slot, 1, backend::generated_schema, snapshot);
    if (!outcome.ok) {
        std::printf("SAVE:FAIL:%s: %s\n", outcome.code.c_str(), outcome.message.c_str());
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <slot-directory>\n", argc > 0 ? argv[0] : "equivalence");
        return 2;
    }
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    cactus_raylib_fake::set_screen_size(1280, 720);
    backend::register_persistence_adapter(backend::make_example_file_adapter(std::filesystem::path(argv[1])));
    entt::registry registry;
    cactus_headless_test::init_and_load(registry);
    play(registry);
    save("played", registry);

    const auto outcome = backend::generated_execute_restore_request(registry, "played", 2, backend::generated_schema);
    if (!outcome.ok) {
        std::printf("RESTORE:FAIL:%s: %s\n", outcome.code.c_str(), outcome.message.c_str());
        return 1;
    }
    save("restored", registry);
    std::printf("SAVE:OK\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "unhandled exception: %s\n", error.what());
    return 1;
}
