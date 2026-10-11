#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace cactus {

// A float field stays a plain `+=` only when its deltas can never meet.
class AccumulationPlan {
public:
    // Marks every buffered statement in the program's AST.
    explicit AccumulationPlan(const DecoratedProgram& program);

    [[nodiscard]] bool empty() const {
        return tables_.empty();
    }
    // One table function name per buffered field, sorted.
    [[nodiscard]] const std::vector<std::string>& tables() const {
        return tables_;
    }
    // The flushes `handler` runs on entry: one per buffered field it reads or
    // writes other than by accumulating.
    [[nodiscard]] std::string entry_flushes(const HandlerIdentity& handler) const;

    [[nodiscard]] static std::string table_name(const AccumulationTarget& target, const DecoratedProgram& program);

private:
    std::vector<std::string> tables_;
    std::unordered_map<HandlerIdentity, std::vector<std::string>, HandlerIdentityHash> entry_flushes_;
};

}  // namespace cactus
