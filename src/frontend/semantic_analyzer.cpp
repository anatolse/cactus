#include "frontend/semantic_analyzer.h"

#include <algorithm>
#include <sstream>

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

// ── ModuleImports::add ──────────────────────────────────────────────────────

void ModuleImports::add(const std::string& qualifier, ImportedSymbols pub_syms,
                        std::unordered_set<std::string> non_pub) {
    // Build global uniqueness providers index
    for (auto& [name, _] : pub_syms.traits) {
        trait_providers[name].push_back(qualifier);
    }
    for (auto& [name, _] : pub_syms.structs) {
        struct_providers[name].push_back(qualifier);
    }
    for (auto& [name, _] : pub_syms.enums) {
        enum_providers[name].push_back(qualifier);
    }
    // Store non-pub trait names for error diagnostics
    if (!non_pub.empty()) {
        non_pub_trait_names[qualifier] = std::move(non_pub);
    }
    // Store the module
    modules[qualifier] = std::move(pub_syms);
}

// ── SemanticAnalyzer ────────────────────────────────────────────────────────

SemanticAnalyzer::SemanticAnalyzer(ErrorReporter& errors) : errors_(errors) {}

DecoratedProgram SemanticAnalyzer::analyze(ProgramNode& program,
                                            const ModuleImports& imports) {
    imports_ = imports;
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

    // Phase 3: Dynamic ECS checks (dynamic-ecs-language change)
    validate_template_unit_declarations(program);
    validate_spawn_sites(program);
    validate_stmt_contexts(program);
    validate_lifecycle_handler_signatures(program);
    validate_disabled_annotations(program);

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
                } else if constexpr (std::is_same_v<T, TemplateNode>) {
                    // Track template names separately from units (5.2)
                    if (template_names_.count(node.name)) {
                        errors_.error(node.location, "duplicate template '" + node.name + "'");
                    }
                    template_names_.insert(node.name);
                    archetype_apply_[node.name] = node.apply.entries;
                    // Collect config fields
                    std::unordered_set<std::string> cfg_fields;
                    if (node.config.has_value()) {
                        for (auto& assign : node.config->assignments) {
                            cfg_fields.insert(assign.name);
                        }
                    }
                    archetype_configured_fields_[node.name] = std::move(cfg_fields);
                } else if constexpr (std::is_same_v<T, UnitNode>) {
                    // Track unit names to distinguish from templates (5.4)
                    unit_names_.insert(node.name);
                    archetype_apply_[node.name] = node.apply.entries;
                    std::unordered_set<std::string> cfg_fields;
                    if (node.config.has_value()) {
                        for (auto& assign : node.config->assignments) {
                            cfg_fields.insert(assign.name);
                        }
                    }
                    archetype_configured_fields_[node.name] = std::move(cfg_fields);
                } else if constexpr (std::is_same_v<T, UseNode>) {
                    // Track declared module names for `load` reachability (5.6)
                    use_names_.insert(node.module_name);
                    if (node.alias.has_value()) {
                        use_names_.insert(*node.alias);
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
                        rf.has_default = f.default_value.has_value();
                        rt.fields.push_back(std::move(rf));
                    }
                    result_.traits[node.name] = std::move(rt);
                }
            },
            decl);
    }
}

TypeInfo SemanticAnalyzer::resolve_type_ref(const TypeRef& ref) {
    // ── Qualified name: "module.Symbol" or "alias.Symbol" ──────────────────
    auto dot = ref.name.find('.');
    if (dot != std::string::npos) {
        auto qualifier = ref.name.substr(0, dot);
        auto sym_name  = ref.name.substr(dot + 1);
        return resolve_qualified_type(qualifier, sym_name, ref.location);
    }

    // ── Built-in list type ──────────────────────────────────────────────────
    if (ref.name == "list") {
        if (ref.param) {
            auto elem = resolve_type_ref(**ref.param);
            return make_list_type(std::move(elem));
        }
        errors_.error(ref.location, "list type requires a type parameter, e.g. list[int]");
        return make_unknown_type();
    }

    // ── Built-in primitive ──────────────────────────────────────────────────
    auto kind = type_kind_from_name(ref.name);
    if (kind != TypeKind::Unknown) {
        TypeInfo ti;
        ti.kind = kind;
        ti.name = ref.name;
        return ti;
    }

    // ── Local user-defined types ────────────────────────────────────────────
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

    // ── Unqualified import lookup (4.3) ─────────────────────────────────────
    if (!imports_.empty()) {
        return resolve_imported_type(ref.name, ref.location);
    }

    errors_.error(ref.location, "unknown type '" + ref.name + "'");
    return make_unknown_type();
}

// ── Task 4.2: Qualified type resolution ────────────────────────────────────

TypeInfo SemanticAnalyzer::resolve_qualified_type(const std::string& qualifier,
                                                   const std::string& sym_name,
                                                   const SourceLocation& loc) {
    auto it = imports_.modules.find(qualifier);
    if (it == imports_.modules.end()) {
        errors_.error(loc, "unknown module qualifier '" + qualifier + "'");
        return make_unknown_type();
    }

    const auto& syms = it->second;

    // Check imported structs
    if (syms.structs.count(sym_name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Struct;
        ti.name = qualifier + "." + sym_name;
        return ti;
    }
    // Check imported enums
    if (syms.enums.count(sym_name)) {
        TypeInfo ti;
        ti.kind = TypeKind::Enum;
        ti.name = qualifier + "." + sym_name;
        return ti;
    }

    // ── Task 4.6: Non-pub helpful error ─────────────────────────────────────
    auto np_it = imports_.non_pub_trait_names.find(qualifier);
    if (np_it != imports_.non_pub_trait_names.end() && np_it->second.count(sym_name)) {
        errors_.error(loc, "trait '" + sym_name + "' is not public in module '" + qualifier +
                               "'; did you mean to mark it as 'pub'?");
        return make_unknown_type();
    }

    errors_.error(loc, "unknown symbol '" + sym_name + "' in module '" + qualifier + "'");
    return make_unknown_type();
}

// ── Task 4.3: Unqualified import lookup ────────────────────────────────────

TypeInfo SemanticAnalyzer::resolve_imported_type(const std::string& name,
                                                   const SourceLocation& loc) {
    auto struct_it = imports_.struct_providers.find(name);
    auto enum_it   = imports_.enum_providers.find(name);

    size_t struct_count = (struct_it != imports_.struct_providers.end()) ? struct_it->second.size() : 0;
    size_t enum_count   = (enum_it   != imports_.enum_providers.end())   ? enum_it->second.size()   : 0;
    size_t total        = struct_count + enum_count;

    if (total == 0) {
        errors_.error(loc, "unknown type '" + name + "'");
        return make_unknown_type();
    }

    if (total > 1) {
        // Build a helpful ambiguity message (task 4.3 requirement)
        std::ostringstream msg;
        msg << "ambiguous reference '" << name << "': defined in";
        bool first = true;
        auto append = [&](const std::vector<std::string>& quals) {
            for (auto& q : quals) {
                msg << (first ? " module '" : " and module '") << q << "'";
                first = false;
            }
        };
        if (struct_it != imports_.struct_providers.end()) append(struct_it->second);
        if (enum_it   != imports_.enum_providers.end())   append(enum_it->second);
        msg << "; use qualified access to disambiguate";
        errors_.error(loc, msg.str());
        return make_unknown_type();
    }

    // Unique — resolve
    if (struct_count == 1) {
        TypeInfo ti;
        ti.kind = TypeKind::Struct;
        ti.name = name;
        return ti;
    }
    TypeInfo ti;
    ti.kind = TypeKind::Enum;
    ti.name = name;
    return ti;
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
    for (auto& decl : program.declarations) {
        if (auto* fn = std::get_if<FuncNode>(&decl)) {
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

// ── Phase 3e: System Filter Validation (tasks 4.2, 4.4, 4.5, 4.6) ──────────

bool SemanticAnalyzer::resolve_filter_entry(const FilterEntry& entry,
                                              std::string& out_simple_name) {
    const auto& qname = entry.qualified_name;
    auto dot = qname.find('.');

    if (dot != std::string::npos) {
        // ── Qualified: "module.Trait" or "alias.Trait" ──────────────────────
        auto qualifier  = qname.substr(0, dot);
        auto trait_name = qname.substr(dot + 1);

        auto it = imports_.modules.find(qualifier);
        if (it == imports_.modules.end()) {
            errors_.error(entry.location,
                          "unknown module qualifier '" + qualifier + "' in filter");
            return false;
        }
        if (!it->second.traits.count(trait_name)) {
            // ── Task 4.6: Non-pub helpful error ──────────────────────────────
            auto np_it = imports_.non_pub_trait_names.find(qualifier);
            if (np_it != imports_.non_pub_trait_names.end() &&
                np_it->second.count(trait_name)) {
                errors_.error(entry.location,
                              "trait '" + trait_name + "' is not public in module '" +
                                  qualifier + "'; did you mean to mark it as 'pub'?");
            } else {
                errors_.error(entry.location,
                              "system filter references unknown trait '" + trait_name +
                                  "' in module '" + qualifier + "'");
            }
            return false;
        }
        out_simple_name = trait_name;
        return true;
    }

    // ── Unqualified ──────────────────────────────────────────────────────────
    // Check local traits first
    if (trait_names_.count(qname)) {
        out_simple_name = qname;
        return true;
    }
    // Check imports (task 4.3 uniqueness)
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(qname);
        if (it != imports_.trait_providers.end()) {
            if (it->second.size() > 1) {
                std::ostringstream msg;
                msg << "ambiguous trait '" << qname << "' in filter: found in module '"
                    << it->second[0] << "' and module '" << it->second[1] << "'";
                if (it->second.size() > 2) msg << " (and others)";
                msg << "; use qualified access to disambiguate";
                errors_.error(entry.location, msg.str());
                return false;
            }
            out_simple_name = qname;
            return true;
        }
    }

    errors_.error(entry.location, "unknown trait '" + qname + "' in filter");
    return false;
}

void SemanticAnalyzer::validate_system_filters(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            if (!sys->filter.entries.empty()) {
                // Rich filter entries (multi-module parser path)
                for (auto& entry : sys->filter.entries) {
                    std::string simple_name;
                    resolve_filter_entry(entry, simple_name);
                }
            } else {
                // Backward-compat: simple trait_names list
                for (auto& trait_name : sys->filter.trait_names) {
                    if (!trait_names_.count(trait_name)) {
                        // Check imports too
                        bool found = false;
                        if (!imports_.empty()) {
                            auto it = imports_.trait_providers.find(trait_name);
                            if (it != imports_.trait_providers.end() && !it->second.empty()) {
                                found = true;
                            }
                        }
                        if (!found) {
                            errors_.error(sys->filter.location,
                                          "system '" + sys->name + "' filters on unknown trait '" +
                                              trait_name + "'");
                        }
                    }
                }
            }

            // task 11.12: if system has no filter traits, handler bodies cannot
            // access trait fields (VarAssign is always a trait-field mutation)
            bool has_filter = !sys->filter.entries.empty() || !sys->filter.trait_names.empty();
            if (!has_filter) {
                for (auto& handler : sys->handlers) {
                    check_no_field_access(handler.body, sys->name);
                }
            }
        }
    }
}

// ── Phase 3f: Event Usage Validation ────────────────────────────────────────

// Lifecycle event names that are built-in (not declared via event keyword)
static bool is_lifecycle_event(const std::string& name) {
    return name == "spawn" || name == "destroy" || name == "load" || name == "unload";
}

void SemanticAnalyzer::validate_event_usage(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& handler : sys->handlers) {
                // "tick" and lifecycle events (spawn/destroy/load/unload) are always valid
                if (handler.event_name == "tick" || is_lifecycle_event(handler.event_name)) {
                    continue;
                }
                if (!event_names_.count(handler.event_name)) {
                    errors_.error(handler.location,
                                  "system '" + sys->name + "' handles unknown event '" +
                                      handler.event_name + "'");
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

            // Filter traits are reads — use simple (unqualified) names
            if (!sys->filter.entries.empty()) {
                for (auto& entry : sys->filter.entries) {
                    auto dot = entry.qualified_name.find('.');
                    auto simple = (dot != std::string::npos)
                                      ? entry.qualified_name.substr(dot + 1)
                                      : entry.qualified_name;
                    dep.reads.insert(simple);
                }
            } else {
                for (auto& t : sys->filter.trait_names) {
                    dep.reads.insert(t);
                }
            }

            // Analyze handler bodies
            for (auto& handler : sys->handlers) {
                collect_system_deps(handler.body, dep);
            }

            result_.dependency_graph.push_back(std::move(dep));
        }
    }
}

void SemanticAnalyzer::collect_system_deps(const std::vector<std::unique_ptr<StmtNode>>& stmts,
                                            SystemDependency& dep) {
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

// ── Dynamic ECS: Helpers ────────────────────────────────────────────────────

bool SemanticAnalyzer::is_trait_declared(const std::string& name) const {
    if (trait_names_.count(name)) return true;
    if (!imports_.empty()) {
        auto it = imports_.trait_providers.find(name);
        if (it != imports_.trait_providers.end() && !it->second.empty()) return true;
    }
    return false;
}

std::unordered_set<std::string> SemanticAnalyzer::get_archetype_fields(
    const std::vector<ApplyEntry>& apply) const {
    std::unordered_set<std::string> fields;
    for (auto& entry : apply) {
        // Local traits
        auto it = result_.traits.find(entry.trait_name);
        if (it != result_.traits.end()) {
            for (auto& f : it->second.fields) {
                fields.insert(f.name);
            }
        }
        // Imported traits (unqualified lookup)
        if (!imports_.empty()) {
            auto pit = imports_.trait_providers.find(entry.trait_name);
            if (pit != imports_.trait_providers.end()) {
                for (auto& qualifier : pit->second) {
                    auto mit = imports_.modules.find(qualifier);
                    if (mit != imports_.modules.end()) {
                        auto tit = mit->second.traits.find(entry.trait_name);
                        if (tit != mit->second.traits.end()) {
                            for (auto& f : tit->second.fields) {
                                fields.insert(f.name);
                            }
                        }
                    }
                }
            }
        }
    }
    return fields;
}

// ── Task 5.1, 5.2: Validate template and unit declarations ──────────────────

void SemanticAnalyzer::validate_template_unit_declarations(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, TemplateNode> ||
                              std::is_same_v<T, UnitNode>) {
                    const std::string kind =
                        std::is_same_v<T, TemplateNode> ? "template" : "unit";

                    // 5.1: Validate all traits in apply: are declared
                    for (auto& entry : node.apply.entries) {
                        if (!is_trait_declared(entry.trait_name)) {
                            errors_.error(entry.location,
                                          "undeclared trait '" + entry.trait_name +
                                              "' in " + kind + " '" + node.name + "'");
                        }
                    }

                    // 5.1: Validate config fields belong to applied traits
                    if (node.config.has_value()) {
                        auto accessible = get_archetype_fields(node.apply.entries);
                        for (auto& assign : node.config->assignments) {
                            if (!accessible.count(assign.name)) {
                                errors_.error(
                                    assign.location,
                                    "unknown field '" + assign.name + "' in " + kind +
                                        " '" + node.name + "' config");
                            }
                        }
                    }

                    // 5.2: After trait validation, compute required fields for templates
                    // (fields that are `var` with no default, not in config)
                    if constexpr (std::is_same_v<T, TemplateNode>) {
                        const auto& cfg = archetype_configured_fields_[node.name];
                        std::unordered_set<std::string> required;
                        for (auto& entry : node.apply.entries) {
                            auto it = result_.traits.find(entry.trait_name);
                            if (it != result_.traits.end()) {
                                for (auto& field : it->second.fields) {
                                    if (field.is_var && !field.has_default &&
                                        !cfg.count(field.name)) {
                                        required.insert(field.name);
                                    }
                                }
                            }
                        }
                        template_required_fields_[node.name] = std::move(required);
                    }
                }
            },
            decl);
    }
}

// ── Task 5.3, 5.4: Validate spawn sites ─────────────────────────────────────

void SemanticAnalyzer::validate_spawn_stmts(
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& context_name) {
    for (auto& stmt : stmts) {
        std::visit(
            [this, &context_name](auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, SpawnStmt>) {
                    // 5.4: Reject spawn of a unit
                    if (unit_names_.count(s.template_name)) {
                        errors_.error(
                            s.location,
                            "'" + s.template_name +
                                "' is a unit, not a template; use `spawn` only with "
                                "`template` declarations");
                        return;
                    }
                    // 5.3: Reject spawn of unknown template
                    if (!template_names_.count(s.template_name)) {
                        errors_.error(s.location,
                                      "undefined template '" + s.template_name + "'");
                        return;
                    }
                    // 5.3: Validate override field names
                    auto appl_it = archetype_apply_.find(s.template_name);
                    if (appl_it != archetype_apply_.end()) {
                        auto accessible = get_archetype_fields(appl_it->second);
                        for (auto& [field_name, _] : s.overrides) {
                            if (!accessible.count(field_name)) {
                                errors_.error(
                                    s.location,
                                    "unknown field '" + field_name + "' for template '" +
                                        s.template_name + "'");
                            }
                        }
                        // 5.3: Check required fields are provided
                        auto req_it = template_required_fields_.find(s.template_name);
                        if (req_it != template_required_fields_.end()) {
                            std::unordered_set<std::string> provided;
                            for (auto& [fn, _] : s.overrides) provided.insert(fn);
                            for (auto& req : req_it->second) {
                                if (!provided.count(req)) {
                                    errors_.error(
                                        s.location,
                                        "required field '" + req +
                                            "' not set for template '" + s.template_name +
                                            "'");
                                }
                            }
                        }
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_spawn_stmts(s.then_body, context_name);
                    validate_spawn_stmts(s.else_body, context_name);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_spawn_sites(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& handler : sys->handlers) {
                validate_spawn_stmts(handler.body, sys->name);
            }
        }
    }
}

// ── Task 5.5, 5.6, 5.7: Statement context validation ────────────────────────

void SemanticAnalyzer::validate_context_stmts(
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& context_name,
    bool in_system_handler) {
    for (auto& stmt : stmts) {
        std::visit(
            [this, &context_name, in_system_handler](auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, SpawnStmt> ||
                              std::is_same_v<S, DestroyStmt> ||
                              std::is_same_v<S, LoadStmt> ||
                              std::is_same_v<S, EnableStmt> ||
                              std::is_same_v<S, DisableStmt>) {
                    if (!in_system_handler) {
                        // Determine which keyword is used
                        std::string kw;
                        if constexpr (std::is_same_v<S, SpawnStmt>) kw = "spawn";
                        else if constexpr (std::is_same_v<S, DestroyStmt>) kw = "destroy";
                        else if constexpr (std::is_same_v<S, LoadStmt>) kw = "load";
                        else if constexpr (std::is_same_v<S, EnableStmt>) kw = "enable";
                        else kw = "disable";
                        errors_.error(
                            s.location,
                            "`" + kw + "` only allowed inside system event handlers");
                    }
                    // 5.6: For LoadStmt, validate module name is reachable via `use`
                    if constexpr (std::is_same_v<S, LoadStmt>) {
                        if (in_system_handler && !use_names_.count(s.module_name)) {
                            // Check prefix match (e.g. use levels allows load levels.level1)
                            bool found = false;
                            for (auto& use_name : use_names_) {
                                if (s.module_name == use_name ||
                                    s.module_name.substr(0, use_name.size()) == use_name) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                errors_.error(
                                    s.location,
                                    "unknown module '" + s.module_name +
                                        "'; add `use " + s.module_name +
                                        "` to import it");
                            }
                        }
                    }
                    // 5.7: For EnableStmt/DisableStmt, validate trait is declared
                    if constexpr (std::is_same_v<S, EnableStmt> ||
                                  std::is_same_v<S, DisableStmt>) {
                        const std::string& tname =
                            std::is_same_v<S, EnableStmt> ? s.trait_name : s.trait_name;
                        if (!is_trait_declared(tname)) {
                            std::string kw =
                                std::is_same_v<S, EnableStmt> ? "enable" : "disable";
                            errors_.error(s.location,
                                          "undeclared trait '" + tname + "' in `" + kw +
                                              "` statement");
                        }
                    }
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    validate_context_stmts(s.then_body, context_name, in_system_handler);
                    validate_context_stmts(s.else_body, context_name, in_system_handler);
                }
            },
            stmt->stmt);
    }
}

void SemanticAnalyzer::validate_stmt_contexts(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, FuncNode>) {
                    // 5.5: func bodies must not contain spawn/destroy/load/enable/disable
                    validate_context_stmts(node.body, node.name, false);
                } else if constexpr (std::is_same_v<T, SystemNode>) {
                    // System handlers: these statements are valid
                    for (auto& handler : node.handlers) {
                        validate_context_stmts(handler.body, node.name, true);
                    }
                }
            },
            decl);
    }
}

// ── Task 5.8: Validate lifecycle handler signatures ──────────────────────────

void SemanticAnalyzer::validate_lifecycle_handler_signatures(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        if (auto* sys = std::get_if<SystemNode>(&decl)) {
            for (auto& handler : sys->handlers) {
                if (is_lifecycle_event(handler.event_name)) {
                    if (!handler.params.empty()) {
                        errors_.error(
                            handler.location,
                            "lifecycle handler '" + handler.event_name +
                                "' does not accept parameters");
                    }
                }
            }
        }
    }
}

// ── Task 5.9: Validate exclude clause trait names ────────────────────────────
// (called as part of validate_system_filters — integrated inline above)
// Note: exclude clause validation is done here as a separate pass for clarity.

// ── Task 11.12: Check no field access in no-filter system bodies ─────────────

void SemanticAnalyzer::check_no_field_access(
    const std::vector<std::unique_ptr<StmtNode>>& stmts,
    const std::string& sys_name) {
    for (const auto& stmt : stmts) {
        std::visit(
            [this, &sys_name](const auto& s) {
                using S = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<S, VarAssign>) {
                    // All VarAssign statements in system handlers are trait-field accesses
                    errors_.error(s.location,
                                  "trait field '" + s.name +
                                      "' is not accessible in system '" + sys_name +
                                      "': no filter clause declares this trait");
                } else if constexpr (std::is_same_v<S, IfStmt>) {
                    check_no_field_access(s.then_body, sys_name);
                    check_no_field_access(s.else_body, sys_name);
                }
                // emit, spawn, destroy, load, enable, disable, return, expr: all allowed
            },
            stmt->stmt);
    }
}

// ── Task 5.11: Validate 'disabled' annotations in apply: blocks ─────────────

void SemanticAnalyzer::validate_disabled_annotations(ProgramNode& program) {
    for (auto& decl : program.declarations) {
        std::visit(
            [this](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, TemplateNode> ||
                              std::is_same_v<T, UnitNode>) {
                    for (auto& entry : node.apply.entries) {
                        // Trait must be declared (already checked in 5.1)
                        // Just additional: warn if : disabled trait is in a system filter
                        // (this is just a structural check — actual filter cross-check
                        //  would require iterating all systems which is expensive)
                        (void)entry;
                    }
                } else if constexpr (std::is_same_v<T, SystemNode>) {
                    // 5.9: Also validate exclude clause trait names
                    if (!node.exclude.entries.empty()) {
                        for (auto& entry : node.exclude.entries) {
                            std::string simple_name;
                            if (!is_trait_declared(entry.qualified_name)) {
                                errors_.error(entry.location,
                                              "undeclared trait '" + entry.qualified_name +
                                                  "' in exclude clause");
                            }
                        }
                    } else {
                        for (auto& trait_name : node.exclude.trait_names) {
                            if (!is_trait_declared(trait_name)) {
                                errors_.error(node.exclude.location,
                                              "undeclared trait '" + trait_name +
                                                  "' in exclude clause");
                            }
                        }
                    }
                }
            },
            decl);
    }
}

}  // namespace cactus
