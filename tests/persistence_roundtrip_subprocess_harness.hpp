#pragma once

// Shared `main()`/`fail()` for the roundtrip subprocess executables (see
// persistence_roundtrip_subprocess_main.cpp and persistence_references_
// roundtrip_subprocess_main.cpp): each supplies only its own run_save/
// run_restore, both taking a `const std::filesystem::path&` slot directory
// and returning `int`. `argv[1]` selects the mode; `argv[2]` is the slot
// directory. Prints one "SAVE:"/"RESTORE:" result line and exits 0 on
// success, matching what the driving test (test_persistence_roundtrip_
// subprocess.cpp) inspects.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace cactus_test {

[[noreturn]] inline void fail_roundtrip(const char* prefix, const std::string& message) {
    std::printf("%s:FAIL:%s\n", prefix, message.c_str());
    std::exit(1);
}

template <typename RunSave, typename RunRestore>
int run_roundtrip_main(int argc, char** argv, RunSave&& run_save, RunRestore&& run_restore) try {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <save|restore> <slot-directory>\n", argc > 0 ? argv[0] : "roundtrip");
        return 2;
    }
    const std::string mode(argv[1]);
    const std::filesystem::path directory(argv[2]);
    if (mode == "save") {
        return std::forward<RunSave>(run_save)(directory);
    }
    if (mode == "restore") {
        return std::forward<RunRestore>(run_restore)(directory);
    }
    std::fprintf(stderr, "unknown mode '%s'\n", mode.c_str());
    return 2;
} catch (const std::exception& error) {
    std::fprintf(stderr, "unhandled exception: %s\n", error.what());
    return 1;
}

}  // namespace cactus_test
