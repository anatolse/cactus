#include "common/ast_expressions.hpp"
#include "common/duration.hpp"
#include "common/numeric.hpp"
#include "frontend/semantic_analyzer.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cactus {

namespace {

// A value the fold knows, or Unknown. Floats are folded in `float`, as at run time.
struct Folded {
    enum class Kind : std::uint8_t { Unknown, Bool, Int, Float, Enum, Vector, Duration };
    Kind kind = Kind::Unknown;
    bool boolean        = false;
    int integer         = 0;  // also an enum value's index
    float real          = 0.0F;
    Duration duration{};
    std::array<float, 3> vector{};
    std::size_t width = 0;

    [[nodiscard]] bool known() const {
        return kind != Kind::Unknown;
    }
    [[nodiscard]] bool is_false() const {
        return kind == Kind::Bool && !boolean;
    }
    [[nodiscard]] bool is_true() const {
        return kind == Kind::Bool && boolean;
    }
    [[nodiscard]] bool is_zero() const {
        return (kind == Kind::Int && integer == 0) || (kind == Kind::Float && real == 0.0F) ||
               (kind == Kind::Duration && duration.units == 0) ||
               (kind == Kind::Vector && std::ranges::all_of(vector, [](float v) { return v == 0.0F; }));
    }
    // At rest means bit-equal: -0.0 is not the default 0.0 and NaN is never at rest.
    [[nodiscard]] bool same_bits(const Folded& other) const {
        if (kind != other.kind) {
            return false;
        }
        switch (kind) {
            case Kind::Bool:
                return boolean == other.boolean;
            case Kind::Int:
            case Kind::Enum:
                return integer == other.integer;
            case Kind::Float:
                return std::bit_cast<std::uint32_t>(real) == std::bit_cast<std::uint32_t>(other.real);
            case Kind::Duration:
                return duration == other.duration;
            case Kind::Vector:
                return width == other.width &&
                       std::ranges::equal(vector, other.vector, [](float a, float b) {
                           return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
                       });
            case Kind::Unknown:
                return false;
        }
        return false;
    }

    static Folded of_bool(bool value) {
        return {.kind = Kind::Bool, .boolean = value};
    }
    static Folded of_int(int value) {
        return {.kind = Kind::Int, .integer = value};
    }
    static Folded of_float(float value) {
        return {.kind = Kind::Float, .real = value};
    }
    static Folded of_duration(Duration value) {
        return {.kind = Kind::Duration, .duration = value};
    }
};

std::optional<float> as_float(const Folded& value) {
    if (value.kind == Folded::Kind::Float) {
        return value.real;
    }
    if (value.kind == Folded::Kind::Int) {
        return static_cast<float>(value.integer);
    }
    return std::nullopt;
}

// `left op right` for one operand type; nullopt for an operator the type lacks.
template <typename T>
std::optional<T> apply_arithmetic(const std::string& op, T left, T right) {
    if (op == "+") {
        return num::add(left, right);
    }
    if (op == "-") {
        return num::sub(left, right);
    }
    if (op == "*") {
        return num::mul(left, right);
    }
    if (op == "/") {
        return num::div(left, right);
    }
    if constexpr (std::is_same_v<T, int>) {
        if (op == "%") {
            return num::mod(left, right);
        }
    }
    return std::nullopt;
}

template <typename T>
std::optional<bool> apply_comparison(const std::string& op, T left, T right) {
    static const std::array<std::pair<const char*, bool (*)(T, T)>, 6> kOperators{{
        {"==", [](T a, T b) { return a == b; }},
        {"!=", [](T a, T b) { return a != b; }},
        {"<", [](T a, T b) { return a < b; }},
        {">", [](T a, T b) { return a > b; }},
        {"<=", [](T a, T b) { return a <= b; }},
        {">=", [](T a, T b) { return a >= b; }},
    }};
    const auto found = std::ranges::find(kOperators, op, [](const auto& entry) { return std::string(entry.first); });
    return found == kOperators.end() ? std::nullopt : std::optional{found->second(left, right)};
}

Folded vector_arithmetic(const std::string& op, const Folded& left, const Folded& right) {
    if (left.width != right.width || (op != "+" && op != "-")) {
        return {};
    }
    Folded result{.kind = Folded::Kind::Vector, .width = left.width};
    for (std::size_t index = 0; index < left.width; ++index) {
        result.vector.at(index) =
            op == "+" ? left.vector.at(index) + right.vector.at(index) : left.vector.at(index) - right.vector.at(index);
    }
    return result;
}

std::optional<DurationOperand> duration_operand(const Folded& value) {
    if (value.kind == Folded::Kind::Duration) {
        return value.duration;
    }
    if (value.kind == Folded::Kind::Int) {
        return std::int64_t{value.integer};
    }
    return std::nullopt;
}

Folded duration_arithmetic(const std::string& op, const Folded& left, const Folded& right) {
    const auto a      = duration_operand(left);
    const auto b      = duration_operand(right);
    const auto result = a.has_value() && b.has_value() ? apply_duration_operator(op, *a, *b) : std::nullopt;
    if (!result.has_value()) {
        return {};
    }
    if (const auto* duration = std::get_if<Duration>(&*result)) {
        return Folded::of_duration(*duration);
    }
    return Folded::of_float(std::get<float>(*result));
}

Folded arithmetic(const std::string& op, const Folded& left, const Folded& right) {
    if (left.kind == Folded::Kind::Duration || right.kind == Folded::Kind::Duration) {
        return duration_arithmetic(op, left, right);
    }
    if (left.kind == Folded::Kind::Int && right.kind == Folded::Kind::Int) {
        const auto value = apply_arithmetic(op, left.integer, right.integer);
        return value.has_value() ? Folded::of_int(*value) : Folded{};
    }
    if (left.kind == Folded::Kind::Vector && right.kind == Folded::Kind::Vector) {
        return vector_arithmetic(op, left, right);
    }
    const auto a = as_float(left);
    const auto b = as_float(right);
    const auto value = a.has_value() && b.has_value() ? apply_arithmetic(op, *a, *b) : std::nullopt;
    return value.has_value() ? Folded::of_float(*value) : Folded{};
}

Folded comparison(const std::string& op, const Folded& left, const Folded& right) {
    const bool equality = op == "==" || op == "!=";
    const bool same_kind = left.kind == right.kind;
    if (same_kind && equality && (left.kind == Folded::Kind::Bool || left.kind == Folded::Kind::Enum)) {
        const bool same = left.kind == Folded::Kind::Bool ? left.boolean == right.boolean : left.integer == right.integer;
        return Folded::of_bool(same == (op == "=="));
    }
    std::optional<bool> value;
    if (left.kind == Folded::Kind::Int && right.kind == Folded::Kind::Int) {
        value = apply_comparison(op, left.integer, right.integer);
    } else if (left.kind == Folded::Kind::Duration || right.kind == Folded::Kind::Duration) {
        value = same_kind ? apply_comparison(op, left.duration, right.duration) : std::nullopt;
    } else if (const auto a = as_float(left), b = as_float(right); a.has_value() && b.has_value()) {
        value = apply_comparison(op, *a, *b);
    }
    return value.has_value() ? Folded::of_bool(*value) : Folded{};
}

bool is_comparison(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
}

// Folds a rule with every cell field read of its cell aliases replaced by the field's default.
class RestFold {
public:
    RestFold(const CellDomainNode& cells,
             const std::unordered_map<std::string, const ExprNode*>& constants,
             const std::unordered_map<std::string, const ExprNode*>& defaults)
        : cells_(&cells)
        , constants_(&constants)
        , defaults_(&defaults) {}

    // NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
    [[nodiscard]] Folded fold(const ExprNode& expr) const {
        return std::visit(
            [&](const auto& node) -> Folded {
                if constexpr (requires { this->fold_node(node); }) {
                    return fold_node(node);
                } else {
                    return {};
                }
            },
            expr.expr);
    }

    [[nodiscard]] std::optional<Folded> cell_default(const std::string& alias, const std::string& field) const {
        if (!cells_->names(alias)) {
            return std::nullopt;
        }
        const auto found = defaults_->find(field);
        if (found == defaults_->end()) {
            return std::nullopt;
        }
        return fold(*found->second);
    }

    void bind(const std::string& name, Folded value) {
        locals_[name] = value;
    }
    void forget(const std::string& name) {
        locals_[name] = Folded{};
    }
    [[nodiscard]] const std::unordered_map<std::string, Folded>& locals() const {
        return locals_;
    }
    void restore(std::unordered_map<std::string, Folded> locals) {
        locals_ = std::move(locals);
    }

private:
    [[nodiscard]] static Folded fold_node(const LiteralExpr& literal) {
        try {
            switch (literal.kind) {
                case LiteralExpr::Kind::Int:
                    return Folded::of_int(static_cast<int>(static_cast<std::uint32_t>(std::stoull(literal.value))));
                case LiteralExpr::Kind::Float:
                    return Folded::of_float(std::stof(literal.value));
                case LiteralExpr::Kind::Bool:
                    return Folded::of_bool(literal.value == "true");
                case LiteralExpr::Kind::Duration: {
                    const auto units = duration_literal_units(literal.value);
                    return units.has_value() ? Folded::of_duration(Duration{*units}) : Folded{};
                }
                default:
                    return {};
            }
        } catch (...) {
            return {};
        }
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- constants may name constants
    [[nodiscard]] Folded fold_node(const IdentExpr& ident) const {
        if (const auto local = locals_.find(ident.name); local != locals_.end()) {
            return local->second;
        }
        const auto constant = constants_->find(ident.name);
        if (constant == constants_->end() || !evaluating_.insert(ident.name).second) {
            return {};
        }
        auto value = fold(*constant->second);
        evaluating_.erase(ident.name);
        return value;
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
    [[nodiscard]] Folded fold_node(const MemberExpr& member) const {
        if (member.resolved_enum_member.has_value()) {
            return {.kind = Folded::Kind::Enum, .integer = member.resolved_enum_member->index};
        }
        if (const auto* owner = std::get_if<IdentExpr>(&member.object->expr); owner != nullptr) {
            if (auto value = cell_default(owner->name, member.member); value.has_value()) {
                return *value;
            }
        }
        const auto object = fold(*member.object);
        if (object.kind != Folded::Kind::Vector) {
            return {};
        }
        static constexpr std::array<const char*, 3> kComponents{"x", "y", "z"};
        for (std::size_t index = 0; index < object.width; ++index) {
            if (member.member == kComponents.at(index)) {
                return Folded::of_float(object.vector.at(index));
            }
        }
        return {};
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
    [[nodiscard]] Folded fold_node(const UnaryExpr& unary) const {
        const auto operand = fold(*unary.operand);
        if (unary.op == "not") {
            return operand.kind == Folded::Kind::Bool ? Folded::of_bool(!operand.boolean) : Folded{};
        }
        if (operand.kind == Folded::Kind::Int) {
            return Folded::of_int(num::neg(operand.integer));
        }
        if (operand.kind == Folded::Kind::Float) {
            return Folded::of_float(-operand.real);
        }
        if (operand.kind == Folded::Kind::Duration) {
            return Folded::of_duration(-operand.duration);
        }
        return {};
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
    [[nodiscard]] Folded fold_node(const BinaryExpr& binary) const {
        const auto left = fold(*binary.left);
        if (binary.op == "and" && left.is_false()) {
            return left;
        }
        if (binary.op == "or" && left.is_true()) {
            return left;
        }
        const auto right = fold(*binary.right);
        if (binary.op == "and" || binary.op == "or") {
            const bool decided = binary.op == "and" ? right.is_false() : right.is_true();
            if (decided) {
                return right;
            }
            return left.known() && right.known() ? right : Folded{};
        }
        return is_comparison(binary.op) ? comparison(binary.op, left, right) : arithmetic(binary.op, left, right);
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
    [[nodiscard]] Folded fold_node(const IfExpr& conditional) const {
        const auto condition = fold(*conditional.condition);
        if (condition.is_true()) {
            return fold(*conditional.then_expr);
        }
        if (condition.is_false()) {
            return fold(*conditional.else_expr);
        }
        return {};
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- an expression tree
    [[nodiscard]] Folded fold_node(const CallExpr& call) const {
        const auto* ident = std::get_if<IdentExpr>(&call.callee->expr);
        if (ident == nullptr || (ident->name != "vec2" && ident->name != "vec3") ||
            call.args.size() != (ident->name == "vec2" ? 2U : 3U)) {
            return {};
        }
        Folded result{.kind = Folded::Kind::Vector, .width = call.args.size()};
        for (std::size_t index = 0; index < call.args.size(); ++index) {
            const auto component = as_float(fold(*call.args[index]));
            if (!component.has_value()) {
                return {};
            }
            result.vector.at(index) = *component;
        }
        return result;
    }

    const CellDomainNode* cells_;
    const std::unordered_map<std::string, const ExprNode*>* constants_;
    const std::unordered_map<std::string, const ExprNode*>* defaults_;
    std::unordered_map<std::string, Folded> locals_;
    mutable std::unordered_set<std::string> evaluating_;
};

Folded aggregate_identity(const ReducerDecl& reducer, const Folded& input) {
    switch (reducer.kind) {
        case ReducerKind::Count:
            return Folded::of_int(0);
        case ReducerKind::Sum:
            if (input.kind == Folded::Kind::Duration) {
                return Folded::of_duration(Duration{});
            }
            return input.kind == Folded::Kind::Int ? Folded::of_int(0) : Folded::of_float(0.0F);
        case ReducerKind::Any:
            return Folded::of_bool(false);
        default:
            return {};
    }
}

// The first construct that keeps a row at rest from proving it changes nothing.
struct RestProof {
    const CellDomainNode* cells = nullptr;
    const std::string* retained = nullptr;  // the cell alias whose fields the handler may write
    RestFold* fold              = nullptr;
    std::optional<std::pair<SourceLocation, std::string>> failure;

    void fail(const SourceLocation& location, std::string what) {
        if (!failure.has_value()) {
            failure.emplace(location, std::move(what));
        }
    }

    [[nodiscard]] bool writes_own_cell(const VarAssign& assign) const {
        return assign.target == nullptr && assign.path.size() == 1 && cells->names(assign.name) &&
               (retained == nullptr || *retained == assign.name);
    }

    void check_assign(const VarAssign& assign) {
        if (assign.target == nullptr && assign.path.empty() && fold->locals().contains(assign.name)) {
            const auto value = fold->fold(*assign.value);
            fold->bind(assign.name, assign.op == "=" ? value : Folded{});
            return;
        }
        const auto spelling = assign.name + (assign.path.empty() ? "" : "." + assign.path.front());
        if (!writes_own_cell(assign)) {
            fail(assign.location, "the write to '" + spelling + "'");
            return;
        }
        const auto current = fold->cell_default(assign.name, assign.path.front());
        const auto value   = fold->fold(*assign.value);
        Folded stored      = value;
        if (assign.op != "=" && current.has_value()) {
            stored = arithmetic(std::string(1, assign.op.front()), *current, value);
        }
        if (!current.has_value() || !stored.same_bits(*current)) {
            fail(assign.location, "the write to '" + spelling + "'");
        }
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- nested blocks
    void check_block(const std::vector<std::unique_ptr<StmtNode>>& body) {
        for (const auto& stmt : body) {
            if (stmt == nullptr || failure.has_value()) {
                return;
            }
            if (std::holds_alternative<ReturnStmt>(stmt->stmt)) {
                return;
            }
            check(*stmt);
        }
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- nested blocks
    void check_branches(const IfStmt& branch) {
        std::vector<std::pair<const ExprNode*, const std::vector<std::unique_ptr<StmtNode>>*>> arms;
        arms.emplace_back(branch.condition.get(), &branch.then_body);
        for (const auto& else_if : branch.else_if_branches) {
            arms.emplace_back(else_if.condition.get(), &else_if.body);
        }
        arms.emplace_back(nullptr, &branch.else_body);
        // A constant condition selects its branch; an unknown one needs every branch to pass.
        const auto before = fold->locals();
        std::unordered_set<std::string> changed;
        for (const auto& [condition, body] : arms) {
            const auto value = condition == nullptr ? Folded::of_bool(true) : fold->fold(*condition);
            if (value.is_false()) {
                continue;
            }
            fold->restore(before);
            check_block(*body);
            for (const auto& [name, held] : before) {
                if (!fold->locals().at(name).same_bits(held)) {
                    changed.insert(name);
                }
            }
            if (value.is_true()) {
                break;
            }
        }
        fold->restore(before);
        for (const auto& name : changed) {
            fold->forget(name);
        }
    }

    // NOLINTNEXTLINE(misc-no-recursion) -- nested blocks
    void check(const StmtNode& stmt) {
        std::visit(
            [&](const auto& node) {
                using S = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<S, LetStmt>) {
                    fold->bind(node.name, fold->fold(*node.value));
                } else if constexpr (std::is_same_v<S, VarAssign>) {
                    check_assign(node);
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    check_branches(node);
                } else if constexpr (std::is_same_v<S, ForeachStmt>) {
                    check_block(node.body);
                } else if constexpr (std::is_same_v<S, MatchStmt>) {
                    for (const auto& arm : node.arms) {
                        check_block(arm.body);
                    }
                } else if constexpr (std::is_same_v<S, EmitStmt>) {
                    fail(node.location, "`emit " + node.event_name + "`");
                } else {
                    fail(stmt.location, "a statement with an effect");
                }
            },
            stmt.stmt);
    }
};

// Whether some `where:` predicate is false at the defaults.
bool guarded_at_rest(const RuleNode& rule, const RestFold& fold) {
    return rule.where_clause.has_value() &&
           std::ranges::any_of(rule.where_clause->predicates,
                               [&](const auto& predicate) { return fold.fold(*predicate).is_false(); });
}

// A sum that folds to zero or an `any` that folds to false adds nothing for a row at rest.
void check_reducers(const ReduceClause& reduce, RestFold& fold, RestProof& proof) {
    for (const auto& reducer : reduce.reducers) {
        const auto value = reducer.input == nullptr ? Folded{} : fold.fold(*reducer.input);
        const bool idle  = (reducer.kind == ReducerKind::Sum && value.is_zero()) ||
                          (reducer.kind == ReducerKind::Any && value.is_false());
        if (!idle) {
            proof.fail(reducer.location, "reducer '" + reducer.name + "'");
        }
        fold.bind(reducer.name, aggregate_identity(reducer, value));
    }
}

std::unordered_map<std::string, const ExprNode*> cell_defaults(const TraitNode* node) {
    std::unordered_map<std::string, const ExprNode*> defaults;
    if (node == nullptr) {
        return defaults;
    }
    for (const auto& field : node->fields) {
        if (field.modifiers.is_var && field.default_value.has_value()) {
            defaults.emplace(field.name, field.default_value->get());
        }
    }
    return defaults;
}

}  // namespace

void SemanticAnalyzer::prove_rest_law(RuleNode& rule) {
    auto& cells       = *rule.cells;
    const auto* trait = find_resolved_trait(cells.trait);
    if (trait == nullptr) {
        return;
    }
    // Defaults are known for traits of this module, whose declarations analysis holds.
    const auto defaults = cell_defaults(find_local_trait_node(cells.trait));
    collect_rest_fields(rule, *trait);
    RestFold fold(cells, const_initializers_, defaults);
    if (guarded_at_rest(rule, fold)) {
        cells.keeps_rest = true;
        return;
    }
    const auto& per = rule.reduce.has_value() ? rule.reduce->per_binding : std::nullopt;
    RestProof proof{.cells = &cells, .retained = per.has_value() && cells.names(*per) ? &*per : nullptr, .fold = &fold};
    if (rule.reduce.has_value()) {
        check_reducers(*rule.reduce, fold, proof);
    }
    // A reduced handler that keeps no cell runs once per group, never per row.
    if (!rule.reduce.has_value() || proof.retained != nullptr) {
        for (const auto& handler : rule.handlers) {
            if (handler.alias.has_value()) {
                fold.forget(*handler.alias);
            }
            proof.check_block(handler.body);
        }
    }
    cells.keeps_rest = !proof.failure.has_value();
    if (cells.keeps_rest || trait->has_only_enum_dimensions()) {
        return;
    }
    const auto& [location, what] = *proof.failure;
    errors_.warning(location,
                    "rule '" + rule.name + "' visits every cell of '" + cells.trait_name + "': " + what +
                        " may change a cell that holds its defaults; add a `where:` condition that is false at the "
                        "defaults, such as " +
                        rest_guard_suggestion(rule, defaults));
}

const TraitNode* SemanticAnalyzer::find_local_trait_node(const SymbolId& trait) const {
    if (result_.ast == nullptr || trait.module != current_module_id_) {
        return nullptr;
    }
    for (const auto& decl : result_.ast->declarations) {
        if (const auto* node = std::get_if<TraitNode>(&decl); node != nullptr && node->name == trait.local_name) {
            return node;
        }
    }
    return nullptr;
}

void SemanticAnalyzer::collect_rest_fields(RuleNode& rule, const ResolvedTrait& trait) {
    auto& cells = *rule.cells;
    std::unordered_set<std::string> named;
    const auto note = [&](const std::string& alias, const std::string& field) {
        if (cells.names(alias) && trait.find_cell_field(field) != nullptr) {
            named.insert(field);
        }
    };
    const auto read = [&](const ExprNode& root) {
        visit_expression(root, [&](const ExprNode& node) {
            const auto* member = std::get_if<MemberExpr>(&node.expr);
            const auto* owner  = member == nullptr ? nullptr : std::get_if<IdentExpr>(&member->object->expr);
            if (owner != nullptr) {
                note(owner->name, member->member);
            }
        });
    };
    for_each_rule_body_expression(rule, read);
    if (rule.reduce.has_value()) {
        for_each_reducer_expr(*rule.reduce, read);
    }
    for (const auto& handler : rule.handlers) {
        for_each_nested_statement(handler.body, [&](const StmtNode& stmt) {
            if (const auto* assign = std::get_if<VarAssign>(&stmt.stmt);
                assign != nullptr && assign->target == nullptr && !assign->path.empty()) {
                note(assign->name, assign->path.front());
            }
        });
    }
    cells.rest_fields.clear();
    for (const auto& field : trait.fields) {
        if (named.contains(field.name)) {
            cells.rest_fields.push_back(field.name);
        }
    }
}

std::string SemanticAnalyzer::rest_guard_suggestion(const RuleNode& rule,
                                                    const std::unordered_map<std::string, const ExprNode*>& defaults) const {
    const auto& cells = *rule.cells;
    const auto& alias = cells.aliases.front();
    const auto* trait = find_resolved_trait(cells.trait);
    if (cells.rest_fields.empty() || trait == nullptr) {
        return "`where: " + alias + ".<field> != <default>`";
    }
    const auto& field = cells.rest_fields.front();
    const auto found  = std::ranges::find(trait->fields, field, &ResolvedField::name);
    const auto kind   = found == trait->fields.end() ? TypeKind::Unknown : found->type.kind;
    const auto value  = defaults.find(field);
    const auto* literal =
        value == defaults.end() ? nullptr : std::get_if<LiteralExpr>(&value->second->expr);
    const auto spelling = alias + "." + field;
    if (kind == TypeKind::Bool && literal != nullptr && literal->value == "false") {
        return "`where: " + spelling + "`";
    }
    if ((kind == TypeKind::Float || kind == TypeKind::Int) && literal != nullptr &&
        std::stod(literal->value) == 0.0) {
        return "`where: " + spelling + (kind == TypeKind::Float ? " > 0.0`" : " > 0`");
    }
    return "`where: " + spelling + " != " + (literal != nullptr ? literal->value : std::string("<default>")) + "`";
}

}  // namespace cactus
