// Plays a fixture and writes one world digest per frame to `argv[1]`, so a
// build with --trace-rules can be compared with one without. A traced build
// reads its trace options from the arguments after that.
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <print>

namespace {

namespace backend = cactus::runtime::entt_backend;

constexpr int kFrames = 90;
// Uneven frame times, so some frames run no fixed step and some run several.
constexpr std::array<float, 4> kFrameTimes{1.0F / 60.0F, 2.5F / 60.0F, 0.5F / 60.0F, 1.0F / 30.0F};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::println(stderr, "usage: {} <digest file> [trace options]", argv[0]);
        return 2;
    }
#ifdef CACTUS_TRACED
    // The digest file stands where the parser expects the program name.
    if (!backend::configure_rule_trace_from_args(argc - 1, argv + 1)) {
        return 2;
    }
#endif
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    std::ofstream out(argv[1]);
    {
        entt::registry registry;
        cactus_headless_test::init_and_load(registry);
        for (int frame = 0; frame < kFrames; ++frame) {
            cactus_headless_test::drive_frame(registry, kFrameTimes.at(static_cast<std::size_t>(frame) % kFrameTimes.size()));
            out << backend::generated_world_digest(registry) << '\n';
        }
        cactus_headless_test::dispatch_unload_event(registry);
    }
#ifdef CACTUS_TRACED
    backend::flush_rule_trace();
    if (!backend::rule_trace_enabled()) {
        return 1;
    }
#endif
    return out.good() ? 0 : 1;
}
