// NOLINTBEGIN(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
// -- Catch2 assertion macros intentionally expand through do-while and expression decomposition.
#include "backends/cpp-entt/voxel_surfaces.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <utility>
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

void observe(VoxelSurfaceGrid& grid, const VoxelVolumeDesc& desc, const std::vector<Cell>& cells) {
    grid.begin(desc);
    std::uint64_t ordinal = 1;
    for (const auto& cell : cells) {
        grid.note_solid(cell.x, cell.y, cell.z, cell.texture, ordinal++, std::less<>{});
    }
    grid.end();
}

void observe(VoxelSurfaceGrid& grid, const std::vector<Cell>& cells) {
    observe(grid, small_desc(), cells);
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
// ── Smooth surfaces ──────────────────────────────────────────────────────────

namespace {

VoxelVolumeDesc smooth_desc(std::array<int, 3> max, float roughness = 0.25F, int seed = 0) {
    return VoxelVolumeDesc{.origin    = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                           .cell_size = 1.0F,
                           .min       = {0, 0, 0},
                           .max       = max,
                           .surface   = {.smooth = true, .roughness = roughness, .seed = seed}};
}

// Every cell of the box from lo to hi, inclusive, minus `air`.
std::vector<Cell> solid_box(std::array<int, 3> lo, std::array<int, 3> hi, const std::vector<std::array<int, 3>>& air = {},
                            std::uint32_t texture = 1) {
    std::vector<Cell> cells;
    for (int x = lo[0]; x <= hi[0]; ++x) {
        for (int y = lo[1]; y <= hi[1]; ++y) {
            for (int z = lo[2]; z <= hi[2]; ++z) {
                if (std::ranges::find(air, std::array<int, 3>{x, y, z}) == air.end()) {
                    cells.push_back({x, y, z, texture});
                }
            }
        }
    }
    return cells;
}

using Point = std::array<float, 3>;

std::vector<Point> mesh_points(const std::vector<VoxelTextureMesh>& meshes) {
    std::vector<Point> points;
    for (const auto& part : meshes) {
        for (std::size_t at = 0; at + 2 < part.mesh.positions.size(); at += 3) {
            points.push_back({part.mesh.positions[at], part.mesh.positions[at + 1], part.mesh.positions[at + 2]});
        }
    }
    return points;
}

std::vector<VoxelTextureMesh> all_meshes(const VoxelSurfaceGrid& grid, std::optional<int> ceiling = std::nullopt) {
    std::vector<VoxelTextureMesh> meshes;
    for (int chunk = 0; chunk < grid.chunk_count(); ++chunk) {
        auto found = build_voxel_chunk_meshes(grid, chunk, ceiling);
        std::ranges::move(found, std::back_inserter(meshes));
    }
    return meshes;
}

// Each undirected edge of a closed triangle soup belongs to exactly two triangles.
bool closed(const std::vector<Point>& points) {
    std::map<std::pair<Point, Point>, int> edges;
    for (std::size_t triangle = 0; triangle + 2 < points.size(); triangle += 3) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& a = points[triangle + corner];
            const auto& b = points[triangle + ((corner + 1) % 3)];
            ++edges[a < b ? std::pair{a, b} : std::pair{b, a}];
        }
    }
    return !edges.empty() && std::ranges::all_of(edges, [](const auto& edge) { return edge.second == 2; });
}

// Whether the point lies within (0.5 + roughness) cells of some solid cell's
// centre on every axis.
bool within_bound(const VoxelSurfaceGrid& grid, const Point& point, float roughness) {
    const float reach = 0.5F + roughness + 1e-4F;
    const auto& desc  = grid.desc();
    for (int x = desc.min[0]; x <= desc.max[0]; ++x) {
        for (int y = desc.min[1]; y <= desc.max[1]; ++y) {
            for (int z = desc.min[2]; z <= desc.max[2]; ++z) {
                const Point centre{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F,
                                   static_cast<float>(z) + 0.5F};
                if (grid.solid(x, y, z) && std::abs(point[0] - centre[0]) <= reach &&
                    std::abs(point[1] - centre[1]) <= reach && std::abs(point[2] - centre[2]) <= reach) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Vertices and triangle centres.
bool surface_within_bound(const VoxelSurfaceGrid& grid, const std::vector<Point>& points, float roughness) {
    for (std::size_t triangle = 0; triangle + 2 < points.size(); triangle += 3) {
        Point centre{};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (!within_bound(grid, points[triangle + corner], roughness)) {
                return false;
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                centre.at(axis) += points[triangle + corner].at(axis) / 3.0F;
            }
        }
        if (!within_bound(grid, centre, roughness)) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("std.noise lattice hashes match the stdlib reference samples", "[runtime][voxel-surfaces][smooth]") {
    CHECK(noise_hash3(2, -3, 4, 1337) == -1554478858);
    CHECK(lattice_noise(2, -3, 4, 1337) >= -1.0F);
    CHECK(lattice_noise(2, -3, 4, 1337) <= 1.0F);
    CHECK(lattice_noise(2, -3, 4, 1337) != lattice_noise(2, -3, 4, 1338));
}

TEST_CASE("smooth roughness is kept within 0 and 0.4", "[runtime][voxel-surfaces][smooth]") {
    CHECK(smooth_roughness(-1.0F) == 0.0F);
    CHECK(smooth_roughness(0.3F) == 0.3F);
    CHECK(smooth_roughness(2.0F) == 0.4F);
    CHECK(smooth_roughness(std::numeric_limits<float>::quiet_NaN()) == 0.0F);
}

TEST_CASE("a lone smooth cell draws a closed shape within its bound", "[runtime][voxel-surfaces][smooth]") {
    for (const float roughness : {0.0F, 0.25F, 0.4F}) {
        VoxelSurfaceGrid grid;
        observe(grid, smooth_desc({2, 2, 2}, roughness, 7), {{1, 1, 1, 3}});
        const auto meshes = all_meshes(grid);
        const auto points = mesh_points(meshes);
        REQUIRE_FALSE(points.empty());
        CHECK(closed(points));
        CHECK(surface_within_bound(grid, points, roughness));
        const auto [lo, hi] = std::ranges::minmax(points, {}, [](const Point& p) { return p[0]; });
        CHECK(hi[0] - lo[0] > 0.2F);
    }
}

TEST_CASE("a dug tunnel stays closed and within the bound of its rock", "[runtime][voxel-surfaces][smooth]") {
    VoxelSurfaceGrid grid;
    observe(grid, smooth_desc({6, 4, 4}, 0.4F, 3),
                   solid_box({0, 0, 0}, {6, 4, 4}, {{1, 2, 2}, {2, 2, 2}, {3, 2, 2}, {4, 2, 2}, {5, 2, 2}}));
    const auto points = mesh_points(all_meshes(grid));
    CHECK(closed(points));
    CHECK(surface_within_bound(grid, points, 0.4F));
    // Tunnel walls lie inside the rock, near the dug cells.
    const auto inner = std::ranges::count_if(points, [](const Point& p) {
        return p[0] > 1.0F && p[0] < 6.0F && p[1] > 1.5F && p[1] < 3.5F && p[2] > 1.5F && p[2] < 3.5F;
    });
    CHECK(inner > 0);
}

TEST_CASE("a smooth slab across two chunks has no crack at the border", "[runtime][voxel-surfaces][smooth]") {
    VoxelSurfaceGrid grid;
    observe(grid, smooth_desc({31, 3, 3}, 0.3F, 11), solid_box({10, 0, 0}, {21, 2, 2}));
    REQUIRE(grid.chunk_count() == 2);
    REQUIRE_FALSE(build_voxel_chunk_meshes(grid, 0, std::nullopt).empty());
    REQUIRE_FALSE(build_voxel_chunk_meshes(grid, 1, std::nullopt).empty());
    CHECK(closed(mesh_points(all_meshes(grid))));
}

TEST_CASE("with no roughness the surface depends only on which cells are solid", "[runtime][voxel-surfaces][smooth]") {
    const auto cells = solid_box({0, 0, 0}, {3, 2, 3}, {{1, 1, 1}, {2, 2, 2}});
    VoxelSurfaceGrid first;
    VoxelSurfaceGrid second;
    observe(first, smooth_desc({3, 2, 3}, 0.0F, 1), cells);
    observe(second, smooth_desc({3, 2, 3}, 0.0F, 99), cells);
    CHECK(mesh_points(all_meshes(first)) == mesh_points(all_meshes(second)));
}

TEST_CASE("a seed moves vertices but keeps the enclosed cells", "[runtime][voxel-surfaces][smooth]") {
    const auto cells = solid_box({0, 0, 0}, {3, 2, 3}, {{1, 1, 1}});
    VoxelSurfaceGrid first;
    VoxelSurfaceGrid second;
    observe(first, smooth_desc({3, 2, 3}, 0.3F, 1), cells);
    observe(second, smooth_desc({3, 2, 3}, 0.3F, 2), cells);
    const auto a = mesh_points(all_meshes(first));
    const auto b = mesh_points(all_meshes(second));
    CHECK(a.size() == b.size());
    CHECK(a != b);
    CHECK(surface_within_bound(second, b, 0.3F));
}

TEST_CASE("the same smooth world gives identical vertices whatever the edit and rebuild order",
          "[runtime][voxel-surfaces][smooth]") {
    const auto desc = smooth_desc({20, 5, 5}, 0.25F, 5);
    const std::vector<std::array<int, 3>> dug{{2, 2, 2}, {3, 2, 2}, {15, 2, 2}, {16, 3, 2}, {17, 2, 3}};
    const auto rock = solid_box({0, 0, 0}, {20, 5, 5});
    const auto final_cells = solid_box({0, 0, 0}, {20, 5, 5}, dug);

    VoxelSurfaceGrid direct;
    observe(direct, desc, final_cells);

    // Digs one cell per frame, in reverse order, noting cells back to front.
    VoxelSurfaceGrid stepwise;
    observe(stepwise, desc, rock);
    std::vector<std::array<int, 3>> done;
    for (const auto& cell : std::views::reverse(dug)) {
        done.push_back(cell);
        auto cells = solid_box({0, 0, 0}, {20, 5, 5}, done);
        std::ranges::reverse(cells);
        observe(stepwise, desc, cells);
        (void)stepwise.take_dirty_chunks();
    }

    REQUIRE(direct.chunk_count() == 2);
    const auto forward = mesh_points(all_meshes(direct));
    CHECK(forward == mesh_points(all_meshes(stepwise)));
    auto backward = mesh_points(build_voxel_chunk_meshes(stepwise, 1, std::nullopt));
    const auto first_chunk = mesh_points(build_voxel_chunk_meshes(stepwise, 0, std::nullopt));
    backward.insert(backward.begin(), first_chunk.begin(), first_chunk.end());
    CHECK(forward == backward);
}

TEST_CASE("a blocky volume keeps its cube faces in chunk meshes", "[runtime][voxel-surfaces]") {
    VoxelSurfaceGrid grid;
    observe(grid, {{0, 0, 0, 1}, {1, 0, 0, 2}});
    const auto meshes = build_voxel_chunk_meshes(grid, 0, std::nullopt);
    REQUIRE(meshes.size() == 2);
    CHECK(meshes[0].texture == 1U);
    CHECK(meshes[0].faces == 5);
    CHECK(meshes[1].texture == 2U);
    const auto faces = grid.chunk_faces(0, std::nullopt);
    std::vector<VoxelFaceQuad> first;
    std::ranges::copy_if(faces, std::back_inserter(first), [](const VoxelFaceQuad& face) { return face.texture == 1U; });
    CHECK(meshes[0].mesh.positions == build_voxel_mesh(grid.desc(), first).positions);
}

TEST_CASE("a cutaway closes the smooth surface at its layer", "[runtime][voxel-surfaces][smooth]") {
    VoxelSurfaceGrid grid;
    observe(grid, smooth_desc({3, 3, 3}, 0.2F, 4), solid_box({0, 0, 0}, {3, 3, 3}));
    const auto points = mesh_points(all_meshes(grid, 1));
    CHECK(closed(points));
    CHECK(std::ranges::all_of(points, [](const Point& p) { return p[1] <= 2.0F + 0.2F + 1e-4F; }));
    CHECK(std::ranges::any_of(points, [](const Point& p) { return p[1] > 1.5F; }));
}
TEST_CASE("a smooth surface point takes the nearest solid cell's texture, lowest coordinate on ties",
          "[runtime][voxel-surfaces][smooth]") {
    VoxelSurfaceGrid grid;
    observe(grid, smooth_desc({3, 3, 3}), {{1, 1, 1, 5}, {2, 1, 1, 6}});
    CHECK(nearest_solid_texture(grid, {1, 1, 1}, {2.0F, 1.5F, 1.5F}, std::nullopt) == 5U);
    CHECK(nearest_solid_texture(grid, {2, 1, 1}, {2.0F, 1.5F, 1.5F}, std::nullopt) == 5U);
    CHECK(nearest_solid_texture(grid, {1, 1, 1}, {2.1F, 1.5F, 1.5F}, std::nullopt) == 6U);
    observe(grid, smooth_desc({3, 3, 3}), {{1, 1, 1, 6}, {2, 1, 1, 5}});
    CHECK(nearest_solid_texture(grid, {2, 1, 1}, {2.0F, 1.5F, 1.5F}, std::nullopt) == 6U);
}

TEST_CASE("two materials on one smooth wall each show their own texture", "[runtime][voxel-surfaces][smooth]") {
    VoxelSurfaceGrid grid;
    observe(grid, smooth_desc({5, 2, 2}, 0.2F),
                   {{1, 1, 1, 1}, {2, 1, 1, 1}, {3, 1, 1, 2}, {4, 1, 1, 2}});
    const auto meshes = build_voxel_chunk_meshes(grid, 0, std::nullopt);
    REQUIRE(meshes.size() == 2);
    CHECK(meshes[0].texture == 1U);
    CHECK(meshes[1].texture == 2U);
    CHECK(meshes[0].faces + meshes[1].faces == 18);
    for (const auto& part : meshes) {
        const auto points = mesh_points({part});
        for (std::size_t triangle = 0; triangle + 2 < points.size(); triangle += 6) {
            const float centre_x = (points[triangle][0] + points[triangle + 1][0] + points[triangle + 2][0]) / 3.0F;
            CHECK((part.texture == 1U ? centre_x < 3.0F : centre_x > 3.0F));
        }
    }
}

TEST_CASE("an isolated smooth dig rebuilds only the chunks within one cell of it", "[runtime][voxel-surfaces][smooth]") {
    const auto desc = smooth_desc({47, 47, 47});
    const auto rock = solid_box({0, 0, 0}, {47, 47, 47});
    const auto chunk_at = [](int x, int y, int z) { return (((x / 16) * 3) + (y / 16)) * 3 + (z / 16); };
    VoxelSurfaceGrid grid;
    observe(grid, desc, rock);
    (void)grid.take_dirty_chunks();

    observe(grid, desc, solid_box({0, 0, 0}, {47, 47, 47}, {{24, 24, 24}}));
    CHECK(grid.take_dirty_chunks() == std::vector<int>{chunk_at(24, 24, 24)});

    // At a chunk corner the cell's 26 neighbours reach 8 chunks.
    observe(grid, desc, solid_box({0, 0, 0}, {47, 47, 47}, {{24, 24, 24}, {16, 16, 16}}));
    std::vector<int> corner;
    for (const int x : {15, 16}) {
        for (const int y : {15, 16}) {
            for (const int z : {15, 16}) {
                corner.push_back(chunk_at(x, y, z));
            }
        }
    }
    std::ranges::sort(corner);
    CHECK(grid.take_dirty_chunks() == corner);

    // A texture write on a chunk's edge reaches the chunk across it too.
    auto cells = solid_box({0, 0, 0}, {47, 47, 47}, {{24, 24, 24}, {16, 16, 16}});
    std::ranges::find_if(cells, [](const Cell& cell) { return cell.x == 31 && cell.y == 20 && cell.z == 20; })->texture = 9;
    observe(grid, desc, cells);
    CHECK(grid.take_dirty_chunks() == std::vector<int>{chunk_at(31, 20, 20), chunk_at(32, 20, 20)});
}

TEST_CASE("a blocky dig at a chunk corner marks only face neighbours' chunks", "[runtime][voxel-surfaces]") {
    VoxelVolumeDesc desc{.cell_size = 1.0F, .min = {0, 0, 0}, .max = {31, 31, 31}};
    VoxelSurfaceGrid grid;
    observe(grid, desc, solid_box({0, 0, 0}, {31, 31, 31}));
    (void)grid.take_dirty_chunks();
    observe(grid, desc, solid_box({0, 0, 0}, {31, 31, 31}, {{16, 16, 16}}));
    CHECK(grid.take_dirty_chunks().size() == 4);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while,bugprone-chained-comparison,readability-function-cognitive-complexity)
