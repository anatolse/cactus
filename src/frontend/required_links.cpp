#include "frontend/required_links.hpp"

#include "frontend/hierarchy_forest.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace cactus {

namespace {

// A `remove` of `removed` takes `trait` away: the trait itself, or any part of its state.
bool removal_takes(const SymbolId& removed, const SymbolId& trait) {
    const auto state = state_of_variant_trait(trait);
    return removed == trait || (state.has_value() && (removed == state_slot_trait(*state) || same_state(removed, trait)));
}

// Whether `command` may take `target` away from some entity.
bool may_lose(const InferredHandlerCommand& command, const SymbolId& target) {
    switch (command.kind) {
        case HandlerCommandKind::Destroy:
            return true;
        case HandlerCommandKind::Remove:
            return command.target.has_value() && removal_takes(*command.target, target);
        case HandlerCommandKind::Add:
            return command.target.has_value() && *command.target != target && same_state(*command.target, target);
        case HandlerCommandKind::Spawn:
        case HandlerCommandKind::Set:
            return false;
    }
    std::unreachable();
}

// How control leaves a statement list of an `on lost` handler.
enum class PathEnd : std::uint8_t { Resolves, FallsThrough, Exits };

bool targets_self(const std::optional<std::unique_ptr<ExprNode>>& target) {
    return !target.has_value() || std::holds_alternative<SelfExpr>((*target)->expr);
}

bool resolving_command(const StmtNode& stmt, const LostLink& link) {
    if (const auto* add = std::get_if<AddTraitStmt>(&stmt.stmt); add != nullptr && targets_self(add->target_expr)) {
        const auto& added = add->resolved_trait_id;
        const bool assigns =
            std::ranges::contains(add->args, link.field, &FieldAssignment::name);
        return added.has_value() &&
               ((*added == link.holder && assigns) || (*added != link.holder && same_state(*added, link.holder)));
    }
    if (const auto* remove = std::get_if<RemoveTraitStmt>(&stmt.stmt);
        remove != nullptr && targets_self(remove->target_expr)) {
        return remove->resolved_trait_id.has_value() && removal_takes(*remove->resolved_trait_id, link.holder);
    }
    const auto* destroy = std::get_if<DestroyStmt>(&stmt.stmt);
    return destroy != nullptr && targets_self(destroy->target_expr);
}

PathEnd path_end(const std::vector<std::unique_ptr<StmtNode>>& body, const LostLink& link);

// Every branch must resolve; one that can exit unresolved makes the whole statement exit.
PathEnd branches_end(const std::vector<const std::vector<std::unique_ptr<StmtNode>>*>& branches,
                     bool exhaustive,
                     const LostLink& link) {
    bool all_resolve = exhaustive;
    for (const auto* branch : branches) {
        const auto end = path_end(*branch, link);
        if (end == PathEnd::Exits) {
            return PathEnd::Exits;
        }
        all_resolve = all_resolve && end == PathEnd::Resolves;
    }
    return all_resolve ? PathEnd::Resolves : PathEnd::FallsThrough;
}

PathEnd statement_end(const StmtNode& stmt, const LostLink& link) {
    if (resolving_command(stmt, link)) {
        return PathEnd::Resolves;
    }
    if (std::holds_alternative<ReturnStmt>(stmt.stmt)) {
        return PathEnd::Exits;
    }
    if (const auto* branch = std::get_if<IfStmt>(&stmt.stmt)) {
        std::vector<const std::vector<std::unique_ptr<StmtNode>>*> bodies{&branch->then_body};
        for (const auto& else_if : branch->else_if_branches) {
            bodies.push_back(&else_if.body);
        }
        bodies.push_back(&branch->else_body);
        return branches_end(bodies, !branch->else_body.empty(), link);
    }
    if (const auto* match = std::get_if<MatchStmt>(&stmt.stmt)) {
        std::vector<const std::vector<std::unique_ptr<StmtNode>>*> bodies;
        bodies.reserve(match->arms.size());
        for (const auto& arm : match->arms) {
            bodies.push_back(&arm.body);
        }
        return branches_end(bodies, match->covers_every_case(), link);
    }
    if (const auto* loop = std::get_if<ForeachStmt>(&stmt.stmt)) {
        return path_end(loop->body, link) == PathEnd::Exits ? PathEnd::Exits : PathEnd::FallsThrough;
    }
    return PathEnd::FallsThrough;
}

// NOLINTNEXTLINE(misc-no-recursion) -- statements nest
PathEnd path_end(const std::vector<std::unique_ptr<StmtNode>>& body, const LostLink& link) {
    for (const auto& stmt : body) {
        if (stmt == nullptr) {
            continue;
        }
        const auto end = statement_end(*stmt, link);
        if (end != PathEnd::FallsThrough) {
            return end;
        }
    }
    return PathEnd::FallsThrough;
}

std::string link_spelling(const LinkField& link) {
    return link.holder.local_name + "." + link.field;
}

// The reachable trait sets, with free traits counted as both present and absent.
class ReachableSets {
public:
    explicit ReachableSets(const ReachabilityFacts& facts) : facts_(&facts) {}

    [[nodiscard]] bool may_have(const ReachableTraitSet& set, const SymbolId& trait) const {
        return facts_->may_carry(set, trait);
    }

    [[nodiscard]] bool matches(const ReachableTraitSet& set,
                               const std::vector<SymbolId>& required,
                               const std::vector<SymbolId>& excluded) const {
        return facts_->may_match(set, required, excluded);
    }

    // Some reachable set matching the domain may carry `trait`.
    [[nodiscard]] bool domain_may_carry(const std::vector<SymbolId>& required,
                                        const std::vector<SymbolId>& excluded,
                                        const SymbolId& trait) const {
        return std::ranges::any_of(facts_->trait_sets, [&](const ReachableTraitSet& set) {
            return matches(set, required, excluded) && may_have(set, trait);
        });
    }

    [[nodiscard]] bool named_may_carry(const SymbolId& entity, const SymbolId& trait) const {
        return std::ranges::any_of(facts_->origins, [&](const OriginReachability& origin) {
            return origin.node.archetype == entity && origin.node.role_path.empty() &&
                   std::ranges::any_of(origin.trait_sets,
                                       [&](const ReachableTraitSet& set) { return may_have(set, trait); });
        });
    }

    [[nodiscard]] const std::vector<ReachableTraitSet>& sets() const {
        return facts_->trait_sets;
    }

private:
    const ReachabilityFacts* facts_;
};

class LinkLossAnalysis {
public:
    LinkLossAnalysis(const DecoratedProgram& program, ErrorReporter& errors)
        : program_(&program)
        , errors_(&errors)
        , sets_(program.reachability)
        , parent_(find_hierarchy_trait(program)) {
        for (const auto& handler : program.execution_graph.handlers) {
            if (!std::ranges::contains(program.reachability.unreachable_handlers, handler.identity)) {
                reachable_.push_back(&handler);
            }
        }
        std::ranges::sort(reachable_, {}, [](const HandlerNode* handler) { return handler->identity.canonical_id(); });
    }

    void check_projections(const LinkField& link) const {
        for (const auto& handler : program_->execution_graph.handlers) {
            for (const auto& projection : handler.contract.projections) {
                if (projection.trait == link.target) {
                    errors_->error(handler.location,
                                   "link field '" + link.field + "' of '" + link.holder.local_name +
                                       "' can't link to projected trait '" + link.target.local_name +
                                       "': a frame-local value can't be a link target");
                }
                if (projection.trait == link.holder) {
                    errors_->error(handler.location,
                                   "can't project '" + link.holder.local_name + "': it holds link field '" + link.field +
                                       "', which would outlive a target destroyed later in the frame");
                }
            }
        }
    }

    // The first reachable handler with a command that loses `link`, and that command.
    [[nodiscard]] std::optional<std::pair<const HandlerNode*, HandlerCommandKind>> first_loss(const LinkField& link) const {
        const bool child_target = target_on_child(link.target);
        for (const auto* handler : reachable_) {
            for (const auto& command : handler->contract.commands) {
                if (may_lose(command, link.target) && (child_target || reaches_target(*handler, command, link.target))) {
                    return std::pair{handler, command.kind};
                }
            }
        }
        return std::nullopt;
    }

    void check_coverage(const LinkField& link, const HandlerNode& loser, HandlerCommandKind kind) const {
        // Each `on lost` handler's domain without the marker its trigger implies.
        std::vector<std::pair<std::vector<SymbolId>, const HandlerNode*>> resolvers;
        for (const auto& handler : program_->execution_graph.handlers) {
            if (handler.identity.trigger.kind != HandlerTriggerKind::TraitAdded ||
                handler.identity.trigger.symbol != link.marker) {
                continue;
            }
            std::vector<SymbolId> required;
            std::ranges::copy_if(handler.contract.selection, std::back_inserter(required), [&](const SymbolId& trait) {
                return trait != link.marker;
            });
            resolvers.emplace_back(std::move(required), &handler);
        }
        const auto lost_by = "link '" + make_canonical_id(link.holder) + "." + link.field +
                             "' can be lost by rule '" + make_canonical_id(loser.identity.rule) + "' (" +
                             handler_command_kind_name(kind) + ")";
        if (resolvers.empty()) {
            errors_->error(loser.location,
                           lost_by + " and has no `on lost " + link_spelling(link) +
                               "` handler; add one that swaps the variant, gives a new target with `add " +
                               link.holder.local_name + ":`, removes '" + link.holder.local_name +
                               "' or destroys the holder");
            return;
        }
        for (const auto& set : sets_.sets()) {
            const auto covered = [&](const auto& resolver) {
                return sets_.matches(set, resolver.first, resolver.second->contract.exclusion);
            };
            if (!sets_.may_have(set, link.holder) || std::ranges::any_of(resolvers, covered)) {
                continue;
            }
            auto message = lost_by + ", but no `on lost " + link_spelling(link) + "` handler covers holders carrying {";
            for (std::size_t index = 0; index < set.size(); ++index) {
                message.append(index == 0 ? "" : ", ").append(set[index].local_name);
            }
            errors_->error(resolvers.front().second->location, message + "}");
            return;
        }
    }

private:
    // A destroyed owner takes its children with it, so any destroy loses a target that can be a child.
    [[nodiscard]] bool target_on_child(const SymbolId& target) const {
        return parent_.has_value() && std::ranges::any_of(sets_.sets(), [&](const ReachableTraitSet& set) {
                   return sets_.may_have(set, target) && sets_.may_have(set, *parent_);
               });
    }

    [[nodiscard]] bool reaches_target(const HandlerNode& handler,
                                      const InferredHandlerCommand& command,
                                      const SymbolId& target) const {
        if (command.entities.empty()) {
            return true;
        }
        const auto& contract = handler.contract;
        return std::ranges::any_of(command.entities, [&](const EntityTarget& entity) {
            switch (entity.kind) {
                case EntityTargetKind::Self:
                    return contract.domain_kind != HandlerDomainKind::Unary ||
                           sets_.domain_may_carry(contract.selection, contract.exclusion, target);
                case EntityTargetKind::PairBinding:
                    return entity.binding_index >= contract.pair_bindings.size() ||
                           sets_.domain_may_carry(contract.pair_bindings[entity.binding_index].required_traits, {}, target);
                case EntityTargetKind::Named:
                    return !entity.symbol.has_value() || sets_.named_may_carry(*entity.symbol, target);
                case EntityTargetKind::Spawned:
                case EntityTargetKind::Unknown:
                    return true;
            }
            std::unreachable();
        });
    }

    const DecoratedProgram* program_;
    ErrorReporter* errors_;
    ReachableSets sets_;
    std::optional<SymbolId> parent_;
    std::vector<const HandlerNode*> reachable_;  // sorted by canonical id
};

}  // namespace

bool resolves_on_every_path(const std::vector<std::unique_ptr<StmtNode>>& body, const LostLink& link) {
    return path_end(body, link) == PathEnd::Resolves;
}

std::vector<LinkField> link_fields(const std::unordered_map<std::string, ResolvedTrait>& traits) {
    std::vector<LinkField> links;
    for (const auto& trait : traits | std::views::values) {
        for (const auto& field : trait.fields) {
            if (trait.symbol_id.has_value() && is_link_type(field.type)) {
                links.push_back(LinkField{.holder = *trait.symbol_id,
                                          .field  = field.name,
                                          .target = *field.type.symbol_id,
                                          .marker = lost_marker_symbol(*trait.symbol_id, field.name)});
            }
        }
    }
    std::ranges::sort(links, {}, [](const LinkField& link) { return make_canonical_id(link.marker); });
    return links;
}

void derive_link_losses(std::vector<HandlerNode>& handlers, const std::unordered_map<std::string, ResolvedTrait>& traits) {
    const auto links = link_fields(traits);
    for (auto& handler : handlers) {
        auto& markers = handler.contract.lost_markers;
        markers.clear();
        for (const auto& link : links) {
            const auto loses = std::ranges::any_of(handler.contract.commands, [&](const InferredHandlerCommand& command) {
                return may_lose(command, link.target);
            });
            if (loses) {
                markers.push_back(link.marker);
            }
        }
    }
}

void validate_required_links(DecoratedProgram& program, ErrorReporter& errors) {
    program.losable_links.clear();
    const LinkLossAnalysis analysis(program, errors);
    for (const auto& link : link_fields(program.traits)) {
        analysis.check_projections(link);
        const auto loss = analysis.first_loss(link);
        if (!loss.has_value()) {
            continue;
        }
        program.losable_links.push_back(link);
        analysis.check_coverage(link, *loss->first, loss->second);
    }
}

}  // namespace cactus
