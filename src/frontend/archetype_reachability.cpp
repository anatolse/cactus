#include "frontend/archetype_reachability.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

const NativeCreation* native_creation(std::string_view function) {
    static const std::map<std::string, NativeCreation, std::less<>> table{
        {"std.editor.spawn_template", NativeCreation{.pub_templates = true}},
        {"std.editor.camera_enter",
         NativeCreation{
             .trait_sets = {{"std.editor.EditorCamera2D", "std.camera.flat.Camera", "std.camera.viewport.Viewport"},
                            {"std.editor.EditorCamera3D",
                             "std.camera.volume.Camera",
                             "std.camera.viewport.Viewport",
                             "std.transform.volume.WorldTransform"}}}},
        {"std.editor.palette_label_slot", NativeCreation{.trait_sets = {{"std.render.text.ScreenLabel"}}}},
    };
    const auto found = table.find(function);
    return found == table.end() ? nullptr : &found->second;
}

namespace {

using TraitIndexValue = std::uint32_t;
using Bits            = std::vector<std::uint64_t>;

constexpr std::size_t kWordBits = 64;

bool test_bit(const Bits& bits, TraitIndexValue index) {
    return ((bits[index / kWordBits] >> (index % kWordBits)) & 1U) != 0;
}

void set_bit(Bits& bits, TraitIndexValue index) {
    bits[index / kWordBits] |= std::uint64_t{1} << (index % kWordBits);
}

void clear_bit(Bits& bits, TraitIndexValue index) {
    bits[index / kWordBits] &= ~(std::uint64_t{1} << (index % kWordBits));
}

// One entity class the program can create: where it came from and which traits it carries.
struct AbstractEntity {
    std::uint32_t origin = 0;
    Bits traits;

    friend auto operator<=>(const AbstractEntity&, const AbstractEntity&) = default;
};

// Every trait of the program, numbered in canonical-id order.
class TraitIndex {
public:
    void collect(const SymbolId& trait) {
        by_canonical_.try_emplace(make_canonical_id(trait), trait);
    }

    void finish(const DecoratedProgram& program) {
        for (const auto& [canonical, symbol] : by_canonical_) {
            index_.emplace(canonical, static_cast<TraitIndexValue>(symbols_.size()));
            symbols_.push_back(symbol);
        }
        slot_of_.resize(symbols_.size());
        variants_of_.resize(symbols_.size());
        for (const auto& [_, trait] : program.traits) {
            if (!trait.state.has_value() || trait.is_state_slot() || !trait.symbol_id.has_value()) {
                continue;
            }
            const auto variant = find(*trait.symbol_id);
            const auto slot    = find(state_slot_trait(*trait.state));
            if (!variant.has_value() || !slot.has_value() || slot_of_[*variant].has_value()) {
                continue;
            }
            slot_of_[*variant] = slot;
            variants_of_[*slot].push_back(*variant);
        }
        for (auto& variants : variants_of_) {
            std::ranges::sort(variants);
        }
    }

    [[nodiscard]] std::optional<TraitIndexValue> find(const SymbolId& trait) const {
        return find(make_canonical_id(trait));
    }

    [[nodiscard]] std::optional<TraitIndexValue> find(const std::string& canonical) const {
        const auto found = index_.find(canonical);
        return found == index_.end() ? std::nullopt : std::optional(found->second);
    }

    [[nodiscard]] std::size_t size() const {
        return symbols_.size();
    }
    [[nodiscard]] std::size_t words() const {
        return (symbols_.size() + kWordBits - 1) / kWordBits;
    }
    [[nodiscard]] const SymbolId& symbol(TraitIndexValue index) const {
        return symbols_[index];
    }
    [[nodiscard]] std::optional<TraitIndexValue> slot_of(TraitIndexValue index) const {
        return slot_of_[index];
    }
    [[nodiscard]] const std::vector<TraitIndexValue>& variants_of(TraitIndexValue slot) const {
        return variants_of_[slot];
    }
    [[nodiscard]] bool is_state(TraitIndexValue index) const {
        return slot_of_[index].has_value() || !variants_of_[index].empty();
    }

private:
    std::map<std::string, SymbolId> by_canonical_;
    std::unordered_map<std::string, TraitIndexValue> index_;
    std::vector<SymbolId> symbols_;
    std::vector<std::optional<TraitIndexValue>> slot_of_;
    std::vector<std::vector<TraitIndexValue>> variants_of_;
};

struct Origin {
    ArchetypeNodeId node;
    std::string archetype;  // canonical id of node.archetype
    std::vector<TraitIndexValue> seed;
};

struct Domain {
    std::vector<TraitIndexValue> required;
    std::vector<TraitIndexValue> excluded;
};

struct NamedNeed {
    std::vector<std::uint32_t> origins;
    TraitIndexValue trait = 0;
};

struct ResolvedTarget {
    EntityTargetKind kind     = EntityTargetKind::Unknown;
    std::size_t binding_index = 0;
    std::vector<std::uint32_t> origins;  // Named and Spawned
};

struct StructuralChange {
    HandlerCommandKind kind = HandlerCommandKind::Add;  // Add or Remove; a projection is an Add
    std::optional<TraitIndexValue> trait;
    std::vector<ResolvedTarget> targets;
};

struct HandlerModel {
    const HandlerNode* node = nullptr;
    std::string canonical_id;
    Domain unary;
    Domain authored;  // `unary` without the trait a lifecycle trigger implies
    std::vector<Domain> bindings;
    std::vector<NamedNeed> named;
    std::vector<StructuralChange> changes;
    std::vector<std::uint32_t> spawned_origins;
    std::vector<std::string> emits;
};

class ReachabilityAnalysis {
public:
    ReachabilityAnalysis(const DecoratedProgram& program, const ReachabilityOptions& options)
        : program_(&program)
        , options_(options) {
        for (const auto& [_, event] : program.events) {
            if (event.is_external) {
                external_events_.insert(declaration_identity(event));
            }
        }
        index_traits();
        build_origins();
        build_handlers();
    }

    void run() {
        std::vector<bool> initially_free(traits_.size(), false);
        while (const auto split = fixpoint(initially_free)) {
            initially_free[*split] = true;
            widened_.push_back(*split);
        }
    }

    // One error per mistake: a self-contradictory domain anywhere, then, in user
    // modules only, a dead domain before a dead filter, missing producer or named need.
    void report(ErrorReporter& errors) const {
        std::set<std::string> reported;
        const auto emit = [&](const HandlerModel& handler, const std::string& message) {
            if (reported.insert(message).second) {
                errors.error(handler.node->location, message);
            }
        };
        for (const auto& handler : handlers_) {
            const auto rule = "rule '" + make_canonical_id(handler.node->identity.rule) + "'";
            if (const auto reason = contradiction(handler); reason.has_value()) {
                emit(handler, rule + " can never match: " + *reason);
                continue;
            }
            if (module_name_is_stdlib(handler.node->identity.rule.module.name)) {
                continue;
            }
            const auto label = "handler '" + handler.canonical_id + "' never runs: ";
            if (const auto reason = dead_domain(handler, handler.authored); reason.has_value()) {
                emit(handler, rule + " never runs: " + *reason);
                continue;
            }
            if (!trigger_produced(handler)) {
                emit(handler, label + "nothing produces '" + handler.node->identity.trigger.source_spelling() + "'");
                continue;
            }
            if (const auto reason = dead_domain(handler, handler.unary); reason.has_value()) {
                emit(handler, label + *reason);
                continue;
            }
            for (const auto excluded : dead_exclusions(handler)) {
                emit(handler,
                     rule + ": `without: " + canonical(excluded) +
                         "` excludes nothing; no reachable archetype matching the rule carries it");
            }
            for (const auto& need : handler.named) {
                if (!named_need_met(need)) {
                    emit(handler,
                         label + "entity '" + named_entity(need) + "' never carries '" + canonical(need.trait) + "'");
                }
            }
        }
        for (const auto& conflict : spawn_order_conflicts()) {
            const auto& first  = handlers_[conflict.first];
            const auto& second = handlers_[conflict.second];
            const auto& reader = handlers_[conflict.reader];
            emit(second,
                 "the order of spawns by '" + first.canonical_id + "' and '" + second.canonical_id +
                     "' is observable by '" + reader.canonical_id +
                     "', whose result depends on row order; add 'after: " +
                     make_canonical_id(first.node->identity.rule) + "' to " + second.canonical_id +
                     ", or 'after: " + make_canonical_id(second.node->identity.rule) + "' to " + first.canonical_id);
        }
    }

    // ── Spawn order ─────────────────────────────────────────────────────────

    struct SpawnOrderConflict {
        std::size_t first  = 0;
        std::size_t second = 0;
        std::size_t reader = 0;
    };

    [[nodiscard]] std::optional<std::size_t> handler_index(const HandlerIdentity& identity) const {
        const auto canonical = identity.canonical_id();
        const auto found     = std::ranges::lower_bound(handlers_, canonical, {}, &HandlerModel::canonical_id);
        return found == handlers_.end() || found->canonical_id != canonical
                   ? std::nullopt
                   : std::optional(static_cast<std::size_t>(found - handlers_.begin()));
    }

    // The activations a handler can run in, named by the phase or host event that starts them.
    [[nodiscard]] std::vector<std::set<std::string>> activations() const {
        std::vector<std::set<std::string>> roots(handlers_.size());
        for (std::size_t index = 0; index < handlers_.size(); ++index) {
            if (reachable_[index] && starts_activation(handlers_[index])) {
                roots[index].insert(make_canonical_id(handlers_[index].node->identity.trigger.symbol));
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t index = 0; index < handlers_.size(); ++index) {
                changed |= reachable_[index] && inherit_activations(roots, index);
            }
        }
        return roots;
    }

    // Adds the activations of every reachable handler that feeds handler `index`.
    bool inherit_activations(std::vector<std::set<std::string>>& roots, std::size_t index) const {
        bool changed = false;
        for (std::size_t source = 0; source < handlers_.size(); ++source) {
            if (!reachable_[source] || !feeds(handlers_[source], handlers_[index])) {
                continue;
            }
            for (const auto& activation : roots[source]) {
                changed |= roots[index].insert(activation).second;
            }
        }
        return changed;
    }

    // Whether `source` can deliver `target`'s trigger: by emitting its event, or,
    // for a lifecycle trigger, by any structural command or spawn.
    [[nodiscard]] static bool feeds(const HandlerModel& source, const HandlerModel& target) {
        const auto& trigger = target.node->identity.trigger;
        if (trigger.kind == HandlerTriggerKind::Event) {
            return std::ranges::binary_search(source.emits, make_canonical_id(trigger.symbol));
        }
        return trigger.is_lifecycle() && (!source.changes.empty() || !source.spawned_origins.empty());
    }

    // Handlers reachable from each handler over schedule and event-flow edges.
    [[nodiscard]] std::vector<std::vector<bool>> orderings() const {
        std::vector<std::vector<std::size_t>> next(handlers_.size());
        const auto link = [&](const HandlerIdentity& before, const HandlerIdentity& after) {
            const auto from = handler_index(before);
            const auto to   = handler_index(after);
            if (from.has_value() && to.has_value()) {
                next[*from].push_back(*to);
            }
        };
        for (const auto& edge : program_->execution_graph.schedule_edges) {
            link(edge.before, edge.after);
        }
        for (const auto& flow : program_->execution_graph.event_flows) {
            link(flow.producer, flow.consumer);
        }
        std::vector<std::vector<bool>> reaches(handlers_.size(), std::vector<bool>(handlers_.size(), false));
        for (std::size_t start = 0; start < handlers_.size(); ++start) {
            if (handlers_[start].spawned_origins.empty()) {
                continue;
            }
            std::vector<std::size_t> pending{start};
            while (!pending.empty()) {
                const auto current = pending.back();
                pending.pop_back();
                for (const auto successor : next[current]) {
                    if (!reaches[start][successor]) {
                        reaches[start][successor] = true;
                        pending.push_back(successor);
                    }
                }
            }
        }
        return reaches;
    }

    [[nodiscard]] static std::vector<const Domain*> row_domains(const HandlerModel& handler) {
        switch (handler.node->contract.domain_kind) {
            case HandlerDomainKind::Selectionless:
                return {};
            case HandlerDomainKind::Unary:
                return {&handler.unary};
            case HandlerDomainKind::Pair: {
                std::vector<const Domain*> domains;
                domains.reserve(handler.bindings.size());
                for (const auto& binding : handler.bindings) {
                    domains.push_back(&binding);
                }
                return domains;
            }
        }
        std::unreachable();
    }

    [[nodiscard]] bool domain_meets(const Domain& domain, const std::vector<std::uint32_t>& origins) const {
        return std::ranges::any_of(entities_, [&](const AbstractEntity& entity) {
            return std::ranges::contains(origins, entity.origin) && matches(entity.traits, domain);
        });
    }

    // A reader whose rows can come from both spawners and whose result depends on their order.
    [[nodiscard]] std::optional<std::size_t> order_observer(std::size_t first, std::size_t second) const {
        for (std::size_t index = 0; index < handlers_.size(); ++index) {
            const auto& reader = handlers_[index];
            if (!reachable_[index] || reader.node->contract.iteration_order != IterationOrder::Creation) {
                continue;
            }
            const auto domains = row_domains(reader);
            if (std::ranges::any_of(domains, [&](const Domain* domain) {
                    return domain_meets(*domain, handlers_[first].spawned_origins) &&
                           domain_meets(*domain, handlers_[second].spawned_origins);
                })) {
                return index;
            }
        }
        return std::nullopt;
    }

    // Whether a phase or host-event handler starts the activation that runs it.
    [[nodiscard]] bool starts_activation(const HandlerModel& handler) const {
        const auto& trigger = handler.node->identity.trigger;
        switch (trigger.kind) {
            case HandlerTriggerKind::Phase:
            case HandlerTriggerKind::RenderStage:
                return true;
            case HandlerTriggerKind::Event:
                return event_is_external(trigger.symbol) || is_boundary_event(trigger.symbol);
            case HandlerTriggerKind::TraitAdded:
            case HandlerTriggerKind::TraitRemoved:
                return false;
        }
        std::unreachable();
    }

    // Lifecycle handlers run in commit rounds, after every handler that starts the activation.
    [[nodiscard]] bool commit_ordered(std::size_t first, std::size_t second) const {
        const auto lifecycle = [&](std::size_t index) {
            return handlers_[index].node->identity.trigger.is_lifecycle();
        };
        return (lifecycle(first) && starts_activation(handlers_[second])) ||
               (lifecycle(second) && starts_activation(handlers_[first]));
    }

    [[nodiscard]] std::vector<SpawnOrderConflict> spawn_order_conflicts() const {
        const auto roots   = activations();
        const auto reaches = orderings();
        std::vector<std::size_t> spawners;
        for (std::size_t index = 0; index < handlers_.size(); ++index) {
            if (reachable_[index] && !handlers_[index].spawned_origins.empty()) {
                spawners.push_back(index);
            }
        }
        std::vector<SpawnOrderConflict> conflicts;
        for (std::size_t left = 0; left < spawners.size(); ++left) {
            for (std::size_t right = left + 1; right < spawners.size(); ++right) {
                const auto first  = spawners[left];
                const auto second = spawners[right];
                const bool shared = std::ranges::any_of(
                    roots[first], [&](const std::string& activation) { return roots[second].contains(activation); });
                if (!shared || reaches[first][second] || reaches[second][first] || commit_ordered(first, second)) {
                    continue;
                }
                if (const auto reader = order_observer(first, second); reader.has_value()) {
                    conflicts.push_back(SpawnOrderConflict{.first = first, .second = second, .reader = *reader});
                }
            }
        }
        return conflicts;
    }

    [[nodiscard]] ReachabilityFacts facts() const {
        ReachabilityFacts facts{.derived = true};
        std::set<std::vector<TraitIndexValue>> all_sets;
        std::vector<std::set<std::vector<TraitIndexValue>>> per_origin(origins_.size());
        for (const auto& entity : entities_) {
            auto members = members_of(entity.traits);
            all_sets.insert(members);
            per_origin[entity.origin].insert(std::move(members));
        }
        for (const auto& members : all_sets) {
            facts.trait_sets.push_back(symbols_of(members));
        }
        for (std::size_t origin = 0; origin < origins_.size(); ++origin) {
            if (per_origin[origin].empty()) {
                continue;
            }
            OriginReachability reach{.node = origins_[origin].node};
            for (const auto& members : per_origin[origin]) {
                reach.trait_sets.push_back(symbols_of(members));
            }
            facts.origins.push_back(std::move(reach));
        }
        for (TraitIndexValue trait = 0; trait < traits_.size(); ++trait) {
            if (free_[trait]) {
                facts.free_traits.push_back(traits_.symbol(trait));
            }
        }
        auto widened = widened_;
        std::ranges::sort(widened);
        for (const auto trait : widened) {
            facts.widened_traits.push_back(traits_.symbol(trait));
        }
        for (std::size_t handler = 0; handler < handlers_.size(); ++handler) {
            if (!reachable_[handler]) {
                facts.unreachable_handlers.push_back(handlers_[handler].node->identity);
            }
        }
        return facts;
    }

private:
    void collect_targets_of(const HandlerContract& contract) {
        const auto collect = [this](const std::vector<SymbolId>& traits) {
            for (const auto& trait : traits) {
                traits_.collect(trait);
            }
        };
        collect(contract.selection);
        collect(contract.exclusion);
        for (const auto& binding : contract.pair_bindings) {
            collect(binding.required_traits);
        }
        for (const auto& requirement : contract.named_requirements) {
            traits_.collect(requirement.trait);
        }
        for (const auto& command : contract.commands) {
            if (command.target.has_value() && command.kind != HandlerCommandKind::Spawn) {
                traits_.collect(*command.target);
            }
        }
        for (const auto& projection : contract.projections) {
            traits_.collect(projection.trait);
        }
    }

    void index_traits() {
        for (const auto& [_, trait] : program_->traits) {
            if (trait.symbol_id.has_value()) {
                traits_.collect(*trait.symbol_id);
            }
        }
        for (const auto& archetype : program_->persistence.archetypes) {
            for (const auto& baseline : archetype.baseline_traits) {
                traits_.collect(baseline.trait);
            }
        }
        for (const auto& handler : program_->execution_graph.handlers) {
            collect_targets_of(handler.contract);
            for (const auto& function : handler.contract.native_spawns) {
                native_functions_.emplace(make_canonical_id(function), function);
            }
        }
        parent_trait_ = find_parent_trait();
        if (parent_trait_.has_value()) {
            traits_.collect(*parent_trait_);
        }
        traits_.finish(*program_);
    }

    [[nodiscard]] std::optional<SymbolId> find_parent_trait() const {
        std::optional<SymbolId> local;
        for (const auto& [_, trait] : program_->traits) {
            if (trait.name != "Parent" || !trait.symbol_id.has_value()) {
                continue;
            }
            if (trait.symbol_id->module.name == "std.core") {
                return trait.symbol_id;
            }
            local = trait.symbol_id;
        }
        return local;
    }

    void build_origins() {
        auto archetypes = program_->persistence.archetypes;
        std::ranges::sort(archetypes, {}, [](const PersistenceArchetypeDescriptor& archetype) {
            return canonical_node_string(archetype.node);
        });
        const auto parent = parent_trait_.has_value() ? traits_.find(*parent_trait_) : std::nullopt;
        for (const auto& archetype : archetypes) {
            Origin origin{.node = archetype.node, .archetype = make_canonical_id(archetype.node.archetype)};
            for (const auto& baseline : archetype.baseline_traits) {
                if (const auto trait = traits_.find(baseline.trait); trait.has_value()) {
                    origin.seed.push_back(*trait);
                }
            }
            if (!archetype.node.role_path.empty() && parent.has_value()) {
                origin.seed.push_back(*parent);
            }
            origins_.push_back(std::move(origin));
        }
        for (const auto& [canonical, function] : native_functions_) {
            add_native_origins(canonical, function);
        }
    }

    // One origin per entity kind a native function creates; a kind using a trait
    // the program lacks is never created.
    void add_native_origins(const std::string& canonical, const SymbolId& function) {
        const auto* creation = native_creation(canonical);
        for (std::size_t kind = 0; creation != nullptr && kind < creation->trait_sets.size(); ++kind) {
            Origin origin{.node      = ArchetypeNodeId{.archetype = function, .role_path = {std::to_string(kind)}},
                          .archetype = canonical};
            for (const auto& trait : creation->trait_sets[kind]) {
                if (const auto index = traits_.find(trait); index.has_value()) {
                    origin.seed.push_back(*index);
                }
            }
            if (origin.seed.size() == creation->trait_sets[kind].size()) {
                origins_.push_back(std::move(origin));
            }
        }
    }

    [[nodiscard]] std::vector<std::uint32_t> native_origins(const SymbolId& function) const {
        auto created         = origins_of(function, false);
        const auto* creation = native_creation(make_canonical_id(function));
        if (creation == nullptr || !creation->pub_templates) {
            return created;
        }
        for (std::uint32_t origin = 0; origin < origins_.size(); ++origin) {
            const auto& archetype = origins_[origin].node.archetype;
            if (archetype.kind == SymbolKind::Template && program_->pub_templates.contains(archetype.local_name)) {
                created.push_back(origin);
            }
        }
        return created;
    }

    [[nodiscard]] std::vector<std::uint32_t> origins_of(const SymbolId& archetype, bool root_only) const {
        const auto canonical = make_canonical_id(archetype);
        std::vector<std::uint32_t> found;
        for (std::uint32_t origin = 0; origin < origins_.size(); ++origin) {
            if (origins_[origin].archetype == canonical && (!root_only || origins_[origin].node.role_path.empty())) {
                found.push_back(origin);
            }
        }
        return found;
    }

    [[nodiscard]] std::vector<TraitIndexValue> indices_of(const std::vector<SymbolId>& traits) const {
        std::vector<TraitIndexValue> indices;
        for (const auto& trait : traits) {
            if (const auto index = traits_.find(trait); index.has_value()) {
                indices.push_back(*index);
            }
        }
        return indices;
    }

    [[nodiscard]] std::vector<ResolvedTarget> resolve_targets(const std::vector<EntityTarget>& targets) const {
        std::vector<ResolvedTarget> resolved;
        for (const auto& target : targets) {
            ResolvedTarget entry{.kind = target.kind, .binding_index = target.binding_index};
            if (target.symbol.has_value() &&
                (target.kind == EntityTargetKind::Named || target.kind == EntityTargetKind::Spawned)) {
                entry.origins = origins_of(*target.symbol, true);
            }
            resolved.push_back(std::move(entry));
        }
        return resolved;
    }

    void build_handlers() {
        std::vector<const HandlerNode*> nodes;
        nodes.reserve(program_->execution_graph.handlers.size());
        for (const auto& handler : program_->execution_graph.handlers) {
            nodes.push_back(&handler);
        }
        std::ranges::sort(nodes, {}, [](const HandlerNode* handler) { return handler->identity.canonical_id(); });
        for (const auto* node : nodes) {
            const auto& contract = node->contract;
            HandlerModel model{.node = node, .canonical_id = node->identity.canonical_id()};
            model.unary =
                Domain{.required = indices_of(contract.selection), .excluded = indices_of(contract.exclusion)};
            model.authored      = model.unary;
            const auto& trigger = node->identity.trigger;
            if (const auto implied = traits_.find(trigger.symbol); trigger.is_lifecycle() && implied.has_value()) {
                auto& side =
                    trigger.kind == HandlerTriggerKind::TraitAdded ? model.authored.required : model.authored.excluded;
                std::erase(side, *implied);
            }
            for (const auto& binding : contract.pair_bindings) {
                model.bindings.push_back(Domain{.required = indices_of(binding.required_traits)});
            }
            for (const auto& requirement : contract.named_requirements) {
                if (const auto trait = traits_.find(requirement.trait); trait.has_value()) {
                    model.named.push_back(NamedNeed{.origins = origins_of(requirement.entity, true), .trait = *trait});
                }
            }
            for (const auto& command : contract.commands) {
                add_command(model, command);
            }
            for (const auto& projection : contract.projections) {
                model.changes.push_back(StructuralChange{.kind    = HandlerCommandKind::Add,
                                                         .trait   = traits_.find(projection.trait),
                                                         .targets = resolve_targets(projection.entities)});
            }
            for (const auto& function : contract.native_spawns) {
                const auto created = native_origins(function);
                model.spawned_origins.insert(model.spawned_origins.end(), created.begin(), created.end());
            }
            for (const auto& event : contract.emits) {
                model.emits.push_back(make_canonical_id(event));
            }
            std::ranges::sort(model.emits);
            handlers_.push_back(std::move(model));
        }
    }

    void add_command(HandlerModel& model, const InferredHandlerCommand& command) {
        switch (command.kind) {
            case HandlerCommandKind::Spawn:
                if (command.target.has_value()) {
                    const auto spawned = origins_of(*command.target, false);
                    model.spawned_origins.insert(model.spawned_origins.end(), spawned.begin(), spawned.end());
                }
                return;
            case HandlerCommandKind::Set:
            case HandlerCommandKind::Destroy:  // destroying an entity fires no lifecycle trigger
                return;
            case HandlerCommandKind::Add:
            case HandlerCommandKind::Remove:
                model.changes.push_back(
                    StructuralChange{.kind  = command.kind,
                                     .trait = command.target.has_value() ? traits_.find(*command.target) : std::nullopt,
                                     .targets = resolve_targets(command.entities)});
                return;
        }
        std::unreachable();
    }

    // ── Diagnostics ─────────────────────────────────────────────────────────

    [[nodiscard]] std::string canonical(TraitIndexValue trait) const {
        return make_canonical_id(traits_.symbol(trait));
    }

    [[nodiscard]] std::string named_entity(const NamedNeed& need) const {
        return need.origins.empty() ? std::string{"?"}
                                    : make_canonical_id(origins_[need.origins.front()].node.archetype);
    }

    [[nodiscard]] std::optional<std::string> two_variants(const std::vector<TraitIndexValue>& required) const {
        for (const auto first : required) {
            const auto slot = traits_.slot_of(first);
            for (const auto second : required) {
                if (first < second && slot.has_value() && slot == traits_.slot_of(second)) {
                    return "it requires two variants of state '" + canonical(*slot) + "': '" + canonical(first) +
                           "' and '" + canonical(second) + "'";
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> contradiction(const HandlerModel& handler) const {
        for (const auto& binding : handler.bindings) {
            if (auto reason = two_variants(binding.required); reason.has_value()) {
                return reason;
            }
        }
        const auto& domain = handler.unary;
        for (const auto required : domain.required) {
            const auto slot = traits_.slot_of(required);
            for (const auto excluded : domain.excluded) {
                if (excluded == required) {
                    return "it requires and excludes '" + canonical(required) + "'";
                }
                if (slot == excluded) {
                    return "it requires '" + canonical(required) + "' and excludes its state '" + canonical(excluded) +
                           "'";
                }
            }
        }
        return two_variants(domain.required);
    }

    [[nodiscard]] std::string joined(const std::vector<TraitIndexValue>& traits) const {
        std::string text;
        for (std::size_t position = 0; position < traits.size(); ++position) {
            if (position > 0) {
                text += position + 1 == traits.size() ? " and " : ", ";
            }
            text += canonical(traits[position]);
        }
        return text;
    }

    [[nodiscard]] std::optional<std::string> dead_domain(const HandlerModel& handler, const Domain& domain) const {
        switch (handler.node->contract.domain_kind) {
            case HandlerDomainKind::Selectionless:
                return std::nullopt;
            case HandlerDomainKind::Unary: {
                if (domain.required.empty() && domain.excluded.empty()) {
                    return std::nullopt;
                }
                if (any_matches(domain)) {
                    return std::nullopt;
                }
                const auto* together = domain.required.size() > 1 ? " together" : "";
                if (domain.excluded.empty() || !any_matches(Domain{.required = domain.required})) {
                    return "no reachable archetype carries " + joined(domain.required) + together;
                }
                return "no reachable archetype carries " + joined(domain.required) + " without " +
                       joined(domain.excluded);
            }
            case HandlerDomainKind::Pair:
                for (std::size_t binding = 0; binding < handler.bindings.size(); ++binding) {
                    if (!any_matches(handler.bindings[binding])) {
                        return "pair binding '" + handler.node->contract.pair_bindings[binding].name +
                               "' matches no reachable archetype (it needs " +
                               joined(handler.bindings[binding].required) + ")";
                    }
                }
                return std::nullopt;
        }
        std::unreachable();
    }

    // `without:` traits no entity of the `each:` domain may carry; the trait an
    // `on removed` trigger implies is not a filter the author wrote.
    [[nodiscard]] std::vector<TraitIndexValue> dead_exclusions(const HandlerModel& handler) const {
        std::vector<TraitIndexValue> dead;
        const Domain selected{.required = handler.unary.required};
        for (const auto excluded : handler.authored.excluded) {
            const auto carried = std::ranges::any_of(entities_, [&](const AbstractEntity& entity) {
                return matches(entity.traits, selected) && may_carry(entity.traits, excluded);
            });
            if (!carried) {
                dead.push_back(excluded);
            }
        }
        return dead;
    }

    // ── Matching ────────────────────────────────────────────────────────────

    [[nodiscard]] bool may_carry(const Bits& bits, TraitIndexValue trait) const {
        return test_bit(bits, trait) || free_[trait];
    }

    [[nodiscard]] bool matches(const Bits& bits, const Domain& domain) const {
        return std::ranges::all_of(domain.required, [&](TraitIndexValue trait) { return may_carry(bits, trait); }) &&
               std::ranges::none_of(domain.excluded, [&](TraitIndexValue trait) { return test_bit(bits, trait); });
    }

    [[nodiscard]] bool any_matches(const Domain& domain) const {
        return std::ranges::any_of(entities_,
                                   [&](const AbstractEntity& entity) { return matches(entity.traits, domain); });
    }

    [[nodiscard]] bool domain_matches(const HandlerModel& handler) const {
        // A global reduction runs once even over an empty domain.
        if (const auto& reduction = handler.node->contract.reduction;
            reduction.has_value() && !reduction->group_binding.has_value()) {
            return true;
        }
        switch (handler.node->contract.domain_kind) {
            case HandlerDomainKind::Selectionless:
                return true;
            case HandlerDomainKind::Unary:
                return any_matches(handler.unary);
            case HandlerDomainKind::Pair:
                return std::ranges::all_of(handler.bindings,
                                           [&](const Domain& binding) { return any_matches(binding); });
        }
        std::unreachable();
    }

    [[nodiscard]] bool named_need_met(const NamedNeed& need) const {
        return std::ranges::any_of(entities_, [&](const AbstractEntity& entity) {
            return std::ranges::contains(need.origins, entity.origin) && may_carry(entity.traits, need.trait);
        });
    }

    [[nodiscard]] bool named_needs_met(const HandlerModel& handler) const {
        return std::ranges::all_of(handler.named, [&](const NamedNeed& need) { return named_need_met(need); });
    }

    [[nodiscard]] bool is_boundary_event(const SymbolId& event) const {
        if (event.local_name != "load" && event.local_name != "unload") {
            return false;
        }
        return event.module.name == "std.core" || !program_->events.contains("std.core." + event.local_name);
    }

    [[nodiscard]] bool event_is_external(const SymbolId& event) const {
        return external_events_.contains(make_canonical_id(event));
    }

    [[nodiscard]] bool trigger_produced(const HandlerModel& handler) const {
        const auto& trigger = handler.node->identity.trigger;
        switch (trigger.kind) {
            case HandlerTriggerKind::Phase:
            case HandlerTriggerKind::RenderStage:
                return true;
            case HandlerTriggerKind::Event:
                return starts_activation(handler) || events_.contains(make_canonical_id(trigger.symbol));
            case HandlerTriggerKind::TraitAdded:
            case HandlerTriggerKind::TraitRemoved: {
                const auto trait = traits_.find(trigger.symbol);
                const auto& seen = trigger.kind == HandlerTriggerKind::TraitAdded ? gained_ : lost_;
                return trait.has_value() && seen[*trait];
            }
        }
        std::unreachable();
    }

    // ── Fixpoint ────────────────────────────────────────────────────────────

    [[nodiscard]] Bits normalized(Bits bits) const {
        for (TraitIndexValue trait = 0; trait < traits_.size(); ++trait) {
            if (free_[trait]) {
                clear_bit(bits, trait);
            } else if (const auto slot = traits_.slot_of(trait); slot.has_value() && test_bit(bits, trait)) {
                set_bit(bits, *slot);
            }
        }
        return bits;
    }

    static bool mark(std::vector<bool>& flags, TraitIndexValue trait) {
        if (flags[trait]) {
            return false;
        }
        flags[trait] = true;
        return true;
    }

    bool insert_entity(std::uint32_t origin, Bits bits) {
        return entities_.insert(AbstractEntity{.origin = origin, .traits = normalized(std::move(bits))}).second;
    }

    bool seed_origin(std::uint32_t origin) {
        if (seeded_[origin]) {
            return false;
        }
        seeded_[origin] = true;
        Bits bits(traits_.words(), 0);
        for (const auto trait : origins_[origin].seed) {
            set_bit(bits, trait);
            mark(gained_, trait);
        }
        insert_entity(origin, std::move(bits));
        return true;
    }

    // A free trait leaves every bitset, so entities that differed only in it merge.
    bool make_free(TraitIndexValue trait) {
        if (free_[trait]) {
            return false;
        }
        free_[trait] = true;
        std::set<AbstractEntity> merged;
        for (auto entity : entities_) {
            clear_bit(entity.traits, trait);
            merged.insert(std::move(entity));
        }
        entities_ = std::move(merged);
        return true;
    }

    [[nodiscard]] std::vector<const AbstractEntity*> select(const HandlerModel& handler,
                                                            const ResolvedTarget& target) const {
        std::vector<const AbstractEntity*> selected;
        for (const auto& entity : entities_) {
            if (target_covers(handler, target, entity)) {
                selected.push_back(&entity);
            }
        }
        return selected;
    }

    [[nodiscard]] bool target_covers(const HandlerModel& handler,
                                     const ResolvedTarget& target,
                                     const AbstractEntity& entity) const {
        switch (target.kind) {
            case EntityTargetKind::Self:
                return handler.node->contract.domain_kind != HandlerDomainKind::Unary ||
                       matches(entity.traits, handler.unary);
            case EntityTargetKind::PairBinding:
                return target.binding_index >= handler.bindings.size() ||
                       matches(entity.traits, handler.bindings[target.binding_index]);
            case EntityTargetKind::Named:
            case EntityTargetKind::Spawned:
                return std::ranges::contains(target.origins, entity.origin);
            case EntityTargetKind::Unknown:
                return true;
        }
        std::unreachable();
    }

    // The entity after an `add` of `trait`; records what it gains and loses.
    Bits added(const Bits& bits, TraitIndexValue trait, bool& changed) {
        auto result = bits;
        if (const auto slot = traits_.slot_of(trait); slot.has_value()) {
            for (const auto variant : traits_.variants_of(*slot)) {
                if (variant != trait && test_bit(result, variant)) {
                    changed |= mark(lost_, variant);
                    clear_bit(result, variant);
                }
            }
            set_bit(result, *slot);
        }
        if (!test_bit(result, trait)) {
            changed |= mark(gained_, trait);
        }
        set_bit(result, trait);
        return result;
    }

    // The entity after a `remove` of `trait`; a state's slot or variant takes the whole state.
    Bits removed(const Bits& bits, TraitIndexValue trait, bool& changed) {
        auto result = bits;
        if (!test_bit(result, trait)) {
            return result;
        }
        const auto slot = traits_.variants_of(trait).empty() ? traits_.slot_of(trait) : std::optional(trait);
        std::vector<TraitIndexValue> dropped{trait};
        if (slot.has_value()) {
            dropped = traits_.variants_of(*slot);
            dropped.push_back(*slot);
        }
        for (const auto drop : dropped) {
            if (test_bit(result, drop)) {
                changed |= mark(lost_, drop);
                clear_bit(result, drop);
            }
        }
        return result;
    }

    bool mark_change(const StructuralChange& change, TraitIndexValue trait) {
        return change.kind == HandlerCommandKind::Add ? mark(gained_, trait) : mark(lost_, trait);
    }

    [[nodiscard]] AbstractEntity changed_entity(const AbstractEntity& entity,
                                                const StructuralChange& change,
                                                TraitIndexValue trait,
                                                bool& changed) {
        return AbstractEntity{.origin = entity.origin,
                              .traits = change.kind == HandlerCommandKind::Add
                                            ? added(entity.traits, trait, changed)
                                            : removed(entity.traits, trait, changed)};
    }

    bool insert_entities(std::vector<AbstractEntity>& fresh) {
        bool changed = false;
        for (auto& entity : fresh) {
            changed |= insert_entity(entity.origin, std::move(entity.traits));
        }
        return changed;
    }

    bool apply_unknown(const StructuralChange& change, TraitIndexValue trait) {
        if (!traits_.is_state(trait)) {
            const bool changed = mark_change(change, trait);
            return make_free(trait) || changed;
        }
        bool changed = false;
        std::vector<AbstractEntity> fresh;
        fresh.reserve(entities_.size());
        for (const auto& entity : entities_) {
            fresh.push_back(changed_entity(entity, change, trait, changed));
        }
        return insert_entities(fresh) || changed;
    }

    bool apply_change(const HandlerModel& handler, const StructuralChange& change) {
        if (!change.trait.has_value()) {
            return false;
        }
        const auto trait = *change.trait;
        bool changed     = false;
        std::vector<AbstractEntity> fresh;
        for (const auto& target : change.targets) {
            if (target.kind == EntityTargetKind::Unknown) {
                changed |= apply_unknown(change, trait);
                continue;
            }
            const auto selected = select(handler, target);
            if (!selected.empty() && free_[trait]) {
                changed |= mark_change(change, trait);
                continue;
            }
            for (const auto* entity : selected) {
                fresh.push_back(changed_entity(*entity, change, trait, changed));
            }
        }
        return insert_entities(fresh) || changed;
    }

    bool activate(std::size_t handler_index) {
        auto& handler = handlers_[handler_index];
        if (reachable_[handler_index] || !trigger_produced(handler) || !domain_matches(handler) ||
            !named_needs_met(handler)) {
            return false;
        }
        reachable_[handler_index] = true;
        events_.insert(handler.emits.begin(), handler.emits.end());
        for (const auto origin : handler.spawned_origins) {
            seed_origin(origin);
        }
        return true;
    }

    // The non-state trait whose removal merges the most entities; ties go to the lowest canonical id.
    [[nodiscard]] std::optional<TraitIndexValue> splitting_trait() const {
        std::optional<TraitIndexValue> best;
        std::size_t best_score = 0;
        for (TraitIndexValue trait = 0; trait < traits_.size(); ++trait) {
            if (free_[trait] || traits_.is_state(trait)) {
                continue;
            }
            std::size_t score = 0;
            for (const auto& entity : entities_) {
                if (!test_bit(entity.traits, trait)) {
                    continue;
                }
                auto without = entity;
                clear_bit(without.traits, trait);
                score += entities_.contains(without) ? 2 : 1;
            }
            if (score > best_score) {
                best       = trait;
                best_score = score;
            }
        }
        return best;
    }

    // Returns a trait to make free when the entity count passes the bound.
    std::optional<TraitIndexValue> fixpoint(const std::vector<bool>& initially_free) {
        free_      = initially_free;
        gained_    = std::vector<bool>(traits_.size(), false);
        lost_      = std::vector<bool>(traits_.size(), false);
        seeded_    = std::vector<bool>(origins_.size(), false);
        reachable_ = std::vector<bool>(handlers_.size(), false);
        entities_.clear();
        events_.clear();
        for (std::uint32_t origin = 0; origin < origins_.size(); ++origin) {
            if (origins_[origin].node.archetype.kind == SymbolKind::Entity) {
                seed_origin(origin);
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t handler = 0; handler < handlers_.size(); ++handler) {
                changed |= activate(handler);
            }
            for (std::size_t handler = 0; handler < handlers_.size(); ++handler) {
                if (!reachable_[handler]) {
                    continue;
                }
                for (const auto& change : handlers_[handler].changes) {
                    changed |= apply_change(handlers_[handler], change);
                }
            }
            if (entities_.size() > options_.widening_bound) {
                if (const auto split = splitting_trait(); split.has_value()) {
                    return split;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<TraitIndexValue> members_of(const Bits& bits) const {
        std::vector<TraitIndexValue> members;
        for (TraitIndexValue trait = 0; trait < traits_.size(); ++trait) {
            if (test_bit(bits, trait)) {
                members.push_back(trait);
            }
        }
        return members;
    }

    [[nodiscard]] ReachableTraitSet symbols_of(const std::vector<TraitIndexValue>& members) const {
        ReachableTraitSet symbols;
        symbols.reserve(members.size());
        for (const auto trait : members) {
            symbols.push_back(traits_.symbol(trait));
        }
        return symbols;
    }

    const DecoratedProgram* program_;
    ReachabilityOptions options_;
    TraitIndex traits_;
    std::optional<SymbolId> parent_trait_;
    std::map<std::string, SymbolId> native_functions_;  // called functions that create entities
    std::vector<Origin> origins_;
    std::vector<HandlerModel> handlers_;

    std::set<AbstractEntity> entities_;
    std::vector<bool> free_;
    std::vector<bool> gained_;
    std::vector<bool> lost_;
    std::vector<bool> seeded_;
    std::vector<bool> reachable_;
    std::set<std::string> events_;
    std::set<std::string> external_events_;
    std::vector<TraitIndexValue> widened_;
};

}  // namespace

void derive_archetype_reachability(DecoratedProgram& program,
                                   ErrorReporter& errors,
                                   const ReachabilityOptions& options) {
    ReachabilityAnalysis analysis(program, options);
    analysis.run();
    analysis.report(errors);
    program.reachability = analysis.facts();
}

}  // namespace cactus
