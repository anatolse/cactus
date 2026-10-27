#include "backends/cpp-entt/voxel_surfaces.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

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

// Corners as (x, y, z) offsets in cell units, in order bottom-left,
// bottom-right, top-right, top-left seen from outside the face.
constexpr std::array<std::array<std::array<int, 3>, 4>, 6> kCorners{{
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},  // NegX
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}},  // PosX
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},  // NegY
    {{{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},  // PosY
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},  // NegZ
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},  // PosZ
}};
constexpr std::array<std::size_t, 6> kTriangles{0, 1, 2, 0, 2, 3};
constexpr float kMaxRoughness = 0.4F;

int chunk_span(int extent) noexcept {
    return (extent + VoxelSurfaceGrid::kChunk - 1) / VoxelSurfaceGrid::kChunk;
}

using Point = std::array<float, 3>;

struct SmoothVertex {
    Point position{};  // in cell units
    Point normal{};
};

// The surface-nets vertex of the lattice cube whose lowest corner is the
// centre of cell `low`: the mean of its edge crossings, moved along the
// occupancy gradient by noise, then kept inside every expanded box of a solid
// corner on a crossed edge. Each quad then lies inside its own cell's box.
SmoothVertex smooth_vertex(const VoxelSurfaceGrid& grid,
                           std::array<int, 3> low,
                           std::optional<int> ceiling,
                           float roughness,
                           int seed) noexcept {
    const auto offset = [](std::size_t corner, std::size_t axis) {
        return static_cast<int>((corner >> (2U - axis)) & 1U);
    };
    std::array<bool, 8> solid{};
    for (std::size_t corner = 0; corner < solid.size(); ++corner) {
        solid.at(corner) = grid.visible(
            low[0] + offset(corner, 0), low[1] + offset(corner, 1), low[2] + offset(corner, 2), ceiling);
    }
    constexpr float kInf = std::numeric_limits<float>::infinity();
    Point sum{};
    Point lo{-kInf, -kInf, -kInf};
    Point hi{kInf, kInf, kInf};
    Point gradient{};
    int crossings = 0;
    for (std::size_t corner = 0; corner < solid.size(); ++corner) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!solid.at(corner)) {
                gradient.at(axis) += offset(corner, axis) == 1 ? 1.0F : -1.0F;
            }
            const std::size_t bit   = std::size_t{1} << (2U - axis);
            const std::size_t other = corner | bit;
            if ((corner & bit) != 0 || solid.at(corner) == solid.at(other)) {
                continue;
            }
            ++crossings;
            const std::size_t inside = solid.at(corner) ? corner : other;
            for (std::size_t to = 0; to < 3; ++to) {
                const auto from = static_cast<float>(low.at(to));
                sum.at(to) += from + (static_cast<float>(offset(corner, to) + offset(other, to)) * 0.5F) + 0.5F;
                const float cell = from + static_cast<float>(offset(inside, to));
                lo.at(to)        = std::max(lo.at(to), cell - roughness);
                hi.at(to)        = std::min(hi.at(to), cell + 1.0F + roughness);
            }
        }
    }
    SmoothVertex vertex;
    const float length = std::sqrt((gradient[0] * gradient[0]) + (gradient[1] * gradient[1]) + (gradient[2] * gradient[2]));
    const float push   = length > 0.0F ? roughness * lattice_noise(low[0], low[1], low[2], seed) / length : 0.0F;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float base           = sum.at(axis) / static_cast<float>(crossings);
        vertex.position.at(axis) = std::clamp(base + (gradient.at(axis) * push), lo.at(axis), hi.at(axis));
        vertex.normal.at(axis)   = length > 0.0F ? gradient.at(axis) / length : 0.0F;
    }
    return vertex;
}

void append_smooth_face(const VoxelSurfaceGrid& grid,
                        const VoxelFaceQuad& quad,
                        std::optional<int> ceiling,
                        std::map<std::uint32_t, VoxelTextureMesh>& parts) {
    const auto& desc      = grid.desc();
    const float roughness = smooth_roughness(desc.surface.roughness);
    const auto face       = static_cast<std::size_t>(quad.face);
    const auto& step      = kFaceSteps.at(face);
    std::array<SmoothVertex, 4> corners{};
    Point centre{};
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        const auto& offset = kCorners.at(face).at(corner);
        corners.at(corner) = smooth_vertex(grid,
                                           {quad.cell[0] + offset[0] - 1, quad.cell[1] + offset[1] - 1,
                                            quad.cell[2] + offset[2] - 1},
                                           ceiling, roughness, desc.surface.seed);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            centre.at(axis) += corners.at(corner).position.at(axis) * 0.25F;
        }
    }
    const auto texture = nearest_solid_texture(grid, quad.cell, centre, ceiling);
    auto& part         = parts[texture];
    part.texture       = texture;
    ++part.faces;
    // Planar coordinates across the face, one repeat per cell.
    const std::size_t across = step[0] != 0 ? 1 : 0;
    const std::size_t up     = step[2] != 0 ? 1 : 2;
    const std::array<float, 3> origin{desc.origin.x, desc.origin.y, desc.origin.z};
    for (const auto corner : kTriangles) {
        const auto& vertex = corners.at(corner);
        const bool flat    = vertex.normal == Point{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            part.mesh.positions.push_back(origin.at(axis) + (vertex.position.at(axis) * desc.cell_size));
            part.mesh.normals.push_back(flat ? static_cast<float>(step.at(axis)) : vertex.normal.at(axis));
        }
        part.mesh.texcoords.push_back(vertex.position.at(across));
        part.mesh.texcoords.push_back(vertex.position.at(up));
    }
}

std::int32_t noise_mix(std::int32_t h) noexcept {
    // 32-bit wrapping, and floor division by 65536 as an arithmetic shift.
    constexpr std::uint32_t kMultiplier = 73244475U;
    auto x = static_cast<std::uint32_t>(h) * kMultiplier;
    x += static_cast<std::uint32_t>(static_cast<std::int32_t>(x) >> 16);
    x *= kMultiplier;
    x += static_cast<std::uint32_t>(static_cast<std::int32_t>(x) >> 16);
    return static_cast<std::int32_t>(x);
}

std::int32_t noise_add(std::int32_t h, int value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(h) + static_cast<std::uint32_t>(value));
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
    const auto mark = [&](int nx, int ny, int nz) {
        if (index(nx, ny, nz).has_value()) {
            dirty_[static_cast<std::size_t>(chunk_of(nx, ny, nz))] = 1;
        }
    };
    if (desc_.surface.smooth) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    mark(x + dx, y + dy, z + dz);
                }
            }
        }
        return;
    }
    if (!neighbours) {
        return;
    }
    for (const auto& step : kFaceSteps) {
        mark(x + step[0], y + step[1], z + step[2]);
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

bool VoxelSurfaceGrid::visible(int x, int y, int z, std::optional<int> ceiling) const noexcept {
    return solid(x, y, z) && (!ceiling.has_value() || y <= *ceiling);
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
    for (int lx = cx * kChunk; lx < std::min((cx + 1) * kChunk, extent_[0]); ++lx) {
        for (int ly = cy * kChunk; ly < std::min((cy + 1) * kChunk, extent_[1]); ++ly) {
            for (int lz = cz * kChunk; lz < std::min((cz + 1) * kChunk, extent_[2]); ++lz) {
                const int x = lx + desc_.min[0];
                const int y = ly + desc_.min[1];
                const int z = lz + desc_.min[2];
                if (!visible(x, y, z, ceiling)) {
                    continue;
                }
                for (std::size_t face = 0; face < kFaceSteps.size(); ++face) {
                    const auto& step = kFaceSteps.at(face);
                    if (!visible(x + step[0], y + step[1], z + step[2], ceiling)) {
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
    static constexpr std::array<std::array<float, 2>, 4> kUv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};

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
                mesh.positions.push_back(
                    origin.at(axis) + (static_cast<float>(quad.cell.at(axis) + offset.at(axis)) * desc.cell_size));
                mesh.normals.push_back(static_cast<float>(normal.at(axis)));
            }
            mesh.texcoords.push_back(kUv.at(corner)[0]);
            mesh.texcoords.push_back(kUv.at(corner)[1]);
        }
    }
    return mesh;
}

std::vector<VoxelTextureMesh> build_voxel_chunk_meshes(const VoxelSurfaceGrid& grid,
                                                       int chunk,
                                                       std::optional<int> ceiling) {
    std::map<std::uint32_t, VoxelTextureMesh> parts;
    const auto faces = grid.chunk_faces(chunk, ceiling);
    if (grid.desc().surface.smooth) {
        for (const auto& face : faces) {
            append_smooth_face(grid, face, ceiling, parts);
        }
    } else {
        std::map<std::uint32_t, std::vector<VoxelFaceQuad>> by_texture;
        for (const auto& face : faces) {
            by_texture[face.texture].push_back(face);
        }
        for (const auto& [texture, quads] : by_texture) {
            parts[texture] = VoxelTextureMesh{
                .texture = texture, .mesh = build_voxel_mesh(grid.desc(), quads), .faces = static_cast<int>(quads.size())};
        }
    }
    std::vector<VoxelTextureMesh> meshes;
    meshes.reserve(parts.size());
    for (auto& [texture, part] : parts) {
        meshes.push_back(std::move(part));
    }
    return meshes;
}

std::uint32_t nearest_solid_texture(const VoxelSurfaceGrid& grid,
                                    std::array<int, 3> around,
                                    std::array<float, 3> point,
                                    std::optional<int> ceiling) noexcept {
    std::uint32_t texture = grid.texture(around[0], around[1], around[2]);
    float best            = std::numeric_limits<float>::infinity();
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                const std::array<int, 3> cell{around[0] + dx, around[1] + dy, around[2] + dz};
                if (!grid.visible(cell[0], cell[1], cell[2], ceiling)) {
                    continue;
                }
                float distance = 0.0F;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const float delta = point.at(axis) - (static_cast<float>(cell.at(axis)) + 0.5F);
                    distance += delta * delta;
                }
                if (distance < best) {
                    best    = distance;
                    texture = grid.texture(cell[0], cell[1], cell[2]);
                }
            }
        }
    }
    return texture;
}

float smooth_roughness(float roughness) noexcept {
    return roughness > 0.0F ? std::min(roughness, kMaxRoughness) : 0.0F;
}

std::int32_t noise_hash3(int x, int y, int z, int seed) noexcept {
    return noise_mix(noise_add(noise_mix(noise_add(noise_mix(noise_add(noise_mix(seed), x)), y)), z));
}

float lattice_noise(int x, int y, int z, int seed) noexcept {
    constexpr float kHalfRange = 32767.5F;
    return (static_cast<float>(static_cast<std::uint32_t>(noise_hash3(x, y, z, seed)) & 0xFFFFU) / kHalfRange) - 1.0F;
}

}  // namespace cactus::runtime::entt_backend
