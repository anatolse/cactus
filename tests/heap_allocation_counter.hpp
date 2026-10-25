#pragma once
// Replaces global operator new/delete so a test can count heap allocations.
// Include it in exactly one translation unit of a test binary.

#include <cstddef>
#include <cstdlib>
#include <new>

namespace cactus_test {

inline bool g_counting_allocations = false;
inline std::size_t g_allocations    = 0;

// Counts the allocations made while it is alive.
class AllocationCount {
public:
    AllocationCount() {
        g_allocations          = 0;
        g_counting_allocations = true;
    }
    AllocationCount(const AllocationCount&)            = delete;
    AllocationCount& operator=(const AllocationCount&) = delete;
    AllocationCount(AllocationCount&&)                 = delete;
    AllocationCount& operator=(AllocationCount&&)      = delete;
    ~AllocationCount() {
        g_counting_allocations = false;
    }

    [[nodiscard]] static std::size_t count() noexcept {
        return g_allocations;
    }
};

inline void* counted_allocation(std::size_t size) {
    if (g_counting_allocations) {
        ++g_allocations;
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

}  // namespace cactus_test

void* operator new(std::size_t size) {
    return cactus_test::counted_allocation(size);
}
void* operator new[](std::size_t size) {
    return cactus_test::counted_allocation(size);
}
void operator delete(void* memory) noexcept {
    std::free(memory);
}
void operator delete[](void* memory) noexcept {
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}
