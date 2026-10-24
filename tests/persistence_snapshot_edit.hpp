#pragma once

// Mutable copies of a snapshot's records, for tests that corrupt, reorder or
// extend a captured document. Converted back through SnapshotBuilder seeded
// with the captured tables, so every record whose values still fit a captured
// layout keeps it.

#include "common/persistence_document.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace cactus_test {

struct EditableTrait {
    std::string name;
    bool present = true;
    std::vector<cactus::persistence::StructField> construction;
    std::vector<cactus::persistence::StructField> persisted;

    [[nodiscard]] cactus::persistence::Value* field(std::string_view field_name) {
        for (auto* bucket : {&construction, &persisted}) {
            const auto found = std::ranges::find(*bucket, field_name, &cactus::persistence::StructField::name);
            if (found != bucket->end()) {
                return &found->value;
            }
        }
        return nullptr;
    }

    friend bool operator==(const EditableTrait&, const EditableTrait&) = default;
};

struct EditableRecord {
    cactus::persistence::DocumentId id = 0;
    std::string archetype;
    cactus::persistence::EntityRef parent;
    std::vector<EditableTrait> traits;

    [[nodiscard]] EditableTrait* trait(std::string_view trait_name) {
        const auto found = std::ranges::find(traits, trait_name, &EditableTrait::name);
        return found == traits.end() ? nullptr : &*found;
    }
};

[[nodiscard]] inline std::vector<EditableRecord> records_of(const cactus::persistence::Snapshot& snapshot) {
    std::vector<EditableRecord> records;
    records.reserve(snapshot.entity_count());
    for (const auto entity : snapshot.entities()) {
        EditableRecord record{.id = entity.id(), .archetype = std::string(entity.archetype()), .parent = entity.parent()};
        for (std::size_t index = 0; index < entity.trait_count(); ++index) {
            const auto trait = entity.trait_at(index);
            record.traits.push_back(EditableTrait{.name         = std::string(trait.name()),
                                                  .present      = trait.present(),
                                                  .construction = trait.construction(),
                                                  .persisted    = trait.persisted()});
        }
        records.push_back(std::move(record));
    }
    return records;
}

// `tables` supplies the header and the layouts to reuse.
[[nodiscard]] inline cactus::persistence::Snapshot snapshot_of(const cactus::persistence::Snapshot& tables,
                                                               const std::vector<EditableRecord>& records) {
    auto builder = cactus::persistence::SnapshotBuilder::with_tables_of(tables);
    if (tables.truncated()) {
        builder.mark_truncated();
    }
    for (const auto& record : records) {
        builder.add_entity(record.id, record.archetype, record.parent);
        for (const auto& trait : record.traits) {
            if (trait.present) {
                builder.add_trait(trait.name, trait.construction, trait.persisted);
            } else {
                builder.add_absent_trait(trait.name);
            }
        }
    }
    return std::move(builder).finish();
}

template <class Edit>
[[nodiscard]] cactus::persistence::Snapshot edited(const cactus::persistence::Snapshot& snapshot, Edit edit) {
    auto records = records_of(snapshot);
    edit(records);
    return snapshot_of(snapshot, records);
}

}  // namespace cactus_test
