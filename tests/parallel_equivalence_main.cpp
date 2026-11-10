// Plays a fixture and writes one world digest per frame to `argv[1]`, so a
// sequential and a parallel build can be compared. `argv[2]` is the worker
// count, or "default".
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/fake_raylib.hpp"
#include "fake_raylib/headless_frame_driver.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>

namespace {

namespace backend = cactus::runtime::entt_backend;

constexpr int kFrames = 120;

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <digest file> <workers|default>\n", argv[0]);
        return 2;
    }
    const std::string workers = argv[2];
    backend::set_parallel_workers(workers == "default" ? std::nullopt
                                                       : std::optional<std::size_t>{std::stoul(workers)});
    cactus_raylib_fake::reset();
    cactus_raylib_fake::set_window_ready(true);
    std::ofstream out(argv[1]);
    {
        entt::registry registry;
        cactus_headless_test::init_and_load(registry);
        for (int frame = 0; frame < kFrames; ++frame) {
            cactus_headless_test::drive_frame(registry, 1.0F / 60.0F);
            out << backend::generated_world_digest(registry) << '\n';
        }
    }
    backend::set_parallel_workers(std::nullopt);
    return out.good() ? 0 : 1;
}
