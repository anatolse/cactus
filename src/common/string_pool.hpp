#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cactus {

class StringPool {
public:
    uint64_t intern(std::string_view str);
    [[nodiscard]] std::string_view lookup(uint64_t id) const;
    [[nodiscard]] bool contains(std::string_view str) const;
    [[nodiscard]] size_t size() const {
        return str_to_id_.size();
    }

private:
    std::unordered_map<std::string, uint64_t> str_to_id_;
    std::unordered_map<uint64_t, std::string> id_to_str_;
    uint64_t next_id_ = 1;
};

}  // namespace cactus
