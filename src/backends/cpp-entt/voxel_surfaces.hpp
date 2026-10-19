#pragma once

#include <raylib.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cactus::runtime::entt_backend {

// ── Voxel surfaces ───────────────────────────────────────────────────────────
// The visible faces of a std.voxel.volume Volume. Each frame the renderer
// notes every solid cell; the grid compares them with the previous frame and
// marks the chunks whose faces changed, so unchanged chunks keep their meshes.

enum class VoxelFace : std::uint8_t { NegX, PosX, NegY, PosY, NegZ, PosZ };

struct VoxelVolumeDesc {
    Vector3 origin{};  // world position of the minimum corner of cell (0, 0, 0)
    float cell_size = 1.0F;
    std::array<int, 3> min{};  // inclusive cell bounds
    std::array<int, 3> max{};

    friend bool operator==(const VoxelVolumeDesc& lhs, const VoxelVolumeDesc& rhs) noexcept {
        return lhs.origin.x == rhs.origin.x && lhs.origin.y == rhs.origin.y && lhs.origin.z == rhs.origin.z &&
               lhs.cell_size == rhs.cell_size && lhs.min == rhs.min && lhs.max == rhs.max;
    }
};

// How a chunk draws under a cutaway ceiling.
enum class VoxelChunkView : std::uint8_t { Hidden, Full, Cut };

struct VoxelFaceQuad {
    std::array<int, 3> cell{};
    VoxelFace face = VoxelFace::NegX;
    std::uint32_t texture = 0;  // the cell's texture asset handle
};

class VoxelSurfaceGrid {
public:
    static constexpr int kChunk = 16;

    // Starts one frame's observation. A description that differs from the last
    // one starts from an empty volume.
    void begin(const VoxelVolumeDesc& desc);
    // One solid cell, identified by `key`. Cells outside the bounds are ignored.
    // When several notes name one cell, the texture of the key for which
    // `earlier(key, other)` holds against every other wins.
    template <typename Earlier>
    void note_solid(int x, int y, int z, std::uint32_t texture, std::uint64_t key, const Earlier& earlier) {
        const auto at = index(x, y, z);
        if (!at.has_value()) {
            return;
        }
        if (seen_[*at] != frame_) {
            note_first(*at, x, y, z, texture, key);
            return;
        }
        if (earlier(key, owner_[*at])) {
            owner_[*at] = key;
            set_texture(*at, x, y, z, texture);
        }
    }
    // Cells not noted since begin() become air.
    void end();

    [[nodiscard]] bool solid(int x, int y, int z) const noexcept;
    [[nodiscard]] std::uint32_t texture(int x, int y, int z) const noexcept;
    [[nodiscard]] const VoxelVolumeDesc& desc() const noexcept { return desc_; }
    [[nodiscard]] int chunk_count() const noexcept;
    // Lowest and highest cell y of a chunk.
    [[nodiscard]] std::pair<int, int> chunk_y_range(int chunk) const noexcept;
    // Hidden when the whole chunk is above the ceiling, Cut when the ceiling
    // hides any of its cells or the layer just above it, Full otherwise.
    [[nodiscard]] VoxelChunkView chunk_view(int chunk, std::optional<int> ceiling) const noexcept;
    // Exposed faces of the chunk's solid cells, in cell order. With a ceiling,
    // cells above it are hidden and count as air for their neighbours.
    [[nodiscard]] std::vector<VoxelFaceQuad> chunk_faces(int chunk, std::optional<int> ceiling) const;
    // Chunks whose faces changed since the last call, ascending; clears them.
    [[nodiscard]] std::vector<int> take_dirty_chunks();
    // Changes on every observed difference.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] std::optional<std::size_t> index(int x, int y, int z) const noexcept;
    void note_first(std::size_t at, int x, int y, int z, std::uint32_t texture, std::uint64_t key);
    void set_texture(std::size_t at, int x, int y, int z, std::uint32_t texture);
    [[nodiscard]] int chunk_of(int x, int y, int z) const noexcept;
    void mark_dirty(int x, int y, int z, bool neighbours);

    VoxelVolumeDesc desc_;
    std::array<int, 3> extent_{};
    std::array<int, 3> chunks_{};
    std::vector<std::uint8_t> solid_;
    std::vector<std::uint32_t> texture_;
    std::vector<std::uint64_t> owner_;
    std::vector<std::uint32_t> seen_;
    std::vector<std::uint8_t> dirty_;
    std::uint32_t frame_    = 0;
    std::size_t noted_      = 0;
    std::size_t solid_count_ = 0;
    std::uint64_t revision_ = 0;
    bool described_         = false;
};

// Two triangles per face, counter-clockwise seen from outside, with the whole
// texture on every cell face (upright on the sides).
struct VoxelMeshData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
};

[[nodiscard]] VoxelMeshData build_voxel_mesh(const VoxelVolumeDesc& desc, const std::vector<VoxelFaceQuad>& faces);

}  // namespace cactus::runtime::entt_backend
