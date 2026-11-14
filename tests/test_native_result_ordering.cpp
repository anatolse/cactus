// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "frontend/ast.hpp"
#include "frontend/native_field_reads.hpp"
#include "frontend/native_result_ordering.hpp"
#include "stdlib_modules.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cactus;
using cactus_test::parse_module;
using cactus_test::parse_stdlib;
using cactus_test::ParsedModule;

namespace {

// The canonical ids of every extern function, native emitter or native
// consumer whose result has an order but no declared one.
class OrderedNatives {
public:
    explicit OrderedNatives(const std::vector<ParsedModule>& modules) : modules_(&modules) {
        for (const auto& module : modules) {
            for (const auto& declaration : module.program.declarations) {
                if (const auto* node = std::get_if<StructNode>(&declaration)) {
                    structs_.emplace(node->name, node);
                }
            }
        }
    }

    [[nodiscard]] std::vector<std::string> undeclared() const {
        std::vector<std::string> missing;
        const auto require = [&](const std::string& canonical) {
            if (native_result_order(canonical) == nullptr) {
                missing.push_back(canonical);
            }
        };
        for (const auto& module : *modules_) {
            for (const auto& declaration : module.program.declarations) {
                const auto* func = std::get_if<FuncNode>(&declaration);
                if (func != nullptr && func->is_extern && func->return_type.has_value() && ordered(*func->return_type)) {
                    require(module.name + "." + func->name);
                }
                const auto* rule = std::get_if<ExternRuleNode>(&declaration);
                if (rule != nullptr && std::ranges::any_of(rule->handlers, [](const ExternHandlerNode& handler) {
                        return !handler.emits.empty() || !handler.commands.empty();
                    })) {
                    require(module.name + "." + rule->name);
                }
            }
        }
        for (const auto& consumer : native_consumers()) {
            require(std::string(consumer.event));
        }
        return missing;
    }

private:
    // An entity, a list, or a struct that carries an entity.
    [[nodiscard]] bool ordered(const TypeRef& type) const {  // NOLINT(misc-no-recursion) -- struct nesting is finite
        if (type.name == "entity_id" || type.name == "list") {
            return true;
        }
        const auto local = type.name.substr(type.name.rfind('.') + 1);
        const auto found = structs_.find(local);
        return found != structs_.end() &&
               std::ranges::any_of(found->second->fields, [&](const FieldNode& field) { return ordered(field.type); });
    }

    const std::vector<ParsedModule>* modules_;
    std::map<std::string, const StructNode*> structs_;
};

// The canonical ids of every stdlib extern function that observes `world`
// with no field-read summary.
std::vector<std::string> unsummarized_world_natives(const std::vector<ParsedModule>& modules) {
    std::vector<std::string> missing;
    for (auto& native : cactus_test::world_natives(modules)) {
        if (native_field_reads(native) == nullptr) {
            missing.push_back(std::move(native));
        }
    }
    return missing;
}

bool is_query_module(const std::string& module) {
    return module == "std.query" || (module.starts_with("std.physics.") && module.ends_with(".query"));
}

bool is_physics_query(const std::string& module, const std::string& function) {
    return module.starts_with("std.physics.") && function.starts_with("query_");
}

std::vector<std::string> unclassified_queries(const std::vector<ParsedModule>& modules) {
    std::vector<std::string> missing;
    for (const auto& module : modules) {
        for (const auto& declaration : module.program.declarations) {
            const auto* func = std::get_if<FuncNode>(&declaration);
            if (func == nullptr || !func->is_extern ||
                !(is_query_module(module.name) || is_physics_query(module.name, func->name))) {
                continue;
            }
            if (auto canonical = module.name + "." + func->name; !native_is_world_scan(canonical).has_value()) {
                missing.push_back(std::move(canonical));
            }
        }
    }
    return missing;
}

}  // namespace

TEST_CASE("Native world scans: every query native is classified", "[native-field-reads]") {
    const auto modules = parse_stdlib();
    REQUIRE(modules.size() > 10);
    CHECK(unclassified_queries(modules).empty());
}

TEST_CASE("Native world scans: an unclassified query native is named", "[native-field-reads]") {
    std::vector<ParsedModule> modules;
    modules.push_back(parse_module("module std.query\n"
                                   "pub extern func exists() bool\n"
                                   "pub extern func newest() entity_id\n",
                                   "query.cactus"));
    CHECK(unclassified_queries(modules) == std::vector<std::string>{"std.query.newest"});
}

TEST_CASE("Native world scans: the table classifies scans", "[native-field-reads]") {
    CHECK(native_is_world_scan("std.query.count") == true);
    CHECK(native_is_world_scan("std.query.hierarchy_preorder") == true);
    CHECK(native_is_world_scan("std.physics.volume.query.overlap_sphere") == true);
    CHECK(native_is_world_scan("std.physics.flat.query_cast_nearest") == true);
    CHECK(native_is_world_scan("std.query.exists") == false);
    CHECK(native_is_world_scan("std.query.children") == false);
    CHECK_FALSE(native_is_world_scan("std.math.vec3.length").has_value());
}

TEST_CASE("Native field reads: every world native declares its reads", "[native-field-reads]") {
    const auto modules = parse_stdlib();
    REQUIRE(modules.size() > 10);
    CHECK(unsummarized_world_natives(modules).empty());
}

TEST_CASE("Native field reads: an unsummarized world native is named", "[native-field-reads]") {
    std::vector<ParsedModule> modules;
    modules.push_back(parse_module("module std.query\n"
                                   "pub extern func exists() bool\n"
                                   "pub extern func newest() entity_id\n",
                                   "query.cactus"));
    modules.push_back(parse_module("module std.fake\n"
                                   "pub extern func probe() float\n",
                                   "fake.cactus"));
    CHECK(unsummarized_world_natives(modules) == std::vector<std::string>{"std.query.newest"});
}

TEST_CASE("Native field reads: declared summaries", "[native-field-reads]") {
    const std::vector<NativeFieldRead> flat{
        {.module = "std.transform.flat", .trait = "WorldTransform", .field = "position"}};
    REQUIRE(native_field_reads("std.transform.flat.world_position") != nullptr);
    CHECK(*native_field_reads("std.transform.flat.world_position") == flat);
    REQUIRE(native_field_reads("std.query.children") != nullptr);
    CHECK(native_field_reads("std.query.children")->empty());
    CHECK(native_field_reads("std.math.vec3.length") == nullptr);
}

TEST_CASE("Native field reads: the summarized natives are listed in order", "[native-field-reads]") {
    const auto natives = summarized_natives();
    CHECK(std::ranges::is_sorted(natives));
    CHECK(std::ranges::contains(natives, "std.query.count"));
    CHECK(std::ranges::contains(natives, "std.transform.volume.world_position"));
    CHECK(std::ranges::all_of(natives, [](std::string_view native) { return native_field_reads(native) != nullptr; }));
}

TEST_CASE("Native result ordering: every ordered stdlib native declares its order", "[native-result-ordering]") {
    const auto modules = parse_stdlib();
    REQUIRE(modules.size() > 10);
    CHECK(OrderedNatives(modules).undeclared().empty());
}

TEST_CASE("Native result ordering: an undeclared ordered extern is named", "[native-result-ordering]") {
    std::vector<ParsedModule> modules;
    modules.push_back(parse_module("module std.fake\n"
                                   "pub struct Hit:\n"
                                   "    other: entity_id\n"
                                   "pub extern func pick() list[entity_id]\n"
                                   "pub extern func probe() Hit\n"
                                   "pub extern func length() float\n",
                                   "fake.cactus"));
    CHECK(OrderedNatives(modules).undeclared() == std::vector<std::string>{"std.fake.pick", "std.fake.probe"});
}

TEST_CASE("Native result ordering: declared orders", "[native-result-ordering]") {
    REQUIRE(native_result_order("std.query.children") != nullptr);
    CHECK(native_result_order("std.query.children")->kind == NativeOrderKind::Creation);
    CHECK(native_result_order("std.query.hierarchy_preorder")->kind == NativeOrderKind::Hierarchy);
    CHECK(native_result_order("std.physics.flat.query.nearest")->kind == NativeOrderKind::Keyed);
    CHECK(native_result_order("std.ui.RoutePointer")->name == "leave, enter, press, release, click");
    CHECK(native_result_order("std.math.vec3.length") == nullptr);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
