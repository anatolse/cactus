// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#ifndef CACTUS_ROUNDTRIP_EXE_PATH
#error "CACTUS_ROUNDTRIP_EXE_PATH must name the built persistence_roundtrip_subprocess executable"
#endif
#ifndef CACTUS_REFERENCES_ROUNDTRIP_EXE_PATH
#error "CACTUS_REFERENCES_ROUNDTRIP_EXE_PATH must name the built persistence_references_roundtrip_subprocess executable"
#endif

#include "backends/cpp-entt/runtime.hpp"
#include "persistence_test_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

// std::system runs the command through cmd.exe /c on Windows, with each
// argument individually double-quoted — robust to spaces in either the exe
// or the slot directory path (e.g. a Windows temp path under a spaced
// username). cmd.exe's own quote-stripping only special-cases command lines
// with exactly two quote characters; with more than two (one pair per
// argument, as here) it instead blindly strips the first and last quote of
// the whole line, mangling everything between the two argument pairs. An
// extra outer quote pair sacrifices itself to that stripping and leaves the
// per-argument quoting intact underneath.
int run_subprocess(const std::string& exe_path, const std::string& mode, const std::filesystem::path& directory) {
    const std::string inner = "\"" + exe_path + "\" " + mode + " \"" + directory.string() + "\"";
    const std::string command = "\"" + inner + "\"";
    return std::system(command.c_str());  // NOLINT(cert-env33-c,concurrency-mt-unsafe)
}

}  // namespace

TEST_CASE("save in one process and restore in a fresh process round-trips the world",
          "[persistence][restore][subprocess]") {
    cactus_test::ScopedTempDirectory slot_directory;

    const auto save_exit_code = run_subprocess(CACTUS_ROUNDTRIP_EXE_PATH, "save", slot_directory.path());
    CHECK(save_exit_code == 0);

    const auto restore_exit_code = run_subprocess(CACTUS_ROUNDTRIP_EXE_PATH, "restore", slot_directory.path());
    CHECK(restore_exit_code == 0);
}

TEST_CASE("references, list-nested entity_id, and a canonical asset reference round-trip across processes",
          "[persistence][restore][subprocess][references]") {
    cactus_test::ScopedTempDirectory slot_directory;

    const auto save_exit_code =
        run_subprocess(CACTUS_REFERENCES_ROUNDTRIP_EXE_PATH, "save", slot_directory.path());
    CHECK(save_exit_code == 0);

    const auto restore_exit_code =
        run_subprocess(CACTUS_REFERENCES_ROUNDTRIP_EXE_PATH, "restore", slot_directory.path());
    CHECK(restore_exit_code == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison)
