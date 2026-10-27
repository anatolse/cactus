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

// How the solid cells are drawn: cubes, or one rounded surface moved by noise.
struct VoxelSurfaceStyle {
    bool smooth     = false;
    float roughness = 0.0F;  // fraction of cell_size
    int seed        = 0;

    friend bool operator==(const VoxelSurfaceStyle&, const VoxelSurfaceStyle&) = default;
};

struct VoxelVolumeDesc {
    Vector3 origin{};  // world position of the minimum corner of cell (0, 0, 0)
    float cell_size = 1.0F;
    std::array<int, 3> min{};  // inclusive cell bounds
    std::array<int, 3> max{};
    VoxelSurfaceStyle surface{};

    friend bool operator==(const VoxelVolumeDesc& lhs, const VoxelVolumeDesc& rhs) noexcept {
        return lhs.origin.x == rhs.origin.x && lhs.origin.y == rhs.origin.y && lhs.origin.z == rhs.origin.z &&
               lhs.cell_size == rhs.cell_size && lhs.min == rhs.min && lhs.max == rhs.max &&
               lhs.surface == rhs.surface;
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
    // Solid and not hidden by the ceiling.
    [[nodiscard]] bool visible(int x, int y, int z, std::optional<int> ceiling) const noexcept;
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
    // A smooth surface always marks all 26 neighbours, since a cell shapes and
    // textures the surface one cell around it.
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

// One texture's part of a chunk; `faces` counts the cell faces it stands for.
struct VoxelTextureMesh {
    std::uint32_t texture = 0;
    VoxelMeshData mesh;
    int faces = 0;
};

// A chunk's meshes, one per texture in ascending texture order. A smooth
// surface puts one vertex in each lattice cube between cell centres that the
// surface crosses, so a chunk reads only the cells within one cell of its own.
[[nodiscard]] std::vector<VoxelTextureMesh> build_voxel_chunk_meshes(const VoxelSurfaceGrid& grid,
                                                                     int chunk,
                                                                     std::optional<int> ceiling);

// The texture of the visible solid cell, within one cell of `around`, whose
// centre is nearest to `point` (in cell units: cell x spans [x, x + 1]); ties
// keep the lowest x, then y, then z.
[[nodiscard]] std::uint32_t nearest_solid_texture(const VoxelSurfaceGrid& grid,
                                                  std::array<int, 3> around,
                                                  std::array<float, 3> point,
                                                  std::optional<int> ceiling) noexcept;

// Roughness as the smooth surface uses it: within [0, 0.4], and 0 for NaN.
[[nodiscard]] float smooth_roughness(float roughness) noexcept;

// std.noise's lattice hash (version 1), and its value at a lattice point
// mapped to [-1, 1].
[[nodiscard]] std::int32_t noise_hash3(int x, int y, int z, int seed) noexcept;
[[nodiscard]] float lattice_noise(int x, int y, int z, int seed) noexcept;

}  // namespace cactus::runtime::entt_backend
