#include "frontend/semantic_analyzer.h"

#include <algorithm>

namespace cactus {

namespace {

const std::unordered_set<std::string>& builtin_types() {
    static const std::unordered_set<std::string> types = {"int",  "float",     "bool",   "string", "vec2",
                                                           "vec3", "quat",      "color",  "entity_id",
                                                           "void", "list"};
    return types;
}

TypeKind type_kind_from_name(const std::string& name) {
    if (name == "int") return TypeKind::Int;
    if (name == "float") return TypeKind::Float;
    if (name == "bool") return TypeKind::Bool;
    if (name == "string") return TypeKind::String;
    if (name == "vec2") return TypeKind::Vec2;
    if (name == "vec3") return TypeKind::Vec3;
    if (name == "quat") return TypeKind::Quat;
    if (name == "color") return TypeKind::Color;
    if (name == "entity_id") return TypeKind::EntityId;
    if (name == "void") return TypeKind::Void;
    return TypeKind::Unknown;
}

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(ErrorReporter& errors) : errors_(errors) {}

DecoratedProgram SemanticAnalyzer::analyze(ProgramNode& program) {
    result_.ast = &program;

    // Phase 1: Collect all type declarations
    collect_types(program);

    // Phase 2: Resolve types in fields
    resolve_all_types(program);

    // Phase 3: Semantic checks
    check_const_strings(program);
    check_func_purity(program);
    check_no_recursion(program);
    check_persist_sync(program);
    validate_system_filters(program);
    validate_event_usage(program);

    // Phase 4: Build dependency graph
    build_dependency_graph(program);

    return std::move(result_);
}

// ── Phase 1: Collect Types ──────────────────────────────────────────────────

void SemanticAnalyzer::collect_types(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, StructNode>) {
                    if (struct_names_.count(node.name)) {
                        errors_.error(node.location, "duplicate struct '" + node.name + "'");
                    }
                    struct_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, EnumNode>) {
                    if (enum_names_.count(node.name)) {
                        errors_.error(node.location, "duplicate enum '" + node.name + "'");
                    }
                    enum_names_.insert(node.name);
                    ResolvedEnum re;
                    re.name = node.name;
                    for (auto& v : node.variants) {
                        re.variants.push_back(v.name);
                    }
                    result_.enums[node.name] = std::move(re);
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    if (trait_names_.count(node.name)) {
                        errors_.error(node.location, "duplicate trait '" + node.name + "'");
                    }
                    trait_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, EventNode>) {
                    event_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    func_names_.insert(node.name);
                } else if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& a : node.assignments) {
                        result_.string_pool.intern(a.name);
                    }
                }
            },
            decl);
    }
}

// ── Phase 2: Resolve Types ──────────────────────────────────────────────────

void SemanticAnalyzer::resolve_all_types(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, StructNode>) {
                    ResolvedStruct rs;
                    rs.name = node.name;
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name = f.name;
                        rf.type = resolve_type_ref(f.type);
                        rs.fields.push_back(std::move(rf));
                    }
                    result_.structs[node.name] = std::move(rs);
                } else if constexpr (std::is_same_v<T, TraitNode>) {
                    ResolvedTrait rt;
                    rt.name = node.name;
                    rt.is_pub = node.is_pub;
                    for (auto& f : node.fields) {
                        ResolvedField rf;
                        rf.name = f.name;
                        rf.type = resolve_type_ref(f.type);
                        rf.is_let = f.modifiers.is_let;
                        rf.is_var = f.modifiers.is_var;
                        rf.is_persist = f.modifiers.is_persist;
                        rf.is_sync = f.modifiers.is_sync;
                        rf.is_pub = f.modifiers.is_pub;
                        rt.fields.push_back(std::move(rf));
                    }
                    result_.traits[node.name] = std::move(rt);
                }
            },
            decl);
    }
}

TypeInfo SemanticAnalyzer::resolve_type_ref(const TypeRef& ref) {
    // Check built-in types
    if (ref.name == "list") {
        if (ref.param) {
            auto elem = resolve_type_ref(**ref.param);
            return make_list_type(std::move(elem));
        }
        errors_.error(ref.location, "list type requires a type parameter, e.g. list[int]");
        return make_unknown_type();
    }

    auto kind = type_kind_from_name(ref.name);
    if (kind != TypeKind::Unknown) {
        TypeInfo ti;
        ti.kind = kind;
        ti.name = ref.name;
        return ti;
    }

    // Check user-defined types
    if (struct_names_.count(ref.name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Struct;
        ti.name = ref.name;
        return ti;
    }
    if (enum_names_.count(ref.name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Enum;
        ti.name = ref.name;
        return ti;
    }

    errors_.error(ref.location, "unknown type '" + ref.name + "'");
    return make_unknown_type();
}

bool SemanticAnalyzer::is_known_type(const std::string& name) const {
    return builtin_types().count(name) || struct_names_.count(name) || enum_names_.count(name);
}

// ── Phase 3a: Const String Check ────────────────────────────────────────────

void SemanticAnalyzer::check_const_strings(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ConstBlockNode>) {
                    for (auto& a : node.assignments) {
                        check_const_strings_expr(*a.value, true);
                    }
                } else if constexpr (std::is_same_v<T, FuncNode>) {
                    for (auto& stmt : node.body) {
                        std::visit(
                            [this](auto& s) {
                                using S = std::decay_t<decltype(s)>;
                                if constexpr (std::is_same_v<S, VarAssign>) {
                                    check_const_strings_expr(*s.value, false);
                                } else if constexpr (std::is_same_v<S, ExprStmt>) {
                                    check_const_strings_expr(*s.expr, false);
                                } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                                    if (s.value) check_const_strings_expr(**s.value, false);
                                } else if constexpr (std::is_same_v<S, EmitStmt>) {
                                    for (auto& arg : s.args) check_const_strings_expr(*arg, false);
                                }
                            },
                            stmt->stmt);
                    }
                }
            },
            decl);
    }
}

void SemanticAnalyzer::check_const_strings_expr(const ExprNode& expr, bool in_const) {
    std::visit(
        [this, in_const](auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, LiteralExpr>) {
                if (e.kind == LiteralExpr::Kind::String && !in_const) {
                    errors_.error(e.location, "string literals are only allowed in const blocks");
                }
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                check_const_strings_expr(*e.left, in_const);
                check_const_strings_expr(*e.right, in_const);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                check_const_strings_expr(*e.operand, in_const);
            } else if constexpr (std::is_same_v<E, CallExpr>) {
                check_const_strings_expr(*e.callee, in_const);
                for (auto& arg : e.args) check_const_strings_expr(*arg, in_const);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                check_const_strings_expr(*e.object, in_const);
            } else if constexpr (std::is_same_v<E, ListExpr>) {
                for (auto& el : e.elements) check_const_strings_expr(*el, in_const);
            }
        },
        expr.expr);
}

// ── Phase 3b: Func Purity ───────────────────────────────────────────────────

void SemanticAnalyzer::check_func_purity(ProgramNode& program) {
    // Build call graph first
    for (auto& decl : program.declarations) {
        if (auto* fn = std::get_if<FuncNode>(&decl)) {
            std::unordered_set<std::string> callees;
            for (auto& stmt : fn->body) {
                check_func_purity_stmt(*stmt, fn->name);
            }
        }
    }
}

void SemanticAnalyzer::check_func_purity_stmt(const StmtNode& stmt, const std::string& func_name) {
    std::visit(
        [this, &func_name](auto& s) {
            using S = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<S, EmitStmt>) {
                errors_.error(s.location, "func '" + func_name + "' cannot use 'emit' (funcs must be pure)");
            } else if constexpr (std::is_same_v<S, VarAssign>) {
                check_func_purity_expr(*s.value, func_name);
            } else if constexpr (std::is_same_v<S, ExprStmt>) {
                check_func_purity_expr(*s.expr, func_name);
            } else if constexpr (std::is_same_v<S, ReturnStmt>) {
                if (s.value) check_func_purity_expr(**s.value, func_name);
            } else if constexpr (std::is_same_v<S, IfStmt>) {
                check_func_purity_expr(*s.condition, func_name);
                for (auto& inner : s.then_body) check_func_purity_stmt(*inner, func_name);
                for (auto& inner : s.else_body) check_func_purity_stmt(*inner, func_name);
            }
        },
        stmt.stmt);
}

void SemanticAnalyzer::check_func_purity_expr(const ExprNode& expr, const std::string& func_name) {
    std::visit(
        [this, &func_name](auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, CallExpr>) {
                check_func_purity_expr(*e.callee, func_name);
                for (auto& arg : e.args) check_func_purity_expr(*arg, func_name);
                // Record call for recursion detection
                if (auto* ident = std::get_if<IdentExpr>(&e.callee->expr)) {
                    call_graph_[func_name].insert(ident->name);
                }
            } else if constexpr (std::is_same_v<E, BinaryExpr>) {
                check_func_purity_expr(*e.left, func_name);
                check_func_purity_expr(*e.right, func_name);
            } else if constexpr (std::is_same_v<E, UnaryExpr>) {
                check_func_purity_expr(*e.operand, func_name);
            } else if constexpr (std::is_same_v<E, MemberExpr>) {
                check_func_purity_expr(*e.object, func_name);
            }
        },
        expr.expr);
}

// ── Phase 3c: No Recursion ──────────────────────────────────────────────────

void SemanticAnalyzer::check_no_recursion(ProgramNode& program) {
    // DFS cycle detection on call graph
    for (auto& [func, callees] : call_graph_) {
        std::unordered_set<std::string> visited;
        std::vector<std::string> stack = {func};
        visited.insert(func);

        while (!stack.empty()) {
            auto current = stack.back();
            stack.pop_back();

            auto it = call_graph_.find(current);
            if (it == call_graph_.end()) continue;

            for (auto& callee : it->second) {
                if (callee == func) {
                    // Find location
                    for (auto& decl : program.declarations) {
                        if (auto* fn = std::get_if<FuncNode>(&decl)) {
                            if (fn->name == func) {
                                errors_.error(fn->location, "func '" + func + "' is recursive (recursion is not allowed)");
                                break;
                            }
                        }
                    }
                    break;
                }
                if (!visited.count(callee)) {
                    visited.insert(callee);
                    stack.push_back(callee);
                }
            }
        }
    }
}

// ── Phase 3d: Persist/Sync Validation ───────────────────────────────────────

void SemanticAnalyzer::check_persist_sync(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* trait = std::get_if<TraitNode>(&decl)) {
            for (auto& field : trait->fields) {
                if (field.modifiers.is_persist && field.modifiers.is_let) {
                    errors_.error(field.location, "persist modifier can only be used on 'var' fields, not 'let'");
                }
                if (field.modifiers.is_sync && field.modifiers.is_let) {
                    errors_.error(field.location, "sync modifier can only be used on 'var' fields, not 'let'");
                }
            }
        }
    }
}

// ── Phase 3e: System Filter Validation ──────────────────────────────────────

void SemanticAnalyzer::validate_system_filters(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& trait_name : sys->filter.trait_names) {
                if (!trait_names_.count(trait_name)) {
                    errors_.error(sys->filter.location, "system '" + sys->name + "' filters on unknown trait '" + trait_name + "'");
                }
            }
        }
    }
}

// ── Phase 3f: Event Usage Validation ────────────────────────────────────────

void SemanticAnalyzer::validate_event_usage(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& handler : sys->handlers) {
                if (handler.event_name != "tick" && !event_names_.count(handler.event_name)) {
                    errors_.error(handler.location, "system '" + sys->name + "' handles unknown event '" + handler.event_name + "'");
                }
            }
        }
    }
}

// ── Phase 4: Dependency Graph ───────────────────────────────────────────────

void SemanticAnalyzer::build_dependency_graph(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            SystemDependency dep;
            dep.system_name = sys->name;

            // Filter traits are reads
            for (auto& t : sys->filter.trait_names) {
                dep.reads.insert(t);
            }

            // Analyze handler bodies
            for (auto& handler : sys->handlers) {
                collect_system_deps(handler.body, dep);
            }

            result_.dependency_graph.push_back(std::move(dep));
        }
    }
}

void SemanticAnalyzer::collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts, SystemDependency& dep) {
    for (auto& stmt : stmts) {
        std::visit(
            [this, &dep](auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, VarAssign>) {
                    dep.writes.insert(s.name);
                } else if constexpr (std::is_same_v<S, EmitStmt>) {
                    dep.emits.insert(s.event_name);
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    collect_system_deps(s.then_body, dep);
                    collect_system_deps(s.else_body, dep);
                }
            },
            stmt->stmt);
    }
}

}  // namespace cactus
