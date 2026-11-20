#pragma once

#include "frontend/semantic_analyzer.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

// A float field stays a plain `+=` only when its deltas can never meet.
class AccumulationPlan {
public:
    // Marks every buffered statement in the program's AST.
    explicit AccumulationPlan(const DecoratedProgram& program);

    // A clamped field whose deltas can meet keeps their exact sum per cell for the activation.
    struct ClampedTable {
        TraitFieldRef field;
        std::string table;
        std::string value_type;
    };

    [[nodiscard]] bool empty() const {
        return tables_.empty() && clamped_.empty();
    }
    [[nodiscard]] const std::vector<ClampedTable>& clamped_tables() const {
        return clamped_;
    }
    // The pending-sum table of clamped `access`, or empty when its deltas never meet.
    [[nodiscard]] std::string clamped_table(const TraitFieldRef& access) const;
    // One table function name per buffered field, in the order of fields().
    [[nodiscard]] const std::vector<std::string>& tables() const {
        return tables_;
    }
    // The buffered trait fields with the trait and field each table holds.
    [[nodiscard]] const std::vector<TraitFieldRef>& fields() const {
        return fields_;
    }
    [[nodiscard]] bool buffered(const TraitFieldRef& access) const;
    // The flushes `handler` runs on entry: one per buffered field it accesses
    // where no settle call in its body covers every access.
    [[nodiscard]] std::string entry_flushes(const HandlerIdentity& handler) const;
    // What runs after `handler` when it may move component storage.
    [[nodiscard]] std::string exit_rebase(const HandlerIdentity& handler) const;

    // The templates holding the float and the clamped tables, one specialization per trait.
    static constexpr const char* FLOAT_TABLES   = "accumulation";
    static constexpr const char* CLAMPED_TABLES = "clamped_sums";

    [[nodiscard]] static std::string table_name(const SymbolId& trait,
                                                const std::string& field,
                                                const DecoratedProgram& program,
                                                const char* holder = FLOAT_TABLES);

private:
    std::vector<ClampedTable> clamped_;
    std::vector<std::string> tables_;
    std::vector<TraitFieldRef> fields_;
    std::unordered_map<HandlerIdentity, std::vector<std::string>, HandlerIdentityHash> entry_flushes_;
    std::unordered_set<HandlerIdentity, HandlerIdentityHash> rebases_;
};

// Scoped, not a parameter: rewrite_expr's recursive calls pass only partial context.
class AccumulationScope {
public:
    explicit AccumulationScope(const AccumulationPlan& plan);
    AccumulationScope(const AccumulationScope&)            = delete;
    AccumulationScope& operator=(const AccumulationScope&) = delete;
    AccumulationScope(AccumulationScope&&)                 = delete;
    AccumulationScope& operator=(AccumulationScope&&)      = delete;
    ~AccumulationScope();

    [[nodiscard]] static const AccumulationPlan* active();

private:
    const AccumulationPlan* previous_;
};

}  // namespace cactus
