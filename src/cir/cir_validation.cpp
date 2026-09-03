#include "cir/cir_validation.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus::cir {
namespace {

using NodeKinds = std::unordered_map<std::string, CirNodeKind>;
using Adjacency = std::unordered_map<std::string, std::vector<std::string>>;

enum class Color : std::uint8_t { White, Gray, Black };

const std::vector<std::string> EMPTY_SUCCESSORS;

/// Accumulates diagnostics against one CIR document. Every check reports and
/// continues so a malformed artifact surfaces all of its problems at once.
class Validator {
public:
    Validator(const CirProgram& cir, ErrorReporter& errors)
        : cir_(&cir)
        , errors_(&errors) {}

    bool run() {
        const auto errors_before = errors_->error_count();
        collect_nodes();
        check_ordering();
        check_relation_endpoints();
        check_schedule_acyclic();
        check_activation_schedules();
        return errors_->error_count() == errors_before;
    }

private:
    void report(const std::string& message) {
        errors_->error({}, "cir: " + message);
    }

    void declare(const CirNodeId& id, CirNodeKind kind) {
        if (!nodes_.emplace(id.value, kind).second) {
            report("duplicate node id '" + id.value + "'");
            return;
        }
        node_order_.push_back(id.value);
    }

    void collect_nodes() {
        for (const auto& handler : cir_->handlers) {
            declare(handler.id, CirNodeKind::Handler);
        }
        for (const auto& phase : cir_->phases) {
            declare(phase.id, CirNodeKind::Phase);
        }
        for (const auto& producer : cir_->event_producers) {
            declare(producer.id, producer.kind);
        }
        for (const auto& raster : cir_->rasterizations) {
            declare(raster.id, CirNodeKind::Rasterization);
        }
    }

    [[nodiscard]] bool node_exists(const CirNodeId& id) const {
        return nodes_.contains(id.value);
    }

    [[nodiscard]] bool node_is(const CirNodeId& id, CirNodeKind kind) const {
        const auto found = nodes_.find(id.value);
        return found != nodes_.end() && found->second == kind;
    }

    void require_node(const CirNodeId& id, const std::string& relation, const std::string& role) {
        if (!node_exists(id)) {
            report(relation + " " + role + " '" + id.value + "' names no node");
        }
    }

    void require_handler(const CirNodeId& id, const std::string& relation, const std::string& role) {
        if (!node_exists(id)) {
            report(relation + " " + role + " '" + id.value + "' names no node");
            return;
        }
        if (!node_is(id, CirNodeKind::Handler)) {
            report(relation + " " + role + " '" + id.value + "' is not a handler node");
        }
    }

    void require_phase(const CirNodeId& id, const std::string& relation, const std::string& role) {
        if (!node_exists(id)) {
            report(relation + " " + role + " '" + id.value + "' names no node");
            return;
        }
        if (!node_is(id, CirNodeKind::Phase)) {
            report(relation + " " + role + " '" + id.value + "' is not a phase node");
        }
    }

    template <typename Range, typename Compare>
    void require_sorted(const Range& range, Compare compare, const std::string& what) {
        if (!std::ranges::is_sorted(range, compare)) {
            report(what + " are not in canonical order");
        }
    }

    void check_ordering() {
        const auto by_id = [](const auto& left, const auto& right) { return left.id < right.id; };
        require_sorted(cir_->handlers, by_id, "handler nodes");
        require_sorted(cir_->phases, by_id, "phase nodes");
        require_sorted(cir_->event_producers, by_id, "event-producer nodes");
        require_sorted(cir_->rasterizations, by_id, "rasterization nodes");
        require_sorted(
            cir_->traits,
            [](const CirTrait& left, const CirTrait& right) { return symbol_precedes(left.symbol, right.symbol); },
            "traits");
        require_sorted(
            cir_->rule_groups,
            [](const CirRuleGroup& left, const CirRuleGroup& right) { return symbol_precedes(left.rule, right.rule); },
            "rule groups");
        require_sorted(cir_->schedule_dependencies, schedule_relation_precedes, "schedule dependencies");
        require_sorted(cir_->phase_dependencies, phase_dependency_precedes, "phase dependencies");
        require_sorted(cir_->phase_barriers, phase_barrier_precedes, "phase barriers");
        require_sorted(cir_->event_flows, event_flow_precedes, "event flows");
        require_sorted(cir_->render_pass_flows, render_flow_precedes, "render-pass flows");
        require_sorted(
            cir_->activation_schedules,
            [](const CirActivationSchedule& left, const CirActivationSchedule& right) {
                return trigger_precedes(left.activation, right.activation);
            },
            "activation schedules");
    }

    void check_relation_endpoints() {
        for (const auto& group : cir_->rule_groups) {
            for (const auto& handler : group.handlers) {
                require_handler(handler, "rule group '" + make_canonical_id(group.rule) + "'", "member");
            }
        }
        for (const auto& relation : cir_->schedule_dependencies) {
            require_handler(relation.before, "schedule dependency", "before");
            require_handler(relation.after, "schedule dependency", "after");
        }
        for (const auto& relation : cir_->phase_dependencies) {
            require_phase(relation.upstream, "phase dependency", "upstream");
            require_phase(relation.downstream, "phase dependency", "downstream");
        }
        for (const auto& relation : cir_->phase_barriers) {
            require_phase(relation.upstream_phase, "phase barrier", "upstream");
            require_handler(relation.downstream_handler, "phase barrier", "downstream");
        }
        for (const auto& relation : cir_->event_flows) {
            if (!node_exists(relation.producer)) {
                report("event flow producer '" + relation.producer.value + "' names no node");
            } else if (node_is(relation.producer, CirNodeKind::Phase) ||
                       node_is(relation.producer, CirNodeKind::Rasterization)) {
                report("event flow producer '" + relation.producer.value + "' is not a handler or producer node");
            }
            require_handler(relation.consumer, "event flow", "consumer");
        }
        for (const auto& relation : cir_->render_pass_flows) {
            require_node(relation.before, "render-pass flow", "before");
            require_node(relation.after, "render-pass flow", "after");
        }
    }

    /// Schedule and phase dependencies share one DAG requirement; event flows
    /// are deliberately absent, since producer/consumer feedback is legal.
    [[nodiscard]] Adjacency schedule_adjacency() const {
        Adjacency adjacency;
        for (const auto& relation : cir_->schedule_dependencies) {
            adjacency[relation.before.value].push_back(relation.after.value);
        }
        for (const auto& relation : cir_->phase_dependencies) {
            adjacency[relation.upstream.value].push_back(relation.downstream.value);
        }
        return adjacency;
    }

    void report_cycle(const std::vector<std::string>& path, const std::string& closing) {
        const auto start = std::ranges::find(path, closing);
        std::string cycle;
        for (auto it = start; it != path.end(); ++it) {
            cycle += *it + " -> ";
        }
        cycle += closing;
        if (reported_cycles_.insert(cycle).second) {
            report("cyclic schedule relations: " + cycle);
        }
    }

    void check_schedule_acyclic() {
        const auto adjacency = schedule_adjacency();
        std::unordered_map<std::string, Color> color;
        std::vector<std::string> path;

        std::function<void(const std::string&)> visit = [&](const std::string& node) {
            color[node] = Color::Gray;
            path.push_back(node);
            const auto successors = adjacency.find(node);
            for (const auto& next : successors == adjacency.end() ? EMPTY_SUCCESSORS : successors->second) {
                if (color[next] == Color::Gray) {
                    report_cycle(path, next);
                } else if (color[next] == Color::White) {
                    visit(next);
                }
            }
            path.pop_back();
            color[node] = Color::Black;
        };

        for (const auto& node : node_order_) {
            if (color[node] == Color::White) {
                visit(node);
            }
        }
    }

    /// Handlers named in the activation's stable order, reporting entries that
    /// are not handler nodes or appear twice.
    std::unordered_set<std::string> check_stable_order(const CirActivationSchedule& schedule,
                                                       const std::string& activation) {
        std::unordered_set<std::string> ordered;
        for (const auto& handler : schedule.stable_order) {
            require_handler(handler, "activation '" + activation + "' stable order", "entry");
            if (!ordered.insert(handler.value).second) {
                report("activation '" + activation + "' lists '" + handler.value + "' twice in its stable order");
            }
        }
        return ordered;
    }

    void check_levels(const CirActivationSchedule& schedule,
                      const std::string& activation,
                      const std::unordered_set<std::string>& ordered) {
        std::unordered_set<std::string> levelled;
        std::uint64_t expected_index = 0;
        for (const auto& level : schedule.levels) {
            if (level.index != expected_index) {
                report("activation '" + activation + "' dependency levels are not consecutively indexed");
            }
            ++expected_index;
            for (const auto& handler : level.handlers) {
                if (!ordered.contains(handler.value)) {
                    report("activation '" + activation + "' level " + std::to_string(level.index) + " contains '" +
                           handler.value + "', which is absent from its stable order");
                } else if (!levelled.insert(handler.value).second) {
                    report("activation '" + activation + "' assigns '" + handler.value +
                           "' to more than one dependency level");
                }
            }
        }
        for (const auto& handler : schedule.stable_order) {
            if (!levelled.contains(handler.value)) {
                report("activation '" + activation + "' orders '" + handler.value +
                       "' without assigning it a dependency level");
            }
        }
    }

    void check_activation_schedules() {
        for (const auto& schedule : cir_->activation_schedules) {
            const auto activation = make_canonical_id(schedule.activation.symbol);
            check_levels(schedule, activation, check_stable_order(schedule, activation));
        }
    }

    const CirProgram* cir_;
    ErrorReporter* errors_;
    NodeKinds nodes_;
    /// Declaration order of `nodes_`, so cycle diagnostics never inherit hash
    /// iteration order.
    std::vector<std::string> node_order_;
    std::unordered_set<std::string> reported_cycles_;
};

}  // namespace

bool validate_program(const CirProgram& cir, ErrorReporter& errors) {
    return Validator(cir, errors).run();
}

}  // namespace cactus::cir
