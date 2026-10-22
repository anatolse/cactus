#ifndef CACTUS_EXTERNAL_HANDLER_GENERATED_CPP
#error "CACTUS_EXTERNAL_HANDLER_GENERATED_CPP must name the generated translation unit"
#endif

#define CACTUS_GENERATED_NO_MAIN
#include CACTUS_EXTERNAL_HANDLER_GENERATED_CPP

#ifndef CACTUS_EXTERNAL_HANDLER_MISSING_IMPLEMENTATIONS

namespace linking = cactus_gen::external_handler_linking;

void cactus_gen::external_handler_linking::NativeMovement::on(
    const linking::FixedTickPhase& trigger,
    entt::entity entity,
    const linking::Position& position,
    linking::Velocity& velocity,
    const Capabilities<linking::FixedTickPhase>& capabilities) {
    capabilities.set<linking::Heading>(entity, {.angle = 1.0F});
#ifdef CACTUS_EXTERNAL_HANDLER_DISALLOWED_SET
    capabilities.set<linking::Velocity>(entity, {});
#endif
    velocity.value += position.value * static_cast<float>(trigger.dt);
}

#ifndef CACTUS_EXTERNAL_HANDLER_PARTIAL_IMPLEMENTATIONS
void cactus_gen::external_handler_linking::NativeMovement::on(const linking::ResetEvent& trigger,
                                                              entt::entity entity,
                                                              const linking::Velocity& velocity,
                                                              linking::Position& position,
                                                              const Capabilities<linking::ResetEvent>& capabilities) {
    (void)entity;
    (void)capabilities;
    position.value = velocity.value + static_cast<float>(trigger.value);
}
#endif

#endif

int main() {
#ifndef CACTUS_EXTERNAL_HANDLER_MISSING_IMPLEMENTATIONS
    entt::registry registry;
    cactus::runtime::entt_backend::generated_init_project(registry);
    cactus::runtime::entt_backend::generated_inject_external_event(linking::frameEvent{.dt = 0.25F});
    cactus::runtime::entt_backend::generated_drain_external_events(registry);
#endif
    return 0;
}
