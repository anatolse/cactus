#include "backends/cpp-entt/runtime.hpp"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <memory_resource>
#include <numbers>
#include <raymath.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cactus::runtime::entt_backend {

namespace {
RenderDebugState& render_debug_state_storage() noexcept {
    static RenderDebugState state;
    return state;
}

Camera2D& active_camera_2d_storage() noexcept {
    static Camera2D cam{.offset = {}, .target = {}, .rotation = 0.0F, .zoom = 1.0F};
    return cam;
}

Camera3D& active_camera_3d_storage() noexcept {
    static Camera3D cam{.position   = {.x = 6.0F, .y = 6.0F, .z = 6.0F},
                        .target     = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                        .up         = {.x = 0.0F, .y = 1.0F, .z = 0.0F},
                        .fovy       = 45.0F,
                        .projection = CAMERA_PERSPECTIVE};
    return cam;
}

EditorHitTestImpl& hit_test_impl_storage() noexcept {
    static EditorHitTestImpl impl;
    return impl;
}

EditorSpawnImpl& spawn_impl_storage() noexcept {
    static EditorSpawnImpl impl;
    return impl;
}

struct SpriteSubmission {
    Vector2 position{};
    Vector2 size{};
    Color color{};
    int runtime_id{-1};
    int layer{0};
};

struct MeshSubmission {
    Vector3 position{};
    Quat rotation{};
    Vector3 scale{};
    int mesh_runtime_id{-1};
    int material_runtime_id{-1};
};

struct PointLightSubmission {
    Vector3 position{};
    Color color{};
    float intensity{1.0F};
    float range{10.0F};
};

struct TextSubmission2D {
    Vector2 position{};
    float   rotation_deg{0.0F};
    int     font_size{32};
    Color   color{};
    std::string text;
};

struct TextSubmission3D {
    uint32_t    entity_id{0};
    Vector3     position{};
    Quat        rotation{};
    Vector3     scale{};
    int         font_size{1};
    Color       color{};
    std::string text;
};

struct TextLabel3DEntry {
    RenderTexture2D rt{};
    std::string     cached_text;
    int             cached_font_size{0};
    Color           cached_color{};
    bool            loaded{false};
};

struct TextureResourceEntry {
    Texture2D texture{};
    bool loaded{false};
    bool owned{false};
};

struct MeshResourceEntry {
    Mesh mesh{};
    bool loaded{false};
    bool owned{false};
};

struct MaterialResourceEntry {
    Material material{};
    bool loaded{false};
    bool owned{false};
};

std::pmr::unsynchronized_pool_resource& render_queue_resource() noexcept {
    static std::pmr::unsynchronized_pool_resource resource;
    return resource;
}

std::pmr::vector<SpriteSubmission>& sprite_queue() noexcept {
    static std::pmr::vector<SpriteSubmission> queue{&render_queue_resource()};
    return queue;
}

std::pmr::vector<MeshSubmission>& mesh_queue() noexcept {
    static std::pmr::vector<MeshSubmission> queue{&render_queue_resource()};
    return queue;
}

std::pmr::vector<PointLightSubmission>& point_light_queue() noexcept {
    static std::pmr::vector<PointLightSubmission> queue{&render_queue_resource()};
    return queue;
}

std::vector<TextSubmission2D>& text_2d_queue() noexcept {
    static std::vector<TextSubmission2D> queue;
    return queue;
}

std::vector<TextSubmission3D>& text_3d_queue() noexcept {
    static std::vector<TextSubmission3D> queue;
    return queue;
}

std::unordered_map<uint32_t, TextLabel3DEntry>& text_label_3d_cache() noexcept {
    static std::unordered_map<uint32_t, TextLabel3DEntry> cache;
    return cache;
}

std::unordered_map<int, TextureResourceEntry>& textures() noexcept {
    static std::unordered_map<int, TextureResourceEntry> entries;
    return entries;
}

std::unordered_map<int, MeshResourceEntry>& meshes() noexcept {
    static std::unordered_map<int, MeshResourceEntry> entries;
    return entries;
}

std::unordered_map<int, MaterialResourceEntry>& materials() noexcept {
    static std::unordered_map<int, MaterialResourceEntry> entries;
    return entries;
}

constexpr int kMaxMeshLights  = 4;
constexpr int kPointLightType = 1;

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
constexpr const char* kLightingVertexShader = R"(#version 100

attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kLightingFragmentShader = R"(#version 100

precision mediump float;

varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

#define MAX_LIGHTS 4
#define LIGHT_POINT 1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

void main()
{
    vec4 texelColor = texture2D(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1 && lights[i].type == LIGHT_POINT)
        {
            vec3 light = normalize(lights[i].position - fragPosition);
            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);
            specular += specCo*lights[i].color.rgb;
        }
    }

    vec4 finalColor = texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0));
    finalColor += texelColor*(ambient/10.0)*colDiffuse;
    gl_FragColor = pow(finalColor, vec4(1.0/2.2));
}
)";
#else
constexpr const char* kLightingVertexShader = R"(#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kLightingFragmentShader = R"(#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

#define MAX_LIGHTS 4
#define LIGHT_POINT 1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1 && lights[i].type == LIGHT_POINT)
        {
            vec3 light = normalize(lights[i].position - fragPosition);
            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);
            specular += specCo*lights[i].color.rgb;
        }
    }

    finalColor = texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0));
    finalColor += texelColor*(ambient/10.0)*colDiffuse;
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
)";
#endif

struct LightingShaderState {
    Shader shader{};
    bool loaded{false};
    int ambient_loc{-1};
    int view_pos_loc{-1};
    std::array<int, kMaxMeshLights> enabled_locs{};
    std::array<int, kMaxMeshLights> type_locs{};
    std::array<int, kMaxMeshLights> position_locs{};
    std::array<int, kMaxMeshLights> target_locs{};
    std::array<int, kMaxMeshLights> color_locs{};
};

LightingShaderState& lighting_shader_state() noexcept {
    static LightingShaderState state{};
    return state;
}

std::string light_uniform_name(const int index, const std::string_view field) {
    std::string result{"lights["};
    result += std::to_string(index);
    result += "].";
    result += field;
    return result;
}

void reset_lighting_shader_state() noexcept {
    auto& state = lighting_shader_state();
    state       = {};
    state.enabled_locs.fill(-1);
    state.type_locs.fill(-1);
    state.position_locs.fill(-1);
    state.target_locs.fill(-1);
    state.color_locs.fill(-1);
}

Shader* ensure_lighting_shader() {
    auto& state = lighting_shader_state();
    if (state.loaded) {
        return &state.shader;
    }
    if (!IsWindowReady()) {
        return nullptr;
    }

    reset_lighting_shader_state();
    state.shader = LoadShaderFromMemory(kLightingVertexShader, kLightingFragmentShader);
    if (!IsShaderValid(state.shader)) {
        reset_lighting_shader_state();
        return nullptr;
    }

    state.loaded                                = true;
    state.shader.locs[SHADER_LOC_MATRIX_MODEL]  = GetShaderLocation(state.shader, "matModel");
    state.shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(state.shader, "matNormal");
    state.shader.locs[SHADER_LOC_VECTOR_VIEW]   = GetShaderLocation(state.shader, "viewPos");
    state.ambient_loc                           = GetShaderLocation(state.shader, "ambient");
    state.view_pos_loc                          = GetShaderLocation(state.shader, "viewPos");

    for (int i = 0; i < kMaxMeshLights; ++i) {
        const auto index    = static_cast<std::size_t>(i);
        const auto location = [&](const std::string_view field) {
            const std::string uniform = light_uniform_name(i, field);
            return GetShaderLocation(state.shader, uniform.c_str());
        };
        state.enabled_locs[index]  = location("enabled");
        state.type_locs[index]     = location("type");
        state.position_locs[index] = location("position");
        state.target_locs[index]   = location("target");
        state.color_locs[index]    = location("color");
    }

    return &state.shader;
}

void clear_lighting_shader() noexcept {
    auto& state = lighting_shader_state();
    if (state.loaded && IsWindowReady()) {
        UnloadShader(state.shader);
    }
    reset_lighting_shader_state();
}

void apply_point_lights(const Camera3D& camera) {
    Shader* shader = ensure_lighting_shader();
    if (shader == nullptr) {
        return;
    }

    auto& shader_state = lighting_shader_state();
    const std::array<float, 4> ambient{0.22F, 0.22F, 0.28F, 1.0F};
    const std::array<float, 3> view_pos{camera.position.x, camera.position.y, camera.position.z};
    SetShaderValue(*shader, shader_state.ambient_loc, ambient.data(), SHADER_UNIFORM_VEC4);
    SetShaderValue(*shader, shader_state.view_pos_loc, view_pos.data(), SHADER_UNIFORM_VEC3);

    const int active_lights = std::min(static_cast<int>(point_light_queue().size()), kMaxMeshLights);
    render_debug_state_storage().active_point_lights  = active_lights;
    render_debug_state_storage().used_lit_mesh_shader = active_lights > 0;

    for (int i = 0; i < kMaxMeshLights; ++i) {
        const int enabled = i < active_lights ? 1 : 0;
        const int type    = kPointLightType;
        const auto zero   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        std::array<float, 3> position{zero.x, zero.y, zero.z};
        std::array<float, 3> target{zero.x, zero.y, zero.z};
        std::array<float, 4> color{0.0F, 0.0F, 0.0F, 1.0F};

        if (i < active_lights) {
            const auto& light = point_light_queue()[static_cast<std::size_t>(i)];
            position[0]       = light.position.x;
            position[1]       = light.position.y;
            position[2]       = light.position.z;
            color[0]          = (static_cast<float>(light.color.r) / 255.0F) * light.intensity;
            color[1]          = (static_cast<float>(light.color.g) / 255.0F) * light.intensity;
            color[2]          = (static_cast<float>(light.color.b) / 255.0F) * light.intensity;
            color[3]          = static_cast<float>(light.color.a) / 255.0F;
        }

        const auto index = static_cast<std::size_t>(i);
        SetShaderValue(*shader, shader_state.enabled_locs[index], &enabled, SHADER_UNIFORM_INT);
        SetShaderValue(*shader, shader_state.type_locs[index], &type, SHADER_UNIFORM_INT);
        SetShaderValue(*shader, shader_state.position_locs[index], position.data(), SHADER_UNIFORM_VEC3);
        SetShaderValue(*shader, shader_state.target_locs[index], target.data(), SHADER_UNIFORM_VEC3);
        SetShaderValue(*shader, shader_state.color_locs[index], color.data(), SHADER_UNIFORM_VEC4);
    }
}

void note_missing_asset() noexcept {
    ++render_debug_state_storage().missing_assets;
}

void clear_texture_store() noexcept {
    for (auto& [runtime_id, entry] : textures()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadTexture(entry.texture);
        }
    }
    textures().clear();
}

void clear_mesh_store() noexcept {
    for (auto& [runtime_id, entry] : meshes()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadMesh(entry.mesh);
        }
    }
    meshes().clear();
}

void clear_material_store() noexcept {
    for (auto& [runtime_id, entry] : materials()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadMaterial(entry.material);
        }
    }
    materials().clear();
}

Texture2D* ensure_texture_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = textures()[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        Image image   = GenImageColor(1, 1, WHITE);
        entry.texture = LoadTextureFromImage(image);
        UnloadImage(image);
        entry.loaded = true;
        entry.owned  = true;
    }
    return &entry.texture;
}

Mesh* ensure_mesh_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = meshes()[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        entry.mesh = GenMeshCube(1.0F, 1.0F, 1.0F);
        UploadMesh(&entry.mesh, false);
        entry.loaded = true;
        entry.owned  = true;
    }
    return &entry.mesh;
}

Material* ensure_material_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = materials()[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        entry.material = LoadMaterialDefault();
        entry.loaded   = true;
        entry.owned    = true;
    }
    if (Shader* shader = ensure_lighting_shader(); shader != nullptr) {
        entry.material.shader = *shader;
    }
    entry.material.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
    return &entry.material;
}

Matrix mesh_transform_matrix(const MeshSubmission& submission) noexcept {
    const Matrix scale       = MatrixScale(submission.scale.x, submission.scale.y, submission.scale.z);
    const Matrix rotation    = QuaternionToMatrix(submission.rotation);
    const Matrix translation = MatrixTranslate(submission.position.x, submission.position.y, submission.position.z);
    return MatrixMultiply(MatrixMultiply(scale, rotation), translation);
}

void flush_mesh_queue() noexcept {
    if (mesh_queue().empty()) {
        return;
    }

    render_debug_state_storage().used_default_3d_camera = true;
    render_debug_state_storage().active_point_lights =
        std::min(static_cast<int>(point_light_queue().size()), kMaxMeshLights);
    render_debug_state_storage().used_lit_mesh_shader = render_debug_state_storage().active_point_lights > 0;
    if (!IsWindowReady()) {
        return;
    }

    const Camera3D camera = get_active_camera_3d();

    apply_point_lights(camera);

    BeginMode3D(camera);
    for (const auto& submission : mesh_queue()) {
        Mesh* mesh         = ensure_mesh_resource(submission.mesh_runtime_id);
        Material* material = ensure_material_resource(submission.material_runtime_id);
        if (mesh == nullptr || material == nullptr) {
            continue;
        }
        DrawMesh(*mesh, *material, mesh_transform_matrix(submission));
    }
    EndMode3D();
}

// Shared 1×1 XY-plane mesh (normal = +Z). V coordinates are pre-flipped to
// cancel the RenderTexture2D vertical inversion so text appears upright.
Mesh& text_plane_mesh() noexcept {
    static Mesh mesh{};
    static bool ready{false};
    if (!ready && IsWindowReady()) {
        constexpr int kVerts     = 4;
        constexpr int kTriangles = 2;

        mesh.vertexCount   = kVerts;
        mesh.triangleCount = kTriangles;

        mesh.vertices  = static_cast<float*>(MemAlloc(static_cast<unsigned>(kVerts * 3) * sizeof(float)));
        mesh.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned>(kVerts * 2) * sizeof(float)));
        mesh.normals   = static_cast<float*>(MemAlloc(static_cast<unsigned>(kVerts * 3) * sizeof(float)));
        mesh.indices =
            static_cast<unsigned short*>(MemAlloc(static_cast<unsigned>(kTriangles * 3) * sizeof(unsigned short)));

        // Positions: XY plane, centred, counter-clockwise when viewed from +Z
        // [0] bottom-left  [1] bottom-right  [2] top-right  [3] top-left
        const std::array<float, 12> verts = {
            -0.5F, -0.5F, 0.0F,
             0.5F, -0.5F, 0.0F,
             0.5F,  0.5F, 0.0F,
            -0.5F,  0.5F, 0.0F,
        };
        // UVs: V=0 at top, V=1 at bottom — cancels RenderTexture2D Y-flip
        const std::array<float, 8> uvs = {
            0.0F, 1.0F,
            1.0F, 1.0F,
            1.0F, 0.0F,
            0.0F, 0.0F,
        };
        const std::array<float, 12> normals = {
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
            0.0F, 0.0F, 1.0F,
        };
        const std::array<unsigned short, 6> indices = {0, 1, 2, 0, 2, 3};

        for (int i = 0; i < kVerts * 3; ++i) {
            mesh.vertices[i] = verts[static_cast<size_t>(i)];
        }
        for (int i = 0; i < kVerts * 2; ++i) {
            mesh.texcoords[i] = uvs[static_cast<size_t>(i)];
        }
        for (int i = 0; i < kVerts * 3; ++i) {
            mesh.normals[i] = normals[static_cast<size_t>(i)];
        }
        for (int i = 0; i < kTriangles * 3; ++i) {
            mesh.indices[i] = indices[static_cast<size_t>(i)];
        }

        UploadMesh(&mesh, false);
        ready = true;
    }
    return mesh;
}

void flush_text_2d_queue() noexcept {
    if (text_2d_queue().empty() || !IsWindowReady()) {
        return;
    }
    const Font font          = GetFontDefault();
    constexpr float kSpacing = 1.0F;

    Camera2D camera{};
    camera.zoom = 1.0F;
    BeginMode2D(camera);
    for (const auto& sub : text_2d_queue()) {
        const auto fs      = static_cast<float>(sub.font_size);
        const Vector2 size = MeasureTextEx(font, sub.text.c_str(), fs, kSpacing);
        const Vector2 origin{.x = size.x * 0.5F, .y = size.y * 0.5F};
        DrawTextPro(font, sub.text.c_str(), sub.position, origin, sub.rotation_deg, fs, kSpacing, sub.color);
    }
    EndMode2D();
}

void flush_text_3d_queue() noexcept {
    if (text_3d_queue().empty() || !IsWindowReady()) {
        return;
    }

    constexpr int kPixelsPerWorldUnit = 256;

    // Phase 1: bake dirty render textures (must be outside any BeginMode block)
    for (auto& sub : text_3d_queue()) {
        if (sub.text.empty()) {
            continue;
        }
        auto& entry      = text_label_3d_cache()[sub.entity_id];
        const bool dirty = !entry.loaded || entry.cached_text != sub.text || entry.cached_font_size != sub.font_size ||
                           entry.cached_color.r != sub.color.r || entry.cached_color.g != sub.color.g ||
                           entry.cached_color.b != sub.color.b || entry.cached_color.a != sub.color.a;
        if (!dirty) {
            continue;
        }
        const int tex_h = std::max(64, sub.font_size * kPixelsPerWorldUnit);
        const int tex_w = tex_h * 4;
        if (entry.loaded) {
            UnloadRenderTexture(entry.rt);
        }
        entry.rt     = LoadRenderTexture(tex_w, tex_h);
        entry.loaded = true;

        const int bake_fs   = static_cast<int>(static_cast<float>(tex_h) * 0.75F);
        const int text_w_px = MeasureText(sub.text.c_str(), bake_fs);
        const int draw_x    = (tex_w - text_w_px) / 2;
        const int draw_y    = (tex_h - bake_fs) / 2;

        BeginTextureMode(entry.rt);
        ClearBackground(Color{.r = 0, .g = 0, .b = 0, .a = 0});
        DrawText(sub.text.c_str(), draw_x, draw_y, bake_fs, sub.color);
        EndTextureMode();

        entry.cached_text      = sub.text;
        entry.cached_font_size = sub.font_size;
        entry.cached_color     = sub.color;
    }

    // Phase 2: draw plane meshes inside the 3D camera block
    const Camera3D camera = get_active_camera_3d();

    Mesh& plane = text_plane_mesh();

    Material mat = LoadMaterialDefault();
    BeginMode3D(camera);
    for (const auto& sub : text_3d_queue()) {
        if (sub.text.empty()) {
            continue;
        }
        auto it = text_label_3d_cache().find(sub.entity_id);
        if (it == text_label_3d_cache().end() || !it->second.loaded) {
            continue;
        }
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = it->second.rt.texture;

        const Matrix xform = mesh_transform_matrix(MeshSubmission{
            .position = sub.position,
            .rotation = sub.rotation,
            .scale    = sub.scale,
        });
        DrawMesh(plane, mat, xform);
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    }
    EndMode3D();
    UnloadMaterial(mat);
}

void flush_sprite_queue() noexcept {
    if (sprite_queue().empty()) {
        return;
    }

    std::ranges::stable_sort(sprite_queue(), [](const auto& lhs, const auto& rhs) { return lhs.layer < rhs.layer; });
    render_debug_state_storage().used_default_2d_camera = true;
    render_debug_state_storage().drawn_sprite_layers.clear();
    for (const auto& submission : sprite_queue()) {
        render_debug_state_storage().drawn_sprite_layers.push_back(submission.layer);
    }

    if (!IsWindowReady()) {
        return;
    }

    Camera2D camera{};
    camera.offset   = Vector2{.x = 0.0F, .y = 0.0F};
    camera.target   = Vector2{.x = 0.0F, .y = 0.0F};
    camera.rotation = 0.0F;
    camera.zoom     = 1.0F;

    BeginMode2D(camera);
    for (const auto& submission : sprite_queue()) {
        Texture2D* texture = ensure_texture_resource(submission.runtime_id);
        if (texture == nullptr) {
            continue;
        }
        const Rectangle src{.x      = 0.0F,
                            .y      = 0.0F,
                            .width  = static_cast<float>(texture->width),
                            .height = static_cast<float>(texture->height)};
        const Rectangle dst{.x      = submission.position.x,
                            .y      = submission.position.y,
                            .width  = submission.size.x,
                            .height = submission.size.y};
        DrawTexturePro(*texture, src, dst, Vector2{.x = 0.0F, .y = 0.0F}, 0.0F, submission.color);
    }
    EndMode2D();
}
}  // namespace

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void reset_render_debug_state() noexcept {
    render_debug_state_storage() = {};
    sprite_queue().clear();
    mesh_queue().clear();
    point_light_queue().clear();
    text_2d_queue().clear();
    text_3d_queue().clear();
    if (IsWindowReady()) {
        for (auto& [id, entry] : text_label_3d_cache()) {
            if (entry.loaded) {
                UnloadRenderTexture(entry.rt);
            }
        }
    }
    text_label_3d_cache().clear();
    clear_texture_store();
    clear_mesh_store();
    clear_material_store();
    clear_lighting_shader();
    shared_asset_registry().clear_diagnostics();
}

const RenderDebugState& render_debug_state() noexcept {
    return render_debug_state_storage();
}

void begin_render_frame() noexcept {
    sprite_queue().clear();
    mesh_queue().clear();
    point_light_queue().clear();
    text_2d_queue().clear();
    text_3d_queue().clear();
    render_debug_state_storage().used_default_2d_camera = false;
    render_debug_state_storage().used_default_3d_camera = false;
    render_debug_state_storage().active_point_lights    = 0;
    render_debug_state_storage().used_lit_mesh_shader   = false;
    render_debug_state_storage().drawn_sprite_layers.clear();
}

void end_render_frame() noexcept {
    flush_mesh_queue();
    flush_text_3d_queue();
    flush_sprite_queue();
    flush_text_2d_queue();
    mesh_queue().clear();
    sprite_queue().clear();
    point_light_queue().clear();
    text_2d_queue().clear();
    text_3d_queue().clear();
}

void submit_sprite(const Vector2 position,
                   const Vector2 size,
                   const Color color,
                   const AssetHandle texture,
                   const bool visible,
                   const int layer) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        note_missing_asset();
        return;
    }
    sprite_queue().push_back(SpriteSubmission{
        .position   = position,
        .size       = size,
        .color      = color,
        .runtime_id = resolved.runtime_id,
        .layer      = layer,
    });
    ++render_debug_state_storage().submitted_sprites;
}

void advance_animated_sprite(const AssetHandle texture,
                             int& frame,
                             const int frame_count,
                             const float fps,
                             const bool playing,
                             const float dt) noexcept {
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.valid()) {
        note_missing_asset();
        return;
    }
    if (!playing || frame_count <= 0 || fps <= 0.0F) {
        return;
    }
    const auto step = static_cast<int>(dt * fps);
    if (step <= 0) {
        return;
    }
    frame = (frame + step) % frame_count;
    ++render_debug_state_storage().advanced_animated_sprites;
}

void submit_mesh(const Vector3 position,
                 const Quat rotation,
                 const Vector3 scale,
                 const AssetHandle mesh,
                 const AssetHandle material,
                 const bool visible,
                 const bool /*cast_shadow*/) noexcept {
    if (!visible) {
        return;
    }
    const auto mesh_resolved     = shared_asset_registry().resolve(AssetKind::Mesh, mesh);
    const auto material_resolved = shared_asset_registry().resolve(AssetKind::Material, material);
    if (!mesh_resolved.ready() || !material_resolved.ready()) {
        note_missing_asset();
        return;
    }
    mesh_queue().push_back(MeshSubmission{
        .position            = position,
        .rotation            = rotation,
        .scale               = scale,
        .mesh_runtime_id     = mesh_resolved.runtime_id,
        .material_runtime_id = material_resolved.runtime_id,
    });
    ++render_debug_state_storage().submitted_meshes;
}

void submit_billboard(const Vector3 /*position*/,
                      const Vector2 /*size*/,
                      const Color /*color*/,
                      const AssetHandle texture,
                      const bool visible) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        note_missing_asset();
        return;
    }
    ++render_debug_state_storage().submitted_billboards;
}

void register_point_light(const Vector3 position,
                          const Color color,
                          const float intensity,
                          const float range,
                          const bool enabled) noexcept {
    if (enabled) {
        point_light_queue().push_back(PointLightSubmission{
            .position  = position,
            .color     = color,
            .intensity = intensity,
            .range     = range,
        });
        ++render_debug_state_storage().registered_point_lights;
    }
}

void register_directional_light(const Vector3 /*direction*/,
                                const Color /*color*/,
                                const float /*intensity*/,
                                const bool enabled) noexcept {
    if (enabled) {
        ++render_debug_state_storage().registered_directional_lights;
    }
}

void submit_text_2d(const Vector2 position,
                    const float rotation_rad,
                    const int font_size,
                    const Color color,
                    const std::string& text,
                    const bool visible) noexcept {
    if (!visible) {
        return;
    }
    constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
    text_2d_queue().push_back(TextSubmission2D{
        .position     = position,
        .rotation_deg = rotation_rad * kRadToDeg,
        .font_size    = font_size,
        .color        = color,
        .text         = text,
    });
}

void submit_text_3d(const uint32_t entity_id,
                    const Vector3 position,
                    const Quat rotation,
                    const Vector3 scale,
                    const int font_size,
                    const Color color,
                    const std::string& text,
                    const bool visible) noexcept {
    if (!visible) {
        return;
    }
    text_3d_queue().push_back(TextSubmission3D{
        .entity_id = entity_id,
        .position  = position,
        .rotation  = rotation,
        .scale     = scale,
        .font_size = font_size,
        .color     = color,
        .text      = text,
    });
}

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent) {
    std::pmr::monotonic_buffer_resource scratch_resource;
    std::pmr::unordered_set<entt::entity> active_entities{&scratch_resource};

    std::function<void(entt::entity)> resolve;
    resolve = [&](entt::entity entity) -> void {
        if (!registry.valid(entity) || active_entities.contains(entity) || !has_local_world(entity)) {
            return;
        }

        active_entities.insert(entity);
        bool copied_local         = false;
        const entt::entity parent = get_parent(entity);
        if (parent != entt::null && registry.valid(parent) && has_local_world(parent)) {
            resolve(parent);
            accumulate_from_parent(parent, entity);
            copied_local = true;
        } else {
            copy_local(entity);
        }
        if (!copied_local) {
            copy_local(entity);
        }
        active_entities.erase(entity);
    };

    auto& storage = registry.storage<entt::entity>();
    for (const auto entry : storage.each()) {
        resolve(std::get<0>(entry));
    }
}

void destroy_entity_recursive(
    entt::registry& registry,
    entt::entity entity,
    const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children) {
    static std::pmr::unsynchronized_pool_resource destroying_resource;
    static std::pmr::unordered_set<entt::entity> destroying_entities{&destroying_resource};
    if (!registry.valid(entity) || destroying_entities.contains(entity)) {
        return;
    }

    destroying_entities.insert(entity);
    std::pmr::monotonic_buffer_resource child_resource;
    std::pmr::vector<entt::entity> child_entities{&child_resource};
    visit_children(entity, [&](entt::entity child) { child_entities.push_back(child); });
    for (const auto child : child_entities) {
        destroy_entity_recursive(registry, child, visit_children);
    }
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
    destroying_entities.erase(entity);
}

// ── Active camera state ───────────────────────────────────────────────────────

void set_active_camera_2d(Camera2D cam) noexcept {
    active_camera_2d_storage() = cam;
}

Camera2D get_active_camera_2d() noexcept {
    return active_camera_2d_storage();
}

void set_active_camera_3d(Camera3D cam) noexcept {
    active_camera_3d_storage() = cam;
}

Camera3D get_active_camera_3d() noexcept {
    return active_camera_3d_storage();
}

// ── Editor extern func implementations (std.editor) ──────────────────────────

void register_editor_hit_test_impl(EditorHitTestImpl fn) noexcept {
    hit_test_impl_storage() = std::move(fn);
}

void register_editor_spawn_impl(EditorSpawnImpl fn) noexcept {
    spawn_impl_storage() = std::move(fn);
}

entt::entity editor_spawn_template(entt::registry& registry,
                                   const std::string& template_name,
                                   Vector2 position_2d,
                                   Vector3 position_3d) noexcept {
    if (spawn_impl_storage()) {
        return spawn_impl_storage()(registry, template_name, position_2d, position_3d);
    }
    return entt::entity{entt::null};
}

entt::entity editor_hit_test_2d(entt::registry& registry, Vector2 screen_pos, int mask) noexcept {
    const Vector2 world_pos = editor_screen_to_world_2d(screen_pos);
    if (hit_test_impl_storage()) {
        return hit_test_impl_storage()(registry, world_pos, mask);
    }
    return entt::entity{entt::null};
}

entt::entity editor_raycast_3d(Vector2 /*screen_pos*/, int /*mask*/) noexcept {
    return entt::entity{entt::null};
}

Vector2 editor_screen_to_world_2d(Vector2 screen) noexcept {
    const Camera2D cam = get_active_camera_2d();
    return Vector2{
        .x = ((screen.x - cam.offset.x) / cam.zoom) + cam.target.x,
        .y = ((screen.y - cam.offset.y) / cam.zoom) + cam.target.y,
    };
}

Vector2 editor_mouse_delta_2d() noexcept {
    const Camera2D cam   = get_active_camera_2d();
    const Vector2  delta = GetMouseDelta();
    return Vector2{.x = delta.x / cam.zoom, .y = delta.y / cam.zoom};
}

Vector3 editor_plane_project_3d(Vector2 screen, Vector3 /*plane_origin*/, Vector3 /*plane_normal*/) noexcept {
    (void)screen;
    return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
}

Vector3 editor_mouse_delta_3d() noexcept {
    return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
}

}  // namespace cactus::runtime::entt_backend
