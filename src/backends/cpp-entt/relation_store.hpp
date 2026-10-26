#pragma once

#include <entt/entt.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace cactus::runtime::entt_backend {

// Ordered entity pair -> row slot. Linear probing with backward-shift
// deletion keeps the table free of tombstones, so a steady add/remove cycle
// never allocates.
class PairSlotTable {
public:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] std::uint32_t find(entt::entity from, entt::entity to) const noexcept;
    // The pair must not be present.
    void insert(entt::entity from, entt::entity to, std::uint32_t slot);
    void erase(entt::entity from, entt::entity to) noexcept;
    void clear() noexcept;

private:
    struct Bucket {
        std::uint64_t key  = 0;
        std::uint32_t slot = kNone;
    };

    [[nodiscard]] static std::uint64_t key_of(entt::entity from, entt::entity to) noexcept;
    [[nodiscard]] std::size_t home_of(std::uint64_t key) const noexcept;
    void grow();

    std::vector<Bucket> buckets_;
    std::size_t size_ = 0;
};

// The rows of one relation: a dense, creation-ordered vector keyed by its
// (from, to) pair, with a list of slots per entity on each end. Erased rows
// stay as dead slots until `compact()`, so slots taken during a commit stay
// valid; the commit compacts once at its end.
template <typename Row, bool FromOne = Row::kFromOne, bool ToOne = Row::kToOne>
class RelationStore {
public:
    static constexpr std::uint32_t kNone = PairSlotTable::kNone;

    struct Entry {
        entt::entity from;
        entt::entity to;
        Row row;
    };

    // Live rows in creation order; valid while no row is erased since the last compaction.
    [[nodiscard]] std::span<const Entry> rows() const noexcept {
        assert(dead_ == 0);
        return entries_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size() - dead_;
    }

    [[nodiscard]] bool contains(entt::entity from, entt::entity to) const noexcept {
        return pairs_.find(from, to) != kNone;
    }

    [[nodiscard]] const Row* find(entt::entity from, entt::entity to) const noexcept {
        const auto slot = pairs_.find(from, to);
        return slot == kNone ? nullptr : &entries_[slot].row;
    }

    [[nodiscard]] bool has_from(entt::entity from) const noexcept {
        return head_of(from_ends_, from) != kNone;
    }

    [[nodiscard]] bool has_to(entt::entity to) const noexcept {
        return head_of(to_ends_, to) != kNone;
    }

    // Live rows in creation order, dead slots skipped: valid at any time.
    template <typename Visit>
    void for_each_live(const Visit& visit) const {
        for (std::uint32_t slot = 0; slot < entries_.size(); ++slot) {
            if (alive_[slot] != 0) {
                visit(entries_[slot]);
            }
        }
    }

    template <typename Visit>
    void for_each_from(entt::entity from, const Visit& visit) const {
        for (auto slot = head_of(from_ends_, from); slot != kNone; slot = links_[slot].next_from) {
            visit(entries_[slot]);
        }
    }

    template <typename Visit>
    void for_each_to(entt::entity to, const Visit& visit) const {
        for (auto slot = head_of(to_ends_, to); slot != kNone; slot = links_[slot].next_to) {
            visit(entries_[slot]);
        }
    }

    // Creates the row, or replaces the fields of the existing one in place.
    void add(entt::entity from, entt::entity to, const Row& row) {
        (void)place(from, to, row);
    }

    void erase(entt::entity from, entt::entity to) noexcept {
        if (const auto slot = pairs_.find(from, to); slot != kNone) {
            kill(slot);
        }
    }

    void erase_from(entt::entity from) noexcept {
        while (has_from(from)) {
            kill(head_of(from_ends_, from));
        }
    }

    void erase_to(entt::entity to) noexcept {
        while (has_to(to)) {
            kill(head_of(to_ends_, to));
        }
    }

    void clear() noexcept {
        for (std::uint32_t slot = 0; slot < entries_.size(); ++slot) {
            if (alive_[slot] != 0) {
                kill(slot);
            }
        }
    }

    // A keep run: rows it keeps carry its stamp, `drop_unstamped` erases the rest.
    void begin_run() noexcept {
        ++run_;
    }

    void keep(entt::entity from, entt::entity to, const Row& row) {
        stamps_[place(from, to, row)] = run_;
    }

    void drop_unstamped() noexcept {
        for (std::uint32_t slot = 0; slot < entries_.size(); ++slot) {
            if (alive_[slot] != 0 && stamps_[slot] != run_) {
                kill(slot);
            }
        }
    }

    // Drops dead slots, keeping live rows in creation order.
    void compact() {
        if (dead_ == 0) {
            return;
        }
        for (std::uint32_t slot = 0; slot < entries_.size(); ++slot) {
            if (alive_[slot] != 0) {
                ends_of(from_ends_, entries_[slot].from) = Ends{};
                ends_of(to_ends_, entries_[slot].to)     = Ends{};
            }
        }
        std::uint32_t kept = 0;
        for (std::uint32_t slot = 0; slot < entries_.size(); ++slot) {
            if (alive_[slot] == 0) {
                continue;
            }
            if (kept != slot) {
                entries_[kept] = std::move(entries_[slot]);
                stamps_[kept]  = stamps_[slot];
            }
            ++kept;
        }
        entries_.erase(entries_.begin() + kept, entries_.end());
        stamps_.resize(kept);
        alive_.assign(kept, 1);
        links_.assign(kept, Links{});
        pairs_.clear();
        for (std::uint32_t slot = 0; slot < kept; ++slot) {
            link(slot);
        }
        dead_ = 0;
    }

    // Whether every `one` end holds at most one row per entity.
    [[nodiscard]] bool one_ends_hold() const noexcept {
        for (std::uint32_t slot = 0; slot < entries_.size(); ++slot) {
            const auto& links = links_[slot];
            if (alive_[slot] == 0) {
                continue;
            }
            if ((FromOne && (links.next_from != kNone || links.prev_from != kNone)) ||
                (ToOne && (links.next_to != kNone || links.prev_to != kNone))) {
                return false;
            }
        }
        return true;
    }

private:
    struct Links {
        std::uint32_t next_from = kNone;
        std::uint32_t prev_from = kNone;
        std::uint32_t next_to   = kNone;
        std::uint32_t prev_to   = kNone;
    };

    struct Ends {
        std::uint32_t head = kNone;
        std::uint32_t tail = kNone;
    };

    [[nodiscard]] static std::uint32_t head_of(const std::vector<Ends>& ends, entt::entity entity) noexcept {
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        return index < ends.size() ? ends[index].head : kNone;
    }

    static Ends& ends_of(std::vector<Ends>& ends, entt::entity entity) {
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        if (index >= ends.size()) {
            ends.resize(index + 1);
        }
        return ends[index];
    }

    std::uint32_t place(entt::entity from, entt::entity to, const Row& row) {
        if (const auto slot = pairs_.find(from, to); slot != kNone) {
            entries_[slot].row = row;
            return slot;
        }
        const auto slot = static_cast<std::uint32_t>(entries_.size());
        entries_.push_back(Entry{.from = from, .to = to, .row = row});
        stamps_.push_back(0);
        alive_.push_back(1);
        links_.emplace_back();
        link(slot);
        return slot;
    }

    // Appends `slot` to both of its ends' lists and to the pair table.
    void link(std::uint32_t slot) {
        const auto& entry = entries_[slot];
        auto& links       = links_[slot];
        auto& from        = ends_of(from_ends_, entry.from);
        links.prev_from   = from.tail;
        (from.tail == kNone ? from.head : links_[from.tail].next_from) = slot;
        from.tail = slot;
        auto& to      = ends_of(to_ends_, entry.to);
        links.prev_to = to.tail;
        (to.tail == kNone ? to.head : links_[to.tail].next_to) = slot;
        to.tail = slot;
        pairs_.insert(entry.from, entry.to, slot);
    }

    void kill(std::uint32_t slot) noexcept {
        const auto& entry = entries_[slot];
        auto& links       = links_[slot];
        auto& from        = from_ends_[static_cast<std::size_t>(entt::to_entity(entry.from))];
        (links.prev_from == kNone ? from.head : links_[links.prev_from].next_from) = links.next_from;
        (links.next_from == kNone ? from.tail : links_[links.next_from].prev_from) = links.prev_from;
        auto& to = to_ends_[static_cast<std::size_t>(entt::to_entity(entry.to))];
        (links.prev_to == kNone ? to.head : links_[links.prev_to].next_to) = links.next_to;
        (links.next_to == kNone ? to.tail : links_[links.next_to].prev_to) = links.prev_to;
        links = Links{};
        pairs_.erase(entry.from, entry.to);
        alive_[slot] = 0;
        ++dead_;
    }

    std::vector<Entry> entries_;
    std::vector<std::uint32_t> stamps_;
    std::vector<std::uint8_t> alive_;
    std::vector<Links> links_;
    std::vector<Ends> from_ends_;
    std::vector<Ends> to_ends_;
    PairSlotTable pairs_;
    std::uint32_t dead_ = 0;
    std::uint32_t run_  = 0;
};

// The store holding `Row`'s relation, created on first use; mutations leave
// dead slots for the commit to compact once.
template <typename Row>
RelationStore<Row>& uncompacted_relation_store(entt::registry& registry) {
    if (auto* store = registry.ctx().find<RelationStore<Row>>()) {
        return *store;
    }
    return registry.ctx().emplace<RelationStore<Row>>();
}

// Rows erased outside a commit, by host code destroying an end, are compacted here.
template <typename Row>
RelationStore<Row>& relation_store(entt::registry& registry) {
    auto& store = uncompacted_relation_store<Row>(registry);
    store.compact();
    return store;
}

template <typename Row>
void erase_relation_from(entt::registry& registry, entt::entity entity) {
    uncompacted_relation_store<Row>(registry).erase_from(entity);
}

template <typename Row>
void erase_relation_to(entt::registry& registry, entt::entity entity) {
    uncompacted_relation_store<Row>(registry).erase_to(entity);
}

template <typename Row>
void erase_relation_entity(entt::registry& registry, entt::entity entity) {
    auto& store = uncompacted_relation_store<Row>(registry);
    store.erase_from(entity);
    store.erase_to(entity);
}

// An `add R(from, to)` at its commit position: nothing when an end is stale or lacks its end trait.
template <typename Row>
void add_relation_row(entt::registry& registry, entt::entity from, entt::entity to, const Row& row) {
    if (!registry.valid(from) || !registry.valid(to) || !registry.all_of<typename Row::FromTrait>(from) ||
        !registry.all_of<typename Row::ToTrait>(to)) {
        return;
    }
    uncompacted_relation_store<Row>(registry).add(from, to, row);
}

// A row lives while both ends carry their end traits: EnTT signals the
// removal of a trait and the destruction of its entity alike.
template <typename Row>
void connect_relation_ends(entt::registry& registry) {
    using FromTrait = typename Row::FromTrait;
    using ToTrait   = typename Row::ToTrait;
    (void)uncompacted_relation_store<Row>(registry);
    if constexpr (std::is_same_v<FromTrait, ToTrait>) {
        registry.on_destroy<FromTrait>().template connect<&erase_relation_entity<Row>>();
    } else {
        registry.on_destroy<FromTrait>().template connect<&erase_relation_from<Row>>();
        registry.on_destroy<ToTrait>().template connect<&erase_relation_to<Row>>();
    }
}

}  // namespace cactus::runtime::entt_backend
