#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace cactus {

// Tarjan's algorithm; returns each node's strongly connected component.
class StrongComponents {
public:
    using NodeIndex = std::uint32_t;
    static constexpr NodeIndex kUnvisited = std::numeric_limits<NodeIndex>::max();

    explicit StrongComponents(const std::vector<std::vector<NodeIndex>>& edges)
        : edges_(&edges), order_(edges.size(), kUnvisited), low_(edges.size()), component_(edges.size(), kUnvisited),
          on_stack_(edges.size(), false) {
        for (NodeIndex node = 0; node < edges.size(); ++node) {
            if (order_[node] == kUnvisited) {
                visit(node);
            }
        }
    }

    [[nodiscard]] const std::vector<NodeIndex>& components() const {
        return component_;
    }

private:
    void visit(NodeIndex node) {  // NOLINT(misc-no-recursion) -- depth is bounded by the node count
        order_[node] = low_[node] = next_order_++;
        stack_.push_back(node);
        on_stack_[node] = true;
        for (const auto next : (*edges_)[node]) {
            if (order_[next] == kUnvisited) {
                visit(next);
                low_[node] = std::min(low_[node], low_[next]);
            } else if (on_stack_[next]) {
                low_[node] = std::min(low_[node], order_[next]);
            }
        }
        if (low_[node] != order_[node]) {
            return;
        }
        NodeIndex member = kUnvisited;
        while (member != node) {
            member = stack_.back();
            stack_.pop_back();
            on_stack_[member]  = false;
            component_[member] = next_component_;
        }
        ++next_component_;
    }

    const std::vector<std::vector<NodeIndex>>* edges_;
    std::vector<NodeIndex> order_;
    std::vector<NodeIndex> low_;
    std::vector<NodeIndex> component_;
    std::vector<bool> on_stack_;
    std::vector<NodeIndex> stack_;
    NodeIndex next_order_     = 0;
    NodeIndex next_component_ = 0;
};

}  // namespace cactus
