// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,bugprone-unchecked-optional-access)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "common/error_reporter.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/native_field_reads.hpp"
#include "frontend/native_result_ordering.hpp"
#include "frontend/parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cactus;

namespace {

struct ParsedModule {
    std::string name;
    ProgramNode program;
};

ParsedModule parse_module(const std::string& source, const std::string& filename) {
    ErrorReporter errors;
    Lexer lexer(source, filename, errors);
    Parser parser(lexer.tokenize(), errors);
    ParsedModule parsed{.program = parser.parse_program()};
    REQUIRE_FALSE(errors.has_errors());
    for (const auto& declaration : parsed.program.declarations) {
        if (const auto* module = std::get_if<ModuleNode>(&declaration)) {
            parsed.name = module->name;
        }
    }
    return parsed;
}

std::vector<ParsedModule> parse_stdlib() {
    std::vector<ParsedModule> modules;
    const auto root = std::filesystem::path(CACTUS_TEST_SOURCE_DIR) / "stdlib";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".cactus" || entry.path().string().find("build") != std::string::npos) {
            continue;
        }
        std::ifstream file(entry.path());
        std::stringstream source;
        source << file.rdbuf();
        modules.push_back(parse_module(source.str(), entry.path().string()));
    }
    return modules;
}

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
    for (const auto& module : modules) {
        if (!stdlib_module_observes_world(module.name)) {
            continue;
        }
        for (const auto& declaration : module.program.declarations) {
            const auto* func = std::get_if<FuncNode>(&declaration);
            if (func == nullptr || !func->is_extern) {
                continue;
            }
            if (auto canonical = module.name + "." + func->name; native_field_reads(canonical) == nullptr) {
                missing.push_back(std::move(canonical));
            }
        }
    }
    return missing;
}

}  // namespace

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
