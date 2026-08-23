#pragma once

// Small pixel-inspection helper for real-GL visual tests (see
// cactus_add_render_pass_visual_test in CMakeLists.txt). Plain, portable C++
// on top of raylib's own Image/Color types — no platform branching, since
// GetImageColor works identically on every OS raylib itself supports.

#include <raylib.h>

#include <algorithm>
#include <optional>

namespace cactus_visual_test {

struct PixelBoundingBox {
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    [[nodiscard]] int width() const {
        return max_x - min_x;
    }
    [[nodiscard]] int height() const {
        return max_y - min_y;
    }
};

// Component-wise closeness, not exact match — screenshot pixels go through
// GPU blending/compression rounding, so exact-pixel comparison would be
// flaky in a way that isn't the thing this test cares about.
inline bool colors_close(Color a, Color b, unsigned char tolerance) {
    const auto close = [tolerance](unsigned char x, unsigned char y) {
        return (x > y ? x - y : y - x) <= tolerance;
    };
    return close(a.r, b.r) && close(a.g, b.g) && close(a.b, b.b);
}

// Bounding box of every pixel that isn't close to `background`, or
// std::nullopt if the whole image is background.
inline std::optional<PixelBoundingBox> non_background_bounding_box(const Image& image,
                                                                    Color background,
                                                                    unsigned char tolerance = 10) {
    std::optional<PixelBoundingBox> box;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (colors_close(GetImageColor(image, x, y), background, tolerance)) {
                continue;
            }
            if (!box.has_value()) {
                box = PixelBoundingBox{.min_x = x, .min_y = y, .max_x = x, .max_y = y};
                continue;
            }
            box->min_x = std::min(box->min_x, x);
            box->min_y = std::min(box->min_y, y);
            box->max_x = std::max(box->max_x, x);
            box->max_y = std::max(box->max_y, y);
        }
    }
    return box;
}

}  // namespace cactus_visual_test
