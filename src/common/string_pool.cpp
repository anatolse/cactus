#include "common/string_pool.h"

#include <stdexcept>

namespace cactus {

uint64_t StringPool::intern(std::string_view str) {
    std::string key(str);
    auto it = str_to_id_.find(key);
    if (it != str_to_id_.end()) {
        return it->second;
    }
    uint64_t id = next_id_++;
    str_to_id_[key] = id;
    id_to_str_[id] = key;
    return id;
}

std::string_view StringPool::lookup(uint64_t id) const {
    auto it = id_to_str_.find(id);
    if (it == id_to_str_.end()) {
        throw std::out_of_range("StringPool::lookup: unknown id");
    }
    return it->second;
}

bool StringPool::contains(std::string_view str) const {
    return str_to_id_.contains(std::string(str));
}

}  // namespace cactus
