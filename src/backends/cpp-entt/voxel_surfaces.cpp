#include "backends/cpp-entt/voxel_surfaces.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cactus::runtime::entt_backend {

namespace {

constexpr std::array<std::array<int, 3>, 6> kFaceSteps{{
    {-1, 0, 0},
    {1, 0, 0},
    {0, -1, 0},
    {0, 1, 0},
    {0, 0, -1},
    {0, 0, 1},
}};

int chunk_span(int extent) noexcept {
    return (extent + VoxelSurfaceGrid::kChunk - 1) / VoxelSurfaceGrid::kChunk;
}

}  // namespace

void VoxelSurfaceGrid::begin(const VoxelVolumeDesc& desc) {
    if (!described_ || !(desc == desc_)) {
        desc_      = desc;
        described_ = true;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto span  = std::int64_t{desc.max.at(axis)} - desc.min.at(axis) + 1;
            extent_.at(axis) = static_cast<int>(std::clamp<std::int64_t>(span, 0, std::numeric_limits<int>::max()));
            chunks_.at(axis) = chunk_span(extent_.at(axis));
        }
        const auto cells = static_cast<std::size_t>(extent_[0]) * static_cast<std::size_t>(extent_[1]) *
                           static_cast<std::size_t>(extent_[2]);
        solid_.assign(cells, 0);
        texture_.assign(cells, 0);
        owner_.assign(cells, 0);
        seen_.assign(cells, 0);
        dirty_.assign(static_cast<std::size_t>(chunk_count()), 1);
        frame_       = 0;
        solid_count_ = 0;
        ++revision_;
    }
    ++frame_;
    noted_ = 0;
}

std::optional<std::size_t> VoxelSurfaceGrid::index(int x, int y, int z) const noexcept {
    // Widened so a cell coordinate far outside the bounds cannot overflow.
    const auto lx = std::int64_t{x} - desc_.min[0];
    const auto ly = std::int64_t{y} - desc_.min[1];
    const auto lz = std::int64_t{z} - desc_.min[2];
    if (lx < 0 || ly < 0 || lz < 0 || lx >= extent_[0] || ly >= extent_[1] || lz >= extent_[2]) {
        return std::nullopt;
    }
    const auto column = (static_cast<std::size_t>(lx) * static_cast<std::size_t>(extent_[1])) + static_cast<std::size_t>(ly);
    return (column * static_cast<std::size_t>(extent_[2])) + static_cast<std::size_t>(lz);
}

int VoxelSurfaceGrid::chunk_of(int x, int y, int z) const noexcept {
    const int cx = (x - desc_.min[0]) / kChunk;
    const int cy = (y - desc_.min[1]) / kChunk;
    const int cz = (z - desc_.min[2]) / kChunk;
    return (((cx * chunks_[1]) + cy) * chunks_[2]) + cz;
}

void VoxelSurfaceGrid::mark_dirty(int x, int y, int z, bool neighbours) {
    dirty_[static_cast<std::size_t>(chunk_of(x, y, z))] = 1;
    ++revision_;
    if (!neighbours) {
        return;
    }
    for (const auto& step : kFaceSteps) {
        if (index(x + step[0], y + step[1], z + step[2]).has_value()) {
            dirty_[static_cast<std::size_t>(chunk_of(x + step[0], y + step[1], z + step[2]))] = 1;
        }
    }
}

void VoxelSurfaceGrid::note_first(std::size_t at, int x, int y, int z, std::uint32_t texture, std::uint64_t key) {
    seen_[at]  = frame_;
    owner_[at] = key;
    ++noted_;
    if (solid_[at] == 0) {
        solid_[at]   = 1;
        texture_[at] = texture;
        ++solid_count_;
        mark_dirty(x, y, z, true);
        return;
    }
    set_texture(at, x, y, z, texture);
}

void VoxelSurfaceGrid::set_texture(std::size_t at, int x, int y, int z, std::uint32_t texture) {
    if (texture_[at] != texture) {
        texture_[at] = texture;
        mark_dirty(x, y, z, false);
    }
}

void VoxelSurfaceGrid::end() {
    if (noted_ == solid_count_) {
        return;
    }
    for (int lx = 0; lx < extent_[0]; ++lx) {
        for (int ly = 0; ly < extent_[1]; ++ly) {
            for (int lz = 0; lz < extent_[2]; ++lz) {
                const int x  = lx + desc_.min[0];
                const int y  = ly + desc_.min[1];
                const int z  = lz + desc_.min[2];
                const auto i = *index(x, y, z);
                if (solid_[i] != 0 && seen_[i] != frame_) {
                    solid_[i] = 0;
                    --solid_count_;
                    mark_dirty(x, y, z, true);
                }
            }
        }
    }
}

bool VoxelSurfaceGrid::solid(int x, int y, int z) const noexcept {
    const auto at = index(x, y, z);
    return at.has_value() && solid_[*at] != 0;
}

std::uint32_t VoxelSurfaceGrid::texture(int x, int y, int z) const noexcept {
    const auto at = index(x, y, z);
    return at.has_value() ? texture_[*at] : 0U;
}

int VoxelSurfaceGrid::chunk_count() const noexcept {
    return chunks_[0] * chunks_[1] * chunks_[2];
}

std::pair<int, int> VoxelSurfaceGrid::chunk_y_range(int chunk) const noexcept {
    const int cy  = (chunk / chunks_[2]) % chunks_[1];
    const int low = desc_.min[1] + (cy * kChunk);
    return {low, std::min(low + kChunk - 1, desc_.max[1])};
}

VoxelChunkView VoxelSurfaceGrid::chunk_view(int chunk, std::optional<int> ceiling) const noexcept {
    if (!ceiling.has_value()) {
        return VoxelChunkView::Full;
    }
    const auto [low, high] = chunk_y_range(chunk);
    if (low > *ceiling) {
        return VoxelChunkView::Hidden;
    }
    return *ceiling < std::min(high + 1, desc_.max[1]) ? VoxelChunkView::Cut : VoxelChunkView::Full;
}

std::vector<VoxelFaceQuad> VoxelSurfaceGrid::chunk_faces(int chunk, std::optional<int> ceiling) const {
    std::vector<VoxelFaceQuad> faces;
    if (chunk < 0 || chunk >= chunk_count()) {
        return faces;
    }
    const int cz = chunk % chunks_[2];
    const int cy = (chunk / chunks_[2]) % chunks_[1];
    const int cx = chunk / (chunks_[2] * chunks_[1]);
    const auto visible = [&](int x, int y, int z) { return solid(x, y, z) && (!ceiling.has_value() || y <= *ceiling); };
    for (int lx = cx * kChunk; lx < std::min((cx + 1) * kChunk, extent_[0]); ++lx) {
        for (int ly = cy * kChunk; ly < std::min((cy + 1) * kChunk, extent_[1]); ++ly) {
            for (int lz = cz * kChunk; lz < std::min((cz + 1) * kChunk, extent_[2]); ++lz) {
                const int x = lx + desc_.min[0];
                const int y = ly + desc_.min[1];
                const int z = lz + desc_.min[2];
                if (!visible(x, y, z)) {
                    continue;
                }
                for (std::size_t face = 0; face < kFaceSteps.size(); ++face) {
                    const auto& step = kFaceSteps.at(face);
                    if (!visible(x + step[0], y + step[1], z + step[2])) {
                        faces.push_back(VoxelFaceQuad{
                            .cell = {x, y, z}, .face = static_cast<VoxelFace>(face), .texture = texture(x, y, z)});
                    }
                }
            }
        }
    }
    return faces;
}

std::vector<int> VoxelSurfaceGrid::take_dirty_chunks() {
    std::vector<int> dirty;
    for (std::size_t chunk = 0; chunk < dirty_.size(); ++chunk) {
        if (dirty_[chunk] != 0) {
            dirty.push_back(static_cast<int>(chunk));
            dirty_[chunk] = 0;
        }
    }
    return dirty;
}

VoxelMeshData build_voxel_mesh(const VoxelVolumeDesc& desc, const std::vector<VoxelFaceQuad>& faces) {
    // Corners as (x, y, z) offsets in cell units, in order bottom-left,
    // bottom-right, top-right, top-left seen from outside the face.
    static constexpr std::array<std::array<std::array<float, 3>, 4>, 6> kCorners{{
        {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},  // NegX
        {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}},  // PosX
        {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},  // NegY
        {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},  // PosY
        {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},  // NegZ
        {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},  // PosZ
    }};
    static constexpr std::array<std::array<float, 2>, 4> kUv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    static constexpr std::array<std::size_t, 6> kTriangles{0, 1, 2, 0, 2, 3};

    VoxelMeshData mesh;
    mesh.positions.reserve(faces.size() * 18);
    mesh.normals.reserve(faces.size() * 18);
    mesh.texcoords.reserve(faces.size() * 12);
    for (const auto& quad : faces) {
        const auto face    = static_cast<std::size_t>(quad.face);
        const auto& normal = kFaceSteps.at(face);
        for (const auto corner : kTriangles) {
            const auto& offset = kCorners.at(face).at(corner);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const std::array<float, 3> origin{desc.origin.x, desc.origin.y, desc.origin.z};
                mesh.positions.push_back(origin.at(axis) +
                                         ((static_cast<float>(quad.cell.at(axis)) + offset.at(axis)) * desc.cell_size));
                mesh.normals.push_back(static_cast<float>(normal.at(axis)));
            }
            mesh.texcoords.push_back(kUv.at(corner)[0]);
            mesh.texcoords.push_back(kUv.at(corner)[1]);
        }
    }
    return mesh;
}

}  // namespace cactus::runtime::entt_backend
