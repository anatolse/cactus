#ifndef CACTUS_EXTERNAL_HANDLER_GENERATED_CPP
#error "CACTUS_EXTERNAL_HANDLER_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_EXTERNAL_HANDLER_GENERATED_CPP

#ifndef CACTUS_EXTERNAL_HANDLER_MISSING_IMPLEMENTATIONS

void cactus_external__external_handler_linking__NativeMovement__on__external_handler_linking__fixed_tick(
    const cactus::runtime::entt_backend::external_handler_linking__fixed_tickPhaseRuntimeState& trigger,
    entt::entity entity,
    const external_handler_linking__Position& read_external_handler_linking__Position,
    external_handler_linking__Velocity& write_external_handler_linking__Velocity,
    const cactus::runtime::entt_backend::
        CactusCapabilities__external_handler_linking__NativeMovement__on__external_handler_linking__fixed_tick&
            capabilities) {
    (void)entity;
    (void)capabilities;
    write_external_handler_linking__Velocity.value +=
        read_external_handler_linking__Position.value * static_cast<float>(trigger.dt);
}

void cactus_external__external_handler_linking__NativeMovement__on__external_handler_linking__Reset(
    const external_handler_linking__ResetEvent& trigger,
    entt::entity entity,
    const external_handler_linking__Velocity& read_external_handler_linking__Velocity,
    external_handler_linking__Position& write_external_handler_linking__Position,
    const cactus::runtime::entt_backend::
        CactusCapabilities__external_handler_linking__NativeMovement__on__external_handler_linking__Reset&
            capabilities) {
    (void)entity;
    (void)capabilities;
    write_external_handler_linking__Position.value =
        read_external_handler_linking__Velocity.value + static_cast<float>(trigger.value);
}

#endif

int main() {
#ifndef CACTUS_EXTERNAL_HANDLER_MISSING_IMPLEMENTATIONS
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(external_handler_linking__frameEvent{.dt = 0.25F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
#endif
    return 0;
}
