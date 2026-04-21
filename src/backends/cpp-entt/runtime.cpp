#include "backends/cpp-entt/runtime.h"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <memory_resource>
#include <unordered_map>

#include <raymath.h>

namespace cactus::runtime::entt_backend {

namespace {
RenderDebugState g_render_debug_state;

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

std::pmr::unsynchronized_pool_resource g_render_queue_resource;
std::pmr::vector<SpriteSubmission> g_sprite_queue{&g_render_queue_resource};
std::pmr::vector<MeshSubmission> g_mesh_queue{&g_render_queue_resource};
std::pmr::vector<PointLightSubmission> g_point_light_queue{&g_render_queue_resource};
std::unordered_map<int, TextureResourceEntry> g_textures;
std::unordered_map<int, MeshResourceEntry> g_meshes;
std::unordered_map<int, MaterialResourceEntry> g_materials;

constexpr int kMaxMeshLights = 4;
constexpr int kPointLightType = 1;

constexpr char kLightingVertexShader[] = R"(#version 330

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

constexpr char kLightingFragmentShader[] = R"(#version 330

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

LightingShaderState g_lighting_shader_state{};

void reset_lighting_shader_state() noexcept {
    g_lighting_shader_state = {};
    g_lighting_shader_state.enabled_locs.fill(-1);
    g_lighting_shader_state.type_locs.fill(-1);
    g_lighting_shader_state.position_locs.fill(-1);
    g_lighting_shader_state.target_locs.fill(-1);
    g_lighting_shader_state.color_locs.fill(-1);
}

Shader* ensure_lighting_shader() {
    if (g_lighting_shader_state.loaded) {
        return &g_lighting_shader_state.shader;
    }
    if (!IsWindowReady()) {
        return nullptr;
    }

    reset_lighting_shader_state();
    g_lighting_shader_state.shader = LoadShaderFromMemory(kLightingVertexShader, kLightingFragmentShader);
    if (!IsShaderReady(g_lighting_shader_state.shader)) {
        reset_lighting_shader_state();
        return nullptr;
    }

    g_lighting_shader_state.loaded = true;
    g_lighting_shader_state.shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(g_lighting_shader_state.shader, "matModel");
    g_lighting_shader_state.shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(g_lighting_shader_state.shader, "matNormal");
    g_lighting_shader_state.shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(g_lighting_shader_state.shader, "viewPos");
    g_lighting_shader_state.ambient_loc = GetShaderLocation(g_lighting_shader_state.shader, "ambient");
    g_lighting_shader_state.view_pos_loc = GetShaderLocation(g_lighting_shader_state.shader, "viewPos");

    for (int i = 0; i < kMaxMeshLights; ++i) {
        g_lighting_shader_state.enabled_locs[static_cast<std::size_t>(i)] = GetShaderLocation(g_lighting_shader_state.shader, TextFormat("lights[%i].enabled", i));
        g_lighting_shader_state.type_locs[static_cast<std::size_t>(i)] = GetShaderLocation(g_lighting_shader_state.shader, TextFormat("lights[%i].type", i));
        g_lighting_shader_state.position_locs[static_cast<std::size_t>(i)] = GetShaderLocation(g_lighting_shader_state.shader, TextFormat("lights[%i].position", i));
        g_lighting_shader_state.target_locs[static_cast<std::size_t>(i)] = GetShaderLocation(g_lighting_shader_state.shader, TextFormat("lights[%i].target", i));
        g_lighting_shader_state.color_locs[static_cast<std::size_t>(i)] = GetShaderLocation(g_lighting_shader_state.shader, TextFormat("lights[%i].color", i));
    }

    return &g_lighting_shader_state.shader;
}

void clear_lighting_shader() noexcept {
    if (g_lighting_shader_state.loaded && IsWindowReady()) {
        UnloadShader(g_lighting_shader_state.shader);
    }
    reset_lighting_shader_state();
}

void apply_point_lights(const Camera3D& camera) {
    Shader* shader = ensure_lighting_shader();
    if (shader == nullptr) {
        return;
    }

    const float ambient[4] = {0.22F, 0.22F, 0.28F, 1.0F};
    const float view_pos[3] = {camera.position.x, camera.position.y, camera.position.z};
    SetShaderValue(*shader, g_lighting_shader_state.ambient_loc, ambient, SHADER_UNIFORM_VEC4);
    SetShaderValue(*shader, g_lighting_shader_state.view_pos_loc, view_pos, SHADER_UNIFORM_VEC3);

    const int active_lights = std::min(static_cast<int>(g_point_light_queue.size()), kMaxMeshLights);
    g_render_debug_state.active_point_lights = active_lights;
    g_render_debug_state.used_lit_mesh_shader = active_lights > 0;

    for (int i = 0; i < kMaxMeshLights; ++i) {
        const int enabled = i < active_lights ? 1 : 0;
        const int type = kPointLightType;
        const auto zero = Vector3{0.0F, 0.0F, 0.0F};
        float position[3] = {zero.x, zero.y, zero.z};
        float target[3] = {zero.x, zero.y, zero.z};
        float color[4] = {0.0F, 0.0F, 0.0F, 1.0F};

        if (i < active_lights) {
            const auto& light = g_point_light_queue[static_cast<std::size_t>(i)];
            position[0] = light.position.x;
            position[1] = light.position.y;
            position[2] = light.position.z;
            color[0] = (static_cast<float>(light.color.r) / 255.0F) * light.intensity;
            color[1] = (static_cast<float>(light.color.g) / 255.0F) * light.intensity;
            color[2] = (static_cast<float>(light.color.b) / 255.0F) * light.intensity;
            color[3] = static_cast<float>(light.color.a) / 255.0F;
        }

        SetShaderValue(*shader, g_lighting_shader_state.enabled_locs[static_cast<std::size_t>(i)], &enabled, SHADER_UNIFORM_INT);
        SetShaderValue(*shader, g_lighting_shader_state.type_locs[static_cast<std::size_t>(i)], &type, SHADER_UNIFORM_INT);
        SetShaderValue(*shader, g_lighting_shader_state.position_locs[static_cast<std::size_t>(i)], position, SHADER_UNIFORM_VEC3);
        SetShaderValue(*shader, g_lighting_shader_state.target_locs[static_cast<std::size_t>(i)], target, SHADER_UNIFORM_VEC3);
        SetShaderValue(*shader, g_lighting_shader_state.color_locs[static_cast<std::size_t>(i)], color, SHADER_UNIFORM_VEC4);
    }
}

void note_missing_asset() noexcept {
    ++g_render_debug_state.missing_assets;
}

void clear_texture_store() noexcept {
    for (auto& [runtime_id, entry] : g_textures) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadTexture(entry.texture);
        }
    }
    g_textures.clear();
}

void clear_mesh_store() noexcept {
    for (auto& [runtime_id, entry] : g_meshes) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadMesh(entry.mesh);
        }
    }
    g_meshes.clear();
}

void clear_material_store() noexcept {
    for (auto& [runtime_id, entry] : g_materials) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadMaterial(entry.material);
        }
    }
    g_materials.clear();
}

Texture2D* ensure_texture_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = g_textures[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        Image image = GenImageColor(1, 1, WHITE);
        entry.texture = LoadTextureFromImage(image);
        UnloadImage(image);
        entry.loaded = true;
        entry.owned = true;
    }
    return &entry.texture;
}

Mesh* ensure_mesh_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = g_meshes[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        entry.mesh = GenMeshCube(1.0F, 1.0F, 1.0F);
        UploadMesh(&entry.mesh, false);
        entry.loaded = true;
        entry.owned = true;
    }
    return &entry.mesh;
}

Material* ensure_material_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = g_materials[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        entry.material = LoadMaterialDefault();
        entry.loaded = true;
        entry.owned = true;
    }
    if (Shader* shader = ensure_lighting_shader(); shader != nullptr) {
        entry.material.shader = *shader;
    }
    entry.material.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
    return &entry.material;
}

Matrix mesh_transform_matrix(const MeshSubmission& submission) noexcept {
    const Matrix scale = MatrixScale(submission.scale.x, submission.scale.y, submission.scale.z);
    const Matrix rotation = QuaternionToMatrix(submission.rotation);
    const Matrix translation = MatrixTranslate(submission.position.x, submission.position.y, submission.position.z);
    return MatrixMultiply(MatrixMultiply(scale, rotation), translation);
}

void flush_mesh_queue() noexcept {
    if (g_mesh_queue.empty()) {
        return;
    }

    g_render_debug_state.used_default_3d_camera = true;
    g_render_debug_state.active_point_lights = std::min(static_cast<int>(g_point_light_queue.size()), kMaxMeshLights);
    g_render_debug_state.used_lit_mesh_shader = g_render_debug_state.active_point_lights > 0;
    if (!IsWindowReady()) {
        return;
    }

    Camera3D camera{};
    camera.position = Vector3{6.0F, 6.0F, 6.0F};
    camera.target = Vector3{0.0F, 0.0F, 0.0F};
    camera.up = Vector3{0.0F, 1.0F, 0.0F};
    camera.fovy = 45.0F;
    camera.projection = CAMERA_PERSPECTIVE;

    apply_point_lights(camera);

    BeginMode3D(camera);
    for (const auto& submission : g_mesh_queue) {
        Mesh* mesh = ensure_mesh_resource(submission.mesh_runtime_id);
        Material* material = ensure_material_resource(submission.material_runtime_id);
        if (mesh == nullptr || material == nullptr) {
            continue;
        }
        DrawMesh(*mesh, *material, mesh_transform_matrix(submission));
    }
    EndMode3D();
}

void flush_sprite_queue() noexcept {
    if (g_sprite_queue.empty()) {
        return;
    }

    std::stable_sort(g_sprite_queue.begin(), g_sprite_queue.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.layer < rhs.layer;
    });
    g_render_debug_state.used_default_2d_camera = true;
    g_render_debug_state.drawn_sprite_layers.clear();
    for (const auto& submission : g_sprite_queue) {
        g_render_debug_state.drawn_sprite_layers.push_back(submission.layer);
    }

    if (!IsWindowReady()) {
        return;
    }

    Camera2D camera{};
    camera.offset = Vector2{0.0F, 0.0F};
    camera.target = Vector2{0.0F, 0.0F};
    camera.rotation = 0.0F;
    camera.zoom = 1.0F;

    BeginMode2D(camera);
    for (const auto& submission : g_sprite_queue) {
        Texture2D* texture = ensure_texture_resource(submission.runtime_id);
        if (texture == nullptr) {
            continue;
        }
        const Rectangle src{0.0F, 0.0F, static_cast<float>(texture->width), static_cast<float>(texture->height)};
        const Rectangle dst{submission.position.x, submission.position.y, submission.size.x, submission.size.y};
        DrawTexturePro(*texture, src, dst, Vector2{0.0F, 0.0F}, 0.0F, submission.color);
    }
    EndMode2D();
}
}  // namespace

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void reset_render_debug_state() noexcept {
    g_render_debug_state = {};
    g_sprite_queue.clear();
    g_mesh_queue.clear();
    g_point_light_queue.clear();
    clear_texture_store();
    clear_mesh_store();
    clear_material_store();
    clear_lighting_shader();
    shared_asset_registry().clear_diagnostics();
}

const RenderDebugState& render_debug_state() noexcept {
    return g_render_debug_state;
}

void begin_render_frame() noexcept {
    g_sprite_queue.clear();
    g_mesh_queue.clear();
    g_point_light_queue.clear();
    g_render_debug_state.used_default_2d_camera = false;
    g_render_debug_state.used_default_3d_camera = false;
    g_render_debug_state.active_point_lights = 0;
    g_render_debug_state.used_lit_mesh_shader = false;
    g_render_debug_state.drawn_sprite_layers.clear();
}

void end_render_frame() noexcept {
    flush_mesh_queue();
    flush_sprite_queue();
    g_mesh_queue.clear();
    g_sprite_queue.clear();
    g_point_light_queue.clear();
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
    g_sprite_queue.push_back(SpriteSubmission{
        .position = position,
        .size = size,
        .color = color,
        .runtime_id = resolved.runtime_id,
        .layer = layer,
    });
    ++g_render_debug_state.submitted_sprites;
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
    ++g_render_debug_state.advanced_animated_sprites;
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
    const auto mesh_resolved = shared_asset_registry().resolve(AssetKind::Mesh, mesh);
    const auto material_resolved = shared_asset_registry().resolve(AssetKind::Material, material);
    if (!mesh_resolved.ready() || !material_resolved.ready()) {
        note_missing_asset();
        return;
    }
    g_mesh_queue.push_back(MeshSubmission{
        .position = position,
        .rotation = rotation,
        .scale = scale,
        .mesh_runtime_id = mesh_resolved.runtime_id,
        .material_runtime_id = material_resolved.runtime_id,
    });
    ++g_render_debug_state.submitted_meshes;
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
    ++g_render_debug_state.submitted_billboards;
}

void register_point_light(const Vector3 position,
                          const Color color,
                          const float intensity,
                          const float range,
                          const bool enabled) noexcept {
    if (enabled) {
        g_point_light_queue.push_back(PointLightSubmission{
            .position = position,
            .color = color,
            .intensity = intensity,
            .range = range,
        });
        ++g_render_debug_state.registered_point_lights;
    }
}

void register_directional_light(const Vector3 /*direction*/,
                                const Color /*color*/,
                                const float /*intensity*/,
                                const bool enabled) noexcept {
    if (enabled) {
        ++g_render_debug_state.registered_directional_lights;
    }
}

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent) {
    std::pmr::monotonic_buffer_resource scratch_resource;
    std::pmr::vector<entt::entity> active_entities{&scratch_resource};

    std::function<void(entt::entity)> resolve;
    resolve = [&](entt::entity entity) -> void {
        const bool already_active = std::find(active_entities.begin(), active_entities.end(), entity) != active_entities.end();
        if (!registry.valid(entity) || already_active || !has_local_world(entity)) {
            return;
        }

        active_entities.push_back(entity);
        bool copied_local = false;
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
        if (!active_entities.empty()) {
            active_entities.pop_back();
        }
    };

    auto& storage = registry.storage<entt::entity>();
    for (const auto entry : storage.each()) {
        resolve(std::get<0>(entry));
    }
}

void destroy_entity_recursive(entt::registry& registry,
                              entt::entity entity,
                              const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children) {
    static std::pmr::unsynchronized_pool_resource destroying_resource;
    static std::pmr::vector<entt::entity> destroying_entities{&destroying_resource};
    const bool already_destroying = std::find(destroying_entities.begin(), destroying_entities.end(), entity) != destroying_entities.end();
    if (!registry.valid(entity) || already_destroying) {
        return;
    }

    destroying_entities.push_back(entity);
    std::pmr::monotonic_buffer_resource child_resource;
    std::pmr::vector<entt::entity> child_entities{&child_resource};
    visit_children(entity, [&](entt::entity child) { child_entities.push_back(child); });
    for (const auto child : child_entities) {
        destroy_entity_recursive(registry, child, visit_children);
    }
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
    if (!destroying_entities.empty()) {
        destroying_entities.pop_back();
    }
}

}  // namespace cactus::runtime::entt_backend