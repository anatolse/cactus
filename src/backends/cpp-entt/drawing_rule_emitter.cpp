#include "backends/cpp-entt/drawing_rule_emitter.hpp"

#include "backends/cpp-entt/system_emitter.hpp"
#include "backends/cpp-entt/type_utils.hpp"
#include "common/ast_expressions.hpp"
#include "common/codegen_error.hpp"
#include "common/color_channels.hpp"
#include "common/drawing_rule_builtin_fields.hpp"
#include "common/drawing_rule_intrinsics.hpp"
#include "common/vector_binary_ops.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cactus {

namespace {

// ── GLSL value shapes ──────────────────────────────────────────────────────

enum class GlslType : std::uint8_t { Float, Bool, Vec2, Vec3, Color };

const char* glsl_type_name(GlslType type) {
    switch (type) {
        case GlslType::Float:
            return "float";
        case GlslType::Bool:
            return "bool";
        case GlslType::Vec2:
            return "vec2";
        case GlslType::Vec3:
            return "vec3";
        case GlslType::Color:
            return "vec4";
    }
    std::unreachable();
}

TypeKind glsl_type_to_type_kind(GlslType type) {
    switch (type) {
        case GlslType::Float:
            return TypeKind::Float;
        case GlslType::Bool:
            return TypeKind::Bool;
        case GlslType::Vec2:
            return TypeKind::Vec2;
        case GlslType::Vec3:
            return TypeKind::Vec3;
        case GlslType::Color:
            return TypeKind::Color;
    }
    std::unreachable();
}

GlslType type_kind_to_glsl_type(TypeKind kind) {
    switch (kind) {
        case TypeKind::Float:
            return GlslType::Float;
        case TypeKind::Bool:
            return GlslType::Bool;
        case TypeKind::Vec2:
            return GlslType::Vec2;
        case TypeKind::Vec3:
            return GlslType::Vec3;
        case TypeKind::Color:
            return GlslType::Color;
        default:
            throw std::runtime_error("drawing-rule GLSL codegen: vector-operator result type has no GLSL representation");
    }
}

// A Cactus `int` or enum travels as a GLSL float; integer `/` and `%` use the kGlslIntDivision helpers.
GlslType glsl_type_from(const TypeInfo& type) {
    if (!is_glsl_value_type(type.kind)) {
        throw std::runtime_error("drawing-rule GLSL codegen: unsupported value type '" + type.name + "'");
    }
    return type.kind == TypeKind::Int || type.kind == TypeKind::Enum ? GlslType::Float
                                                                     : type_kind_to_glsl_type(type.kind);
}

struct TranslatedExpr {
    std::string text;
    GlslType type{};
};

// A world value a stage reads, set as a uniform once per draw or once per row.
struct StageUniform {
    std::string glsl_name;
    std::string key;  // what it reads, so two reads of one value share a uniform
    GlslType type{};
    TypeKind source{};  // `int` and enum values are converted to float on upload
    bool per_row = true;
    std::string component_path;      // a field of the row's own trait
    const ExprNode* read = nullptr;  // any other read, lowered by the system emitter
};

struct FilterTrait {
    SymbolId symbol;
    std::string component_local;  // as the domain walk binds it
};

struct GlslCtx {
    const DecoratedProgram* program = nullptr;
    std::string alias;  // the stage handler's own alias ("v" / "f")
    bool is_vertex = false;
    std::unordered_map<std::string, FilterTrait> filter_traits;
    std::unordered_map<std::string, GlslType> locals;
    // Authored `let` locals, emitted as `let_<name>` so no name meets a GLSL reserved word (`half`).
    std::unordered_set<std::string> let_names;
    // An inlined func's parameters, bound to the argument values translated in the caller's scope.
    std::unordered_map<std::string, TranslatedExpr> param_substitutions;
    std::vector<StageUniform>* uniforms = nullptr;  // shared by both stages of one rule
    std::vector<std::string> used_uniforms;         // the ones this stage declares
    bool uses_int_division = false;
};

constexpr std::string_view kGlslIntDivision =
    "float cactus_div(float a, float b)\n"
    "{\n"
    "    int x = int(a);\n"
    "    int y = int(b);\n"
    "    if (y == 0) {\n"
    "        return 0.0;\n"
    "    }\n"
    "    int q = abs(x) / abs(y);\n"
    "    if ((x < 0) != (y < 0)) {\n"
    "        q = -q;\n"
    "        if (q * y != x) {\n"
    "            q -= 1;\n"
    "        }\n"
    "    }\n"
    "    return float(q);\n"
    "}\n"
    "\n"
    "float cactus_mod(float a, float b)\n"
    "{\n"
    "    return a - cactus_div(a, b) * b;\n"
    "}\n"
    "\n";

const char* int_division_helper(ArithmeticOperands operands, std::string_view op, GlslCtx& ctx) {
    if (operands != ArithmeticOperands::Int || op.empty() || (op.front() != '/' && op.front() != '%')) {
        return nullptr;
    }
    ctx.uses_int_division = true;
    return op.front() == '/' ? "cactus_div" : "cactus_mod";
}

std::string local_reference(const std::string& name, const GlslCtx& ctx) {
    return ctx.let_names.contains(name) ? "let_" + name : name;
}

std::string hex_color_literal_to_glsl(const std::string& raw_hex) {
    if (raw_hex.size() != 6 && raw_hex.size() != 8) {
        throw std::runtime_error("drawing-rule GLSL codegen: malformed hex color literal '#" + raw_hex + "'");
    }
    const auto bytes   = hex_color_bytes(raw_hex);
    const auto channel = [&](std::size_t index) {
        // Enough digits that GLSL reads back the exact float a rule uses for this literal.
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<float>::max_digits10) << color_channel_from_byte(bytes[index]);
        return out.str();
    };
    return "vec4(" + channel(0) + ", " + channel(1) + ", " + channel(2) + ", " + channel(3) + ")";
}

// Declares `uniform` once for the rule and once for each stage that reads it.
TranslatedExpr use_uniform(StageUniform uniform, GlslCtx& ctx) {
    auto& all = *ctx.uniforms;
    auto found =
        std::ranges::find_if(all, [&](const StageUniform& existing) { return existing.key == uniform.key; });
    if (found == all.end()) {
        if (uniform.glsl_name.empty()) {
            uniform.glsl_name = "u_read_" + std::to_string(all.size());
        }
        all.push_back(std::move(uniform));
        found = std::prev(all.end());
    }
    if (!std::ranges::contains(ctx.used_uniforms, found->glsl_name)) {
        ctx.used_uniforms.push_back(found->glsl_name);
    }
    return {.text = found->glsl_name, .type = found->type};
}

TranslatedExpr translate_expr(const ExprNode& expr, GlslCtx& ctx);

std::string translate_call_args(const std::vector<std::unique_ptr<ExprNode>>& args, GlslCtx& ctx) {
    std::string joined;
    for (const auto& arg : args) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += translate_expr(*arg, ctx).text;
    }
    return joined;
}

// A pure func whose body is one `return expr` is inlined: each argument is
// translated once in the caller's scope and substituted for its parameter.
TranslatedExpr inline_func_call(const FuncNode& func, const std::vector<std::unique_ptr<ExprNode>>& args, GlslCtx& ctx) {
    if (func.params.size() != args.size()) {
        throw std::runtime_error("drawing-rule GLSL codegen: '" + func.name + "' called with the wrong argument count");
    }
    const auto* ret = func.body.size() == 1 ? std::get_if<ReturnStmt>(&func.body.front()->stmt) : nullptr;
    if (ret == nullptr || !ret->value.has_value()) {
        throw std::runtime_error("drawing-rule GLSL codegen: '" + func.name +
                                 "' body is not a single terminal return");
    }
    auto saved_substitutions = ctx.param_substitutions;
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        ctx.param_substitutions[func.params[i].name] = translate_expr(*args[i], ctx);
    }
    auto result             = translate_expr(**ret->value, ctx);
    ctx.param_substitutions = std::move(saved_substitutions);
    return result;
}

bool is_component_access(GlslType object_type, const std::string& member) {
    switch (object_type) {
        case GlslType::Vec2:
            return member == "x" || member == "y";
        case GlslType::Vec3:
            return member == "x" || member == "y" || member == "z";
        case GlslType::Color:
            return member == "r" || member == "g" || member == "b" || member == "a";
        default:
            return false;
    }
}

const ResolvedField* find_trait_field(const ResolvedTrait* trait, const std::string& name) {
    if (trait == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(trait->fields, name, &ResolvedField::name);
    return found == trait->fields.end() ? nullptr : &*found;
}

TranslatedExpr translate_builtin_field_access(const std::string& field_name, const GlslCtx& ctx) {
    const auto& fields = ctx.is_vertex ? drawing_vertex_input_fields() : drawing_fragment_input_fields();
    if (std::ranges::any_of(fields, [&](const DrawingRuleBuiltinField& field) { return field.name == field_name; })) {
        return {.text = field_name, .type = ctx.locals.at(field_name)};
    }
    throw std::runtime_error("drawing-rule GLSL codegen: '" + ctx.alias + "." + field_name +
                             "' is not a readable built-in field here");
}

// `alias.field`: a field of the row's own trait, set once per row.
TranslatedExpr translate_row_field(const FilterTrait& filter_trait,
                                   const std::string& owner_name,
                                   const std::string& field_name,
                                   GlslCtx& ctx) {
    const auto* trait = EnttCodegenUtils::find_trait(*ctx.program, make_canonical_id(filter_trait.symbol));
    const auto* field = find_trait_field(trait, field_name);
    if (field == nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: unknown trait field '" + owner_name + "." + field_name +
                                 "'");
    }
    const auto name = "u_" + owner_name + "_" + field_name;
    return use_uniform(StageUniform{.glsl_name      = name,
                                    .key            = name,
                                    .type           = glsl_type_from(field->type),
                                    .source         = field->type.kind,
                                    .per_row        = true,
                                    .component_path = filter_trait.component_local + "." + field->name},
                       ctx);
}

// `X[T].field` of a named entity, set once per draw, or `row.link[T].field`, set once per row.
TranslatedExpr translate_trait_read(const ExprNode& read,
                                    const TraitAccessExpr& access,
                                    const std::string& field_name,
                                    GlslCtx& ctx) {
    const auto& trait_id = access.link_target.has_value() ? access.link_target : access.resolved_trait_id;
    const auto* trait = trait_id.has_value() ? EnttCodegenUtils::find_trait(*ctx.program, make_canonical_id(*trait_id))
                                             : nullptr;
    const auto* field = find_trait_field(trait, field_name);
    if (field == nullptr || access.cell) {
        throw std::runtime_error("drawing-rule GLSL codegen: unsupported read '" + expression_spelling(read, "?") + "'");
    }
    const bool per_row = !access.named_ref().has_value();
    return use_uniform(StageUniform{.key     = expression_spelling(read, "?") + (per_row ? "#row" : "#draw"),
                                    .type    = glsl_type_from(field->type),
                                    .source  = field->type.kind,
                                    .per_row = per_row,
                                    .read    = &read},
                       ctx);
}

TranslatedExpr translate_member(const MemberExpr& member, const ExprNode& node, GlslCtx& ctx) {
    if (member.resolved_const_id.has_value()) {
        if (const auto* initializer = EnttCodegenUtils::find_const_initializer(*ctx.program->ast, *member.resolved_const_id)) {
            return translate_expr(*initializer, ctx);
        }
    }
    if (member.resolved_enum_member.has_value()) {
        return {.text = std::to_string(member.resolved_enum_member->index) + ".0", .type = GlslType::Float};
    }
    if (const auto* access = std::get_if<TraitAccessExpr>(&member.object->expr)) {
        return translate_trait_read(node, *access, member.member, ctx);
    }
    const auto* owner     = std::get_if<IdentExpr>(&member.object->expr);
    const bool stage_name = owner != nullptr && !owner->resolved_const_id.has_value() &&
                            !ctx.param_substitutions.contains(owner->name);
    if (stage_name && owner->name == ctx.alias) {
        return translate_builtin_field_access(member.member, ctx);
    }
    if (const auto trait_it = stage_name ? ctx.filter_traits.find(owner->name) : ctx.filter_traits.end();
        trait_it != ctx.filter_traits.end()) {
        return translate_row_field(trait_it->second, owner->name, member.member, ctx);
    }
    if (stage_name && !ctx.locals.contains(owner->name)) {
        throw std::runtime_error("drawing-rule GLSL codegen: unresolved member access '" + owner->name + "." +
                                 member.member + "'");
    }
    // A component of a value, such as a channel of a color.
    const auto object = translate_expr(*member.object, ctx);
    if (!is_component_access(object.type, member.member)) {
        throw std::runtime_error("drawing-rule GLSL codegen: unsupported member access '." + member.member + "'");
    }
    return {.text = object.text + "." + member.member, .type = GlslType::Float};
}

TranslatedExpr translate_call(const CallExpr& call, GlslCtx& ctx) {
    if (const auto* callee_ident = std::get_if<IdentExpr>(&call.callee->expr)) {
        if (callee_ident->name == "vec2") {
            return {.text = "vec2(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Vec2};
        }
        if (callee_ident->name == "vec3") {
            return {.text = "vec3(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Vec3};
        }
        if (callee_ident->name == "color") {
            return {.text = "vec4(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Color};
        }
    }
    if (!call.resolved_callee_id.has_value()) {
        throw std::runtime_error("drawing-rule GLSL codegen: unresolved call in stage handler body");
    }
    const auto& callee = *call.resolved_callee_id;
    if (is_portable_glsl_intrinsic(callee)) {
        // These std.math functions share name and signature with GLSL built-ins.
        return {.text = callee.local_name + "(" + translate_call_args(call.args, ctx) + ")", .type = GlslType::Float};
    }
    const auto* func = find_func_decl(*ctx.program->ast, callee);
    if (func == nullptr) {
        throw std::runtime_error("drawing-rule GLSL codegen: '" + make_canonical_id(callee) +
                                 "' has no registered portable GLSL translation");
    }
    return inline_func_call(*func, call.args, ctx);
}

TranslatedExpr translate_literal(const LiteralExpr& literal) {
    switch (literal.kind) {
        case LiteralExpr::Kind::Int:
            return {.text = literal.value + ".0", .type = GlslType::Float};
        case LiteralExpr::Kind::Float:
            return {.text = literal.value, .type = GlslType::Float};
        case LiteralExpr::Kind::HexColor:
            return {.text = hex_color_literal_to_glsl(literal.value), .type = GlslType::Color};
        case LiteralExpr::Kind::Bool:
            return {.text = literal.value, .type = GlslType::Bool};
        case LiteralExpr::Kind::String:
        case LiteralExpr::Kind::Duration:
            throw std::runtime_error("drawing-rule GLSL codegen: unsupported literal in stage handler body");
    }
    std::unreachable();
}

TranslatedExpr translate_ident(const IdentExpr& ident, GlslCtx& ctx) {
    if (const auto param_it = ctx.param_substitutions.find(ident.name); param_it != ctx.param_substitutions.end()) {
        return param_it->second;
    }
    if (const auto local_it = ctx.locals.find(ident.name); local_it != ctx.locals.end()) {
        return {.text = local_reference(ident.name, ctx), .type = local_it->second};
    }
    if (ident.resolved_const_id.has_value()) {
        if (const auto* initializer = EnttCodegenUtils::find_const_initializer(*ctx.program->ast, *ident.resolved_const_id)) {
            return translate_expr(*initializer, ctx);
        }
    }
    throw std::runtime_error("drawing-rule GLSL codegen: unresolved identifier '" + ident.name + "'");
}

TranslatedExpr translate_binary(const BinaryExpr& binary, GlslCtx& ctx) {
    auto left  = translate_expr(*binary.left, ctx);
    auto right = translate_expr(*binary.right, ctx);
    if (const char* helper = int_division_helper(binary.operands, binary.op, ctx)) {
        return {.text = std::string(helper) + "(" + left.text + ", " + right.text + ")", .type = GlslType::Float};
    }
    const bool is_comparison = binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=" ||
                               binary.op == "==" || binary.op == "!=";
    if (binary.op == "and" || binary.op == "or") {
        return {.text = "(" + left.text + (binary.op == "and" ? " && " : " || ") + right.text + ")",
                .type = GlslType::Bool};
    }
    if (left.type != right.type && is_comparison) {
        throw std::runtime_error("drawing-rule GLSL codegen: cannot use a non-numeric value where a float is required");
    }
    if (left.type != right.type) {
        const auto result_kind = lookup_vector_binary_op_result(
            glsl_type_to_type_kind(left.type), binary.op, glsl_type_to_type_kind(right.type));
        if (!result_kind.has_value()) {
            throw std::runtime_error("drawing-rule GLSL codegen: no operator '" + binary.op + "' for operand types '" +
                                     std::string(glsl_type_name(left.type)) + "' and '" +
                                     std::string(glsl_type_name(right.type)) + "'");
        }
        return {.text = "(" + left.text + " " + binary.op + " " + right.text + ")",
                .type = type_kind_to_glsl_type(*result_kind)};
    }
    return {.text = "(" + left.text + " " + binary.op + " " + right.text + ")",
            .type = is_comparison ? GlslType::Bool : left.type};
}

// Stage bodies are pure, so the chain may repeat the subject.
TranslatedExpr translate_match_expr(const MatchExpr& match_expr, GlslCtx& ctx) {
    const auto subject = translate_expr(*match_expr.subject, ctx).text;
    std::vector<std::pair<std::string, std::string>> tested_arms;
    for (std::size_t index = 0; index + 1 < match_expr.arms.size(); ++index) {
        const auto& arm = match_expr.arms[index];
        tested_arms.emplace_back(translate_expr(*arm.pattern, ctx).text, translate_expr(*arm.body, ctx).text);
    }
    const auto last = translate_expr(*match_expr.arms.back().body, ctx);
    return {.text = EnttCodegenUtils::match_ternary_chain(subject, tested_arms, last.text, false), .type = last.type};
}

TranslatedExpr translate_unary(const UnaryExpr& unary, GlslCtx& ctx) {
    const auto operand = translate_expr(*unary.operand, ctx);
    return {.text = "(" + std::string(unary.op == "not" ? "!" : unary.op) + operand.text + ")", .type = operand.type};
}

TranslatedExpr translate_expr_node(const ExprNode& expr, GlslCtx& ctx) {
    return std::visit(
        [&](const auto& node) -> TranslatedExpr {
            using E = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                return translate_literal(node);
            } else if constexpr (std::is_same_v<E, IdentExpr>) {
                return translate_ident(node, ctx);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                return translate_unary(node, ctx);
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                return translate_binary(node, ctx);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                return translate_member(node, expr, ctx);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                return translate_call(node, ctx);
            } else if constexpr (std::is_same_v<E, IfExpr>) {
                const auto condition = translate_expr(*node.condition, ctx);
                const auto chosen    = translate_expr(*node.then_expr, ctx);
                const auto otherwise = translate_expr(*node.else_expr, ctx);
                return {.text = "(" + condition.text + " ? " + chosen.text + " : " + otherwise.text + ")",
                        .type = chosen.type};
            } else if constexpr (std::is_same_v<E, MatchExpr>) {
                return translate_match_expr(node, ctx);
            } else {
                throw CodegenError(expr.location,
                                   "drawing-rule GLSL codegen: unsupported expression form in stage handler body");
            }
        },
        expr.expr);
}

// The innermost expression that fails gives the diagnostic its location.
TranslatedExpr translate_expr(const ExprNode& expr, GlslCtx& ctx) {
    try {
        return translate_expr_node(expr, ctx);
    } catch (const CodegenError&) {
        throw;
    } catch (const std::runtime_error& error) {
        throw CodegenError(expr.location, error.what());
    }
}

std::string translate_stmts(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx, int indent);

std::string translate_if(const IfStmt& if_stmt, GlslCtx& ctx, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
    std::ostringstream out;
    out << pad << "if (" << translate_expr(*if_stmt.condition, ctx).text << ") {\n";
    out << translate_stmts(if_stmt.then_body, ctx, indent + 1);
    out << pad << "}\n";
    for (const auto& branch : if_stmt.else_if_branches) {
        out << pad << "else if (" << translate_expr(*branch.condition, ctx).text << ") {\n";
        out << translate_stmts(branch.body, ctx, indent + 1);
        out << pad << "}\n";
    }
    if (!if_stmt.else_body.empty()) {
        out << pad << "else {\n";
        out << translate_stmts(if_stmt.else_body, ctx, indent + 1);
        out << pad << "}\n";
    }
    return out.str();
}

// Semantic analysis guarantees exhaustiveness, so the last arm is a plain `else`.
std::string translate_value_match(const MatchStmt& match_stmt, GlslCtx& ctx, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
    const auto subject = translate_expr(*match_stmt.subject, ctx);
    const auto name    = "match_subject_" + std::to_string(match_stmt.location.line) + "_" +
                      std::to_string(match_stmt.location.column);
    std::ostringstream out;
    out << pad << glsl_type_name(subject.type) << " " << name << " = " << subject.text << ";\n";
    for (std::size_t index = 0; index < match_stmt.arms.size(); ++index) {
        const auto& arm = match_stmt.arms[index];
        const bool last = index + 1 == match_stmt.arms.size() || arm.is_wildcard();
        out << pad;
        if (!last) {
            out << (index == 0 ? "if ((" : "else if ((") << name << " == " << translate_expr(*arm.pattern, ctx).text
                << ")) ";
        } else if (index > 0) {
            out << "else ";
        }
        out << "{\n" << translate_stmts(arm.body, ctx, indent + 1) << pad << "}\n";
        if (last) {
            break;
        }
    }
    return out.str();
}

std::string translate_stmts(const std::vector<std::unique_ptr<StmtNode>>& body, GlslCtx& ctx, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 4, ' ');
    std::ostringstream out;
    for (const auto& stmt : body) {
        if (const auto* let_stmt = std::get_if<LetStmt>(&stmt->stmt)) {
            const auto value           = translate_expr(*let_stmt->value, ctx);
            ctx.locals[let_stmt->name] = value.type;
            ctx.let_names.insert(let_stmt->name);
            out << pad << glsl_type_name(value.type) << " " << local_reference(let_stmt->name, ctx) << " = "
                << value.text << ";\n";
        } else if (const auto* assign = std::get_if<VarAssign>(&stmt->stmt)) {
            if (assign->target != nullptr) {
                throw CodegenError(stmt->location, "drawing-rule GLSL codegen: trait access in stage handler body");
            }
            const auto value         = translate_expr(*assign->value, ctx);
            const std::string target = assign->name == ctx.alias && assign->path.size() == 1
                                           ? assign->path.front()
                                           : local_reference(assign->name, ctx);
            if (const char* helper = int_division_helper(assign->operands, assign->op, ctx)) {
                out << pad << target << " = " << helper << "(" << target << ", " << value.text << ");\n";
            } else {
                out << pad << target << " " << assign->op << " " << value.text << ";\n";
            }
        } else if (const auto* if_stmt = std::get_if<IfStmt>(&stmt->stmt)) {
            out << translate_if(*if_stmt, ctx, indent);
        } else if (const auto* match_stmt = std::get_if<MatchStmt>(&stmt->stmt);
                   match_stmt != nullptr && match_stmt->kind == MatchKind::Value) {
            out << translate_value_match(*match_stmt, ctx, indent);
        } else {
            throw CodegenError(stmt->location, "drawing-rule GLSL codegen: unsupported statement in stage handler body");
        }
    }
    return out.str();
}

// ── Shader assembly ─────────────────────────────────────────────────────────

void declare_uniforms(std::ostringstream& out, const GlslCtx& ctx) {
    for (const auto& name : ctx.used_uniforms) {
        const auto found = std::ranges::find(*ctx.uniforms, name, &StageUniform::glsl_name);
        out << "uniform " << glsl_type_name(found->type) << " " << name << ";\n";
    }
}

void seed_builtin_locals(GlslCtx& ctx) {
    const auto& inputs  = ctx.is_vertex ? drawing_vertex_input_fields() : drawing_fragment_input_fields();
    const auto& outputs = ctx.is_vertex ? drawing_vertex_output_fields() : drawing_fragment_output_fields();
    for (const auto* fields : {&inputs, &outputs}) {
        for (const auto& field : *fields) {
            ctx.locals[field.name] = glsl_type_from(field.type);
        }
    }
}

std::string build_vertex_shader(const EventHandlerNode& handler, bool world, GlslCtx& ctx) {
    seed_builtin_locals(ctx);
    // The body is translated first: it discovers the uniforms the header declares.
    const auto body = translate_stmts(handler.body, ctx, 1);
    std::ostringstream out;
    out << "#version 330\n\n";
    out << "in vec3 vertexPosition;\n";
    out << "in vec2 vertexTexCoord;\n\n";
    out << "uniform mat4 mvp;\n";
    declare_uniforms(out, ctx);
    out << "\nout vec2 v_uv;\n";
    out << "out vec4 v_tint;\n\n";
    if (ctx.uses_int_division) {
        out << kGlslIntDivision;
    }
    out << "void main()\n{\n";
    // corner and uv come in as vertex attributes: rlgl batches draws, so gl_VertexID is not 0..5 per quad.
    out << "    vec2 corner = vertexPosition.xy;\n";
    out << "    vec2 uv = vertexTexCoord;\n";
    out << "    float vertex_index = float(gl_VertexID);\n";
    out << "    vec2 world_position = vec2(0.0, 0.0);\n";
    out << "    vec2 screen_position = vec2(0.0, 0.0);\n";
    out << "    vec2 uv_out = uv;\n";
    out << "    vec4 tint_out = vec4(1.0, 1.0, 1.0, 1.0);\n";
    out << body;
    out << "    v_uv = uv_out;\n";
    out << "    v_tint = tint_out;\n";
    out << "    gl_Position = mvp * vec4(" << (world ? "world_position" : "screen_position") << ", 0.0, 1.0);\n";
    out << "}\n";
    return out.str();
}

std::string build_fragment_shader(const EventHandlerNode& handler, GlslCtx& ctx) {
    seed_builtin_locals(ctx);
    const auto body = translate_stmts(handler.body, ctx, 1);
    std::ostringstream out;
    out << "#version 330\n\n";
    out << "in vec2 v_uv;\n";
    out << "in vec4 v_tint;\n\n";
    declare_uniforms(out, ctx);
    out << "\nout vec4 finalColor;\n\n";
    if (ctx.uses_int_division) {
        out << kGlslIntDivision;
    }
    out << "void main()\n{\n";
    out << "    vec2 uv = v_uv;\n";
    out << "    vec4 tint = v_tint;\n";
    out << "    vec2 frag_coord = gl_FragCoord.xy;\n";
    out << "    vec4 frag_color = vec4(0.0, 0.0, 0.0, 0.0);\n";
    out << body;
    out << "    finalColor = frag_color;\n";
    out << "}\n";
    return out.str();
}

// ── Draw function ───────────────────────────────────────────────────────────

std::unordered_map<std::string, FilterTrait> row_traits(const RuleNode& rule, const DecoratedProgram& program) {
    std::unordered_map<std::string, FilterTrait> traits;
    for (const auto& entry : rule.filter.entries) {
        if (!entry.resolved_trait_id.has_value()) {
            continue;
        }
        traits[entry.alias.value_or(entry.resolved_trait_id->local_name)] = FilterTrait{
            .symbol          = *entry.resolved_trait_id,
            .component_local = component_local(
                EnttCodegenUtils::trait_cpp_name(entry.resolved_trait_id, entry.qualified_name, program))};
    }
    return traits;
}

// The C++ value a uniform takes, in the float form a shader holds for `int` and enum values.
std::string uniform_value(const StageUniform& uniform, const std::string& lowered) {
    switch (uniform.source) {
        case TypeKind::Int:
            return "static_cast<float>(" + lowered + ")";
        case TypeKind::Enum:
            return "static_cast<float>(static_cast<int>(" + lowered + "))";
        default:
            return lowered;
    }
}

std::string set_uniform(const StageUniform& uniform, const std::string& lowered, const char* indent) {
    return std::string(indent) + "cactus::runtime::entt_backend::set_drawing_rule_uniform(*shader, loc_" +
           uniform.glsl_name + ", " + uniform_value(uniform, lowered) + ");\n";
}

bool is_window_space_renderer(const SymbolId& rule) {
    return rule == make_symbol_id(SymbolKind::Rule, "std.ui", "RenderUi") ||
           rule == make_symbol_id(SymbolKind::Rule, "std.render.text", "ScreenLabelRender");
}

}  // namespace

std::string drawing_rule_callee(const DecoratedProgram& program, const SymbolId& rule) {
    return system_function_name(program.module_name, rule.local_name, "draw").ref();
}

bool draws_after_window_space(const DecoratedProgram& program, const SymbolId& rule) {
    bool seen = false;
    for (const auto& identity : program.execution_graph.stable_topological_order) {
        if (identity.rule == rule && identity.trigger == drawing_rule_trigger()) {
            return seen;
        }
        seen = seen || (identity.trigger == drawing_rule_trigger() && is_window_space_renderer(identity.rule));
    }
    return false;
}

std::string emit_drawing_rule_function(const RuleNode& rule, const DecoratedProgram& program) {
    const auto* vertex   = find_stage_handler(rule, true);
    const auto* fragment = find_stage_handler(rule, false);
    if (vertex == nullptr || fragment == nullptr || !vertex->alias.has_value() || !fragment->alias.has_value() ||
        program.ast == nullptr) {
        throw CodegenError(rule.location, "drawing-rule GLSL codegen: rule '" + rule.name + "' lacks a stage handler");
    }
    std::vector<StageUniform> uniforms;
    const auto filter_traits = row_traits(rule, program);
    GlslCtx vertex_ctx{.program       = &program,
                       .alias         = *vertex->alias,
                       .is_vertex     = true,
                       .filter_traits = filter_traits,
                       .uniforms      = &uniforms};
    const bool world                = assigns_stage_output(vertex->body, *vertex->alias, "world_position");
    const std::string vertex_glsl   = build_vertex_shader(*vertex, world, vertex_ctx);
    GlslCtx fragment_ctx{.program       = &program,
                         .alias         = *fragment->alias,
                         .is_vertex     = false,
                         .filter_traits = filter_traits,
                         .uniforms      = &uniforms};
    const std::string fragment_glsl = build_fragment_shader(*fragment, fragment_ctx);

    std::ostringstream out;
    out << "void " << system_function_name(program.module_name, rule.name, "draw").name
        << "(entt::registry& registry) {\n";
    out << "    static const char* kVertexShader = R\"GLSL(\n" << vertex_glsl << ")GLSL\";\n";
    out << "    static const char* kFragmentShader = R\"GLSL(\n" << fragment_glsl << ")GLSL\";\n";
    out << "    static cactus::runtime::entt_backend::DrawingRuleShaderState shader_state;\n";
    out << "    Shader* shader = cactus::runtime::entt_backend::ensure_drawing_rule_shader(\n";
    out << "        shader_state, kVertexShader, kFragmentShader);\n";
    out << "    if (shader == nullptr) {\n";
    out << "        return;\n";
    out << "    }\n";
    // The shader loads once, so its uniform locations never change.
    for (const auto& uniform : uniforms) {
        out << "    static const int loc_" << uniform.glsl_name
            << " = cactus::runtime::entt_backend::drawing_rule_uniform_location(*shader, \"" << uniform.glsl_name
            << "\");\n";
    }
    out << EnttSystemEmitter::emit_draw_prelude(rule, program);
    out << "    cactus::runtime::entt_backend::begin_drawing_rule(*shader, " << (world ? "true" : "false") << ");\n";
    for (const auto& uniform : uniforms) {
        if (!uniform.per_row) {
            out << set_uniform(uniform, EnttSystemEmitter::emit_world_value(*uniform.read, program), "    ");
        }
    }
    out << EnttSystemEmitter::emit_domain_rows(
        rule, program, "        cactus::runtime::entt_backend::count_drawing_row();\n", [&](const auto& row_value) {
            std::string body;
            for (const auto& uniform : uniforms) {
                if (uniform.per_row) {
                    body += set_uniform(
                        uniform, uniform.read == nullptr ? uniform.component_path : row_value(*uniform.read), "        ");
                }
            }
            return body + "        cactus::runtime::entt_backend::draw_drawing_rule_quad();\n";
        });
    out << "    cactus::runtime::entt_backend::end_drawing_rule();\n";
    out << "}\n\n";
    return out.str();
}

}  // namespace cactus
