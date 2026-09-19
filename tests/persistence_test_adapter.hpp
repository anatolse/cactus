#pragma once

// A test-only PersistenceAdapter (add-world-save-restore decision 7: "the
// in-memory adapter exists for tests") that stores documents in memory,
// keyed by slot. Proves the write/read adapter contract round trip without
// real file I/O; shared across headless tests that need a concrete adapter
// rather than a bare hand-written lambda per test.
//
// Must be included after CACTUS_HEADLESS_GENERATED_CPP, same as
// fake_raylib/headless_frame_driver.hpp, so the persistence:: and
// runtime::entt_backend:: types it names are already visible.

#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>

namespace cactus_test {

// A fresh, uniquely-named directory for the scope's lifetime, removed on
// exit. Shared by tests exercising the example file adapter, which needs a
// real filesystem location rather than an in-memory map.
class ScopedTempDirectory {
public:
    ScopedTempDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("cactus_test_persistence_" + std::to_string(std::random_device{}()))) {
        std::filesystem::create_directories(path_);
    }
    ~ScopedTempDirectory() { std::filesystem::remove_all(path_); }

    ScopedTempDirectory(const ScopedTempDirectory&)            = delete;
    ScopedTempDirectory& operator=(const ScopedTempDirectory&) = delete;
    ScopedTempDirectory(ScopedTempDirectory&&)                 = delete;
    ScopedTempDirectory& operator=(ScopedTempDirectory&&)      = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

class InMemoryPersistenceAdapter {
public:
    [[nodiscard]] cactus::runtime::entt_backend::PersistenceAdapter as_adapter() {
        return cactus::runtime::entt_backend::PersistenceAdapter{
            .write =
                [this](const std::string& slot,
                      const cactus::persistence::SchemaDescriptor&,
                      const cactus::persistence::Snapshot& snapshot) {
                    documents_[slot] = snapshot;
                    return cactus::runtime::entt_backend::PersistenceWriteResult{.ok = true};
                },
            .read =
                [this](const std::string& slot, const cactus::persistence::SchemaDescriptor&) {
                    const auto found = documents_.find(slot);
                    if (found == documents_.end()) {
                        return cactus::runtime::entt_backend::PersistenceReadResult{
                            .ok      = false,
                            .code    = "io_failure",
                            .message = "no document stored for slot '" + slot + "'"};
                    }
                    return cactus::runtime::entt_backend::PersistenceReadResult{.ok       = true,
                                                                                .snapshot = found->second};
                }};
    }

private:
    std::unordered_map<std::string, cactus::persistence::Snapshot> documents_;
};

// Registers an adapter for the scope's lifetime and always clears it on
// exit, success or failure — generated_persistence_state() is a
// function-local static shared across every test case in the binary, so a
// REQUIRE failure partway through a test must not leak a registered adapter
// into unrelated later tests.
class ScopedPersistenceAdapter {
public:
    explicit ScopedPersistenceAdapter(cactus::runtime::entt_backend::PersistenceAdapter adapter) {
        cactus::runtime::entt_backend::register_persistence_adapter(std::move(adapter));
    }
    ~ScopedPersistenceAdapter() { cactus::runtime::entt_backend::clear_persistence_adapter(); }

    ScopedPersistenceAdapter(const ScopedPersistenceAdapter&)            = delete;
    ScopedPersistenceAdapter& operator=(const ScopedPersistenceAdapter&) = delete;
    ScopedPersistenceAdapter(ScopedPersistenceAdapter&&)                 = delete;
    ScopedPersistenceAdapter& operator=(ScopedPersistenceAdapter&&)      = delete;
};

}  // namespace cactus_test
