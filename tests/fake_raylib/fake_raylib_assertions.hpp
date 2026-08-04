#pragma once

// Assertion helpers for headless behavioral tests built against
// cactus_raylib_fake's call log. Header-only: small enough that a separate
// compiled library isn't warranted.

#include "fake_raylib/fake_raylib.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace cactus_raylib_fake {

// ── Approximate equality for raylib vector types ────────────────────────────
// Mirrors the Catch::Approx convention already used throughout
// test_runtime_stdlib.cpp for floating-point comparisons.

[[nodiscard]] inline bool approx_equal(Vector2 a, Vector2 b, float epsilon = 0.0001F) noexcept {
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon;
}

[[nodiscard]] inline bool approx_equal(Vector3 a, Vector3 b, float epsilon = 0.0001F) noexcept {
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon && std::abs(a.z - b.z) <= epsilon;
}

[[nodiscard]] inline bool colors_equal(Color a, Color b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Attributes a `RecordedDrawMesh::transform` to an entity's world position:
// raylib's Matrix is column-major (Matrix's declared field order is m0, m4,
// m8, m12 for the first row, etc. — see raylib.h), so the translation column
// lives at m12/m13/m14 regardless of how the rotation/scale factors were
// composed into it.
[[nodiscard]] inline bool approx_equal(Matrix transform, Vector3 expected_translation, float epsilon = 0.0001F) noexcept {
    const Vector3 translation{.x = transform.m12, .y = transform.m13, .z = transform.m14};
    return approx_equal(translation, expected_translation, epsilon);
}

// ── Find a recorded call by type ────────────────────────────────────────────
// Returns a pointer to the first matching entry, or nullptr. The pointer is
// valid until the next call that mutates the log (a subsequent frame or a
// reset()).

template <typename T>
[[nodiscard]] const T* find_call(const std::vector<RecordedCall>& log) noexcept {
    for (const auto& entry : log) {
        if (const auto* value = std::get_if<T>(&entry)) {
            return value;
        }
    }
    return nullptr;
}

template <typename T, typename Predicate>
[[nodiscard]] const T* find_call(const std::vector<RecordedCall>& log, Predicate predicate) {
    for (const auto& entry : log) {
        if (const auto* value = std::get_if<T>(&entry)) {
            if (predicate(*value)) {
                return value;
            }
        }
    }
    return nullptr;
}

// ── Call-order assertions ───────────────────────────────────────────────────

using LogPredicate = std::function<bool(const RecordedCall&)>;

// A predicate matching any entry of type T, for use with ordered_subsequence
// / occurs_before when only the call kind (not its argument values) matters.
template <typename T>
[[nodiscard]] LogPredicate is_call() {
    return [](const RecordedCall& entry) { return std::holds_alternative<T>(entry); };
}

// True if `expected` occurs, in order, as a subsequence of `log`: each
// predicate in `expected` must match a log entry at or after the position of
// the previous match, with any number of other (interleaved) entries allowed
// in between.
[[nodiscard]] inline bool ordered_subsequence(const std::vector<RecordedCall>& log,
                                              const std::vector<LogPredicate>& expected) {
    std::size_t search_from = 0;
    for (const auto& predicate : expected) {
        bool found = false;
        for (std::size_t i = search_from; i < log.size(); ++i) {
            if (predicate(log[i])) {
                search_from = i + 1;
                found       = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// True if the first entry matching `before` occurs earlier in `log` than the
// first entry matching `after`. False if either predicate has no match.
[[nodiscard]] inline bool occurs_before(const std::vector<RecordedCall>& log, const LogPredicate& before,
                                        const LogPredicate& after) {
    std::optional<std::size_t> before_index;
    std::optional<std::size_t> after_index;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (!before_index && before(log[i])) {
            before_index = i;
        }
        if (!after_index && after(log[i])) {
            after_index = i;
        }
    }
    return before_index && after_index && *before_index < *after_index;
}

}  // namespace cactus_raylib_fake
