// A second round-trip subprocess executable (task 7.2), built from tests/
// fixtures/persistence_references_runtime.cactus rather than examples/
// world_persistence.cactus. That fixture is the one dedicated to entity_id
// references (mutual, absent-but-equal, and list-nested) and a canonical
// asset reference (Badge.icon) — this proves all three decode correctly
// through real file I/O in a genuinely fresh process, the same claim
// persistence_roundtrip_subprocess_main.cpp already proves for fields,
// template-evaluated construction, and hierarchy. Same two-mode contract as
// that harness: argv[1] selects save/restore, argv[2] is the slot directory,
// one "SAVE:"/"RESTORE:" result line, exit 0 on success.
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "backends/cpp-entt/persistence_file_adapter.hpp"
#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_roundtrip_subprocess_harness.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using cactus::runtime::entt_backend::generated_schema;
using Rival  = persistence_references_runtime__Rival;
using Roster = persistence_references_runtime__Roster;
using Badge  = persistence_references_runtime__Badge;
using cactus::runtime::entt_backend::CreationOrdinal;
using cactus_test::fail_roundtrip;

constexpr const char* kSlot = "roundtrip";

int run_save(const std::filesystem::path& directory) {
    entt::registry registry;
    cactus::runtime::entt_backend::register_persistence_adapter(
        cactus::runtime::entt_backend::make_example_file_adapter(directory));
    cactus_headless_test::init_and_load(registry);

    const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
    if (snapshot.entities.size() != 4) {
        fail_roundtrip("SAVE", "expected 4 captured entities (rival_a, rival_b, lonely, also_lonely)");
    }
    const auto outcome =
        cactus::runtime::entt_backend::generated_execute_save_request(kSlot, 1, generated_schema, snapshot);
    if (!outcome.ok) {
        fail_roundtrip("SAVE", outcome.code + ": " + outcome.message);
    }
    std::printf("SAVE:OK\n");
    return 0;
}

int run_restore(const std::filesystem::path& directory) {
    entt::registry registry;
    cactus::runtime::entt_backend::register_persistence_adapter(
        cactus::runtime::entt_backend::make_example_file_adapter(directory));
    // A fresh load (a different process's own rivals) — restore must fully
    // replace it, exercised through the file adapter and a real process
    // boundary rather than the in-memory adapter's same-process round trip.
    cactus_headless_test::init_and_load(registry);

    const auto outcome =
        cactus::runtime::entt_backend::generated_execute_restore_request(registry, kSlot, 1, generated_schema);
    if (!outcome.ok) {
        fail_roundtrip("RESTORE", outcome.code + ": " + outcome.message);
    }

    struct Entry {
        entt::entity entity;
        std::uint64_t ordinal;
    };
    std::vector<Entry> rivals;
    for (const auto entity : registry.view<Rival, CreationOrdinal>()) {
        rivals.push_back(Entry{.entity = entity, .ordinal = registry.get<CreationOrdinal>(entity).value});
    }
    if (rivals.size() != 4) {
        fail_roundtrip("RESTORE", "expected 4 restored entities carrying Rival");
    }
    // Ordering: restore assigns fresh CreationOrdinal values in document
    // order, and the document was captured in original creation order, so
    // sorting by ordinal recovers the original rival_a/rival_b/lonely/
    // also_lonely sequence even though entt::entity identity itself is not
    // guaranteed stable across a restore (design.md decision 2).
    std::ranges::sort(rivals, {}, &Entry::ordinal);
    const auto rival_a = rivals[0].entity;
    const auto rival_b = rivals[1].entity;
    const auto lonely  = rivals[2].entity;
    const auto also_lonely = rivals[3].entity;

    if (registry.get<Rival>(rival_a).rival != rival_b || registry.get<Rival>(rival_b).rival != rival_a) {
        fail_roundtrip("RESTORE", "mutual references did not resolve to each other's restored entity");
    }
    const auto lonely_target      = registry.get<Rival>(lonely).rival;
    const auto also_lonely_target = registry.get<Rival>(also_lonely).rival;
    if (registry.valid(lonely_target) || registry.valid(also_lonely_target)) {
        fail_roundtrip("RESTORE", "reference to an excluded entity restored as if it were valid");
    }
    if (lonely_target != also_lonely_target) {
        fail_roundtrip("RESTORE", "repeated references to the same excluded target did not stay equal");
    }

    if (!registry.all_of<Roster>(rival_a)) {
        fail_roundtrip("RESTORE", "rival_a lost its Roster after restore");
    }
    const auto& members = registry.get<Roster>(rival_a).members;
    if (members.size() != 2) {
        fail_roundtrip("RESTORE", "Roster.members did not restore both list-nested entity_id references");
    }
    const bool members_match_pair = (members[0] == rival_a && members[1] == rival_b) ||
                                    (members[0] == rival_b && members[1] == rival_a);
    if (!members_match_pair) {
        fail_roundtrip("RESTORE", "Roster.members did not resolve to the restored mutual pair");
    }

    if (!registry.all_of<Badge>(rival_a)) {
        fail_roundtrip("RESTORE", "rival_a lost its Badge after restore");
    }
    if (registry.get<Badge>(rival_a).icon == 0) {
        fail_roundtrip("RESTORE", "Badge.icon's canonical asset reference did not resolve to a valid handle");
    }

    std::printf("RESTORE:OK\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) { return cactus_test::run_roundtrip_main(argc, argv, run_save, run_restore); }
