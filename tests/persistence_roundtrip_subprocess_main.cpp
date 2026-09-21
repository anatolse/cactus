// A minimal, headless, two-mode standalone executable proving a save/restore
// round trip across two genuinely separate process invocations (design.md
// decision 6): file-static runtime holders (pointer router state, editor
// camera rig, the deferred-spawn reservation cursor, ...) all persist across
// two in-process runs, which would mask exactly the bugs a fresh-process
// round trip is meant to catch. `argv[1]` selects the mode; `argv[2]` is the
// save-slot directory. Prints one "SAVE:"/"RESTORE:" result line and exits 0
// on success, 1 otherwise — the driving test (test_persistence_roundtrip_
// subprocess.cpp) only inspects the exit code and that line.
//
// Deliberately not a Catch2 binary: this is a real generated program's real
// entry point, standing in for a shipped game — the round trip must exercise
// exactly the same generated functions a real executable would call, not a
// test-only shortcut.
#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_HEADLESS_GENERATED_CPP

#include "fake_raylib/headless_frame_driver.hpp"
#include "persistence_roundtrip_subprocess_harness.hpp"

#include <cstdio>
#include <string>

namespace {

using cactus::runtime::entt_backend::ArchetypeOrigin;
using cactus::runtime::entt_backend::generated_archetype_node_index;
using cactus::runtime::entt_backend::generated_schema;
using cactus_test::fail_roundtrip;

constexpr const char* kSlot = "roundtrip";
constexpr float kStep       = 1.0F / 60.0F;

template <typename Trait>
entt::entity entity_with(entt::registry& registry) {
    const auto view = registry.view<Trait>();
    return view.begin() == view.end() ? entt::entity{entt::null} : *view.begin();
}

// Boss and the template-spawned Enemy both carry Health, so a bare
// entity_with<Health> is ambiguous — EnTT's iteration order for a single
// component type is not guaranteed to match save-time creation order after a
// restore repopulates the registry from a document. ArchetypeOrigin::node
// records each live entity's originating archetype (set by both load-time
// creation and restore reconstruction), giving an unambiguous lookup.
entt::entity entity_of_archetype(entt::registry& registry, const std::string& archetype) {
    const auto node = generated_archetype_node_index(archetype);
    if (!node) {
        return entt::entity{entt::null};
    }
    for (const auto entity : registry.view<ArchetypeOrigin>()) {
        if (registry.get<ArchetypeOrigin>(entity).node == *node) {
            return entity;
        }
    }
    return entt::entity{entt::null};
}

int run_save(const std::filesystem::path& directory) {
    entt::registry registry;
    cactus::runtime::entt_backend::register_persistence_adapter(
        cactus::runtime::entt_backend::make_example_file_adapter(directory));
    cactus_headless_test::init_and_load(registry);

    // Exercise a template-spawned entity (Enemy, with its own construction
    // provenance and a nested LocalTransform) and the Formation/Member
    // hierarchy, not just the load-time singletons.
    auto& spawner = registry.get<world_persistence__Spawner>(entity_with<world_persistence__Spawner>(registry));
    spawner.pending_enemies = 1;
    cactus_headless_test::drive_frame(registry, kStep);

    auto& boss    = registry.get<world_persistence__Health>(entity_of_archetype(registry, "world_persistence.Boss"));
    boss.current  = 321;

    const auto snapshot = cactus::runtime::entt_backend::generated_capture_world_snapshot(registry);
    if (snapshot.entities.empty()) {
        fail_roundtrip("SAVE", "captured snapshot was empty");
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
    // A fresh load (a different process's own Boss/Warehouse/.../Camera) —
    // restore must fully replace it, the same claim the in-process tests
    // make, now proven with no shared static state between save and restore.
    cactus_headless_test::init_and_load(registry);

    const auto outcome =
        cactus::runtime::entt_backend::generated_execute_restore_request(registry, kSlot, 1, generated_schema);
    if (!outcome.ok) {
        fail_roundtrip("RESTORE", outcome.code + ": " + outcome.message);
    }

    const auto boss_entity = entity_of_archetype(registry, "world_persistence.Boss");
    if (boss_entity == entt::entity{entt::null}) {
        fail_roundtrip("RESTORE", "no restored entity carries the Boss archetype");
    }
    const auto& boss = registry.get<world_persistence__Health>(boss_entity);
    if (boss.maximum != 500 || boss.current != 321) {
        fail_roundtrip("RESTORE", "Boss.Health did not restore its archetype baseline and persisted value");
    }

    const auto badge_entity = entity_with<world_persistence__Badge>(registry);
    if (badge_entity == entt::entity{entt::null}) {
        fail_roundtrip("RESTORE", "no restored entity carries Badge");
    }
    if (registry.get<world_persistence__Badge>(badge_entity).rank != 1) {
        fail_roundtrip("RESTORE", "Formation/Member's Badge.rank did not restore");
    }
    if (registry.all_of<std_core__Parent>(badge_entity)) {
        fail_roundtrip("RESTORE", "Member restored with a Parent link, but Formation was never eligible");
    }

    const auto enemy = entity_of_archetype(registry, "world_persistence.Enemy");
    if (enemy == entt::entity{entt::null}) {
        fail_roundtrip("RESTORE", "no restored entity carries the Enemy archetype");
    }
    if (registry.get<world_persistence__Health>(enemy).maximum != 700) {
        fail_roundtrip("RESTORE", "Enemy's evaluated spawn argument (maximum) did not restore");
    }
    if (!registry.all_of<std_transform_flat__LocalTransform>(enemy)) {
        fail_roundtrip("RESTORE", "restored Enemy is missing its LocalTransform baseline trait");
    }

    std::printf("RESTORE:OK\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) { return cactus_test::run_roundtrip_main(argc, argv, run_save, run_restore); }
