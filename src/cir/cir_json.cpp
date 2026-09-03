#include "cir/cir_json.hpp"

#include "cir/cir_text.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cactus::cir {
namespace {

/// Minimal pretty-printing JSON emitter: two-space indentation, comma
/// placement, and escaping handled once so each writer method below only has to
/// decide member order.
class JsonEmitter {
public:
    explicit JsonEmitter(std::string& out)
        : out_(&out) {}

    template <typename Body>
    void object(Body body) {
        open('{');
        body();
        close('}');
    }

    template <typename Body>
    void array(Body body) {
        open('[');
        body();
        close(']');
    }

    /// Opens the next element of the enclosing array.
    void element() {
        if (levels_.empty()) {
            return;
        }
        if (levels_.back()++ > 0) {
            *out_ += ',';
        }
        *out_ += '\n';
        out_->append(2 * levels_.size(), ' ');
    }

    /// Opens the next member of the enclosing object; the caller writes its
    /// value immediately after.
    void key(std::string_view name) {
        element();
        *out_ += '"';
        append_json_escaped(*out_, name);
        *out_ += "\": ";
    }

    void string(std::string_view value) {
        *out_ += '"';
        append_json_escaped(*out_, value);
        *out_ += '"';
    }

    void boolean(bool value) {
        *out_ += value ? "true" : "false";
    }

    void integer(std::uint64_t value) {
        *out_ += std::to_string(value);
    }

    void integer(std::int64_t value) {
        *out_ += std::to_string(value);
    }

    void number(double value) {
        append_json_number(*out_, value);
    }

    void null() {
        *out_ += "null";
    }

private:
    void open(char bracket) {
        *out_ += bracket;
        levels_.push_back(0);
    }

    void close(char bracket) {
        const bool empty = levels_.back() == 0;
        levels_.pop_back();
        if (!empty) {
            *out_ += '\n';
            out_->append(2 * levels_.size(), ' ');
        }
        *out_ += bracket;
    }

    std::string* out_;
    std::vector<std::size_t> levels_;
};

const char* domain_name(HandlerDomainKind domain) {
    switch (domain) {
        case HandlerDomainKind::Selectionless:
            return "selectionless";
        case HandlerDomainKind::Unary:
            return "unary";
        case HandlerDomainKind::Pair:
            return "pair";
    }
    std::unreachable();
}

const char* implementation_name(HandlerImplementationKind implementation) {
    switch (implementation) {
        case HandlerImplementationKind::Cactus:
            return "cactus";
        case HandlerImplementationKind::External:
            return "external";
    }
    std::unreachable();
}

const char* schedule_kind_name(ScheduleEdgeKind kind) {
    switch (kind) {
        case ScheduleEdgeKind::ExplicitHandler:
            return "explicit-handler";
        case ScheduleEdgeKind::ExplicitRule:
            return "explicit-rule";
        case ScheduleEdgeKind::DataConflict:
            return "data-conflict";
        case ScheduleEdgeKind::EffectConflict:
            return "effect-conflict";
    }
    std::unreachable();
}

const char* orientation_name(ScheduleEdgeOrientation orientation) {
    switch (orientation) {
        case ScheduleEdgeOrientation::Explicit:
            return "explicit";
        case ScheduleEdgeOrientation::WriterBeforeReader:
            return "writer-before-reader";
        case ScheduleEdgeOrientation::DeclarationOrder:
            return "declaration-order";
    }
    std::unreachable();
}

const char* dimension_name(SpatialJoinDimension dimension) {
    switch (dimension) {
        case SpatialJoinDimension::Flat2D:
            return "flat-2d";
        case SpatialJoinDimension::Volume3D:
            return "volume-3d";
    }
    std::unreachable();
}

/// Writes one CIR document. Member order is fixed by the order of the calls in
/// each method, which is what makes repeated serialization byte-identical.
class JsonWriter {
public:
    explicit JsonWriter(std::string& out)
        : json_(out) {}

    void write(const CirProgram& cir) {
        json_.object([&] {
            json_.key("schema");
            json_.string(cir.schema);
            json_.key("version");
            json_.integer(static_cast<std::int64_t>(cir.version));
            json_.key("modules");
            json_.array([&] {
                for (const auto& module : cir.modules) {
                    json_.element();
                    json_.string(module);
                }
            });
            json_.key("traits");
            write_traits(cir);
            json_.key("rule_groups");
            write_rule_groups(cir);
            json_.key("nodes");
            write_nodes(cir);
            json_.key("relations");
            write_relations(cir);
            json_.key("activation_schedules");
            write_activation_schedules(cir);
        });
    }

private:
    /// Typed identity: consumers read `kind`/`module`/`name` directly instead of
    /// parsing the display string back apart.
    void write_symbol(const SymbolId& symbol) {
        json_.object([&] {
            json_.key("kind");
            json_.string(symbol_kind_name(symbol.kind));
            json_.key("module");
            json_.string(symbol.module.name);
            json_.key("name");
            json_.string(symbol.local_name);
            json_.key("canonical");
            json_.string(make_canonical_id(symbol));
        });
    }

    void write_symbols(const std::vector<SymbolId>& symbols) {
        json_.array([&] {
            for (const auto& symbol : symbols) {
                json_.element();
                write_symbol(symbol);
            }
        });
    }

    void write_optional_symbol(const std::optional<SymbolId>& symbol) {
        if (!symbol.has_value()) {
            json_.null();
            return;
        }
        write_symbol(*symbol);
    }

    void write_trigger(const ResolvedHandlerTrigger& trigger) {
        json_.object([&] {
            json_.key("kind");
            json_.string(handler_trigger_kind_name(trigger.kind));
            json_.key("symbol");
            write_symbol(trigger.symbol);
        });
    }

    void write_strings(const std::vector<std::string>& values) {
        json_.array([&] {
            for (const auto& value : values) {
                json_.element();
                json_.string(value);
            }
        });
    }

    void write_node_ids(const std::vector<CirNodeId>& ids) {
        json_.array([&] {
            for (const auto& id : ids) {
                json_.element();
                json_.string(id.value);
            }
        });
    }

    void write_declaration_order(const DeclarationOrder& order) {
        json_.object([&] {
            json_.key("module_index");
            json_.integer(order.module_index);
            json_.key("declaration_index");
            json_.integer(order.declaration_index);
            json_.key("handler_index");
            json_.integer(order.handler_index);
        });
    }

    void write_location(const SourceLocation& location) {
        json_.object([&] {
            json_.key("file");
            json_.string(location.filename);
            json_.key("line");
            json_.integer(static_cast<std::int64_t>(location.line));
            json_.key("column");
            json_.integer(static_cast<std::int64_t>(location.column));
        });
    }

    void write_traits(const CirProgram& cir) {
        json_.array([&] {
            for (const auto& trait : cir.traits) {
                json_.element();
                json_.object([&] {
                    json_.key("symbol");
                    write_symbol(trait.symbol);
                    json_.key("fields");
                    json_.array([&] {
                        for (const auto& field : trait.fields) {
                            json_.element();
                            write_trait_field(field);
                        }
                    });
                });
            }
        });
    }

    void write_trait_field(const CirTraitField& field) {
        json_.object([&] {
            json_.key("name");
            json_.string(field.name);
            json_.key("type");
            json_.string(field.type);
            json_.key("is_var");
            json_.boolean(field.is_var);
            json_.key("is_persist");
            json_.boolean(field.is_persist);
            json_.key("is_sync");
            json_.boolean(field.is_sync);
            json_.key("has_default");
            json_.boolean(field.has_default);
        });
    }

    void write_rule_groups(const CirProgram& cir) {
        json_.array([&] {
            for (const auto& group : cir.rule_groups) {
                json_.element();
                json_.object([&] {
                    json_.key("rule");
                    write_symbol(group.rule);
                    json_.key("handlers");
                    write_node_ids(group.handlers);
                });
            }
        });
    }

    void write_nodes(const CirProgram& cir) {
        json_.object([&] {
            json_.key("handlers");
            json_.array([&] {
                for (const auto& handler : cir.handlers) {
                    json_.element();
                    write_handler(handler);
                }
            });
            json_.key("phases");
            json_.array([&] {
                for (const auto& phase : cir.phases) {
                    json_.element();
                    write_phase(phase);
                }
            });
            json_.key("event_producers");
            json_.array([&] {
                for (const auto& producer : cir.event_producers) {
                    json_.element();
                    json_.object([&] {
                        json_.key("id");
                        json_.string(producer.id.value);
                        json_.key("kind");
                        json_.string(cir_node_kind_name(producer.kind));
                        json_.key("event");
                        write_symbol(producer.event);
                    });
                }
            });
            json_.key("rasterizations");
            json_.array([&] {
                for (const auto& raster : cir.rasterizations) {
                    json_.element();
                    json_.object([&] {
                        json_.key("id");
                        json_.string(raster.id.value);
                        json_.key("kind");
                        json_.string(cir_node_kind_name(CirNodeKind::Rasterization));
                        json_.key("phase");
                        write_symbol(raster.phase);
                    });
                }
            });
        });
    }

    void write_handler(const CirHandlerNode& handler) {
        json_.object([&] {
            json_.key("id");
            json_.string(handler.id.value);
            json_.key("kind");
            json_.string(cir_node_kind_name(CirNodeKind::Handler));
            json_.key("rule");
            write_symbol(handler.rule);
            json_.key("trigger");
            write_trigger(handler.trigger);
            json_.key("implementation");
            json_.string(implementation_name(handler.implementation));
            json_.key("domain");
            json_.string(domain_name(handler.domain));
            json_.key("selection");
            write_symbols(handler.selection);
            json_.key("exclusion");
            write_symbols(handler.exclusion);
            json_.key("bindings");
            write_bindings(handler.bindings);
            json_.key("spatial_join");
            write_spatial_join(handler.spatial_join);
            json_.key("reads");
            write_symbols(handler.reads);
            json_.key("bound_reads");
            write_bound_reads(handler.bound_reads);
            json_.key("writes");
            write_symbols(handler.writes);
            json_.key("projects");
            write_symbols(handler.projects);
            json_.key("emits");
            write_symbols(handler.emits);
            json_.key("commands");
            write_commands(handler.commands);
            json_.key("effects");
            write_strings(handler.effects);
            json_.key("explicit_after");
            write_node_ids(handler.explicit_after);
            json_.key("declaration_order");
            write_declaration_order(handler.declaration_order);
            json_.key("location");
            write_location(handler.location);
        });
    }

    void write_bindings(const std::vector<RelationBinding>& bindings) {
        json_.array([&] {
            for (const auto& binding : bindings) {
                json_.element();
                json_.object([&] {
                    json_.key("name");
                    json_.string(binding.name);
                    json_.key("required_traits");
                    write_symbols(binding.required_traits);
                });
            }
        });
    }

    void write_bound_reads(const std::vector<BoundTraitAccess>& reads) {
        json_.array([&] {
            for (const auto& read : reads) {
                json_.element();
                json_.object([&] {
                    json_.key("binding_index");
                    json_.integer(static_cast<std::uint64_t>(read.binding_index));
                    json_.key("trait");
                    write_symbol(read.trait);
                });
            }
        });
    }

    void write_commands(const std::vector<InferredHandlerCommand>& commands) {
        json_.array([&] {
            for (const auto& command : commands) {
                json_.element();
                json_.object([&] {
                    json_.key("kind");
                    json_.string(handler_command_kind_name(command.kind));
                    json_.key("target");
                    write_optional_symbol(command.target);
                });
            }
        });
    }

    void write_spatial_access(const SpatialJoinAccess& access) {
        json_.object([&] {
            json_.key("trait");
            write_symbol(access.trait);
            json_.key("field_path");
            write_strings(access.field_path);
        });
    }

    void write_spatial_binding(const SpatialJoinBinding& binding) {
        json_.object([&] {
            json_.key("binding_index");
            json_.integer(static_cast<std::uint64_t>(binding.binding_index));
            json_.key("position");
            write_spatial_access(binding.position);
            json_.key("radius");
            write_spatial_access(binding.radius);
        });
    }

    void write_spatial_join(const std::optional<SpatialJoinPlan>& plan) {
        if (!plan.has_value()) {
            json_.null();
            return;
        }
        json_.object([&] {
            json_.key("dimension");
            json_.string(dimension_name(plan->dimension));
            json_.key("left");
            write_spatial_binding(plan->left);
            json_.key("right");
            write_spatial_binding(plan->right);
            json_.key("matched_predicate_index");
            json_.integer(static_cast<std::uint64_t>(plan->matched_predicate_index));
        });
    }

    void write_phase(const CirPhaseNode& phase) {
        json_.object([&] {
            json_.key("id");
            json_.string(phase.id.value);
            json_.key("kind");
            json_.string(cir_node_kind_name(CirNodeKind::Phase));
            json_.key("phase");
            write_symbol(phase.phase);
            json_.key("declared");
            json_.boolean(phase.declared);
            json_.key("source_dependencies");
            json_.array([&] {
                for (const auto& trigger : phase.source_dependencies) {
                    json_.element();
                    write_trigger(trigger);
                }
            });
            json_.key("runtime_root");
            write_optional_symbol(phase.runtime_root);
            json_.key("every_seconds");
            if (phase.every_seconds.has_value()) {
                json_.number(*phase.every_seconds);
            } else {
                json_.null();
            }
            json_.key("max_repetitions");
            if (phase.max_repetitions.has_value()) {
                json_.integer(*phase.max_repetitions);
            } else {
                json_.null();
            }
            json_.key("declaration_order");
            write_declaration_order(phase.declaration_order);
        });
    }

    void write_relations(const CirProgram& cir) {
        json_.object([&] {
            json_.key("schedule_dependencies");
            json_.array([&] {
                for (const auto& relation : cir.schedule_dependencies) {
                    json_.element();
                    write_schedule_relation(relation);
                }
            });
            json_.key("phase_dependencies");
            json_.array([&] {
                for (const auto& relation : cir.phase_dependencies) {
                    json_.element();
                    json_.object([&] {
                        json_.key("upstream");
                        json_.string(relation.upstream.value);
                        json_.key("downstream");
                        json_.string(relation.downstream.value);
                    });
                }
            });
            json_.key("phase_barriers");
            json_.array([&] {
                for (const auto& relation : cir.phase_barriers) {
                    json_.element();
                    json_.object([&] {
                        json_.key("upstream_phase");
                        json_.string(relation.upstream_phase.value);
                        json_.key("downstream_handler");
                        json_.string(relation.downstream_handler.value);
                    });
                }
            });
            json_.key("event_flows");
            json_.array([&] {
                for (const auto& relation : cir.event_flows) {
                    json_.element();
                    json_.object([&] {
                        json_.key("producer");
                        json_.string(relation.producer.value);
                        json_.key("event");
                        write_symbol(relation.event);
                        json_.key("consumer");
                        json_.string(relation.consumer.value);
                    });
                }
            });
            json_.key("render_pass_flows");
            json_.array([&] {
                for (const auto& relation : cir.render_pass_flows) {
                    json_.element();
                    json_.object([&] {
                        json_.key("before");
                        json_.string(relation.before.value);
                        json_.key("after");
                        json_.string(relation.after.value);
                    });
                }
            });
        });
    }

    void write_schedule_relation(const CirScheduleRelation& relation) {
        json_.object([&] {
            json_.key("before");
            json_.string(relation.before.value);
            json_.key("after");
            json_.string(relation.after.value);
            json_.key("kind");
            json_.string(schedule_kind_name(relation.kind));
            json_.key("orientation");
            json_.string(orientation_name(relation.orientation));
            json_.key("trait_provenance");
            json_.array([&] {
                for (const auto& provenance : relation.trait_provenance) {
                    json_.element();
                    json_.object([&] {
                        json_.key("trait");
                        write_symbol(provenance.trait);
                        json_.key("before");
                        write_access_modes(provenance.before);
                        json_.key("after");
                        write_access_modes(provenance.after);
                    });
                }
            });
            json_.key("effect_provenance");
            write_strings(relation.effect_provenance);
        });
    }

    void write_access_modes(const std::vector<CirTraitAccessMode>& modes) {
        json_.array([&] {
            for (const auto mode : modes) {
                json_.element();
                json_.string(cir_trait_access_mode_name(mode));
            }
        });
    }

    void write_activation_schedules(const CirProgram& cir) {
        json_.array([&] {
            for (const auto& schedule : cir.activation_schedules) {
                json_.element();
                json_.object([&] {
                    json_.key("activation");
                    write_trigger(schedule.activation);
                    json_.key("stable_order");
                    write_node_ids(schedule.stable_order);
                    json_.key("levels");
                    json_.array([&] {
                        for (const auto& level : schedule.levels) {
                            json_.element();
                            json_.object([&] {
                                json_.key("index");
                                json_.integer(level.index);
                                json_.key("handlers");
                                write_node_ids(level.handlers);
                            });
                        }
                    });
                });
            }
        });
    }

    JsonEmitter json_;
};

}  // namespace

std::string write_json(const CirProgram& cir) {
    std::string out;
    JsonWriter(out).write(cir);
    out += '\n';
    return out;
}

}  // namespace cactus::cir
