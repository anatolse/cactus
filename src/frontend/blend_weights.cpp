#include "frontend/blend_weights.hpp"

#include "common/duration.hpp"
#include "common/error_reporter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cactus {

namespace {

constexpr double kInfinity     = std::numeric_limits<double>::infinity();
constexpr double kIntLimit     = static_cast<double>(std::numeric_limits<std::int32_t>::max());
constexpr double kUnitsLimit   = static_cast<double>(std::numeric_limits<std::int64_t>::max());
constexpr std::size_t kMaxCall = 32;

// ── Ranges ──────────────────────────────────────────────────────────────────

// `Number` is a `float` or an `int`; a `Duration` range counts units.
enum class Kind : std::uint8_t { Number, Duration, Other };

struct Range {
    Kind kind    = Kind::Other;
    bool integer = false;
    WeightBound lo;
    WeightBound hi;
};

UnboundedSource other_source(std::string spelling) {
    return UnboundedSource{.kind = UnboundedSource::Kind::Other, .spelling = std::move(spelling)};
}

Range unknown(Kind kind, const UnboundedSource& source, bool integer = false) {
    return Range{.kind = kind, .integer = integer, .lo = {.source = source}, .hi = {.source = source}};
}

Range point(double value, Kind kind = Kind::Number, bool integer = false) {
    return Range{.kind = kind, .integer = integer, .lo = {.value = value}, .hi = {.value = value}};
}

// The single value of a range that holds one.
std::optional<double> point_value(const Range& range) {
    return range.lo.value.has_value() && range.lo.value == range.hi.value ? range.lo.value : std::nullopt;
}

template <typename Function>
Range map_values(Range range, Function function) {
    for (auto* side : {&range.lo, &range.hi}) {
        if (side->value.has_value()) {
            side->value = function(*side->value);
        }
    }
    return range;
}

double lo_or_infinite(const WeightBound& side) {
    return side.value.value_or(-kInfinity);
}

double hi_or_infinite(const WeightBound& side) {
    return side.value.value_or(kInfinity);
}

// A computed side; an infinite value has no bound and takes `source`.
WeightBound side_of(double value, const UnboundedSource& source) {
    if (std::isinf(value) || std::isnan(value)) {
        return WeightBound{.source = source};
    }
    return WeightBound{.value = value};
}

// The source of whichever side has no bound, preferring the first.
const UnboundedSource& unbounded_of(const WeightBound& first, const WeightBound& second) {
    return first.value.has_value() ? second.source : first.source;
}

// Rounds a `float` result as binary32 does; a result outside `int` or the unit count range wraps.
Range settle(Range range, const UnboundedSource& wrap_source) {
    if (range.kind == Kind::Number && !range.integer) {
        for (auto* side : {&range.lo, &range.hi}) {
            if (side->value.has_value()) {
                *side = side_of(static_cast<double>(static_cast<float>(*side->value)), wrap_source);
            }
        }
        return range;
    }
    const double limit = range.kind == Kind::Duration ? kUnitsLimit : kIntLimit;
    const auto wraps   = [&](const WeightBound& side) {
        return side.value.has_value() && std::abs(*side.value) >= limit;
    };
    if (range.kind != Kind::Other && (wraps(range.lo) || wraps(range.hi))) {
        return unknown(range.kind, wrap_source, range.integer);
    }
    return range;
}

Range negate(const Range& operand) {
    Range result = operand;
    result.lo    = operand.hi.value.has_value() ? WeightBound{.value = -*operand.hi.value} : operand.hi;
    result.hi    = operand.lo.value.has_value() ? WeightBound{.value = -*operand.lo.value} : operand.lo;
    return result;
}

Range add(const Range& left, const Range& right, Kind kind, bool integer) {
    const auto sum = [](const WeightBound& a, const WeightBound& b) {
        return a.value.has_value() && b.value.has_value() ? WeightBound{.value = *a.value + *b.value}
                                                          : WeightBound{.source = unbounded_of(a, b)};
    };
    return Range{.kind = kind, .integer = integer, .lo = sum(left.lo, right.lo), .hi = sum(left.hi, right.hi)};
}

double times(double a, double b) {
    return a == 0.0 || b == 0.0 ? 0.0 : a * b;
}

Range multiply(const Range& left, const Range& right, Kind kind, bool integer) {
    struct Corner {
        double value;
        const WeightBound* a;
        const WeightBound* b;
    };
    using Corners        = std::array<std::pair<double, const WeightBound*>, 2>;
    const Corners lefts  = {{{lo_or_infinite(left.lo), &left.lo}, {hi_or_infinite(left.hi), &left.hi}}};
    const Corners rights = {{{lo_or_infinite(right.lo), &right.lo}, {hi_or_infinite(right.hi), &right.hi}}};
    std::vector<Corner> corners;
    for (const auto& [a, a_side] : lefts) {
        for (const auto& [b, b_side] : rights) {
            corners.push_back(Corner{.value = times(a, b), .a = a_side, .b = b_side});
        }
    }
    const auto [low, high] = std::ranges::minmax(corners, {}, &Corner::value);
    return Range{.kind    = kind,
                 .integer = integer,
                 .lo      = side_of(low.value, unbounded_of(*low.a, *low.b)),
                 .hi      = side_of(high.value, unbounded_of(*high.a, *high.b))};
}

bool may_be_zero(const Range& range) {
    return lo_or_infinite(range.lo) <= 0.0 && hi_or_infinite(range.hi) >= 0.0;
}

// What a divisor whose range holds zero names: a side with no bound, or the divisor itself.
UnboundedSource divisor_source(const Range& divisor, const std::string& written) {
    if (!divisor.lo.value.has_value()) {
        return divisor.lo.source;
    }
    if (!divisor.hi.value.has_value()) {
        return divisor.hi.source;
    }
    return other_source(written);
}

Range divide(const Range& left, const Range& right, Kind kind, bool integer, const std::string& divisor) {
    if (may_be_zero(right)) {
        return unknown(kind, divisor_source(right, divisor), integer);
    }
    const auto inverse = [](const WeightBound& side) {
        return WeightBound{.value = 1.0 / side.value.value_or(kInfinity)};
    };
    auto low_inverse  = inverse(right.hi);
    auto high_inverse = inverse(right.lo);
    if (!right.lo.value.has_value()) {
        high_inverse.value = -0.0;
    }
    const auto result =
        multiply(left, Range{.kind = Kind::Number, .lo = low_inverse, .hi = high_inverse}, kind, integer);
    return integer ? map_values(result, [](double value) { return std::trunc(value); }) : result;
}

Range unite(const Range& first, const Range& second) {
    if (first.kind != second.kind) {
        return unknown(Kind::Other, first.lo.source);
    }
    const auto pick = [](const WeightBound& a, const WeightBound& b, auto choose) {
        return a.value.has_value() && b.value.has_value() ? WeightBound{.value = choose(*a.value, *b.value)}
                                                          : WeightBound{.source = unbounded_of(a, b)};
    };
    return Range{.kind    = first.kind,
                 .integer = first.integer && second.integer,
                 .lo      = pick(first.lo, second.lo, [](double a, double b) { return std::min(a, b); }),
                 .hi      = pick(first.hi, second.hi, [](double a, double b) { return std::max(a, b); })};
}

// `min` keeps the lower of each side; an unbounded upper side loses to a bounded one.
Range minimum(const Range& a, const Range& b) {
    Range result      = unite(a, b);
    const double high = std::min(hi_or_infinite(a.hi), hi_or_infinite(b.hi));
    result.hi         = side_of(high, unbounded_of(a.hi, b.hi));
    return result;
}

Range maximum(const Range& a, const Range& b) {
    Range result     = unite(a, b);
    const double low = std::max(lo_or_infinite(a.lo), lo_or_infinite(b.lo));
    result.lo        = side_of(low, unbounded_of(a.lo, b.lo));
    return result;
}

// `clamp(x, lo, hi)` gives `lo` below it, `hi` above it, and `x` between them.
Range clamp(const Range& x, const Range& low, const Range& high) {
    return maximum(minimum(maximum(x, low), unite(low, high)), minimum(low, high));
}

Range absolute(const Range& value) {
    if (lo_or_infinite(value.lo) >= 0.0) {
        return value;
    }
    if (hi_or_infinite(value.hi) <= 0.0) {
        return negate(value);
    }
    auto result = maximum(negate(value), value);
    result.lo   = WeightBound{.value = 0.0};
    return result;
}

Range seconds_of(const Range& units) {
    auto result = map_values(units, [](double value) { return value / static_cast<double>(kDurationUnitsPerSecond); });
    result.kind = Kind::Number;
    result.integer = false;
    return result;
}

// Narrows a value known to satisfy `value <op> constant`.
Range narrowed(Range value, std::string_view op, double constant) {
    if (op == "<" || op == "<=") {
        value.hi = WeightBound{.value = std::min(hi_or_infinite(value.hi), constant)};
    } else {
        value.lo = WeightBound{.value = std::max(lo_or_infinite(value.lo), constant)};
    }
    return value;
}

std::string_view mirrored(std::string_view op) {
    if (op == "<") {
        return ">";
    }
    if (op == "<=") {
        return ">=";
    }
    if (op == ">") {
        return "<";
    }
    return "<=";
}

std::string_view negated(std::string_view op) {
    if (op == "<") {
        return ">=";
    }
    if (op == "<=") {
        return ">";
    }
    if (op == ">") {
        return "<=";
    }
    return "<";
}

bool is_comparison(std::string_view op) {
    return op == "<" || op == "<=" || op == ">" || op == ">=";
}

// ── Spelling ────────────────────────────────────────────────────────────────

std::string spelling(const ExprNode& expr);

std::string spelling_of(const TraitAccessExpr& access) {
    std::string text =
        access.short_form ? spelling(*access.object) : spelling(*access.object) + "[" + access.trait_name + "]";
    if (!access.cell_keys.empty()) {
        text += "[";
        for (std::size_t index = 0; index < access.cell_keys.size(); ++index) {
            text += (index == 0 ? "" : ", ") + spelling(*access.cell_keys[index]);
        }
        text += "]";
    }
    return text;
}

std::string spelling(const ExprNode& expr) {
    if (const auto* ident = std::get_if<IdentExpr>(&expr.expr)) {
        return ident->name;
    }
    if (const auto* member = std::get_if<MemberExpr>(&expr.expr)) {
        return spelling(*member->object) + "." + member->member;
    }
    if (const auto* access = std::get_if<TraitAccessExpr>(&expr.expr)) {
        return spelling_of(*access);
    }
    if (const auto* literal = std::get_if<LiteralExpr>(&expr.expr)) {
        return literal->value;
    }
    if (const auto* call = std::get_if<CallExpr>(&expr.expr)) {
        return spelling(*call->callee) + "(...)";
    }
    if (std::holds_alternative<SelfExpr>(expr.expr)) {
        return "self";
    }
    return "the weight";
}

bool same_list(const std::vector<std::unique_ptr<ExprNode>>& left, const std::vector<std::unique_ptr<ExprNode>>& right);

// Two expressions written alike.
bool same_expr(const ExprNode& left, const ExprNode& right) {
    if (left.expr.index() != right.expr.index()) {
        return false;
    }
    if (const auto* ident = std::get_if<IdentExpr>(&left.expr)) {
        return ident->name == std::get<IdentExpr>(right.expr).name;
    }
    if (const auto* member = std::get_if<MemberExpr>(&left.expr)) {
        const auto& other = std::get<MemberExpr>(right.expr);
        return member->member == other.member && same_expr(*member->object, *other.object);
    }
    if (const auto* access = std::get_if<TraitAccessExpr>(&left.expr)) {
        const auto& other = std::get<TraitAccessExpr>(right.expr);
        return access->trait_name == other.trait_name && access->cell == other.cell &&
               same_expr(*access->object, *other.object) && same_list(access->cell_keys, other.cell_keys);
    }
    if (const auto* literal = std::get_if<LiteralExpr>(&left.expr)) {
        const auto& other = std::get<LiteralExpr>(right.expr);
        return literal->kind == other.kind && literal->value == other.value;
    }
    if (const auto* binary = std::get_if<BinaryExpr>(&left.expr)) {
        const auto& other = std::get<BinaryExpr>(right.expr);
        return binary->op == other.op && same_expr(*binary->left, *other.left) &&
               same_expr(*binary->right, *other.right);
    }
    if (const auto* unary = std::get_if<UnaryExpr>(&left.expr)) {
        const auto& other = std::get<UnaryExpr>(right.expr);
        return unary->op == other.op && same_expr(*unary->operand, *other.operand);
    }
    if (const auto* call = std::get_if<CallExpr>(&left.expr)) {
        const auto& other = std::get<CallExpr>(right.expr);
        return same_expr(*call->callee, *other.callee) && same_list(call->args, other.args);
    }
    return std::holds_alternative<SelfExpr>(left.expr);
}

bool same_list(const std::vector<std::unique_ptr<ExprNode>>& left,
               const std::vector<std::unique_ptr<ExprNode>>& right) {
    return std::ranges::equal(left, right, [](const auto& a, const auto& b) { return same_expr(*a, *b); });
}

std::string format_bound(double value) {
    return std::format("{}", static_cast<float>(value));
}

// ── Program facts ───────────────────────────────────────────────────────────

const ResolvedField* find_field(const std::vector<ResolvedField>& fields, std::string_view name) {
    const auto found = std::ranges::find(fields, name, &ResolvedField::name);
    return found == fields.end() ? nullptr : &*found;
}

Kind kind_of(const TypeInfo& type) {
    switch (type.kind) {
        case TypeKind::Float:
        case TypeKind::Int:
            return Kind::Number;
        case TypeKind::Duration:
            return Kind::Duration;
        default:
            return Kind::Other;
    }
}

bool is_root_frame(const SymbolId& event) {
    return event.module.name == "std.core" && event.local_name == "frame";
}

class ProgramFacts {
public:
    explicit ProgramFacts(const DecoratedProgram& program) {
        for (const auto& trait : program.traits | std::views::values) {
            if (trait.symbol_id.has_value()) {
                traits_.emplace(make_canonical_id(*trait.symbol_id), &trait);
            }
        }
        for (const auto& phase : program.phases | std::views::values) {
            if (phase.symbol_id.has_value()) {
                phases_.emplace(make_canonical_id(*phase.symbol_id), &phase);
            }
        }
        for (const auto& func : program.funcs | std::views::values) {
            if (func.symbol_id.has_value()) {
                func_types_.emplace(make_canonical_id(*func.symbol_id), &func);
            }
        }
        if (program.ast == nullptr) {
            return;
        }
        for (const auto& declaration : program.ast->declarations) {
            if (const auto* func = std::get_if<FuncNode>(&declaration);
                func != nullptr && !func->is_extern && func->resolved_func_id.has_value()) {
                funcs_.emplace(make_canonical_id(*func->resolved_func_id), func);
            }
            if (const auto* block = std::get_if<ConstBlockNode>(&declaration)) {
                for (const auto& constant : block->assignments) {
                    if (constant.resolved_const_id.has_value() && constant.value != nullptr) {
                        constants_.emplace(make_canonical_id(*constant.resolved_const_id), constant.value.get());
                    }
                }
            }
        }
    }

    [[nodiscard]] const ResolvedTrait* trait(const SymbolId& symbol) const {
        return find(traits_, symbol);
    }
    [[nodiscard]] const ResolvedPhase* phase(const SymbolId& symbol) const {
        return find(phases_, symbol);
    }
    [[nodiscard]] const FuncNode* func(const SymbolId& symbol) const {
        return find(funcs_, symbol);
    }
    [[nodiscard]] const ResolvedFunc* func_type(const SymbolId& symbol) const {
        return find(func_types_, symbol);
    }
    [[nodiscard]] const ExprNode* constant(const SymbolId& symbol) const {
        return find(constants_, symbol);
    }

    [[nodiscard]] const ResolvedField* field(const TraitFieldRef& ref) const {
        const auto* owner = trait(ref.trait);
        return owner == nullptr || !ref.field.has_value() ? nullptr : find_field(owner->fields, *ref.field);
    }

private:
    template <typename Value>
    static const Value* find(const std::unordered_map<std::string, const Value*>& map, const SymbolId& symbol) {
        const auto found = map.find(make_canonical_id(symbol));
        return found == map.end() ? nullptr : found->second;
    }

    std::unordered_map<std::string, const ResolvedTrait*> traits_;
    std::unordered_map<std::string, const ResolvedPhase*> phases_;
    std::unordered_map<std::string, const FuncNode*> funcs_;
    std::unordered_map<std::string, const ResolvedFunc*> func_types_;
    std::unordered_map<std::string, const ExprNode*> constants_;
};

// ── Evaluation ──────────────────────────────────────────────────────────────

// What a handler adds to evaluation: its phase binding and the fields it writes.
struct HandlerScope {
    std::string binding;
    const ResolvedPhase* phase = nullptr;
    std::set<std::pair<std::string, std::string>> written;  // canonical trait, field
};

// Named values in scope: `let` bindings, parameters and narrowed fields (keyed `field:<spelling>`).
class Env {
public:
    [[nodiscard]] const Range* find(const std::string& name) const {
        const auto* found = entry(name);
        return found == nullptr ? nullptr : &found->value;
    }
    [[nodiscard]] bool contains(const std::string& name) const {
        return entry(name) != nullptr;
    }
    [[nodiscard]] bool is_mutable(const std::string& name) const {
        const auto* found = entry(name);
        return found != nullptr && found->is_mutable;
    }

    void set(const std::string& name, Range value, bool is_mutable = false) {
        const auto found = std::ranges::find(values_, name, &Entry::name);
        if (found != values_.end()) {
            found->value = std::move(value);
            return;
        }
        values_.push_back(Entry{.name = name, .value = std::move(value), .is_mutable = is_mutable});
    }

private:
    struct Entry {
        std::string name;
        Range value;
        bool is_mutable = false;
    };

    [[nodiscard]] const Entry* entry(const std::string& name) const {
        const auto found = std::ranges::find(values_, name, &Entry::name);
        return found == values_.end() ? nullptr : &*found;
    }

    std::vector<Entry> values_;
};

class Evaluator {
public:
    Evaluator(const ProgramFacts& facts, const HandlerScope* handler)
        : facts_(&facts)
        , handler_(handler) {}

    Range eval(const ExprNode& expr, const Env& env) {
        return std::visit([&](const auto& node) { return this->eval_node(node, expr, env); }, expr.expr);
    }

    // The environments in which `condition` holds and fails.
    std::pair<Env, Env> split(const ExprNode& condition, const Env& env) {
        Env holds = env;
        Env fails = env;
        narrow(condition, true, holds);
        narrow(condition, false, fails);
        return {std::move(holds), std::move(fails)};
    }

    // Walks a statement list; returns whether every path ends in a `return`.
    template <typename Visit>
    bool walk(const std::vector<std::unique_ptr<StmtNode>>& body, Env env, const Visit& visit) {
        for (const auto& stmt : body) {
            if (stmt != nullptr && step(*stmt, env, visit)) {
                return true;
            }
        }
        return false;
    }

private:
    template <typename Visit>
    bool step(const StmtNode& stmt, Env& env, const Visit& visit) {
        if (const auto* let = std::get_if<LetStmt>(&stmt.stmt)) {
            env.set(let->name, eval(*let->value, env), let->is_mutable);
            return false;
        }
        if (const auto* assign = std::get_if<VarAssign>(&stmt.stmt)) {
            visit(*assign, env);
            if (assign->target == nullptr && assign->path.empty() && env.contains(assign->name)) {
                env.set(
                    assign->name,
                    assign->op == "=" ? eval(*assign->value, env) : unknown(Kind::Other, other_source(assign->name)),
                    true);
            }
            return false;
        }
        if (const auto* ret = std::get_if<ReturnStmt>(&stmt.stmt)) {
            if (ret->value.has_value() && returns_ != nullptr) {
                returns_->push_back(eval(**ret->value, env));
            }
            return true;
        }
        if (const auto* branch = std::get_if<IfStmt>(&stmt.stmt)) {
            return step_if(*branch, env, visit);
        }
        if (const auto* loop = std::get_if<ForeachStmt>(&stmt.stmt)) {
            Env inner = env;
            inner.set(loop->var_name, unknown(Kind::Other, other_source(loop->var_name)));
            walk(loop->body, std::move(inner), visit);
            return false;
        }
        if (const auto* match = std::get_if<MatchStmt>(&stmt.stmt)) {
            for (const auto& arm : match->arms) {
                walk(arm.body, env, visit);
            }
        }
        return false;
    }

    // After a branch that always returns, the rest of the block runs where its condition failed.
    template <typename Visit>
    bool step_if(const IfStmt& branch, Env& env, const Visit& visit) {
        auto [holds, fails]     = split(*branch.condition, env);
        const bool then_returns = walk(branch.then_body, holds, visit);
        bool others_return      = true;
        for (const auto& else_if : branch.else_if_branches) {
            auto [inner_holds, inner_fails] = split(*else_if.condition, fails);
            others_return                   = walk(else_if.body, std::move(inner_holds), visit) && others_return;
            fails                           = std::move(inner_fails);
        }
        const bool has_else = !branch.else_body.empty();
        others_return       = (has_else && walk(branch.else_body, fails, visit)) && others_return;
        if (then_returns && others_return) {
            return true;
        }
        if (then_returns && !has_else) {
            env = std::move(fails);
        } else if (others_return && branch.else_if_branches.empty()) {
            env = std::move(holds);
        }
        return false;
    }

    // ── Narrowing ──

    void narrow(const ExprNode& condition, bool holds, Env& env) {
        if (const auto* unary = std::get_if<UnaryExpr>(&condition.expr); unary != nullptr && unary->op == "not") {
            narrow(*unary->operand, !holds, env);
            return;
        }
        const auto* binary = std::get_if<BinaryExpr>(&condition.expr);
        if (binary == nullptr) {
            return;
        }
        if ((binary->op == "and" && holds) || (binary->op == "or" && !holds)) {
            narrow(*binary->left, holds, env);
            narrow(*binary->right, holds, env);
            return;
        }
        if (!is_comparison(binary->op)) {
            return;
        }
        const auto op = holds ? std::string_view{binary->op} : negated(binary->op);
        narrow_side(*binary->left, op, *binary->right, env);
        narrow_side(*binary->right, mirrored(op), *binary->left, env);
    }

    void narrow_side(const ExprNode& subject, std::string_view op, const ExprNode& bound, Env& env) {
        const auto key = narrowing_key(subject, env);
        if (key.empty()) {
            return;
        }
        const auto constant = eval(bound, Env{});
        const auto value    = point_value(constant);
        if (constant.kind != Kind::Number || !value.has_value()) {
            return;
        }
        env.set(key, narrowed(eval(subject, env), op, *value));
    }

    // A `let` binding, a parameter, or a field the handler does not write; empty otherwise.
    [[nodiscard]] std::string narrowing_key(const ExprNode& subject, const Env& env) const {
        if (const auto* ident = std::get_if<IdentExpr>(&subject.expr)) {
            return env.contains(ident->name) && !env.is_mutable(ident->name) ? ident->name : std::string{};
        }
        const auto& read = subject.field_read;
        if (!read.has_value() || !read->field.has_value() || !std::holds_alternative<MemberExpr>(subject.expr)) {
            return {};
        }
        if (handler_ != nullptr && handler_->written.contains({make_canonical_id(read->trait), *read->field})) {
            return {};
        }
        return "field:" + spelling(subject);
    }

    // ── Nodes ──

    static Range eval_node(const LiteralExpr& literal, const ExprNode& expr, const Env& /*env*/) {
        try {
            switch (literal.kind) {
                case LiteralExpr::Kind::Int:
                    return point(std::stod(literal.value), Kind::Number, true);
                case LiteralExpr::Kind::Float:
                    return point(static_cast<double>(std::stof(literal.value)));
                case LiteralExpr::Kind::Duration: {
                    const auto units = duration_literal_units(literal.value);
                    return units.has_value() ? point(static_cast<double>(*units), Kind::Duration)
                                             : unknown(Kind::Duration, other_source(spelling(expr)));
                }
                default:
                    break;
            }
        } catch (...) {
            return unknown(Kind::Number, other_source(spelling(expr)));
        }
        return unknown(Kind::Other, other_source(spelling(expr)));
    }

    Range eval_node(const IdentExpr& ident, const ExprNode& expr, const Env& env) {
        if (const auto* found = env.find(ident.name)) {
            return *found;
        }
        if (ident.resolved_const_id.has_value()) {
            return eval_constant(*ident.resolved_const_id, expr);
        }
        return unknown(Kind::Other, other_source(ident.name));
    }

    Range eval_node(const MemberExpr& member, const ExprNode& expr, const Env& env) {
        if (member.resolved_const_id.has_value()) {
            return eval_constant(*member.resolved_const_id, expr);
        }
        if (member.resolved_phase_dt.has_value()) {
            const auto* phase = facts_->phase(*member.resolved_phase_dt);
            return phase == nullptr ? unknown(Kind::Other, other_source(spelling(expr)))
                                    : phase_field(*phase, member.member, spelling(expr));
        }
        if (expr.field_read.has_value()) {
            return field_value(expr, env);
        }
        if (handler_ != nullptr && handler_->phase != nullptr && spelling(*member.object) == handler_->binding &&
            !env.contains(handler_->binding)) {
            return phase_field(*handler_->phase, member.member, spelling(expr));
        }
        return unknown(Kind::Other, other_source(spelling(expr)));
    }

    Range eval_node(const UnaryExpr& unary, const ExprNode& expr, const Env& env) {
        auto operand = eval(*unary.operand, env);
        if (unary.op != "-" || operand.kind == Kind::Other) {
            return unknown(Kind::Other, other_source(spelling(expr)));
        }
        return settle(negate(operand), other_source(spelling(expr)));
    }

    Range eval_node(const BinaryExpr& binary, const ExprNode& expr, const Env& env) {
        const auto left  = eval(*binary.left, env);
        const auto right = eval(*binary.right, env);
        const auto wrap  = other_source(spelling(expr));
        if (left.kind == Kind::Other || right.kind == Kind::Other) {
            return unknown(Kind::Other, wrap);
        }
        if (left.kind == Kind::Duration || right.kind == Kind::Duration) {
            return settle(duration_binary(binary.op, left, right, spelling(*binary.right)), wrap);
        }
        const bool integer = left.integer && right.integer;
        if (binary.op == "+") {
            return settle(add(left, right, Kind::Number, integer), wrap);
        }
        if (binary.op == "-") {
            return settle(add(left, negate(right), Kind::Number, integer), wrap);
        }
        if (binary.op == "*") {
            return settle(multiply(left, right, Kind::Number, integer), wrap);
        }
        if (binary.op == "/") {
            return settle(divide(left, right, Kind::Number, integer, spelling(*binary.right)), wrap);
        }
        return unknown(is_comparison(binary.op) || binary.op == "==" || binary.op == "!=" ? Kind::Other : Kind::Number,
                       wrap,
                       integer);
    }

    Range eval_node(const CallExpr& call, const ExprNode& expr, const Env& env) {
        std::vector<Range> args;
        args.reserve(call.args.size());
        for (const auto& arg : call.args) {
            args.push_back(eval(*arg, env));
        }
        const auto source = other_source(spelling(expr));
        if (const auto* name = std::get_if<IdentExpr>(&call.callee->expr);
            name != nullptr && !call.resolved_callee_id.has_value() && args.size() == 1) {
            return conversion(name->name, args.front(), source);
        }
        if (!call.resolved_callee_id.has_value()) {
            return unknown(Kind::Other, source);
        }
        const auto& callee = *call.resolved_callee_id;
        if (callee.module.name == "std.math") {
            if (auto result = math_call(callee.local_name, args); result.has_value()) {
                return *result;
            }
        }
        if (const auto* func = facts_->func(callee)) {
            return call_func(*func, call, std::move(args), source);
        }
        const auto* type = facts_->func_type(callee);
        return unknown(type != nullptr && type->return_type.has_value() ? kind_of(*type->return_type) : Kind::Other,
                       source);
    }

    Range eval_node(const IfExpr& branch, const ExprNode& /*expr*/, const Env& env) {
        const auto [holds, fails] = split(*branch.condition, env);
        return unite(eval(*branch.then_expr, holds), eval(*branch.else_expr, fails));
    }

    template <typename Node>
    Range eval_node(const Node& /*node*/, const ExprNode& expr, const Env& /*env*/) {
        return unknown(Kind::Other, other_source(spelling(expr)));
    }

    // ── Sources ──

    Range eval_constant(const SymbolId& symbol, const ExprNode& expr) {
        const auto key   = make_canonical_id(symbol);
        const auto* body = facts_->constant(symbol);
        if (body == nullptr || !constants_in_progress_.insert(key).second) {
            return unknown(Kind::Other, other_source(spelling(expr)));
        }
        auto value = eval(*body, Env{});
        constants_in_progress_.erase(key);
        return value;
    }

    Range phase_field(const ResolvedPhase& phase, const std::string& name, const std::string& written) {
        if (phase.has_every && phase.every_seconds.has_value() && name == "dt") {
            return point(static_cast<double>(static_cast<float>(*phase.every_seconds)));
        }
        if (phase.has_every && phase.every_units.has_value() && name == "delta") {
            return point(static_cast<double>(*phase.every_units), Kind::Duration);
        }
        if (phase.has_every && name == "alpha") {
            return Range{.kind = Kind::Number, .lo = {.value = 0.0}, .hi = {.value = 1.0}};
        }
        const auto* field = find_field(phase.fields, name);
        if (field == nullptr || !field->source_binding.has_value()) {
            return unknown(field == nullptr ? Kind::Other : kind_of(field->type), other_source(written));
        }
        const auto& binding = *field->source_binding;
        if (binding.kind == PhaseFieldSource::Kind::UpstreamPhase) {
            const auto* upstream = facts_->phase(binding.source);
            return upstream == nullptr ? unknown(kind_of(field->type), other_source(written))
                                       : phase_field(*upstream, binding.member, written);
        }
        if (is_root_frame(binding.source) && (binding.member == "dt" || binding.member == "delta")) {
            return Range{.kind = binding.member == "dt" ? Kind::Number : Kind::Duration,
                         .lo   = {.value = 0.0},
                         .hi   = {.source = {.kind = UnboundedSource::Kind::PhaseStep, .spelling = written}}};
        }
        return unknown(kind_of(field->type), other_source(written));
    }

    Range field_value(const ExprNode& expr, const Env& env) {
        const auto written = spelling(expr);
        if (const auto* narrowed_value = env.find("field:" + written)) {
            return *narrowed_value;
        }
        const auto& read  = *expr.field_read;
        const auto* field = facts_->field(read);
        if (field == nullptr) {
            return unknown(Kind::Other, other_source(written));
        }
        if (field->clamp.has_value()) {
            return clamped(*field, *facts_->trait(read.trait), written);
        }
        if (field->range.has_value()) {
            return ranged(*field, written);
        }
        const auto kind    = kind_of(field->type);
        const bool integer = field->type.kind == TypeKind::Int;
        if (field->is_var) {
            return unknown(
                kind,
                UnboundedSource{.kind = UnboundedSource::Kind::VarField, .spelling = written, .field = field->name},
                integer);
        }
        return unknown(kind, other_source(written), integer);
    }

    // A ranged `int` field lies in `[lo, hi - 1]` when both bounds are constants.
    Range ranged(const ResolvedField& field, const std::string& written) {
        const auto& range = *field.range;
        const auto source = other_source(written);
        Range result      = unknown(Kind::Number, source, true);
        const auto low    = range.lo != nullptr ? point_value(eval(*range.lo, Env{})) : std::nullopt;
        const auto high   = range.hi != nullptr ? point_value(eval(*range.hi, Env{})) : std::nullopt;
        if (low.has_value()) {
            result.lo = WeightBound{.value = *low};
        }
        if (high.has_value()) {
            result.hi = WeightBound{.value = *high - 1.0};
        }
        return result;
    }

    Range clamped(const ResolvedField& field, const ResolvedTrait& trait, const std::string& written) {
        const auto& clamp  = *field.clamp;
        const auto kind    = kind_of(field.type);
        const auto source  = other_source(written);
        const bool integer = field.type.kind == TypeKind::Int;
        Range result       = unknown(kind, source, integer);
        const auto bound   = [&](const std::optional<ResolvedClampBound>& side) -> std::optional<Range> {
            if (!side.has_value()) {
                return std::nullopt;
            }
            if (!side->let_field.empty()) {
                const auto* let = find_field(trait.fields, side->let_field);
                return let != nullptr && let->range.has_value() ? ranged(*let, written) : unknown(kind, source);
            }
            return side->expr != nullptr ? eval(*side->expr, Env{}) : unknown(kind, source);
        };
        if (const auto low = bound(clamp.min); low.has_value()) {
            result.lo = low->lo.value.has_value() ? low->lo : WeightBound{.source = source};
        }
        if (const auto high = bound(clamp.max); high.has_value()) {
            result.hi = high->hi.value.has_value() ? high->hi : WeightBound{.source = source};
        }
        return result;
    }

    static Range conversion(const std::string& name, const Range& arg, const UnboundedSource& source) {
        if (name == "seconds" && arg.kind == Kind::Duration) {
            return settle(seconds_of(arg), source);
        }
        if (name == "duration" && arg.kind == Kind::Number) {
            return settle(multiply(arg, point(static_cast<double>(kDurationUnitsPerSecond)), Kind::Duration, false),
                          source);
        }
        return unknown(Kind::Other, source);
    }

    static std::optional<Range> math_call(const std::string& name, const std::vector<Range>& args) {
        const bool scalar = std::ranges::all_of(args, [](const Range& arg) { return arg.kind != Kind::Other; });
        if (!scalar) {
            return std::nullopt;
        }
        if (name == "min" && args.size() == 2) {
            return minimum(args[0], args[1]);
        }
        if (name == "max" && args.size() == 2) {
            return maximum(args[0], args[1]);
        }
        if (name == "clamp" && args.size() == 3) {
            return clamp(args[0], args[1], args[2]);
        }
        if (name == "abs" && args.size() == 1) {
            return absolute(args[0]);
        }
        return std::nullopt;
    }

    Range call_func(const FuncNode& func,
                    const CallExpr& call,
                    std::vector<Range> args,
                    const UnboundedSource& source) {
        const auto key = make_canonical_id(*func.resolved_func_id);
        if (call_stack_.size() >= kMaxCall || std::ranges::contains(call_stack_, key)) {
            return unknown(Kind::Number, source);
        }
        Env env;
        for (std::size_t index = 0; index < args.size() && index < func.params.size(); ++index) {
            const auto& name = call.has_named_args() && !call.arg_names[index].empty() ? call.arg_names[index]
                                                                                       : func.params[index].name;
            env.set(name, std::move(args[index]));
        }
        for (const auto& param : func.params) {
            if (!env.contains(param.name)) {
                env.set(param.name, unknown(Kind::Other, other_source(param.name)));
            }
        }
        Evaluator inner(*facts_, nullptr);
        inner.call_stack_ = call_stack_;
        inner.call_stack_.push_back(key);
        std::vector<Range> returns;
        inner.returns_ = &returns;
        inner.walk(func.body, std::move(env), [](const VarAssign&, const Env&) {});
        return std::ranges::fold_left_first(returns, unite).value_or(unknown(Kind::Other, source));
    }

    static Range duration_binary(const std::string& op,
                                 const Range& left,
                                 const Range& right,
                                 const std::string& divisor) {
        const bool both = left.kind == Kind::Duration && right.kind == Kind::Duration;
        if (both && (op == "+" || op == "-")) {
            return add(left, op == "+" ? right : negate(right), Kind::Duration, true);
        }
        if (both && op == "/") {
            return divide(left, right, Kind::Number, false, divisor);
        }
        const bool scaled_left = left.kind == Kind::Duration && right.integer;
        if (scaled_left && op == "*") {
            return multiply(left, right, Kind::Duration, true);
        }
        if (scaled_left && op == "/") {
            return map_values(divide(left, right, Kind::Duration, false, divisor),
                              [](double value) { return std::floor(value); });
        }
        if (right.kind == Kind::Duration && left.integer && op == "*") {
            return multiply(left, right, Kind::Duration, true);
        }
        return unknown(Kind::Other, other_source(divisor));
    }

    const ProgramFacts* facts_;
    const HandlerScope* handler_;
    std::vector<std::string> call_stack_;
    std::vector<Range>* returns_ = nullptr;
    std::unordered_set<std::string> constants_in_progress_;
};

// ── Blend recognition ───────────────────────────────────────────────────────

// The written field `x` of an assignment, and whether a read names the same access.
struct Target {
    const VarAssign* assign;

    [[nodiscard]] std::string spelling_text() const {
        if (assign->target != nullptr) {
            return spelling(*assign->target);
        }
        std::string text = assign->name;
        for (const auto& segment : assign->path) {
            text += "." + segment;
        }
        return text;
    }

    [[nodiscard]] bool same(const ExprNode& read) const {
        if (assign->target != nullptr) {
            return same_expr(*assign->target, read);
        }
        return std::holds_alternative<MemberExpr>(read.expr) && spelling(read) == spelling_text();
    }
};

struct Factor {
    const ExprNode* expr = nullptr;
    bool inverse         = false;
};

// `goal - x` when `toward`, `x - goal` otherwise.
bool is_difference(const ExprNode& expr, const Target& target, bool toward) {
    const auto* binary = std::get_if<BinaryExpr>(&expr.expr);
    return binary != nullptr && binary->op == "-" && target.same(toward ? *binary->right : *binary->left);
}

// Collects the factors beside the difference along the product that holds it; false when none holds it.
bool collect_factors(const ExprNode& expr, const Target& target, bool toward, std::vector<Factor>& out) {
    if (is_difference(expr, target, toward)) {
        return true;
    }
    const auto* binary = std::get_if<BinaryExpr>(&expr.expr);
    if (binary == nullptr || (binary->op != "*" && binary->op != "/")) {
        return false;
    }
    if (collect_factors(*binary->left, target, toward, out)) {
        out.push_back(Factor{.expr = binary->right.get(), .inverse = binary->op == "/"});
        return true;
    }
    if (binary->op == "*" && collect_factors(*binary->right, target, toward, out)) {
        out.push_back(Factor{.expr = binary->left.get()});
        return true;
    }
    return false;
}

// The weight factors of `difference * w...`, or nullopt when `expr` is not that product.
std::optional<std::vector<Factor>> weight_factors(const ExprNode& expr, const Target& target, bool toward) {
    std::vector<Factor> factors;
    if (!collect_factors(expr, target, toward, factors)) {
        return std::nullopt;
    }
    return factors;
}

bool is_lerp(const CallExpr& call) {
    if (!call.resolved_callee_id.has_value() || call.args.size() != 3 || call.has_named_args()) {
        return false;
    }
    const auto& callee = *call.resolved_callee_id;
    const auto& module = callee.module.name;
    return callee.local_name == "lerp" &&
           (module == "std.math" || module == "std.math.vec2" || module == "std.math.vec3");
}

class BlendFinder {
public:
    BlendFinder(const ProgramFacts& facts, std::vector<BlendWeight>& out)
        : facts_(&facts)
        , out_(&out) {}

    void handler(const RuleNode& rule, const EventHandlerNode& handler) {
        HandlerScope scope{.binding = handler.alias.value_or(handler.event_name)};
        if (handler.resolved_trigger.has_value() && handler.resolved_trigger->kind == HandlerTriggerKind::Phase) {
            scope.phase = facts_->phase(handler.resolved_trigger->symbol);
        }
        for_each_nested_statement(handler.body, [&](const StmtNode& stmt) {
            const auto* assign = std::get_if<VarAssign>(&stmt.stmt);
            if (assign != nullptr && assign->field_write.has_value() && assign->field_write->field.has_value()) {
                scope.written.emplace(make_canonical_id(assign->field_write->trait), *assign->field_write->field);
            }
            if (assign != nullptr && assign->accumulation.has_value()) {
                scope.written.emplace(make_canonical_id(assign->accumulation->trait), assign->accumulation->field);
            }
        });
        Evaluator evaluator(*facts_, &scope);
        rule_ = &rule;
        evaluator.walk(
            handler.body, Env{}, [&](const VarAssign& assign, const Env& env) { statement(evaluator, assign, env); });
    }

private:
    void statement(Evaluator& evaluator, const VarAssign& assign, const Env& env) {
        if (!assign.field_write.has_value() || !blendable(*assign.field_write)) {
            return;
        }
        const Target target{.assign = &assign};
        if (assign.op == "+=" || assign.op == "-=") {
            accumulate(evaluator, target, weight_factors(*assign.value, target, assign.op == "+="), env);
            return;
        }
        if (assign.op == "=") {
            assigned(evaluator, target, *assign.value, env);
        }
    }

    [[nodiscard]] bool blendable(const TraitFieldRef& write) const {
        const auto* field = facts_->field(write);
        return field != nullptr && (field->type.kind == TypeKind::Float || field->type.kind == TypeKind::Vec2 ||
                                    field->type.kind == TypeKind::Vec3);
    }

    void assigned(Evaluator& evaluator, const Target& target, const ExprNode& value, const Env& env) {
        if (const auto* branch = std::get_if<IfExpr>(&value.expr)) {
            auto [holds, fails] = evaluator.split(*branch->condition, env);
            assigned(evaluator, target, *branch->then_expr, holds);
            assigned(evaluator, target, *branch->else_expr, fails);
            return;
        }
        if (const auto* call = std::get_if<CallExpr>(&value.expr); call != nullptr && is_lerp(*call)) {
            if (target.same(*call->args[0])) {
                accumulate(evaluator, target, std::vector<Factor>{{.expr = call->args[2].get()}}, env);
            }
            return;
        }
        const auto* binary = std::get_if<BinaryExpr>(&value.expr);
        if (binary == nullptr || (binary->op != "+" && binary->op != "-") || !target.same(*binary->left)) {
            return;
        }
        accumulate(evaluator, target, weight_factors(*binary->right, target, binary->op == "+"), env);
    }

    void accumulate(Evaluator& evaluator,
                    const Target& target,
                    const std::optional<std::vector<Factor>>& factors,
                    const Env& env) {
        if (!factors.has_value()) {
            return;
        }
        Range product = point(1.0);
        for (const auto& factor : *factors) {
            const auto part = evaluator.eval(*factor.expr, env);
            if (part.kind != Kind::Number) {
                return;
            }
            const auto written = spelling(*factor.expr);
            product            = settle(factor.inverse ? divide(product, part, Kind::Number, false, written)
                                                       : multiply(product, part, Kind::Number, false),
                                        other_source(written));
        }
        out_->push_back(BlendWeight{.rule     = rule_->name,
                                    .field    = target.spelling_text(),
                                    .location = target.assign->location,
                                    .lower    = product.lo,
                                    .upper    = product.hi});
    }

    const ProgramFacts* facts_;
    std::vector<BlendWeight>* out_;
    const RuleNode* rule_ = nullptr;
};

// ── Warnings ────────────────────────────────────────────────────────────────

// Sides with no bound come first, then sides with a bound outside [0, 1].
std::vector<std::string> reasons(const BlendWeight& blend, bool lower, bool upper) {
    const auto& lo           = blend.lower.source.spelling;
    const auto& hi           = blend.upper.source.spelling;
    const bool lower_unknown = !blend.lower.value.has_value();
    const bool upper_unknown = !blend.upper.value.has_value();
    std::vector<std::string> parts;
    if (lower_unknown && upper_unknown && lo == hi) {
        parts.push_back("it has no lower and no upper bound because '" + lo + "' has none");
        return parts;
    }
    if (lower_unknown) {
        parts.push_back("it has no lower bound because '" + lo + "' has none");
    }
    if (upper_unknown) {
        parts.push_back("it has no upper bound because '" + hi + "' has none");
    }
    if (lower && !lower_unknown) {
        parts.push_back("it can reach " + format_bound(*blend.lower.value));
    }
    if (upper && !upper_unknown) {
        parts.push_back("it can reach " + format_bound(*blend.upper.value));
    }
    return parts;
}

std::string fixes(const BlendWeight& blend, bool lower, bool upper) {
    std::string text = "wrap the weight in math.max(0.0, ...)";
    if (upper) {
        text = lower ? "wrap the weight in math.clamp(..., 0.0, 1.0)" : "wrap the weight in math.min(1.0, ...)";
    }
    if (upper && !blend.upper.value.has_value() && blend.upper.source.kind == UnboundedSource::Kind::PhaseStep) {
        text = "run the rule in a periodic phase such as fixed_tick, or " + text;
    }
    std::map<std::string, std::pair<bool, bool>> fields;
    if (lower && !blend.lower.value.has_value() && blend.lower.source.kind == UnboundedSource::Kind::VarField) {
        fields[blend.lower.source.field].first = true;
    }
    if (upper && !blend.upper.value.has_value() && blend.upper.source.kind == UnboundedSource::Kind::VarField) {
        fields[blend.upper.source.field].second = true;
    }
    for (const auto& [field, sides] : fields) {
        const auto& [min, max] = sides;
        text += ", or declare '" + field + "' clamped(" + (min ? "min = ..." : "") + (min && max ? ", " : "") +
                (max ? "max = ..." : "") + ")";
    }
    return text;
}

std::optional<std::string> warning_text(const BlendWeight& blend) {
    const auto prefix = "rule '" + blend.rule + "' blends '" + blend.field + "' toward its goal with a weight ";
    if (blend.lower.value.has_value() && *blend.lower.value > 1.0) {
        return prefix + "of at least " + format_bound(*blend.lower.value) + ": '" + blend.field +
               "' always passes its goal";
    }
    if (blend.upper.value.has_value() && *blend.upper.value < 0.0) {
        return prefix + "of at most " + format_bound(*blend.upper.value) + ": '" + blend.field +
               "' always moves away from its goal";
    }
    const bool lower = !blend.lower.value.has_value() || *blend.lower.value < 0.0;
    const bool upper = !blend.upper.value.has_value() || *blend.upper.value > 1.0;
    if (!lower && !upper) {
        return std::nullopt;
    }
    return prefix + "not proven to stay in [0, 1]: " + spelled_list(reasons(blend, lower, upper), std::identity{}) +
           "; " + fixes(blend, lower, upper);
}

}  // namespace

std::vector<BlendWeight> find_blend_weights(const DecoratedProgram& program) {
    std::vector<BlendWeight> blends;
    if (program.ast == nullptr) {
        return blends;
    }
    const ProgramFacts facts(program);
    BlendFinder finder(facts, blends);
    for (const auto& declaration : program.ast->declarations) {
        const auto* rule = std::get_if<RuleNode>(&declaration);
        if (rule == nullptr) {
            continue;
        }
        for (const auto& handler : rule->handlers) {
            finder.handler(*rule, handler);
        }
    }
    return blends;
}

void check_blend_weights(const DecoratedProgram& program, ErrorReporter& errors) {
    for (const auto& blend : find_blend_weights(program)) {
        if (const auto text = warning_text(blend); text.has_value()) {
            errors.warning(blend.location, *text);
        }
    }
}

}  // namespace cactus
