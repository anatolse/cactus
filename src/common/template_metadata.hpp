#pragma once

#include "frontend/semantic_analyzer.hpp"

namespace cactus {

inline ImportedTemplate exported_template(const DecoratedProgram& program,
                                           const std::string& module,
                                           const std::string& name) {
    const auto symbol = make_symbol_id(SymbolKind::Template, module, name);
    ImportedTemplate result;
    result.name = symbol.local_name;
    result.module_name = symbol.module.name;
    result.canonical_id = make_canonical_id(symbol);
    result.symbol_id = symbol;
    if (const auto parameters = program.template_parameters.find(name); parameters != program.template_parameters.end()) {
        result.parameters = parameters->second;
    }
    if (const auto blueprint = program.template_blueprints.find(name); blueprint != program.template_blueprints.end()) {
        result.blueprint = blueprint->second;
    }
    return result;
}

}  // namespace cactus
