// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/voxel_surfaces.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

using namespace cactus::runtime::entt_backend;

namespace {

// A 4 x 3 x 4 volume at (10, 0, 0) with y in -1..1, like the voxel fixture.
VoxelVolumeDesc small_desc() {
    return VoxelVolumeDesc{.origin    = Vector3{.x = 10.0F, .y = 0.0F, .z = 0.0F},
                           .cell_size = 1.0F,
                           .min       = {0, -1, 0},
                           .max       = {3, 1, 3}};
}

struct Cell {
    int x;
    int y;
    int z;
    std::uint32_t texture;
};

void observe(VoxelSurfaceGrid& grid, const std::vector<Cell>& cells) {
    grid.begin(small_desc());
    std::uint64_t ordinal = 1;
    for (const auto& cell : cells) {
        grid.note_solid(cell.x, cell.y, cell.z, cell.texture, ordinal++, std::less<>{});
    }
    grid.end();
}

std::vector<VoxelFaceQuad> all_faces(const VoxelSurfaceGrid& grid, std::optional<int> ceiling = std::nullopt) {
    std::vector<VoxelFaceQuad> faces;
    for (int chunk = 0; chunk < grid.chunk_count(); ++chunk) {
        const auto found = grid.chunk_faces(chunk, ceiling);
        faces.insert(faces.end(), found.begin(), found.end());
    }
    return faces;
}

std::size_t count_faces(const std::vector<VoxelFaceQuad>& faces, VoxelFace face) {
    return static_cast<std::size_t>(std::ranges::count(faces, face, &VoxelFaceQuad::face));
}

}  // namespace

TEST_CASE("a lone solid cell shows all six faces, two adjacent cells hide the shared pair",
          "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{1, 0, 1, 7}});
    CHECK(all_faces(grid).size() == 6);
    observe(grid, {{1, 0, 1, 7}, {2, 0, 1, 7}});
    const auto faces = all_faces(grid);
    CHECK(faces.size() == 10);
    CHECK(count_faces(faces, VoxelFace::PosX) == 1);
    CHECK(count_faces(faces, VoxelFace::NegX) == 1);
}

TEST_CASE("faces on the volume boundary are exposed", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{0, -1, 0, 3}});
    const auto faces = all_faces(grid);
    CHECK(faces.size() == 6);
    CHECK(count_faces(faces, VoxelFace::NegY) == 1);
}

TEST_CASE("removing a cell exposes its neighbour and marks the chunk dirty", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{1, 0, 1, 7}, {2, 0, 1, 7}});
    (void)grid.take_dirty_chunks();
    observe(grid, {{1, 0, 1, 7}});
    CHECK_FALSE(grid.solid(2, 0, 1));
    CHECK(all_faces(grid).size() == 6);
    CHECK_FALSE(grid.take_dirty_chunks().empty());
}

TEST_CASE("an unchanged frame dirties nothing", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{1, 0, 1, 7}, {2, 0, 1, 8}});
    CHECK_FALSE(grid.take_dirty_chunks().empty());
    const auto revision = grid.revision();
    observe(grid, {{2, 0, 1, 8}, {1, 0, 1, 7}});
    CHECK(grid.take_dirty_chunks().empty());
    CHECK(grid.revision() == revision);
}

TEST_CASE("a texture change keeps faces but changes their material", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{1, 0, 1, 7}});
    (void)grid.take_dirty_chunks();
    observe(grid, {{1, 0, 1, 9}});
    CHECK_FALSE(grid.take_dirty_chunks().empty());
    const auto faces = all_faces(grid);
    REQUIRE(faces.size() == 6);
    CHECK(std::ranges::all_of(faces, [](const VoxelFaceQuad& face) { return face.texture == 9U; }));
}

TEST_CASE("adjacent materials keep their own textures", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{0, 0, 0, 1}, {1, 0, 0, 2}, {2, 0, 0, 3}});
    const auto faces = all_faces(grid);
    for (const auto& face : faces) {
        CHECK(face.texture == static_cast<std::uint32_t>(face.cell[0] + 1));
    }
    CHECK(faces.size() == 14);
}

TEST_CASE("duplicate cells take the earliest-created texture", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    grid.begin(small_desc());
    grid.note_solid(1, 0, 1, 5, 20, std::less<>{});
    grid.note_solid(1, 0, 1, 6, 10, std::less<>{});
    grid.note_solid(1, 0, 1, 7, 30, std::less<>{});
    grid.end();
    CHECK(grid.texture(1, 0, 1) == 6U);
}

TEST_CASE("a cutaway hides cells above its layer and exposes the section", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{1, -1, 1, 7}, {1, 0, 1, 7}, {1, 1, 1, 7}});
    CHECK(all_faces(grid).size() == 14);
    const auto cut = all_faces(grid, 0);
    CHECK(cut.size() == 10);
    CHECK(std::ranges::none_of(cut, [](const VoxelFaceQuad& face) { return face.cell[1] > 0; }));
    CHECK(count_faces(cut, VoxelFace::PosY) == 1);
}

TEST_CASE("a cutaway on a chunk's top layer still cuts that chunk", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    grid.begin(VoxelVolumeDesc{.cell_size = 1.0F, .min = {0, 0, 0}, .max = {0, 31, 0}});
    for (int y = 0; y < 32; ++y) {
        grid.note_solid(0, y, 0, 1, static_cast<std::uint64_t>(y) + 1, std::less<>{});
    }
    grid.end();
    REQUIRE(grid.chunk_count() == 2);
    CHECK(grid.chunk_view(0, std::nullopt) == VoxelChunkView::Full);
    CHECK(grid.chunk_view(0, 15) == VoxelChunkView::Cut);
    CHECK(count_faces(grid.chunk_faces(0, 15), VoxelFace::PosY) == 1);
    CHECK(grid.chunk_view(1, 15) == VoxelChunkView::Hidden);
    CHECK(grid.chunk_view(0, 16) == VoxelChunkView::Full);
    CHECK(grid.chunk_view(1, 16) == VoxelChunkView::Cut);
    CHECK(grid.chunk_view(1, 31) == VoxelChunkView::Full);
}

TEST_CASE("cells outside the bounds are ignored and a new description resets the grid",
          "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{9, 0, 0, 1}, {1, 0, 1, 1}});
    CHECK(all_faces(grid).size() == 6);
    auto wider = small_desc();
    wider.max  = {7, 1, 3};
    grid.begin(wider);
    grid.end();
    CHECK(all_faces(grid).empty());
    CHECK(grid.chunk_count() == 1);
}

TEST_CASE("a quad mesh gives two counter-clockwise triangles and a whole texture per face",
          "[runtime][voxel-surfaces]") {
    const auto desc = small_desc();
    const std::vector<VoxelFaceQuad> faces{{.cell = {2, -1, 3}, .face = VoxelFace::PosY, .texture = 1},
                                           {.cell = {0, 0, 0}, .face = VoxelFace::NegX, .texture = 1}};
    const auto mesh = build_voxel_mesh(desc, faces);
    REQUIRE(mesh.positions.size() == 2U * 6U * 3U);
    REQUIRE(mesh.texcoords.size() == 2U * 6U * 2U);
    // Top face of cell (2, -1, 3): y = origin.y + 0, x in [12, 13], z in [3, 4].
    for (std::size_t vertex = 0; vertex < 6; ++vertex) {
        CHECK(mesh.positions[(vertex * 3) + 1] == Catch::Approx(0.0F));
        CHECK(mesh.normals[(vertex * 3) + 1] == 1.0F);
        CHECK(mesh.positions[vertex * 3] >= 12.0F);
        CHECK(mesh.positions[vertex * 3] <= 13.0F);
    }
    // Each triangle winds counter-clockwise seen along its normal.
    for (std::size_t triangle = 0; triangle < 4; ++triangle) {
        const auto at = [&](std::size_t corner, std::size_t axis) {
            return mesh.positions[(((triangle * 3) + corner) * 3) + axis];
        };
        const float ax = at(1, 0) - at(0, 0);
        const float ay = at(1, 1) - at(0, 1);
        const float az = at(1, 2) - at(0, 2);
        const float bx = at(2, 0) - at(0, 0);
        const float by = at(2, 1) - at(0, 1);
        const float bz = at(2, 2) - at(0, 2);
        const float nx = (ay * bz) - (az * by);
        const float ny = (az * bx) - (ax * bz);
        const float nz = (ax * by) - (ay * bx);
        const std::size_t first = triangle * 3 * 3;
        CHECK((nx * mesh.normals[first]) + (ny * mesh.normals[first + 1]) + (nz * mesh.normals[first + 2]) > 0.0F);
    }
    float min_u = 1.0F;
    float max_u = 0.0F;
    for (std::size_t vertex = 0; vertex < 6; ++vertex) {
        min_u = std::min(min_u, mesh.texcoords[vertex * 2]);
        max_u = std::max(max_u, mesh.texcoords[vertex * 2]);
    }
    CHECK(min_u == 0.0F);
    CHECK(max_u == 1.0F);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
