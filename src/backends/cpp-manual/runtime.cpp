#include "backends/cpp-manual/runtime.h"

#include <vector>

namespace cactus::runtime::manual_backend {

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void propagate_hierarchy(std::size_t entity_count,
                         const ParentResolver& resolve_parent,
                         const CopyLocalTransformFn& copy_local,
                         const AccumulateTransformFn& accumulate_from_parent) {
    std::vector<std::uint8_t> active(entity_count, 0);

    const auto resolve = [&](const auto& self, std::size_t entity) -> void {
        if (entity >= entity_count || active[entity] != 0U) {
            return;
        }

        active[entity] = 1U;
        bool copied_local = false;

        if (const auto parent = resolve_parent(entity); parent.has_value() && parent.value() < entity_count) {
            self(self, parent.value());
            accumulate_from_parent(parent.value(), entity);
            copied_local = true;
        }

        if (!copied_local) {
            copy_local(entity);
        }

        active[entity] = 0U;
    };

    for (std::size_t entity = 0; entity < entity_count; ++entity) {
        resolve(resolve, entity);
    }
}

void destroy_entity_recursive(std::size_t entity,
                              const IsValidEntityFn& is_valid,
                              const VisitChildrenFn& visit_children,
                              const RemoveEntityFn& remove_entity) {
    static std::vector<std::size_t> destroying_entities;

    for (const auto active_entity : destroying_entities) {
        if (active_entity == entity) {
            return;
        }
    }

    if (!is_valid(entity)) {
        return;
    }

    destroying_entities.push_back(entity);
    visit_children(entity, [&](std::size_t child) {
        destroy_entity_recursive(child, is_valid, visit_children, remove_entity);
    });

    if (is_valid(entity)) {
        remove_entity(entity);
    }

    if (!destroying_entities.empty()) {
        destroying_entities.pop_back();
    }
}

}  // namespace cactus::runtime::manual_backend