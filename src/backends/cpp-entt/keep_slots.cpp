#include "backends/cpp-entt/keep_slots.hpp"

#include "backends/cpp-entt/type_utils.hpp"

#include <algorithm>
#include <utility>

namespace cactus {

namespace {

const KeepSlotPlan*& active_plan() {
    thread_local const KeepSlotPlan* plan = nullptr;
    return plan;
}

void sort_unique(std::vector<std::uint32_t>& slots) {
    std::ranges::sort(slots);
    const auto [first, last] = std::ranges::unique(slots);
    slots.erase(first, last);
}

struct Keeper {
    const HandlerNode* handler = nullptr;
    SymbolId kept;
};

// Keep rules written in Cactus with what each keeps, in canonical order.
std::vector<Keeper> keep_handlers(const DecoratedProgram& program) {
    std::vector<Keeper> keepers;
    for (const auto& handler : program.execution_graph.handlers) {
        if (handler.kept.has_value() && handler.implementation == HandlerImplementationKind::Cactus) {
            keepers.push_back({.handler = &handler, .kept = *handler.kept});
        }
    }
    std::ranges::sort(keepers, {}, [](const Keeper& keeper) { return keeper.handler->identity.canonical_id(); });
    return keepers;
}

}  // namespace

KeepSlotPlan::KeepSlotPlan(const DecoratedProgram& program) {
    const auto find_trait = [&](const SymbolId& trait) {
        return EnttCodegenUtils::find_trait(program, make_canonical_id(trait));
    };
    const auto find_relation = [&](const SymbolId& relation) {
        return EnttCodegenUtils::find_relation(program, relation);
    };
    std::vector<std::pair<Keeper, KeepInputs>> inputs;
    for (const auto& keeper : keep_handlers(program)) {
        auto read = keep_inputs(keeper.handler->contract, find_trait, find_relation);
        add_slots(read, keeper.kept);
        inputs.emplace_back(keeper, std::move(read));
    }
    for (auto& [canonical, entry] : traits_) {
        entry.second = static_cast<std::uint32_t>(slot_count_++);
    }
    for (auto& [key, slot] : fields_) {
        slot = static_cast<std::uint32_t>(slot_count_++);
    }
    const auto rules = rules_by_id(program.ast);
    for (auto& [keeper, read] : inputs) {
        auto keep = make_keep(read, keeper.kept);
        if (const auto rule = rules.find(keeper.handler->identity.rule); rule != rules.end()) {
            keep.evaluation = classify_keep(*rule->second, keeper.handler->contract, find_relation);
        }
        keeps_.emplace(keeper.handler->identity, std::move(keep));
    }
}

void KeepSlotPlan::add_slots(const KeepInputs& read, const SymbolId& kept) {
    for (const auto& trait : read.traits) {
        traits_.try_emplace(make_canonical_id(trait), trait, 0);
    }
    for (const auto& field : read.fields) {
        fields_.try_emplace(keep_field_key(field), 0);
    }
    if (kept.kind != SymbolKind::Relation) {
        traits_.try_emplace(make_canonical_id(kept), kept, 0);
    }
}

KeepSlotPlan::Keep KeepSlotPlan::make_keep(KeepInputs& read, const SymbolId& kept) const {
    Keep keep{.index = static_cast<std::uint32_t>(keeps_.size()), .relations = std::move(read.relations)};
    if (kept.kind == SymbolKind::Relation) {
        keep.output_relation = kept;
    } else {
        keep.output_slot = traits_.at(make_canonical_id(kept)).second;
    }
    keep.slots.reserve(read.traits.size() + read.fields.size());
    for (const auto& trait : read.traits) {
        keep.slots.push_back(traits_.at(make_canonical_id(trait)).second);
    }
    for (const auto& field : read.fields) {
        keep.slots.push_back(fields_.at(keep_field_key(field)));
    }
    sort_unique(keep.slots);
    return keep;
}

const KeepSlotPlan::Keep* KeepSlotPlan::keep(const HandlerIdentity& handler) const {
    const auto found = keeps_.find(handler);
    return found == keeps_.end() ? nullptr : &found->second;
}

std::optional<std::uint32_t> KeepSlotPlan::trait_slot(const SymbolId& trait) const {
    const auto found = traits_.find(make_canonical_id(trait));
    return found == traits_.end() ? std::nullopt : std::optional{found->second.second};
}

std::vector<std::uint32_t> KeepSlotPlan::write_slots(const SymbolId& trait,
                                                     const std::optional<std::string>& field) const {
    const auto canonical = make_canonical_id(trait);
    std::vector<std::uint32_t> slots;
    if (field.has_value()) {
        if (const auto found = fields_.find({canonical, *field}); found != fields_.end()) {
            slots.push_back(found->second);
        }
        return slots;
    }
    for (auto it = fields_.lower_bound({canonical, std::string{}}); it != fields_.end() && it->first.first == canonical;
         ++it) {
        slots.push_back(it->second);
    }
    if (const auto slot = trait_slot(trait)) {
        slots.push_back(*slot);
    }
    return slots;
}

std::vector<std::uint32_t> KeepSlotPlan::contract_write_slots(const HandlerContract& contract) const {
    std::vector<std::uint32_t> slots;
    for (const auto& trait : contract.writes) {
        const auto& access = field_access(contract.write_fields, trait);
        if (access.all) {
            std::ranges::copy(write_slots(trait, std::nullopt), std::back_inserter(slots));
            continue;
        }
        for (const auto& field : access.fields) {
            std::ranges::copy(write_slots(trait, field), std::back_inserter(slots));
        }
    }
    sort_unique(slots);
    return slots;
}

std::string KeepSlotPlan::emit_setup(const DecoratedProgram& program) const {
    if (empty()) {
        return {};
    }
    std::string out = "namespace cactus::runtime::entt_backend {\n";
    out += "inline KeepVersions& generated_keep_versions(entt::registry& registry) {\n";
    out += "    if (auto* versions = registry.ctx().find<KeepVersions>()) {\n";
    out += "        return *versions;\n";
    out += "    }\n";
    out += "    auto& versions = registry.ctx().emplace<KeepVersions>(std::size_t{" + std::to_string(slot_count_) +
           "}, std::size_t{" + std::to_string(keeps_.size()) + "});\n";
    std::vector<const Keep*> ordered;
    ordered.reserve(keeps_.size());
    for (const auto& [identity, keep] : keeps_) {
        ordered.push_back(&keep);
    }
    std::ranges::sort(ordered, {}, &Keep::index);
    for (const auto* keep : ordered) {
        if (keep->evaluation.level != KeepLevel::Delta) {
            continue;
        }
        for (const auto slot : keep->slots) {
            out += "    versions.subscribe(" + std::to_string(slot) + ", " + std::to_string(keep->index) + ");\n";
        }
    }
    out += "    return versions;\n";
    out += "}\n\n";
    out += "template <std::uint32_t Slot>\n";
    out += "void generated_keep_input_changed(entt::registry& registry, entt::entity entity) {\n";
    out += "    generated_keep_versions(registry).touch(Slot, entity);\n";
    out += "}\n\n";
    out += "inline void generated_connect_keep_inputs([[maybe_unused]] entt::registry& registry) {\n";
    for (const auto& [canonical, entry] : traits_) {
        const auto cpp  = EnttCodegenUtils::trait_cpp_name(entry.first, entry.first.local_name, program);
        const auto slot = std::to_string(entry.second);
        for (const char* signal : {"on_construct", "on_update", "on_destroy"}) {
            out += "    registry.";
            out += signal;
            out += "<";
            out += cpp;
            out += ">().connect<&generated_keep_input_changed<";
            out += slot;
            out += ">>();\n";
        }
    }
    out += "}\n";
    out += "}  // namespace cactus::runtime::entt_backend\n\n";
    return out;
}

std::string KeepSlotPlan::touches(const std::vector<std::uint32_t>& slots,
                                  const std::string& indent,
                                  const std::string& entity) {
    std::string out;
    for (const auto slot : slots) {
        out += indent + "cactus::runtime::entt_backend::generated_keep_versions(registry).touch(" +
               std::to_string(slot) + (entity.empty() ? "" : ", " + entity) + ");\n";
    }
    return out;
}

KeepSlotScope::KeepSlotScope(const KeepSlotPlan& plan) : previous_(std::exchange(active_plan(), &plan)) {}

KeepSlotScope::~KeepSlotScope() {
    active_plan() = previous_;
}

const KeepSlotPlan* KeepSlotScope::active() {
    return active_plan();
}

}  // namespace cactus
